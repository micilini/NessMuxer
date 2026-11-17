#pragma once
#ifndef NESSMUXER_STATIC
#define NESSMUXER_STATIC
#endif
#include "ness_muxer_v2.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace ness2 {
using Bytes = std::vector<uint8_t>;
inline constexpr int64_t second = 10000000, max_time = 24LL * 3600 * second;
inline std::atomic<uint64_t> encoders{}, muxers{}, packets{}, codecs{},
    packet_bytes{};
inline std::atomic<uint64_t> gpu_completion_states{}, gpu_input_leases{};
struct Failure {
  int32_t code;
  uint32_t stage;
  int32_t native;
  std::string message;
};
[[noreturn]] inline void fail(int32_t code, uint32_t stage, const char *text,
                              int32_t native = 0) {
  throw Failure{code, stage, native, text};
}
template <class T> inline void envelope(const T *input) {
  if (!input)
    fail(NESS_V2_E_ARGUMENT, 1, "Missing envelope");
  if (input->size != sizeof(T) || input->version != 2)
    fail(NESS_V2_E_VERSION, 1, "Invalid envelope size/version");
}
template <class T> inline T fresh() {
  T value{};
  value.size = sizeof(T);
  value.version = 2;
  return value;
}
template <class F> int32_t boundary(ness_error_v2 *error, F fn) noexcept {
  if (error && (error->size != sizeof(*error) || error->version != 2))
    return NESS_V2_E_VERSION;
  if (error)
    *error = fresh<ness_error_v2>();
  try {
    return fn();
  } catch (const Failure &f) {
    if (error) {
      error->code = f.code;
      error->stage = f.stage;
      error->native_code = f.native;
      std::memcpy(error->message_utf8, f.message.data(),
                  (std::min)(size_t{255}, f.message.size()));
    }
    return f.code;
  } catch (const std::bad_alloc &) {
    if (error) {
      error->code = NESS_V2_E_ALLOC;
      std::memcpy(error->message_utf8, "Allocation failed", 18);
    }
    return NESS_V2_E_ALLOC;
  } catch (...) {
    if (error) {
      error->code = NESS_V2_E_INTERNAL;
      std::memcpy(error->message_utf8, "Native failure", 15);
    }
    return NESS_V2_E_INTERNAL;
  }
}
inline std::span<const uint8_t> view(ness_bytes_v2 bytes,
                                     uint64_t limit = 16 * 1024 * 1024) {
  if (!bytes.data || !bytes.bytes || bytes.bytes > limit)
    fail(NESS_V2_E_ARGUMENT, 3, "Invalid byte view");
  return {bytes.data, static_cast<size_t>(bytes.bytes)};
}
inline void timestamp(int64_t pts, int64_t duration) {
  if (pts < 0 || duration <= 0 || duration > second ||
      pts > max_time - duration)
    fail(NESS_V2_E_ARGUMENT, 3, "Timestamp/duration outside 24 hour domain");
}
struct Codec {
  ness_codec_view_v2 info{};
  Bytes avcc;
};
struct Packet {
  ness_packet_view_v2 info{};
  Bytes bytes;
  Packet(ness_packet_view_v2 value, Bytes payload)
      : info(value), bytes(std::move(payload)) {
    ++packets;
    packet_bytes += bytes.size();
  }
  ~Packet() {
    --packets;
    packet_bytes -= bytes.size();
  }
};
Bytes normalize(std::span<const uint8_t> input, uint32_t format, bool preserve_annex_padding = false);
Bytes codec_private(std::span<const uint8_t> sequence);
void validate_codec(std::span<const uint8_t> avcc);
void configure(const ness_encoder_config_v2 &config);
void configure_muxer(const ness_muxer_config_v2 &config);

// Backend output is copied before the producer unlocks/recycles its bitstream.
struct InputIdentity {
  uint64_t id;
  int64_t pts, duration;
};
class PacketCollector {
  std::array<std::optional<InputIdentity>, 64> pending_{};
  std::deque<std::shared_ptr<Packet>> ready_;
  size_t pending_count_{};
  uint64_t bytes_{};
  uint32_t max_pending_;
  uint64_t max_bytes_;

public:
  uint64_t submitted{}, emitted{}, discarded{};
  explicit PacketCollector(const ness_encoder_config_v2 &config)
      : max_pending_(config.max_pending_frames),
        max_bytes_(config.max_packet_bytes) {}
  bool can_submit() const noexcept { return pending_count_ < max_pending_; }
  bool can_receive_output() const noexcept {
    return ready_.size() < 32 && bytes_ < max_bytes_;
  }
  void validate(InputIdentity input) const;
  void remember(InputIdentity input);
  void accept(std::span<const uint8_t>, uint32_t format, int64_t pts,
              std::optional<int64_t> dts, bool key);
  void accept_normalized(Bytes, int64_t pts, std::optional<int64_t> dts, bool key);
  std::shared_ptr<Packet> receive();
  void discard() noexcept;
  size_t pending() const noexcept { return pending_count_; }
  size_t queued() const noexcept { return ready_.size(); }
  uint64_t bytes() const noexcept { return bytes_; }
};
struct Sink {
  virtual void write(std::span<const uint8_t>) = 0;
  virtual uint64_t tell() = 0;
  virtual void seek(uint64_t) = 0;
  virtual void flush() = 0;
  virtual void close() = 0;
  virtual ~Sink() = default;
};
std::unique_ptr<Sink> file_sink(ness_bytes_v2 path);
class Writer {
  std::unique_ptr<Sink> sink_;
  ness_muxer_config_v2 config_;
  std::optional<Codec> codec_;
  std::deque<std::shared_ptr<Packet>> buffered_;
  Bytes packet_buffer_;
  uint64_t buffered_bytes_{};
  bool initialized_{}, finished_{}, discarded_{};
  std::optional<Failure> error_;
  uint64_t segment_{}, duration_offset_{}, cluster_{}, cluster_size_offset_{},
      cluster_payload_{}, cluster_time_{}, cues_seek_{};
  uint64_t written_{}, clusters_{}, extent_{};
  int64_t last_dts_{-1}, duration_{}, previous_pts_{};
  struct Cue {
    uint64_t time, position;
  };
  std::vector<Cue> cues_;
  void begin();
  void close_cluster();
  void write_one(const std::shared_ptr<Packet> &);
  void guard() const;

public:
  Writer(std::unique_ptr<Sink>, ness_muxer_config_v2);
  ~Writer();
  void set_codec(const Codec &);
  void write(std::shared_ptr<Packet>);
  void finalize();
  void discard();
  ness_muxer_stats_v2 stats() const;
};
struct InputCompletion {
  std::atomic<bool> ready{};
  InputCompletion() { ++gpu_completion_states; }
  virtual void poll() noexcept {}
  virtual ~InputCompletion() { --gpu_completion_states; }
};
class Encoder {
public:
  virtual void submit(const ness_cpu_frame_v2 &) = 0;
  virtual std::shared_ptr<InputCompletion>
  submit_gpu(const ness_gpu_frame_v2 &) {
    fail(NESS_V2_E_UNAVAILABLE, 2,
         "This explicit software encoder rejects GPU input");
  }
  virtual ness_gpu_info_v2 gpu_info() {
    fail(NESS_V2_E_UNAVAILABLE, 2,
         "This explicit software encoder has no GPU device");
  }
  virtual std::shared_ptr<Packet> receive() = 0;
  virtual std::optional<Codec> description() = 0;
  virtual void drain() = 0;
  virtual void discard() = 0;
  virtual ness_encoder_info_v2 info() = 0;
  virtual ness_encoder_stats_v2 stats() = 0;
  virtual ~Encoder() = default;
};
std::unique_ptr<Encoder> mf_encoder(ness_encoder_config_v2);
std::unique_ptr<Encoder> mf_gpu_encoder(ness_encoder_config_v2, const ness_gpu_device_v2 &);
std::unique_ptr<Encoder> qsv_encoder(ness_encoder_config_v2, const ness_gpu_device_v2 &);
std::unique_ptr<Encoder> nvenc_encoder(ness_encoder_config_v2, const ness_gpu_device_v2 &);
std::unique_ptr<Encoder> amf_encoder(ness_encoder_config_v2, const ness_gpu_device_v2 &);
std::unique_ptr<Encoder> x264_encoder(ness_encoder_config_v2);
} // namespace ness2
struct ness_packet_v2 {
  std::shared_ptr<ness2::Packet> value;
};
struct ness_codec_v2 {
  ness2::Codec value;
  ness_codec_v2(ness2::Codec c) : value(std::move(c)) { ++ness2::codecs; }
  ~ness_codec_v2() { --ness2::codecs; }
};
struct ness_encoder_v2 {
  std::unique_ptr<ness2::Encoder> value;
  ness_encoder_v2(std::unique_ptr<ness2::Encoder> e) : value(std::move(e)) {
    ++ness2::encoders;
  }
  ~ness_encoder_v2() { --ness2::encoders; }
};
struct ness_mkv_v2 {
  std::unique_ptr<ness2::Writer> value;
  ness_mkv_v2(std::unique_ptr<ness2::Writer> m) : value(std::move(m)) {
    ++ness2::muxers;
  }
  ~ness_mkv_v2() { --ness2::muxers; }
};

struct ness_input_lease_v2 {
  std::shared_ptr<ness2::InputCompletion> value;
  ness_input_lease_v2() { ++ness2::gpu_input_leases; }
  ~ness_input_lease_v2() { --ness2::gpu_input_leases; }
};
