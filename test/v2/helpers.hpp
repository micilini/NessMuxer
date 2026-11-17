#pragma once
#include "ness_muxer_v2.h"
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstring>
template <class T> T fresh() {
    T value{};
    value.size = sizeof(T);
    value.version = 2;
    return value;
}
inline void check(bool value, const char *text) {
    if (!value)
        throw std::runtime_error(text);
}
inline void checked(int result, const ness_error_v2 &e) {
    if (result < 0)
        throw std::runtime_error(std::string(reinterpret_cast<const char *>(e.message_utf8)) + " [" +
                                 std::to_string(result) + "]");
}
template <class T, void (*Free)(T *)> using Owner = std::unique_ptr<T, decltype(Free)>;
using Encoder = Owner<ness_encoder_v2, ness_encoder_release_v2>;
using Muxer = Owner<ness_mkv_v2, ness_mkv_release_v2>;
using Packet = Owner<ness_packet_v2, ness_packet_release_v2>;
using Codec = Owner<ness_codec_v2, ness_codec_release_v2>;
inline ness_encoder_config_v2 config(uint32_t num = 30000, uint32_t den = 1001) {
    auto value = fresh<ness_encoder_config_v2>();
    value.backend = 1;
    value.width = 320;
    value.height = 240;
    value.fps_num = num;
    value.fps_den = den;
    value.bitrate_bps = 800000;
    value.profile = 66;
    value.color = 1;
    return value;
}
inline Encoder encoder(ness_encoder_config_v2 value) {
    ness_encoder_v2 *raw{};
    auto e = fresh<ness_error_v2>();
    checked(ness_encoder_create_v2(&value, &raw, &e), e);
    return Encoder(raw, ness_encoder_release_v2);
}
inline Muxer muxer(const std::string &path, const ness_codec_v2 *codec = nullptr, uint32_t slots = 32) {
    auto c = fresh<ness_muxer_config_v2>();
    c.track_id = 1;
    c.max_buffered_packets = slots;
    c.max_buffered_bytes = 4 * 1024 * 1024;
    c.output_path_utf8 = {reinterpret_cast<const uint8_t *>(path.data()), path.size()};
    ness_mkv_v2 *raw{};
    auto e = fresh<ness_error_v2>();
    checked(ness_mkv_open_v2(&c, codec, &raw, &e), e);
    return Muxer(raw, ness_mkv_release_v2);
}
inline void quiet() {
    auto stats = fresh<ness_runtime_stats_v2>();
    check(ness_runtime_stats_get_v2(&stats, nullptr) == 0, "runtime stats");
    check(stats.encoders == 0 && stats.muxers == 0 && stats.packets == 0 && stats.codecs == 0 &&
              stats.packet_bytes == 0,
          "owned runtime resources leaked");
}
inline std::string quote(const std::string &text) {
    std::string out = "\"";
    for (auto c : text) {
        if (c == '"' || c == '\\')
            out += '\\';
        if (static_cast<unsigned char>(c) < 32)
            out += ' ';
        else
            out += c;
    }
    return out + '"';
}
inline std::filesystem::path path_utf8(const std::string &value) {
    return std::filesystem::path(
        std::u8string_view(reinterpret_cast<const char8_t *>(value.data()), value.size()));
}
template <class T> void binary_write(std::ostream &file, T value) {
    file.write(reinterpret_cast<const char *>(&value), sizeof(value));
    if (!file)
        throw std::runtime_error("fixture write");
}
template <class T> T binary_read(std::istream &file) {
    T value{};
    file.read(reinterpret_cast<char *>(&value), sizeof(value));
    if (!file)
        throw std::runtime_error("fixture read");
    return value;
}
