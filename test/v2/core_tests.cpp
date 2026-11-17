#include "core.hpp"
#include "helpers.hpp"
#include <iostream>
#include <array>
namespace {
struct State {
    std::vector<uint8_t> bytes;
    uint64_t at{};
    std::string failure;
    int calls{};
    bool failed{};
};
class MemorySink : public ness2::Sink {
    std::shared_ptr<State> state_;
    void fail(const char *operation) {
        if (state_->failure == operation && !state_->failed) {
            state_->failed = true;
            ness2::fail(NESS_V2_E_IO, operation == std::string("write") ? 4u : 5u, "Injected sink failure");
        }
    }

  public:
    explicit MemorySink(std::shared_ptr<State> state) : state_(std::move(state)) {}
    void write(std::span<const uint8_t> data) override {
        fail("write");
        if (state_->at + data.size() > 65536)
            throw std::runtime_error("memory sink budget");
        if (state_->bytes.size() < state_->at + data.size())
            state_->bytes.resize(static_cast<size_t>(state_->at + data.size()));
        std::memcpy(state_->bytes.data() + state_->at, data.data(), data.size());
        state_->at += data.size();
    }
    uint64_t tell() override { return state_->at; }
    void seek(uint64_t at) override {
        fail("seek");
        state_->at = at;
    }
    void flush() override { fail("flush"); }
    void close() override { fail("close"); }
};
ness2::Codec codec() {
    ness2::Codec c;
    c.info = fresh<ness_codec_view_v2>();
    c.info.width = 320;
    c.info.height = 240;
    c.info.fps_num = 30000;
    c.info.fps_den = 1001;
    c.info.profile = 66;
    c.info.color = 1;
    const std::array<uint8_t, 15> sequence{0, 0, 0, 1, 0x67, 66, 0, 30, 0x80, 0, 0, 1, 0x68, 0x88, 0x80};
    c.avcc = ness2::codec_private(sequence);
    return c;
}
std::shared_ptr<ness2::Packet> packet(uint64_t index = 0) {
    auto info = fresh<ness_packet_view_v2>();
    info.track_id = 1;
    info.bitstream = 1;
    info.flags = 3;
    info.frame_id = index;
    info.pts_hns = static_cast<int64_t>(index * 10000000 * 1001 / 30000);
    info.dts_hns = info.pts_hns;
    info.duration_hns = static_cast<int64_t>((index + 1) * 10000000 * 1001 / 30000) - info.pts_hns;
    return std::make_shared<ness2::Packet>(info, std::vector<uint8_t>{0, 0, 0, 3, 0x65, 0x88, 0x80});
}
} // namespace
int main(int argc, char **argv) {
    try {
        const std::string scope = argc > 1 ? argv[1] : "collector";
        if (scope == "collector") {
            auto c = config();
            c.max_pending_frames = 4;
            c.max_packet_bytes = 1024;
            ness2::PacketCollector collector(c);
            for (int i = 0; i < 3; ++i)
                collector.remember({uint64_t(90 + i), int64_t(i) * 333333, 333333 + i});
            std::vector<uint8_t> source{0, 0, 0, 1, 0x65, 0x88, 0x80};
            const int order[] = {0, 2, 1};
            // A test encoder emits nothing until EOS, then reorders output in decode order.
            check(!collector.receive() && collector.pending() == 3, "test encoder did not delay output");
            for (int decode = 0; decode < 3; ++decode)
                collector.accept(source, 2, int64_t(order[decode]) * 333333, int64_t(decode) * 333333, true);
            std::fill(source.begin(), source.end(), uint8_t{0xee});
            for (int decode = 0; decode < 3; ++decode) {
                auto p = collector.receive();
                check(p && p->info.frame_id == uint64_t(90 + order[decode]) &&
                          p->info.pts_hns == int64_t(order[decode]) * 333333 &&
                          p->info.dts_hns == int64_t(decode) * 333333 &&
                          p->info.duration_hns == 333333 + order[decode] && p->bytes.back() == 0x80,
                      "delayed/reordered output lost identity or borrowed producer bytes");
            }
            check(!collector.receive() && !collector.pending() && collector.submitted == 3 &&
                      collector.emitted == 3,
                  "EOS tail accounting");
            collector.remember({100, 2000000, 333333});
            collector.discard();
            check(collector.discarded == 1 && !collector.pending(), "discard treated as normal drain");
        } else if(scope == "annex-padding") {
            ness2::Bytes driver{0,0,0,1,0x65,0x88,0x80};
            driver.resize(1024,0);
            const auto canonical=ness2::normalize(driver,NESS_V2_ANNEX_B);
            auto preserved=ness2::normalize(driver,NESS_V2_ANNEX_B,true);
            check(canonical.size()==7 && preserved.size()==driver.size(),
                  "Native CBR payload padding policy changed canonical callers or discarded hardware bytes");
            check(preserved[0]==0 && preserved[1]==0 && preserved[2]==3 && preserved[3]==252 &&
                  std::equal(driver.begin()+4,driver.end(),preserved.begin()+4),
                  "Preserved AVCC length/payload differs from exact driver access unit");
            auto c=config();c.max_pending_frames=4;c.max_packet_bytes=64;
            ness2::PacketCollector limited(c);limited.remember({1,0,333333});
            bool pressure{};
            try { limited.accept_normalized(preserved,0,0,true); }
            catch(const ness2::Failure& error) { pressure=error.code==NESS_V2_E_BACKPRESSURE; }
            check(pressure && limited.pending()==1 && !limited.emitted && !limited.bytes(),
                  "CBR padding escaped byte budget or consumed an input on backpressure");
            c.max_packet_bytes=2048;ness2::PacketCollector collector(c);
            collector.remember({1,0,333333});collector.accept_normalized(std::move(preserved),0,0,true);
            std::fill(driver.begin(),driver.end(),uint8_t{0xee});
            auto owned=collector.receive();
            check(owned && owned->bytes.size()==1024 && owned->bytes[4]==0x65 && owned->bytes.back()==0 &&
                  !collector.pending() && !collector.bytes(),"Owned padded packet lost payload or budget accounting");
        } else if (scope == "codec-buffer") {
            auto state = std::make_shared<State>();
            auto c = fresh<ness_muxer_config_v2>();
            c.track_id = 1;
            c.max_buffered_packets = 1;
            c.max_buffered_bytes = 1024;
            ness2::Writer writer(std::make_unique<MemorySink>(state), c);
            auto first = packet();
            writer.write(first);
            first.reset();
            check(writer.stats().buffered_packets == 1, "first packet not owned");
            bool pressure = false;
            try {
                writer.write(packet(1));
            } catch (const ness2::Failure &error) {
                pressure = error.code == NESS_V2_E_BACKPRESSURE;
            }
            check(pressure, "unbounded waiting for CodecPrivate");
            writer.set_codec(codec());
            check(writer.stats().packets == 1 && !writer.stats().buffered_packets,
                  "first IDR lost on deferred codec");
            writer.finalize();
            check(writer.stats().finalized, "deferred codec finalization");
        } else if (scope == "io") {
            for (const auto *operation : {"write", "seek", "flush", "close"}) {
                auto state = std::make_shared<State>();
                state->failure = operation;
                auto c = fresh<ness_muxer_config_v2>();
                c.track_id = 1;
                c.max_buffered_packets = 2;
                c.max_buffered_bytes = 1024;
                ness2::Writer writer(std::make_unique<MemorySink>(state), c);
                bool propagated = false;
                try {
                    writer.set_codec(codec());
                    writer.write(packet());
                    writer.finalize();
                } catch (const ness2::Failure &error) {
                    propagated = error.code == NESS_V2_E_IO;
                }
                check(propagated && writer.stats().failed && !writer.stats().finalized,
                      "I/O/finalize error was hidden or marked intact");
                bool latched = false;
                try {
                    writer.finalize();
                } catch (const ness2::Failure &error) {
                    latched = error.code == NESS_V2_E_IO;
                }
                check(latched, "finalization failure was not preserved");
            }
        } else if (scope == "timestamps") {
            for (uint64_t i = 0; i < 2000000; ++i) {
                const auto absolute = i * 10000000 * 1001 / 30000;
                const auto rounded = (absolute + 500) / 1000;
                const auto reference =
                    ((i / 30000) * 10000000 * 1001 + ((i % 30000) * 10000000 * 1001) / 30000 + 500) / 1000;
                check(rounded == reference, "absolute rational time drift");
            }
            auto state = std::make_shared<State>();
            auto c = fresh<ness_muxer_config_v2>();
            c.track_id = 1;
            c.max_buffered_packets = 2;
            c.max_buffered_bytes = 1024;
            ness2::Writer writer(std::make_unique<MemorySink>(state), c);
            writer.set_codec(codec());
            writer.write(packet());
            auto late = packet(300000);
            writer.write(late);
            writer.finalize();
            check(writer.stats().clusters == 2 &&
                      writer.stats().duration_hns == late->info.pts_hns + late->info.duration_hns,
                  "large timestamp/cluster boundary or last duration lost");
        } else
            throw std::runtime_error("unknown core test");
        check(ness2::packet_bytes == 0 && ness2::packets == 0, "core packet ownership leaked");
        std::cout << "{\"status\":\"PASS\",\"scope\":" << quote(scope)
                  << ",\"test_encoder_or_sink_declared\":true}\n";
        return 0;
    } catch (const ness2::Failure &error) {
        std::cout << "{\"status\":\"FAIL\",\"message\":" << quote(error.message) << "}\n";
        return 1;
    } catch (const std::exception &error) {
        std::cout << "{\"status\":\"FAIL\",\"message\":" << quote(error.what()) << "}\n";
        return 1;
    }
}
