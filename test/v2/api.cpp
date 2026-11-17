#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include "helpers.hpp"
#include <iostream>
#include <atomic>
#include <thread>
#include <map>

static std::string utf8(const wchar_t *text) {
    const auto length =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, nullptr, 0, nullptr, nullptr);
    check(length > 0, "Unicode arguments");
    std::string value(length, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, value.data(), length, nullptr, nullptr);
    value.pop_back();
    return value;
}
static void encode(const std::string &path, uint32_t num, uint32_t den, uint32_t count) {
    auto enc = encoder(config(num, den));
    auto output = muxer(path);
    auto error = fresh<ness_error_v2>();
    std::ofstream fixture(path_utf8(path + ".packets"), std::ios::binary);
    check(static_cast<bool>(fixture), "packet fixture open");
    bool description_written = false, first_buffered = false;
    uint64_t received = 0;
    std::map<uint64_t, std::pair<int64_t, int64_t>> expected;
    std::vector<uint8_t> y(352 * 240), uv(368 * 120);
    auto receive = [&] {
        for (;;) {
            ness_packet_v2 *raw{};
            const auto result = ness_encoder_receive_v2(enc.get(), &raw, &error);
            Packet packet(raw, ness_packet_release_v2);
            if (result == NESS_V2_AGAIN || result == NESS_V2_END)
                return result;
            checked(result, error);
            auto view = fresh<ness_packet_view_v2>();
            checked(ness_packet_view_get_v2(packet.get(), &view, &error), error);
            check(view.frame_id == 1000 + received && view.pts_hns == expected.at(view.frame_id).first &&
                      view.duration_hns == expected.at(view.frame_id).second &&
                      view.dts_hns == view.pts_hns && (view.flags & NESS_V2_DTS_VALID),
                  "real encoder identity/timestamps changed");
            checked(ness_mkv_write_packet_v2(output.get(), packet.get(), &error), error);
            if (!description_written) {
                auto before = fresh<ness_muxer_stats_v2>();
                checked(ness_mkv_stats_get_v2(output.get(), &before, &error), error);
                check(before.buffered_packets == 1 && before.packets == 0,
                      "first IDR was not retained while metadata unavailable");
                first_buffered = true;
                ness_codec_v2 *description{};
                checked(ness_encoder_description_v2(enc.get(), &description, &error), error);
                Codec codec(description, ness_codec_release_v2);
                auto c = fresh<ness_codec_view_v2>();
                checked(ness_codec_view_get_v2(codec.get(), &c, &error), error);
                binary_write(fixture, c.width);
                binary_write(fixture, c.height);
                binary_write(fixture, c.fps_num);
                binary_write(fixture, c.fps_den);
                binary_write(fixture, c.profile);
                binary_write(fixture, c.color);
                binary_write(fixture, static_cast<uint32_t>(c.avc_decoder_config.bytes));
                fixture.write(reinterpret_cast<const char *>(c.avc_decoder_config.data),
                              static_cast<std::streamsize>(c.avc_decoder_config.bytes));
                std::vector<uint8_t> retained(view.payload.data, view.payload.data + view.payload.bytes);
                packet.reset();
                checked(ness_mkv_set_codec_v2(output.get(), codec.get(), &error), error);
                description_written = true;
                // Snapshot retained bytes after the producer packet handle was already released.
                raw = nullptr;
                auto copy = view;
                copy.payload = {retained.data(), retained.size()};
                checked(ness_packet_copy_v2(&copy, &raw, &error), error);
                packet.reset(raw);
                checked(ness_packet_view_get_v2(packet.get(), &view, &error), error);
            }
            binary_write(fixture, view.frame_id);
            binary_write(fixture, view.pts_hns);
            binary_write(fixture, view.dts_hns);
            binary_write(fixture, view.duration_hns);
            binary_write(fixture, view.flags);
            binary_write(fixture, static_cast<uint32_t>(view.payload.bytes));
            fixture.write(reinterpret_cast<const char *>(view.payload.data),
                          static_cast<std::streamsize>(view.payload.bytes));
            ++received;
        }
    };
    for (uint32_t i = 0; i < count; ++i) {
        std::fill(y.begin(), y.end(), uint8_t{0xa5});
        std::fill(uv.begin(), uv.end(), uint8_t{0xa5});
        for (size_t row = 0; row < 240; ++row)
            std::memset(y.data() + row * 352, 40 + i % 150, 320);
        for (size_t row = 0; row < 120; ++row)
            std::memset(uv.data() + row * 368, 128, 320);
        auto frame = fresh<ness_cpu_frame_v2>();
        frame.format = 1;
        frame.width = 320;
        frame.height = 240;
        frame.frame_id = 1000 + i;
        frame.pts_hns = int64_t(i) * 10000000 * den / num;
        frame.duration_hns = int64_t(i + 1) * 10000000 * den / num - frame.pts_hns;
        frame.planes[0] = {y.data(), y.size(), 352, 0};
        frame.planes[1] = {uv.data(), uv.size(), 368, 0};
        expected.emplace(frame.frame_id, std::pair{frame.pts_hns, frame.duration_hns});
        checked(ness_encoder_submit_cpu_v2(enc.get(), &frame, &error), error);
        std::fill(y.begin(), y.end(), uint8_t{0xee});
        std::fill(uv.begin(), uv.end(), uint8_t{0xee});
        receive();
    }
    checked(ness_encoder_drain_v2(enc.get(), &error), error);
    check(receive() == NESS_V2_END, "EOS did not finish");
    check(received == count && first_buffered, "lost first/tail access unit");
    checked(ness_mkv_finalize_v2(output.get(), &error), error);
    auto stats = fresh<ness_muxer_stats_v2>();
    checked(ness_mkv_stats_get_v2(output.get(), &stats, &error), error);
    check(stats.bytes_written == std::filesystem::file_size(path_utf8(path)) && stats.finalized &&
              stats.packets == count && !stats.failed &&
              stats.duration_hns == int64_t(count) * 10000000 * den / num,
          "final packet duration/stats incorrect");
    auto encoded = fresh<ness_encoder_stats_v2>();
    checked(ness_encoder_stats_get_v2(enc.get(), &encoded, &error), error);
    check(encoded.submitted == count && encoded.emitted == count && !encoded.pending && encoded.drained,
          "drain did not account for accepted inputs");
    std::cout << "{\"status\":\"PASS\",\"scope\":\"encode\",\"frames\":" << count << ",\"fps_num\":" << num
              << ",\"fps_den\":" << den << ",\"duration_hns\":" << stats.duration_hns
              << ",\"padded_strides\":true,\"input_overwritten_after_submit\":true,\"deferred_codec_first_"
                 "idr_retained\":true,\"path\":"
              << quote(path) << "}\n";
}
int wmain(int argc, wchar_t **argv) {
    try {
        const std::string scope = argc > 1 ? utf8(argv[1]) : "api";
        if (scope == "encode") {
            check(argc >= 3, "output path required");
            auto path = path_utf8(utf8(argv[2]));
            if (path.extension() != L".mkv") {
                std::filesystem::create_directories(path);
                path /= L"encoded-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                        std::to_wstring(GetTickCount64()) + L".mkv";
            }
            auto encoded = path.u8string();
            encode(std::string(reinterpret_cast<const char *>(encoded.data()), encoded.size()),
                   argc > 3 ? std::stoul(utf8(argv[3])) : 30000, argc > 4 ? std::stoul(utf8(argv[4])) : 1001,
                   argc > 5 ? std::stoul(utf8(argv[5])) : 90);
        } else if (scope == "api") {
            {
                auto c = config();
                auto error = fresh<ness_error_v2>();
                auto info = fresh<ness_encoder_info_v2>();
                checked(ness_encoder_probe_v2(&c, &info, &error), error);
                check(info.software == 1 && info.gpu_input == 0 && info.fps_num == 30000 &&
                          info.fps_den == 1001,
                      "explicit software probe mismatch");
                auto enc = encoder(c);
                std::atomic<int32_t> foreign{};
                std::thread other([&] {
                    auto i = fresh<ness_encoder_info_v2>();
                    auto e = fresh<ness_error_v2>();
                    foreign = ness_encoder_info_get_v2(enc.get(), &i, &e);
                    ness_encoder_release_v2(enc.get());
                });
                other.join();
                check(foreign == NESS_V2_E_THREAD, "cross-thread encoder use accepted");
                auto gpu = fresh<ness_gpu_frame_v2>();
                gpu.resource_type = 1;
                gpu.format = 1;
                gpu.width = 320;
                gpu.height = 240;
                gpu.device = gpu.texture = reinterpret_cast<void *>(1);
                gpu.duration_hns = 333333;
                ness_input_lease_v2 *lease{};
                check(ness_encoder_submit_gpu_v2(enc.get(), &gpu, &lease, &error) == NESS_V2_E_UNAVAILABLE &&
                          !lease,
                      "unqualified GPU input accepted");
                c.policy = 1;
                ness_encoder_v2 *invalid{};
                check(ness_encoder_create_v2(&c, &invalid, &error) == NESS_V2_E_UNAVAILABLE && !invalid,
                      "hardware policy silently fell back");
                c = config();
                c.backend = 2;
#ifdef NESS_TEST_X264_V2
                checked(ness_encoder_create_v2(&c, &invalid, &error), error);
                ness_encoder_release_v2(invalid);
                invalid = nullptr;
#else
                check(ness_encoder_create_v2(&c, &invalid, &error) == NESS_V2_E_UNAVAILABLE,
                      "x264 assumed installed");
#endif
                c = config();
                c.size = 4;
                check(ness_encoder_create_v2(&c, &invalid, &error) == NESS_V2_E_VERSION,
                      "wrong envelope accepted");
                std::vector<uint8_t> bytes(320 * 240 * 3 / 2, 128);
                auto frame = fresh<ness_cpu_frame_v2>();
                frame.format = 1;
                frame.width = 320;
                frame.height = 240;
                frame.duration_hns = 333333;
                frame.planes[0] = {bytes.data(), 320 * 240, 320, 0};
                frame.planes[1] = {bytes.data() + 320 * 240, 320 * 240 / 2, 320, 0};
                checked(ness_encoder_submit_cpu_v2(enc.get(), &frame, &error), error);
                ness_packet_v2 *raw{};
                checked(ness_encoder_receive_v2(enc.get(), &raw, &error), error);
                Packet packet(raw, ness_packet_release_v2);
                check(packet != nullptr, "probe has no packet");
                checked(ness_encoder_discard_v2(enc.get(), &error), error);
                auto view = fresh<ness_packet_view_v2>();
                checked(ness_packet_view_get_v2(packet.get(), &view, &error), error);
                check(view.payload.bytes > 0, "discard invalidated a retained packet");
            }
            quiet();
            std::cout
                << "{\"status\":\"PASS\",\"scope\":\"api\",\"thread_affinity\":true,\"gpu_unavailable\":true,"
                   "\"no_hardware_fallback\":true,\"retained_packet_survives_discard\":true}\n";
        } else if (scope == "eos") {
            uint64_t accepted = 0, received = 0;
            {
                auto c = config();
                auto enc = encoder(c);
                auto e = fresh<ness_error_v2>();
                std::vector<uint8_t> data(320 * 240 * 3 / 2, 128);
                for (uint64_t i = 0; i < 64; ++i) {
                    auto frame = fresh<ness_cpu_frame_v2>();
                    frame.format = 1;
                    frame.width = 320;
                    frame.height = 240;
                    frame.frame_id = i;
                    frame.pts_hns = static_cast<int64_t>(i * 10000000 * 1001 / 30000);
                    frame.duration_hns = 333667;
                    frame.planes[0] = {data.data(), 320 * 240, 320, 0};
                    frame.planes[1] = {data.data() + 320 * 240, 320 * 240 / 2, 320, 0};
                    const auto code = ness_encoder_submit_cpu_v2(enc.get(), &frame, &e);
                    if (code == NESS_V2_E_BACKPRESSURE)
                        break;
                    checked(code, e);
                    ++accepted;
                }
                checked(ness_encoder_drain_v2(enc.get(), &e), e);
                for (;;) {
                    ness_packet_v2 *raw{};
                    const auto code = ness_encoder_receive_v2(enc.get(), &raw, &e);
                    Packet packet(raw, ness_packet_release_v2);
                    if (code == NESS_V2_END)
                        break;
                    checked(code, e);
                    check(code == 0 && packet, "drain stopped before END");
                    ++received;
                }
                check(received == accepted && accepted >= 16, "EOS lost pending output");
            }
            quiet();
            std::cout << "{\"status\":\"PASS\",\"scope\":\"eos\",\"accepted\":" << accepted
                      << ",\"received\":" << received << "}\n";
        } else if (scope == "lifetime") {
            auto c = config();
            auto info = fresh<ness_encoder_info_v2>();
            auto error = fresh<ness_error_v2>();
            for (int i = 0; i < 16; ++i)
                checked(ness_encoder_probe_v2(&c, &info, &error), error);
            PROCESS_MEMORY_COUNTERS_EX before{}, after{};
            before.cb = after.cb = sizeof(before);
            DWORD handles_before{}, handles_after{};
            GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&before),
                                 sizeof(before));
            GetProcessHandleCount(GetCurrentProcess(), &handles_before);
            const auto missing =
                (std::filesystem::temp_directory_path() /
                 (L"NessMuxer-absent-" + std::to_wstring(GetCurrentProcessId())) / L"out.mkv")
                    .u8string();
            const std::string missing_path(reinterpret_cast<const char *>(missing.data()), missing.size());
            check(!std::filesystem::exists(path_utf8(missing_path).parent_path()),
                  "partial-failure test path already exists");
            for (int i = 0; i < 200; ++i) {
                NessMuxerConfig legacy{};
                legacy.output_path = missing_path.c_str();
                legacy.width = 320;
                legacy.height = 240;
                legacy.fps = 30;
                legacy.bitrate_kbps = 800;
                legacy.encoder_type = 1;
                legacy.codec_type = 1;
                NessMuxer *failed{};
                check(ness_muxer_open(&failed, &legacy) == NESS_ERROR_IO && !failed,
                      "encoder-initialized/output-open failure was not cleaned up");
                checked(ness_encoder_probe_v2(&c, &info, &error), error);
                auto bad = c;
                bad.width = 321;
                ness_encoder_v2 *invalid{};
                check(ness_encoder_create_v2(&bad, &invalid, &error) == -3 && !invalid,
                      "partial failure accepted");
                quiet();
            }
            GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&after),
                                 sizeof(after));
            GetProcessHandleCount(GetCurrentProcess(), &handles_after);
            check(after.PrivateUsage <= before.PrivateUsage + 16 * 1024 * 1024 &&
                      handles_after <= handles_before + 4,
                  "process resources exceeded warm-up baseline allowance");
            std::cout << "{\"status\":\"PASS\",\"scope\":\"lifetime\",\"cycles\":200,\"encoder_initialized_"
                         "output_failures\":200,\"private_before\":"
                      << before.PrivateUsage << ",\"private_after\":" << after.PrivateUsage
                      << ",\"handles_before\":" << handles_before << ",\"handles_after\":" << handles_after
                      << "}\n";
        } else
            throw std::runtime_error("unknown scope");
        quiet();
        return 0;
    } catch (const std::exception &error) {
        std::cout << "{\"status\":\"FAIL\",\"message\":" << quote(error.what()) << "}\n";
        return 1;
    }
}
