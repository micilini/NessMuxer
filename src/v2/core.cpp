#include "core.hpp"
#include "ness_buffered_io.hpp"
#include <algorithm>
#include <bit>
#include <filesystem>
#include <climits>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <cstdio>
#endif

namespace ness2 {
static uint32_t be32(const uint8_t *p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
static void put_be(Bytes &bytes, uint64_t value, size_t count) {
    for (size_t i = count; i; --i)
        bytes.push_back(static_cast<uint8_t>(value >> ((i - 1) * 8)));
}
static std::vector<std::span<const uint8_t>> nals(std::span<const uint8_t> input, uint32_t format,
                                                 bool preserve_padding = false) {
    std::vector<std::span<const uint8_t>> result;
    if (format == NESS_V2_AVCC4) {
        size_t at = 0;
        while (at < input.size()) {
            if (input.size() - at < 4)
                fail(-3, 3, "Incomplete AVC NAL length");
            const auto size = be32(input.data() + at);
            at += 4;
            if (!size || size > input.size() - at)
                fail(-3, 3, "Invalid AVC NAL length");
            result.push_back(input.subspan(at, size));
            at += size;
        }
    } else if (format == NESS_V2_ANNEX_B) {
        auto start = [&](size_t from) -> std::pair<size_t, size_t> {
            for (size_t i = from; i + 2 < input.size(); ++i) {
                if (input[i] == 0 && input[i + 1] == 0) {
                    if (input[i + 2] == 1)
                        return {i, 3};
                    if (i + 3 < input.size() && input[i + 2] == 0 && input[i + 3] == 1)
                        return {i, 4};
                }
            }
            return {input.size(), 0};
        };
        auto [at, prefix] = start(0);
        if (!prefix || std::any_of(input.begin(), input.begin() + at, [](uint8_t b) { return b != 0; }))
            fail(-3, 3, "Invalid Annex B start");
        while (prefix) {
            const auto begin = at + prefix;
            const auto next = start(begin);
            auto end = next.first;
            while (!preserve_padding && end > begin && input[end - 1] == 0)
                --end;
            if (end == begin)
                fail(-3, 3, "Empty Annex B NAL");
            result.push_back(input.subspan(begin, end - begin));
            at = next.first;
            prefix = next.second;
        }
    } else
        fail(-3, 3, "Unknown H264 bitstream format");
    if (result.empty() || result.size() > 1024)
        fail(-3, 3, "Invalid access unit NAL count");
    for (auto nal : result)
        if ((nal[0] & 0x80) || !(nal[0] & 31) || (nal[0] & 31) > 23)
            fail(-3, 3, "Invalid H264 NAL header");
    return result;
}
Bytes normalize(std::span<const uint8_t> input, uint32_t format, bool preserve_annex_padding) {
    Bytes output;
    output.reserve(input.size() + 16);
    for (auto nal : nals(input, format, preserve_annex_padding)) {
        put_be(output, nal.size(), 4);
        output.insert(output.end(), nal.begin(), nal.end());
    }
    return output;
}
void validate_codec(std::span<const uint8_t> data) {
    if (data.size() < 11 || data[0] != 1 || (data[4] & 3) != 3 || (data[4] & 0xfc) != 0xfc ||
        (data[5] & 0xe0) != 0xe0)
        fail(-3, 3, "Expected AVCDecoderConfigurationRecord with 4-byte NAL lengths");
    size_t at = 6;
    const auto sps = data[5] & 31;
    if (!sps)
        fail(-3, 3, "AVC configuration lacks SPS");
    auto units = [&](uint32_t count, uint8_t type) {
        for (uint32_t i = 0; i < count; ++i) {
            if (at + 2 > data.size())
                fail(-3, 3, "Truncated AVC configuration");
            size_t length = (size_t(data[at]) << 8) | data[at + 1];
            at += 2;
            if (!length || length > data.size() - at || (data[at] & 31) != type)
                fail(-3, 3, "Invalid AVC parameter set");
            at += length;
        }
    };
    units(sps, 7);
    if (at >= data.size() || !data[at])
        fail(-3, 3, "AVC configuration lacks PPS");
    const auto pps = data[at++];
    units(pps, 8);
    // High-profile extension bytes are retained when supplied by a producer.
    if (at != data.size() && data[1] != 100 && data[1] != 110 && data[1] != 122 && data[1] != 144)
        fail(-3, 3, "Unexpected AVC configuration tail");
}
Bytes codec_private(std::span<const uint8_t> sequence) {
    if (!sequence.empty() && sequence[0] == 1) {
        validate_codec(sequence);
        return {sequence.begin(), sequence.end()};
    }
    auto units = nals(sequence, NESS_V2_ANNEX_B);
    std::vector<std::span<const uint8_t>> sps, pps;
    for (auto nal : units) {
        if ((nal[0] & 31) == 7)
            sps.push_back(nal);
        if ((nal[0] & 31) == 8)
            pps.push_back(nal);
    }
    if (sps.empty() || pps.empty() || sps.size() > 31 || pps.size() > 255 || sps.front().size() < 4)
        fail(-3, 3, "Codec description lacks valid SPS/PPS");
    Bytes result{1, sps[0][1], sps[0][2], sps[0][3], 255, static_cast<uint8_t>(0xe0 | sps.size())};
    auto append = [&](const auto &list) {
        for (auto nal : list) {
            if (nal.size() > 65535)
                fail(-3, 3, "Parameter set exceeds AVC length");
            put_be(result, nal.size(), 2);
            result.insert(result.end(), nal.begin(), nal.end());
        }
    };
    append(sps);
    result.push_back(static_cast<uint8_t>(pps.size()));
    append(pps);
    validate_codec(result);
    return result;
}
void configure(const ness_encoder_config_v2 &c) {
    envelope(&c);
    if ((c.backend != NESS_V2_BACKEND_MF_SOFTWARE && c.backend != NESS_V2_BACKEND_X264) ||
        c.policy != NESS_V2_POLICY_SOFTWARE)
        fail(NESS_V2_E_UNAVAILABLE, 1,
             "An explicit software backend and software policy are required; no implicit hardware fallback");
    if (!c.width || !c.height || c.width > 4096 || c.height > 4096 || (c.width & 1) || (c.height & 1) ||
        !c.fps_num || !c.fps_den || c.fps_den > 100000 || c.fps_num < c.fps_den ||
        uint64_t(c.fps_num) > 120ULL * c.fps_den || c.bitrate_bps < 10000 || c.bitrate_bps > 100000000 ||
        !c.max_pending_frames || c.max_pending_frames > 64 || !c.max_packet_bytes ||
        c.max_packet_bytes > 16 * 1024 * 1024)
        fail(-3, 1, "Invalid encoder dimensions/rate/bitrate/queue budget");
    if (c.rate_control != NESS_V2_CBR || c.profile != NESS_V2_PROFILE_BASELINE ||
        c.color != NESS_V2_BT709_LIMITED || c.max_b_frames)
        fail(NESS_V2_E_UNAVAILABLE, 1, "Unqualified rate control/profile/color/B-frame configuration");
}
void PacketCollector::validate(InputIdentity input) const {
    if (!can_submit())
        fail(NESS_V2_E_BACKPRESSURE, 2, "Pending encoder input budget exhausted");
    timestamp(input.pts, input.duration);
    for (const auto &item : pending_)
        if (item && (item->id == input.id || item->pts == input.pts))
            fail(-3, 2, "In-flight input identity or PTS reused");
}
void PacketCollector::remember(InputIdentity input) {
    validate(input);
    for (auto &item : pending_)
        if (!item) {
            item = input;
            ++pending_count_;
            ++submitted;
            return;
        }
}
void PacketCollector::accept(std::span<const uint8_t> data, uint32_t format, int64_t pts,
                             std::optional<int64_t> dts, bool key) {
    if (!can_receive_output())
        fail(NESS_V2_E_BACKPRESSURE, 3, "Encoded packet budget exhausted");
    if(std::none_of(pending_.begin(),pending_.end(),[&](const auto& input) {return input && input->pts==pts;}))
        fail(NESS_V2_E_ENCODER,3,"Encoder output has unknown or uninitialized PTS");
    accept_normalized(normalize(data, format),pts,dts,key);
}
void PacketCollector::accept_normalized(Bytes bytes,int64_t pts,std::optional<int64_t> dts,bool key) {
    if (!can_receive_output())
        fail(NESS_V2_E_BACKPRESSURE, 3, "Encoded packet budget exhausted");
    auto found = std::find_if(pending_.begin(), pending_.end(),
                              [&](const auto &item) { return item && item->pts == pts; });
    if (found == pending_.end())
        fail(NESS_V2_E_ENCODER, 3, "Encoder output has unknown or uninitialized PTS");
    if (bytes.size() > max_bytes_ - bytes_)
        fail(NESS_V2_E_BACKPRESSURE, 3, "Encoded packet bytes exceed budget");
    if (dts && (*dts < 0 || *dts > max_time))
        fail(-3, 3, "Invalid decode timestamp");
    auto info = fresh<ness_packet_view_v2>();
    info.track_id = 1;
    info.flags = (key ? NESS_V2_KEYFRAME : 0) | (dts ? NESS_V2_DTS_VALID : 0);
    info.bitstream = NESS_V2_AVCC4;
    info.frame_id = (*found)->id;
    info.pts_hns = pts;
    info.dts_hns = dts.value_or(0);
    info.duration_hns = (*found)->duration;
    auto packet = std::make_shared<Packet>(info, std::move(bytes));
    ready_.push_back(packet);
    bytes_ += packet->bytes.size();
    found->reset();
    --pending_count_;
    ++emitted;
}
std::shared_ptr<Packet> PacketCollector::receive() {
    if (ready_.empty())
        return {};
    auto value = std::move(ready_.front());
    ready_.pop_front();
    bytes_ -= value->bytes.size();
    return value;
}
void PacketCollector::discard() noexcept {
    discarded += pending_count_ + ready_.size();
    for (auto &item : pending_)
        item.reset();
    pending_count_ = 0;
    ready_.clear();
    bytes_ = 0;
}

class FileSink final : public Sink {
#ifdef _WIN32
    HANDLE handle_{INVALID_HANDLE_VALUE};
    std::unique_ptr<nessmuxer::io::BufferedWrites> writes_;
    uint64_t position_{};
    static void checked(bool ok, const char *operation) {
        if (!ok)
            fail(-2, 4, operation, static_cast<int32_t>(HRESULT_FROM_WIN32(GetLastError())));
    }

  public:
    explicit FileSink(ness_bytes_v2 bytes) {
        auto data = view(bytes, 131072);
        if (std::find(data.begin(), data.end(), 0) != data.end())
            fail(-3, 4, "Path contains NUL");
        const auto count =
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, reinterpret_cast<const char *>(data.data()),
                                static_cast<int>(data.size()), nullptr, 0);
        if (!count)
            fail(-3, 4, "Path is not strict UTF8");
        std::wstring path(count, L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, reinterpret_cast<const char *>(data.data()),
                            static_cast<int>(data.size()), path.data(), count);
        if (!std::filesystem::path(path).is_absolute())
            fail(-3, 4, "Output path must be absolute");
        handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        checked(handle_ != INVALID_HANDLE_VALUE, "Cannot create new output file");
        try {
            writes_ = std::make_unique<nessmuxer::io::BufferedWrites>(64 * 1024 * 1024,
                [this](uint64_t offset, std::span<const uint8_t> bytes) {
                    LARGE_INTEGER at{}; at.QuadPart = static_cast<LONGLONG>(offset);
                    checked(SetFilePointerEx(handle_, at, nullptr, FILE_BEGIN) != 0, "Output seek failed");
                    DWORD done{};
                    checked(WriteFile(handle_, bytes.data(), static_cast<DWORD>(bytes.size()), &done, nullptr) != 0,
                        "Output write failed");
                    if (done != bytes.size()) fail(NESS_V2_E_IO, NESS_V2_STAGE_OUTPUT, "Short output write");
                });
        } catch (...) { CloseHandle(handle_); handle_ = INVALID_HANDLE_VALUE; throw; }
    }
    ~FileSink() {
        if (writes_) { try { writes_->finish(); } catch (...) {} }
        if (handle_ != INVALID_HANDLE_VALUE)
            CloseHandle(handle_);
    }
    void write(std::span<const uint8_t> bytes) override {
        if (bytes.size() > INT64_MAX - position_) fail(NESS_V2_E_ARGUMENT, NESS_V2_STAGE_OUTPUT, "Output size overflow");
        try { writes_->write(position_, bytes); }
        catch (const nessmuxer::io::BufferFull &) {
            fail(NESS_V2_E_BACKPRESSURE, NESS_V2_STAGE_OUTPUT, "Recording storage backlog exceeded its 64 MiB output buffer");
        }
        position_ += bytes.size();
    }
    uint64_t tell() override {
        writes_->check();
        return position_;
    }
    void seek(uint64_t offset) override {
        if (offset > INT64_MAX)
            fail(-3, 4, "Output seek overflow");
        writes_->check(); position_ = offset;
    }
    void flush() override { writes_->drain(); checked(FlushFileBuffers(handle_) != 0, "Output flush failed"); }
    void close() override {
        if (handle_ == INVALID_HANDLE_VALUE)
            return;
        std::exception_ptr failure;
        try { writes_->finish(); } catch (...) { failure = std::current_exception(); }
        const auto file = std::exchange(handle_, INVALID_HANDLE_VALUE);
        if (!CloseHandle(file) && !failure) checked(false, "Output close failed");
        if (failure) std::rethrow_exception(failure);
    }
#else
    FILE *file_{};

  public:
    explicit FileSink(ness_bytes_v2 path) {
        auto bytes = view(path, 131072);
        std::string name(bytes.begin(), bytes.end());
        file_ = fopen(name.c_str(), "wbx");
        if (!file_)
            fail(-2, 4, "Cannot create output file");
    }
    ~FileSink() {
        if (file_)
            fclose(file_);
    }
    void write(std::span<const uint8_t> bytes) override {
        if (fwrite(bytes.data(), 1, bytes.size(), file_) != bytes.size())
            fail(-2, 4, "Output write failed");
    }
    uint64_t tell() override {
        auto result = ftell(file_);
        if (result < 0)
            fail(-2, 4, "Output tell failed");
        return static_cast<uint64_t>(result);
    }
    void seek(uint64_t at) override {
        if (at > LONG_MAX || fseek(file_, static_cast<long>(at), SEEK_SET))
            fail(-2, 4, "Output seek failed");
    }
    void flush() override {
        if (fflush(file_))
            fail(-2, 4, "Output flush failed");
    }
    void close() override {
        if (file_) {
            auto old = file_;
            file_ = nullptr;
            if (fclose(old))
                fail(-2, 4, "Output close failed");
        }
    }
#endif
};
std::unique_ptr<Sink> file_sink(ness_bytes_v2 path) { return std::make_unique<FileSink>(path); }
static Bytes number(uint64_t value, size_t width = 0) {
    if (!width) {
        width = 1;
        while (width < 8 && (value >> (width * 8)))
            ++width;
    }
    Bytes bytes;
    put_be(bytes, value, width);
    return bytes;
}
static Bytes identifier(uint32_t id) {
    size_t width = 1;
    while (width < 4 && (id >> (width * 8)))
        ++width;
    return number(id, width);
}
static Bytes size_value(uint64_t value, size_t width = 0) {
    if (!width) {
        width = 1;
        while (width < 8 && value >= ((uint64_t{1} << (width * 7)) - 1))
            ++width;
    }
    if (value >= ((uint64_t{1} << (width * 7)) - 1))
        fail(-3, 4, "EBML size overflow");
    return number(value | (uint64_t{1} << (width * 7)), width);
}
static Bytes element(uint32_t id, std::span<const uint8_t> payload) {
    auto bytes = identifier(id);
    auto size = size_value(payload.size());
    bytes.insert(bytes.end(), size.begin(), size.end());
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}
static void append(Bytes &destination, Bytes value) {
    destination.insert(destination.end(), value.begin(), value.end());
}
static Bytes integer(uint32_t id, uint64_t value, size_t width = 0) {
    return element(id, number(value, width));
}
static Bytes text(uint32_t id, const char *value) {
    return element(id, {reinterpret_cast<const uint8_t *>(value), std::strlen(value)});
}
static Bytes real(double value) { return number(std::bit_cast<uint64_t>(value), 8); }
static uint64_t rounded(int64_t hns) {
    return (static_cast<uint64_t>(hns) + 500) / 1000;
} // TimestampScale=100000 ns, half-up absolute rounding.
void configure_muxer(const ness_muxer_config_v2 &config) {
    envelope(&config);
    if (config.track_id != 1 || !config.max_buffered_packets || config.max_buffered_packets > 64 ||
        !config.max_buffered_bytes || config.max_buffered_bytes > 4 * 1024 * 1024)
        fail(-3, 1, "Invalid Matroska track/buffering budget");
}
Writer::Writer(std::unique_ptr<Sink> sink, ness_muxer_config_v2 config)
    : sink_(std::move(sink)), config_(config) {
    configure_muxer(config_);
    config_.output_path_utf8 = {};
    cues_.reserve(2048);
}
Writer::~Writer() {
    try {
        if (!finished_ && sink_)
            sink_->close();
    } catch (...) {
    }
}
void Writer::guard() const {
    if (error_)
        throw *error_;
    if (discarded_)
        fail(-4, 4, "Matroska output was discarded");
    if (finished_)
        fail(-4, 4, "Matroska output already finalized");
}
void Writer::begin() {
    if (initialized_)
        return;
    if (!codec_)
        fail(-4, 4, "CodecPrivate is not available");
    Bytes ebml;
    append(ebml, integer(0x4286, 1));
    append(ebml, integer(0x42f7, 1));
    append(ebml, integer(0x42f2, 4));
    append(ebml, integer(0x42f3, 8));
    append(ebml, text(0x4282, "matroska"));
    append(ebml, integer(0x4287, 4));
    append(ebml, integer(0x4285, 2));
    sink_->write(element(0x1a45dfa3, ebml));
    sink_->write(identifier(0x18538067));
    sink_->write(Bytes{1, 255, 255, 255, 255, 255, 255, 255});
    segment_ = sink_->tell();
    // Fixed-width seek fields can be patched without moving media data.
    Bytes seekhead;
    std::array<size_t, 3> positions{};
    const uint32_t ids[] = {0x1549a966, 0x1654ae6b, 0x1c53bb6b};
    for (size_t i = 0; i < 3; ++i) {
        Bytes item;
        append(item, element(0x53ab, identifier(ids[i])));
        const auto before = item.size();
        append(item, integer(0x53ac, 0, 8));
        auto encoded = element(0x4dbb, item);
        positions[i] = seekhead.size() + encoded.size() - item.size() + before + 3;
        append(seekhead, std::move(encoded));
    }
    auto head = element(0x114d9b74, seekhead);
    const auto seek_payload = sink_->tell() + head.size() - seekhead.size();
    sink_->write(head);
    const auto info_position = sink_->tell() - segment_;
    Bytes info;
    append(info, integer(0x2ad7b1, 100000));
    append(info, text(0x4d80, "NessMuxer v2"));
    append(info, text(0x5741, "NessMuxer v2"));
    const auto duration_at = info.size();
    append(info, element(0x4489, real(0)));
    auto info_element = element(0x1549a966, info);
    duration_offset_ = sink_->tell() + info_element.size() - info.size() + duration_at + 3;
    sink_->write(info_element);
    const auto track_position = sink_->tell() - segment_;
    const auto &c = codec_->info;
    Bytes track;
    append(track, integer(0xd7, 1));
    append(track, integer(0x73c5, 1));
    append(track, integer(0x83, 1));
    append(track, integer(0x9c, 0));
    append(track, text(0x86, "V_MPEG4/ISO/AVC"));
    append(track, element(0x63a2, codec_->avcc));
    append(track, integer(0x23e383, 1000000000ULL * c.fps_den / c.fps_num));
    Bytes video;
    append(video, integer(0xb0, c.width));
    append(video, integer(0xba, c.height));
    Bytes color;
    append(color, integer(0x55b1, 1));
    append(color, integer(0x55b9, 1));
    append(color, integer(0x55ba, 1));
    append(color, integer(0x55bb, 1));
    append(video, element(0x55b0, color));
    append(track, element(0xe0, video));
    sink_->write(element(0x1654ae6b, element(0xae, track)));
    const auto end = sink_->tell();
    sink_->seek(seek_payload + positions[0]);
    sink_->write(number(info_position, 8));
    sink_->seek(seek_payload + positions[1]);
    sink_->write(number(track_position, 8));
    cues_seek_ = seek_payload + positions[2];
    sink_->seek(end);
    initialized_ = true;
}
void Writer::close_cluster() {
    if (!cluster_payload_)
        return;
    const auto end = sink_->tell();
    sink_->seek(cluster_size_offset_);
    sink_->write(size_value(end - cluster_payload_, 8));
    sink_->seek(end);
    cluster_payload_ = 0;
}
// Packet metadata is at most a few dozen bytes. Build it on the stack and
// copy the encoded payload once into reusable storage for one sink write.
// This preserves the existing EBML widths, order and signed reference bits.
struct BlockFields {
    std::array<uint8_t, 32> bytes{};
    size_t count{};
    static size_t size_width(uint64_t value) {
        size_t width = 1;
        while (width < 8 && value >= ((uint64_t{1} << (width * 7)) - 1)) ++width;
        if (value >= ((uint64_t{1} << (width * 7)) - 1)) fail(-3, 4, "EBML size overflow");
        return width;
    }
    void number(uint64_t value, size_t width) {
        if (width > 8 || count > bytes.size() - width) fail(-3, 4, "Block metadata exceeds its budget");
        for (size_t i = width; i; --i) bytes[count++] = static_cast<uint8_t>(value >> ((i - 1) * 8));
    }
    void size(uint64_t value) {
        const auto width = size_width(value);
        number(value | (uint64_t{1} << (width * 7)), width);
    }
    void integer(uint8_t id, uint64_t value, size_t width = 0) {
        if (!width) {
            width = 1;
            while (width < 8 && (value >> (width * 8))) ++width;
        }
        number(id, 1); size(width); number(value, width);
    }
    std::span<const uint8_t> view() const { return {bytes.data(), count}; }
};
void Writer::write_one(const std::shared_ptr<Packet> &packet) {
    const auto &p = packet->info;
    if (p.track_id != 1 || !(p.flags & NESS_V2_DTS_VALID))
        fail(-3, 4, "Matroska requires a known decode timestamp and track one");
    timestamp(p.pts_hns, p.duration_hns);
    if (p.dts_hns < last_dts_)
        fail(-3, 4, "Packets are not in decode order");
    if (!written_ && !(p.flags & NESS_V2_KEYFRAME))
        fail(-3, 4, "First video packet must be an IDR/keyframe");
    const auto pts = rounded(p.pts_hns);
    auto relative = static_cast<int64_t>(pts) - static_cast<int64_t>(cluster_time_);
    if (!cluster_payload_ || relative < INT16_MIN || relative > INT16_MAX ||
        sink_->tell() - cluster_payload_ > 4 * 1024 * 1024) {
        close_cluster();
        cluster_ = sink_->tell() - segment_;
        cluster_time_ = pts;
        sink_->write(identifier(0x1f43b675));
        cluster_size_offset_ = sink_->tell();
        sink_->write(size_value(0, 8));
        cluster_payload_ = sink_->tell();
        sink_->write(integer(0xe7, cluster_time_));
        ++clusters_;
        relative = 0;
    }
    if (packet->bytes.size() > packet_buffer_.max_size() - 64)
        fail(-3, 4, "Packet and block metadata exceed the storage domain");
    BlockFields tail;
    tail.integer(0x9b, rounded(p.pts_hns + p.duration_hns) - pts);
    if (!(p.flags & NESS_V2_KEYFRAME))
        tail.integer(0xfb, static_cast<uint64_t>(previous_pts_ - static_cast<int64_t>(pts)), 8);
    const auto block_size = uint64_t(packet->bytes.size()) + 4;
    const auto group_size = 1 + BlockFields::size_width(block_size) + block_size + tail.count;
    BlockFields prefix;
    prefix.number(0xa0, 1); prefix.size(group_size);
    prefix.number(0xa1, 1); prefix.size(block_size);
    prefix.number(0x81, 1); prefix.number(static_cast<uint16_t>(relative), 2); prefix.number(0, 1);
    const auto total = prefix.count + packet->bytes.size() + tail.count;
    packet_buffer_.clear();
    if (packet_buffer_.capacity() < total) packet_buffer_.reserve(total);
    for (const auto part : {prefix.view(), std::span<const uint8_t>(packet->bytes), tail.view()})
        packet_buffer_.insert(packet_buffer_.end(), part.begin(), part.end());
    sink_->write(packet_buffer_);
    if ((p.flags & NESS_V2_KEYFRAME) && (cues_.empty() || pts >= cues_.back().time + 10000)) {
        if (cues_.size() >= 86401)
            fail(NESS_V2_E_BACKPRESSURE, 4, "Cue budget exhausted");
        cues_.push_back({pts, cluster_});
    }
    previous_pts_ = static_cast<int64_t>(pts);
    last_dts_ = p.dts_hns;
    duration_ = (std::max)(duration_, p.pts_hns + p.duration_hns);
    extent_ = sink_->tell();
    ++written_;
}
void Writer::set_codec(const Codec &codec) {
    guard();
    validate_codec(codec.avcc);
    if (codec_) {
        if (codec_->avcc != codec.avcc)
            fail(-3, 4, "Codec cannot change within a track");
        return;
    }
    codec_ = codec;
    try {
        if (!buffered_.empty()) {
            begin();
            while (!buffered_.empty()) {
                write_one(buffered_.front());
                buffered_bytes_ -= buffered_.front()->bytes.size();
                buffered_.pop_front();
            }
        }
    } catch (const Failure &error) {
        error_ = error;
        throw;
    }
}
void Writer::write(std::shared_ptr<Packet> packet) {
    guard();
    if (!packet)
        fail(-3, 4, "Missing packet");
    if (!codec_) {
        if (buffered_.size() >= config_.max_buffered_packets ||
            packet->bytes.size() > config_.max_buffered_bytes - buffered_bytes_)
            fail(NESS_V2_E_BACKPRESSURE, 4, "Waiting-for-codec buffer budget exhausted");
        buffered_bytes_ += packet->bytes.size();
        buffered_.push_back(std::move(packet));
        return;
    }
    try {
        begin();
        write_one(packet);
    } catch (const Failure &error) {
        if (error.code == NESS_V2_E_IO)
            error_ = error;
        throw;
    }
}
void Writer::finalize() {
    if (finished_)
        return;
    guard();
    try {
        if (!codec_ || !buffered_.empty() || !written_)
            fail(-4, 5, "Cannot finalize without codec and complete video packets");
        close_cluster();
        const auto cue_position = sink_->tell() - segment_;
        Bytes entries;
        for (auto cue : cues_) {
            Bytes point = integer(0xb3, cue.time);
            Bytes position = integer(0xf7, 1);
            append(position, integer(0xf1, cue.position));
            append(point, element(0xb7, position));
            append(entries, element(0xbb, point));
        }
        sink_->write(element(0x1c53bb6b, entries));
        const auto end = sink_->tell();
        sink_->seek(duration_offset_);
        sink_->write(real(static_cast<double>(duration_) / 1000.0));
        sink_->seek(cues_seek_);
        sink_->write(number(cue_position, 8));
        sink_->seek(end);
        sink_->flush();
        sink_->close();
        extent_ = end;
        finished_ = true;
    } catch (const Failure &error) {
        if (!error_)
            error_ = error;
        try {
            sink_->close();
        } catch (...) {
        }
        throw *error_;
    }
}
void Writer::discard() {
    if (finished_)
        fail(-4, 4, "Cannot discard finalized output");
    buffered_.clear();
    buffered_bytes_ = 0;
    discarded_ = true;
    sink_->close();
}
ness_muxer_stats_v2 Writer::stats() const {
    auto value = fresh<ness_muxer_stats_v2>();
    value.packets = written_;
    value.buffered_packets = buffered_.size();
    value.buffered_bytes = buffered_bytes_;
    value.clusters = clusters_;
    value.bytes_written = extent_;
    value.duration_hns = duration_;
    value.finalized = finished_;
    value.failed = error_.has_value();
    return value;
}
} // namespace ness2

extern "C" {
int32_t ness_packet_copy_v2(const ness_packet_view_v2 *input, ness_packet_v2 **out, ness_error_v2 *e) {
    if (out)
        *out = nullptr;
    return ness2::boundary(e, [&] {
        ness2::envelope(input);
        if (!out || input->reserved || input->track_id != 1 || (input->flags & ~3u))
            ness2::fail(-3, 3, "Invalid packet metadata");
        ness2::timestamp(input->pts_hns, input->duration_hns);
        auto data = ness2::normalize(ness2::view(input->payload), input->bitstream);
        auto info = *input;
        info.bitstream = NESS_V2_AVCC4;
        info.payload = {};
        *out = new ness_packet_v2{std::make_shared<ness2::Packet>(info, std::move(data))};
        return 0;
    });
}
int32_t ness_packet_view_get_v2(const ness_packet_v2 *p, ness_packet_view_v2 *out, ness_error_v2 *e) {
    return ness2::boundary(e, [&] {
        ness2::envelope(out);
        if (!p)
            ness2::fail(-3, 3, "Missing packet");
        *out = p->value->info;
        out->payload = {p->value->bytes.data(), p->value->bytes.size()};
        return 0;
    });
}
void ness_packet_release_v2(ness_packet_v2 *p) { delete p; }
int32_t ness_codec_copy_v2(const ness_codec_view_v2 *input, ness_codec_v2 **out, ness_error_v2 *e) {
    if (out)
        *out = nullptr;
    return ness2::boundary(e, [&] {
        ness2::envelope(input);
        if (!out || !input->width || !input->height || input->width > 4096 || input->height > 4096 ||
            !input->fps_num || !input->fps_den || input->fps_den > 100000 ||
            input->fps_num < input->fps_den || uint64_t(input->fps_num) > 120ULL * input->fps_den ||
            input->color != NESS_V2_BT709_LIMITED)
            ness2::fail(-3, 3, "Invalid codec dimensions/rate");
        auto data = ness2::view(input->avc_decoder_config, 65536);
        ness2::validate_codec(data);
        if (input->profile != data[1])
            ness2::fail(-3, 3, "Codec profile metadata differs from avcC");
        ness2::Codec codec{*input, {data.begin(), data.end()}};
        codec.info.avc_decoder_config = {};
        *out = new ness_codec_v2(std::move(codec));
        return 0;
    });
}
int32_t ness_codec_view_get_v2(const ness_codec_v2 *c, ness_codec_view_v2 *out, ness_error_v2 *e) {
    return ness2::boundary(e, [&] {
        ness2::envelope(out);
        if (!c)
            ness2::fail(-3, 3, "Missing codec description");
        *out = c->value.info;
        out->avc_decoder_config = {c->value.avcc.data(), c->value.avcc.size()};
        return 0;
    });
}
void ness_codec_release_v2(ness_codec_v2 *c) { delete c; }
int32_t ness_mkv_open_v2(const ness_muxer_config_v2 *config, const ness_codec_v2 *codec, ness_mkv_v2 **out,
                         ness_error_v2 *e) {
    if (out)
        *out = nullptr;
    return ness2::boundary(e, [&] {
        ness2::envelope(config);
        if (!out)
            ness2::fail(-3, 1, "Missing muxer output");
        ness2::configure_muxer(*config);
        auto writer = std::make_unique<ness2::Writer>(ness2::file_sink(config->output_path_utf8), *config);
        if (codec)
            writer->set_codec(codec->value);
        *out = new ness_mkv_v2(std::move(writer));
        return 0;
    });
}
int32_t ness_mkv_set_codec_v2(ness_mkv_v2 *m, const ness_codec_v2 *c, ness_error_v2 *e) {
    return ness2::boundary(e, [&] {
        if (!m || !c)
            ness2::fail(-3, 4, "Missing muxer/codec");
        m->value->set_codec(c->value);
        return 0;
    });
}
int32_t ness_mkv_write_packet_v2(ness_mkv_v2 *m, const ness_packet_v2 *p, ness_error_v2 *e) {
    return ness2::boundary(e, [&] {
        if (!m || !p)
            ness2::fail(-3, 4, "Missing muxer/packet");
        m->value->write(p->value);
        return 0;
    });
}
int32_t ness_mkv_finalize_v2(ness_mkv_v2 *m, ness_error_v2 *e) {
    return ness2::boundary(e, [&] {
        if (!m)
            ness2::fail(-3, 5, "Missing muxer");
        m->value->finalize();
        return 0;
    });
}
int32_t ness_mkv_discard_v2(ness_mkv_v2 *m, ness_error_v2 *e) {
    return ness2::boundary(e, [&] {
        if (!m)
            ness2::fail(-3, 4, "Missing muxer");
        m->value->discard();
        return 0;
    });
}
int32_t ness_mkv_stats_get_v2(ness_mkv_v2 *m, ness_muxer_stats_v2 *out, ness_error_v2 *e) {
    return ness2::boundary(e, [&] {
        ness2::envelope(out);
        if (!m)
            ness2::fail(-3, 4, "Missing muxer");
        *out = m->value->stats();
        return 0;
    });
}
void ness_mkv_release_v2(ness_mkv_v2 *m) { delete m; }
int32_t ness_gpu_runtime_stats_get_v2(ness_gpu_runtime_stats_v2* out, ness_error_v2* e) {
    return ness2::boundary(e,[&] {
        ness2::envelope(out); *out=ness2::fresh<ness_gpu_runtime_stats_v2>();
        out->completion_states=ness2::gpu_completion_states.load();
        out->input_leases=ness2::gpu_input_leases.load(); return 0;
    });
}
int32_t ness_runtime_stats_get_v2(ness_runtime_stats_v2 *out, ness_error_v2 *e) {
    return ness2::boundary(e, [&] {
        ness2::envelope(out);
        out->encoders = ness2::encoders;
        out->muxers = ness2::muxers;
        out->packets = ness2::packets;
        out->codecs = ness2::codecs;
        out->packet_bytes = ness2::packet_bytes;
        return 0;
    });
}
}
