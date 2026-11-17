#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#include "core.hpp"
#include "helpers.hpp"
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <new>

static thread_local bool count_allocations{};
static thread_local uint64_t allocations{};
void *operator new(size_t bytes) {
    if (count_allocations) ++allocations;
    if (auto *p = std::malloc(bytes ? bytes : 1)) return p;
    throw std::bad_alloc();
}
void *operator new[](size_t bytes) { return ::operator new(bytes); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, size_t) noexcept { std::free(p); }
void operator delete[](void *p, size_t) noexcept { std::free(p); }

namespace {
double process_cpu_seconds() {
#ifdef _WIN32
    FILETIME created{}, ended{}, kernel{}, user{};
    check(GetProcessTimes(GetCurrentProcess(), &created, &ended, &kernel, &user) != FALSE, "process CPU time unavailable");
    const auto ticks = [](FILETIME value) { return (uint64_t(value.dwHighDateTime) << 32) | value.dwLowDateTime; };
    return double(ticks(kernel) + ticks(user)) / 10000000;
#else
    return double(std::clock()) / CLOCKS_PER_SEC;
#endif
}
struct Output {
    ness2::Bytes bytes;
    uint64_t position{}, writes{}, checksum{};
    bool retain{};
};
class TestSink final : public ness2::Sink {
    std::shared_ptr<Output> output_;
public:
    explicit TestSink(std::shared_ptr<Output> output) : output_(std::move(output)) {}
    void write(std::span<const uint8_t> bytes) override {
        auto &out = *output_;
        if (out.retain) {
            check(out.position + bytes.size() <= 64 * 1024 * 1024, "fixture exceeds memory budget");
            out.bytes.resize((std::max)(out.bytes.size(), static_cast<size_t>(out.position + bytes.size())));
            std::memcpy(out.bytes.data() + out.position, bytes.data(), bytes.size());
        }
        if (!bytes.empty()) out.checksum += bytes.front() + bytes.back() + bytes[bytes.size() / 2];
        out.position += bytes.size(); ++out.writes;
    }
    uint64_t tell() override { return output_->position; }
    void seek(uint64_t at) override { output_->position = at; }
    void flush() override {}
    void close() override {}
};
ness2::Codec codec() {
    auto info = fresh<ness_codec_view_v2>();
    info.width = 320; info.height = 240; info.fps_num = 30000; info.fps_den = 1001;
    info.profile = 66; info.color = NESS_V2_BT709_LIMITED;
    const std::array<uint8_t, 15> sequence{0,0,0,1,0x67,66,0,30,0x80,0,0,1,0x68,0x88,0x80};
    return {info, ness2::codec_private(sequence)};
}
ness_muxer_config_v2 configuration() {
    auto config = fresh<ness_muxer_config_v2>();
    config.track_id = 1; config.max_buffered_packets = 4; config.max_buffered_bytes = 4 * 1024 * 1024;
    return config;
}
void stamp(ness2::Packet &packet, uint64_t index, bool key) {
    auto &p = packet.info;
    p.frame_id = index;
    p.pts_hns = static_cast<int64_t>(index * 10000000ULL * 1001 / 30000);
    p.dts_hns = p.pts_hns;
    p.duration_hns = static_cast<int64_t>((index + 1) * 10000000ULL * 1001 / 30000) - p.pts_hns;
    p.flags = NESS_V2_DTS_VALID | (key ? NESS_V2_KEYFRAME : 0);
}
std::shared_ptr<ness2::Packet> packet(size_t size, uint64_t index, bool key) {
    check(size >= 8 && size <= 16 * 1024 * 1024, "fixture packet budget");
    auto info = fresh<ness_packet_view_v2>(); info.track_id = 1; info.bitstream = NESS_V2_AVCC4;
    ness2::Bytes bytes(size, static_cast<uint8_t>(0x20 + index % 93));
    for (size_t i = 0; i < 4; ++i) bytes[i] = static_cast<uint8_t>((size - 4) >> (24 - i * 8));
    bytes[4] = key ? 0x65 : 0x41;
    auto result = std::make_shared<ness2::Packet>(info, std::move(bytes));
    stamp(*result, index, key); return result;
}
struct Element { uint64_t id; size_t payload, size, end; };
uint64_t integer(std::span<const uint8_t> bytes) {
    check(bytes.size() <= 8, "oversized integer");
    uint64_t value{}; for (auto b : bytes) value = (value << 8) | b; return value;
}
Element element(std::span<const uint8_t> data, size_t at) {
    check(at < data.size() && data[at], "missing EBML identifier");
    size_t width = 1; uint8_t mask = 0x80;
    while (!(data[at] & mask)) { ++width; mask >>= 1; }
    check(width <= 4 && width <= data.size() - at, "invalid identifier width");
    const auto id = integer(data.subspan(at, width)); at += width;
    check(at < data.size() && data[at], "missing EBML size");
    width = 1; mask = 0x80;
    while (!(data[at] & mask)) { ++width; mask >>= 1; }
    check(width <= 8 && width <= data.size() - at, "invalid size width");
    uint64_t size = data[at] & (mask - 1);
    for (size_t i = 1; i < width; ++i) size = (size << 8) | data[at + i];
    at += width;
    if (id == 0x18538067 && size == ((uint64_t{1} << (width * 7)) - 1))
        size = data.size() - at; // Matroska permits an unknown-sized Segment.
    check(size <= data.size() - at, "element exceeds parent");
    return {id, at, static_cast<size_t>(size), at + static_cast<size_t>(size)};
}
void boundary_fixture(const char *path) {
    const size_t sizes[]{8,112,113,114,115,116,117,118,120,122,123,124,125,126,127,128,129,
        16365,16366,16367,16368,16369,16370,16379,16380,16381,16382,16383,16384,
        2097136,2097137,2097138,2097139,2097140,2097144,2097145,2097146,2097147,2097150,2097151,2097152};
    auto out = std::make_shared<Output>(); out->retain = true;
    ness2::Writer writer(std::make_unique<TestSink>(out), configuration()); writer.set_codec(codec());
    std::vector<std::shared_ptr<ness2::Packet>> expected;
    for (size_t i = 0; i < std::size(sizes); ++i) {
        expected.push_back(packet(sizes[i], i + (i >= 13 ? 300000 : 0), i % 7 == 0));
        writer.write(expected.back());
    }
    writer.finalize();
    const std::span<const uint8_t> data(out->bytes);
    auto first = element(data, 0); auto segment = element(data, first.end);
    check(first.id == 0x1a45dfa3 && segment.id == 0x18538067 && segment.end == data.size(), "Matroska envelope");
    size_t observed{}; int64_t previous_pts{};
    for (size_t at = segment.payload; at < segment.end;) {
        const auto container = element(data, at); at = container.end;
        if (container.id != 0x1f43b675) continue;
        uint64_t cluster{};
        for (size_t next = container.payload; next < container.end;) {
            const auto child = element(data, next); next = child.end;
            if (child.id == 0xe7) { cluster = integer(data.subspan(child.payload, child.size)); continue; }
            check(child.id == 0xa0 && observed < expected.size(), "unexpected cluster child");
            const auto &wanted = *expected[observed++];
            const auto pts = (wanted.info.pts_hns + 500) / 1000;
            bool block{}, duration{}, reference{};
            for (size_t field = child.payload; field < child.end;) {
                const auto e = element(data, field); field = e.end;
                const auto value = data.subspan(e.payload, e.size);
                if (e.id == 0xa1) {
                    check(!block && value.size() >= 4 && value[0] == 0x81 && value[3] == 0, "block prefix");
                    const auto relative = static_cast<int16_t>((uint16_t(value[1]) << 8) | value[2]);
                    check(static_cast<int64_t>(cluster) + relative == pts, "block timestamp changed");
                    check(std::equal(value.begin() + 4, value.end(), wanted.bytes.begin(), wanted.bytes.end()), "payload changed"); block = true;
                } else if (e.id == 0x9b) {
                    check(!duration && integer(value) == uint64_t((wanted.info.pts_hns + wanted.info.duration_hns + 500) / 1000 - pts), "block duration changed"); duration = true;
                } else if (e.id == 0xfb) {
                    check(!reference && value.size() == 8 && integer(value) == static_cast<uint64_t>(previous_pts - pts), "reference timestamp changed"); reference = true;
                } else throw std::runtime_error("unexpected block group element");
            }
            check(block && duration && reference == !(wanted.info.flags & NESS_V2_KEYFRAME), "block fields lost");
            previous_pts = pts;
        }
    }
    check(observed == expected.size(), "packet count changed");
    if (path) {
        check(!std::filesystem::exists(path_utf8(path)), "preserve earlier byte fixture");
        std::ofstream output(path_utf8(path), std::ios::binary);
        output.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
        check(bool(output), "fixture output failed");
    }
    std::cout << "{\"status\":\"PASS\",\"packets\":" << observed << ",\"bytes\":" << data.size() << "}\n";
}
void hot_path(bool benchmark, bool realtime = false) {
    auto out = std::make_shared<Output>();
    ness2::Writer writer(std::make_unique<TestSink>(out), configuration()); writer.set_codec(codec());
    const std::array<size_t, 4> sizes = realtime ? std::array<size_t, 4>{2048,8192,24576,65536}
                                               : std::array<size_t, 4>{2048,24576,98304,524288};
    std::array<std::shared_ptr<ness2::Packet>, 4> inputs;
    for (size_t i = 0; i < inputs.size(); ++i) inputs[i] = packet(sizes[i], 0, true);
    writer.write(inputs[0]);
    // Prewarm the largest block before tracking allocations; then vary sizes.
    stamp(*inputs.back(), 1, false); writer.write(inputs.back());
    allocations = 0;
    const auto cpu = process_cpu_seconds(); const auto start = std::chrono::steady_clock::now();
    const uint64_t count = benchmark ? 200000 : 48;
    count_allocations = true;
    for (uint64_t i = 2; i < count + 2; ++i) {
        auto &input = inputs[benchmark ? i % inputs.size() : 0];
        stamp(*input, i, i % 60 == 0); writer.write(input);
    }
    count_allocations = false;
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const auto cpu_seconds = process_cpu_seconds() - cpu;
    const auto measured_allocations = allocations; writer.finalize();
    std::cout << "{\"status\":\"" << (benchmark || !measured_allocations ? "PASS" : "FAIL")
        << "\",\"packets\":" << count << ",\"wallSeconds\":" << seconds << ",\"cpuSeconds\":" << cpu_seconds
        << ",\"ordinaryAllocations\":" << measured_allocations << ",\"sinkWrites\":" << out->writes
        << ",\"checksum\":" << out->checksum << ",\"meanPayloadBytes\":" << (sizes[0]+sizes[1]+sizes[2]+sizes[3])/4
        << ",\"syntheticNoDiskIo\":true}\n";
    check(benchmark || !measured_allocations, "steady packet writes allocated temporary buffers");
}
}
int main(int argc, char **argv) {
    try {
        check(argc >= 2, "provide allocation, boundaries, emit or benchmark");
        const std::string scope(argv[1]);
        if (scope == "allocation" || scope == "benchmark" || scope == "benchmark-realtime")
            hot_path(scope != "allocation", scope == "benchmark-realtime");
        else if (scope == "boundaries" || (scope == "emit" && argc == 3)) boundary_fixture(argc == 3 ? argv[2] : nullptr);
        else throw std::runtime_error("unknown writer storage scope");
        check(!ness2::packet_bytes && !ness2::packets, "packet ownership leaked"); return 0;
    } catch (const ness2::Failure &error) {
        count_allocations = false; std::cerr << error.message << '\n';
    } catch (const std::exception &error) {
        count_allocations = false; std::cerr << error.what() << '\n';
    }
    return 1;
}
