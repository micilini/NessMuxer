#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "core.hpp"
#include "legacy_bridge.h"
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <future>
#include <mutex>
#include <thread>

namespace {
template <class T, void (*Release)(T *)> using Owner = std::unique_ptr<T, decltype(Release)>;
struct Legacy {
    std::mutex api_mutex, queue_mutex;
    std::condition_variable changed;
    std::function<void()> work;
    bool exit{}, closed{};
    int close_code{};
    std::thread worker;
    Owner<ness_encoder_v2, ness_encoder_release_v2> encoder{nullptr, ness_encoder_release_v2};
    Owner<ness_mkv_v2, ness_mkv_release_v2> muxer{nullptr, ness_mkv_release_v2};
    NessMuxerConfig config{};
    std::string path;
    uint64_t submitted{}, written{};
    std::string message;
    Legacy() {
        worker = std::thread([this] {
            for (;;) {
                std::function<void()> task;
                {
                    std::unique_lock lock(queue_mutex);
                    changed.wait(lock, [&] { return exit || static_cast<bool>(work); });
                    if (exit && !work)
                        break;
                    task = std::move(work);
                    work = {};
                }
                task();
            }
        });
    }
    ~Legacy() {
        if (worker.joinable()) {
            call([&] {
                encoder.reset();
                muxer.reset();
                return 0;
            });
            {
                std::lock_guard lock(queue_mutex);
                exit = true;
            }
            changed.notify_one();
            worker.join();
        }
    }
    template <class F> int call(F action) {
        std::packaged_task<int()> task(std::move(action));
        auto result = task.get_future();
        {
            std::lock_guard lock(queue_mutex);
            work = [&task] { task(); };
        }
        changed.notify_one();
        return result.get();
    }
    int checked(int result, const ness_error_v2 &error) {
        if (result < 0) {
            message = reinterpret_cast<const char *>(error.message_utf8);
            return result == NESS_V2_E_IO         ? NESS_ERROR_IO
                   : result == NESS_V2_E_ARGUMENT ? NESS_ERROR_PARAM
                   : result == NESS_V2_E_STATE    ? NESS_ERROR_STATE
                                                  : NESS_ERROR_ENCODER;
        }
        return 0;
    }
    void counts() {
        if (encoder) {
            auto stats = ness2::fresh<ness_encoder_stats_v2>();
            auto error = ness2::fresh<ness_error_v2>();
            if (ness_encoder_stats_get_v2(encoder.get(), &stats, &error) == 0)
                submitted = stats.submitted;
        }
        if (muxer) {
            auto stats = ness2::fresh<ness_muxer_stats_v2>();
            auto error = ness2::fresh<ness_error_v2>();
            if (ness_mkv_stats_get_v2(muxer.get(), &stats, &error) == 0)
                written = stats.packets;
        }
    }
    int receive() {
        auto error = ness2::fresh<ness_error_v2>();
        for (;;) {
            ness_packet_v2 *raw{};
            const auto result = ness_encoder_receive_v2(encoder.get(), &raw, &error);
            Owner<ness_packet_v2, ness_packet_release_v2> packet(raw, ness_packet_release_v2);
            if (result == NESS_V2_AGAIN || result == NESS_V2_END) {
                counts();
                return 0;
            }
            if (result < 0)
                return checked(result, error);
            auto status = ness_mkv_write_packet_v2(muxer.get(), packet.get(), &error);
            if (status < 0)
                return checked(status, error);
            ness_codec_v2 *raw_codec{};
            status = ness_encoder_description_v2(encoder.get(), &raw_codec, &error);
            Owner<ness_codec_v2, ness_codec_release_v2> codec(raw_codec, ness_codec_release_v2);
            if (status == 0) {
                status = ness_mkv_set_codec_v2(muxer.get(), codec.get(), &error);
                if (status < 0)
                    return checked(status, error);
            } else if (status < 0)
                return checked(status, error);
        }
    }
};
void copy_message(const std::string &value, char *out, size_t count) {
    if (out && count) {
        const auto bytes = (std::min)(count - 1, value.size());
        std::memcpy(out, value.data(), bytes);
        out[bytes] = 0;
    }
}
} // namespace
extern "C" int ness_legacy_v2_create(void **output, const NessMuxerConfig *input, char *message,
                                     size_t size) {
    if (output)
        *output = nullptr;
    try {
        auto owned = std::make_unique<Legacy>();
        owned->config = *input;
        const auto length = std::strlen(input->output_path);
        auto wide_size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input->output_path,
                                             static_cast<int>(length), nullptr, 0);
        const auto page = wide_size ? CP_UTF8 : CP_ACP;
        if (!wide_size)
            wide_size =
                MultiByteToWideChar(page, 0, input->output_path, static_cast<int>(length), nullptr, 0);
        std::wstring wide(wide_size, L'\0');
        MultiByteToWideChar(page, 0, input->output_path, static_cast<int>(length), wide.data(), wide_size);
        const auto absolute = std::filesystem::absolute(wide).u8string();
        owned->path.assign(reinterpret_cast<const char *>(absolute.data()), absolute.size());
        const auto result = owned->call([&] {
            auto config = ness2::fresh<ness_encoder_config_v2>();
            config.backend = 1;
            config.width = input->width;
            config.height = input->height;
            config.fps_num = input->fps;
            config.fps_den = 1;
            config.bitrate_bps = static_cast<uint32_t>(input->bitrate_kbps) * 1000;
            config.profile = 66;
            config.color = 1;
            auto error = ness2::fresh<ness_error_v2>();
            ness_encoder_v2 *encoder{};
            auto status = ness_encoder_create_v2(&config, &encoder, &error);
            owned->encoder.reset(encoder);
            if (status < 0)
                return owned->checked(status, error);
            auto writer = ness2::fresh<ness_muxer_config_v2>();
            writer.track_id = 1;
            writer.max_buffered_packets = 32;
            writer.max_buffered_bytes = 4 * 1024 * 1024;
            writer.output_path_utf8 = {reinterpret_cast<const uint8_t *>(owned->path.data()),
                                       owned->path.size()};
            ness_mkv_v2 *muxer{};
            status = ness_mkv_open_v2(&writer, nullptr, &muxer, &error);
            owned->muxer.reset(muxer);
            return owned->checked(status, error);
        });
        copy_message(owned->message, message, size);
        if (result)
            return result;
        *output = owned.release();
        return 0;
    } catch (...) {
        copy_message("Legacy bridge initialization failed", message, size);
        return NESS_ERROR_ENCODER;
    }
}
extern "C" int ness_legacy_v2_write(void *value, const uint8_t *bytes, int count, int64_t pts,
                                    int explicit_pts, char *message, size_t size) {
    auto &bridge = *static_cast<Legacy *>(value);
    std::lock_guard serial(bridge.api_mutex);
    try {
        const auto result = bridge.call([&] {
            if (bridge.closed)
                return NESS_ERROR_STATE;
            const uint64_t expected = uint64_t(bridge.config.width) * bridge.config.height * 3 / 2;
            if (!bytes || count < 0 || static_cast<uint64_t>(count) < expected)
                return NESS_ERROR_PARAM;
            auto frame = ness2::fresh<ness_cpu_frame_v2>();
            frame.format = 1;
            frame.width = bridge.config.width;
            frame.height = bridge.config.height;
            frame.frame_id = bridge.submitted;
            frame.pts_hns =
                explicit_pts ? pts : static_cast<int64_t>(bridge.submitted * 10000000ULL / bridge.config.fps);
            frame.duration_hns =
                static_cast<int64_t>((bridge.submitted + 1) * 10000000ULL / bridge.config.fps -
                                     bridge.submitted * 10000000ULL / bridge.config.fps);
            frame.planes[0] = {bytes, uint64_t(frame.width) * frame.height, frame.width, 0};
            frame.planes[1] = {bytes + size_t(frame.width) * frame.height,
                               uint64_t(frame.width) * frame.height / 2, frame.width, 0};
            auto error = ness2::fresh<ness_error_v2>();
            const auto status = ness_encoder_submit_cpu_v2(bridge.encoder.get(), &frame, &error);
            bridge.counts();
            if (status < 0)
                return bridge.checked(status, error);
            return bridge.receive();
        });
        copy_message(bridge.message, message, size);
        return result;
    } catch (...) {
        copy_message("Legacy bridge submission failed", message, size);
        return NESS_ERROR_ENCODER;
    }
}
extern "C" int ness_legacy_v2_close(void *value, int64_t *frames, int64_t *packets, char *message,
                                    size_t size) {
    auto &bridge = *static_cast<Legacy *>(value);
    std::lock_guard serial(bridge.api_mutex);
    int result = NESS_ERROR;
    try {
        result = bridge.call([&] {
            if (bridge.closed)
                return bridge.close_code;
            bridge.closed = true;
            auto error = ness2::fresh<ness_error_v2>();
            auto status = ness_encoder_drain_v2(bridge.encoder.get(), &error);
            int code = status < 0 ? bridge.checked(status, error) : bridge.receive();
            if (!code && bridge.submitted) {
                status = ness_mkv_finalize_v2(bridge.muxer.get(), &error);
                code = bridge.checked(status, error);
            } else if (!bridge.submitted)
                (void)ness_mkv_discard_v2(bridge.muxer.get(), &error);
            bridge.counts();
            bridge.encoder.reset();
            bridge.muxer.reset();
            bridge.close_code = code;
            return code;
        });
    } catch (...) {
        bridge.message = "Legacy bridge finalization failed";
    }
    *frames = static_cast<int64_t>(bridge.submitted);
    *packets = static_cast<int64_t>(bridge.written);
    copy_message(bridge.message, message, size);
    return result;
}
extern "C" void ness_legacy_v2_counts(void *value, int64_t *frames, int64_t *packets) {
    auto &bridge = *static_cast<Legacy *>(value);
    std::lock_guard lock(bridge.api_mutex);
    *frames = static_cast<int64_t>(bridge.submitted);
    *packets = static_cast<int64_t>(bridge.written);
}
extern "C" void ness_legacy_v2_destroy(void *value) { delete static_cast<Legacy *>(value); }
