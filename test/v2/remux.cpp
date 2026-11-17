#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#define NESSMUXER_STATIC
#include "helpers.hpp"
#include <iostream>
static std::string utf8(const wchar_t *value) {
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, nullptr, 0, nullptr, nullptr);
    check(n > 0, "path encoding");
    std::string result(n, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, result.data(), n, nullptr, nullptr);
    result.pop_back();
    return result;
}
int wmain(int argc, wchar_t **argv) {
    try {
        check(argc >= 3, "input packet fixture and output path required");
        const auto input = utf8(argv[1]), path = utf8(argv[2]);
        bool long_run = argc > 3;
        uint64_t written = 0;
        int64_t end = 0;
        {
            std::ifstream file(path_utf8(input), std::ios::binary);
            check(static_cast<bool>(file), "preencoded fixture open");
            auto description = fresh<ness_codec_view_v2>();
            description.width = binary_read<uint32_t>(file);
            description.height = binary_read<uint32_t>(file);
            description.fps_num = binary_read<uint32_t>(file);
            description.fps_den = binary_read<uint32_t>(file);
            description.profile = binary_read<uint32_t>(file);
            description.color = binary_read<uint32_t>(file);
            const auto size = binary_read<uint32_t>(file);
            check(size < 65536, "codec budget");
            std::vector<uint8_t> avcc(size);
            file.read(reinterpret_cast<char *>(avcc.data()), size);
            description.avc_decoder_config = {avcc.data(), avcc.size()};
            ness_codec_v2 *raw{};
            auto error = fresh<ness_error_v2>();
            checked(ness_codec_copy_v2(&description, &raw, &error), error);
            Codec codec(raw, ness_codec_release_v2);
            auto writer = muxer(path, codec.get());
            for (;;) {
                if (file.peek() == std::char_traits<char>::eof())
                    break;
                auto metadata = fresh<ness_packet_view_v2>();
                metadata.track_id = 1;
                metadata.bitstream = 1;
                metadata.frame_id = binary_read<uint64_t>(file);
                metadata.pts_hns = binary_read<int64_t>(file);
                metadata.dts_hns = binary_read<int64_t>(file);
                metadata.duration_hns = binary_read<int64_t>(file);
                metadata.flags = binary_read<uint32_t>(file);
                const auto bytes = binary_read<uint32_t>(file);
                check(bytes <= 16 * 1024 * 1024, "packet fixture budget");
                std::vector<uint8_t> payload(bytes);
                file.read(reinterpret_cast<char *>(payload.data()), bytes);
                check(static_cast<bool>(file), "truncated packet fixture");
                const uint64_t repeat =
                    long_run ? uint64_t(description.fps_num) * 7200 / description.fps_den : 1;
                for (uint64_t i = 0; i < repeat; ++i) {
                    if (long_run) {
                        metadata.frame_id = i;
                        metadata.pts_hns =
                            static_cast<int64_t>(i * 10000000 * description.fps_den / description.fps_num);
                        metadata.dts_hns = metadata.pts_hns;
                        metadata.duration_hns =
                            static_cast<int64_t>((i + 1) * 10000000 * description.fps_den /
                                                 description.fps_num) -
                            metadata.pts_hns;
                        check((metadata.flags & 1) != 0, "long timeline requires a preencoded IDR");
                    }
                    metadata.payload = {payload.data(), payload.size()};
                    ness_packet_v2 *packet{};
                    checked(ness_packet_copy_v2(&metadata, &packet, &error), error);
                    Packet owned(packet, ness_packet_release_v2);
                    checked(ness_mkv_write_packet_v2(writer.get(), packet, &error), error);
                    ++written;
                    end = metadata.pts_hns + metadata.duration_hns;
                }
                if (long_run)
                    break;
            }
            checked(ness_mkv_finalize_v2(writer.get(), &error), error);
            auto stats = fresh<ness_muxer_stats_v2>();
            checked(ness_mkv_stats_get_v2(writer.get(), &stats, &error), error);
            check(stats.packets == written && stats.duration_hns == end && stats.finalized,
                  "standalone muxer counters");
        }
        quiet();
        std::cout << "{\"status\":\"PASS\",\"scope\":\"packet-only\",\"encoder_created\":false,\"packets\":"
                  << written << ",\"duration_hns\":" << end
                  << ",\"long_timeline\":" << (long_run ? "true" : "false") << "}\n";
        return 0;
    } catch (const std::exception &error) {
        std::cout << "{\"status\":\"FAIL\",\"message\":" << quote(error.what()) << "}\n";
        return 1;
    }
}
