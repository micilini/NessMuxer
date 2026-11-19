#pragma once
// Bounded, owned write handoff. Disk operations run outside the producer lock.
// Offsets and FIFO order are preserved, including container header patches.
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <mutex>
#include <span>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace nessmuxer::io {
class BufferFull final : public std::runtime_error {
public:
  BufferFull() : std::runtime_error("Recording storage backlog exceeded its bounded write buffer") {}
};
class BufferedWrites final {
public:
  struct Stats {
    uint64_t capacity_bytes{}, pending_bytes{}, peak_pending_bytes{}, submitted_bytes{}, completed_bytes{},
        submitted_operations{}, completed_operations{};
    double maximum_write_ms{};
  };
private:
  struct Operation { uint64_t offset{}; uint32_t bytes{}; };
  static constexpr size_t chunk_bytes = 65536, operation_limit = 8192;
  mutable std::mutex mutex_;
  std::condition_variable available_, completed_;
  std::vector<uint8_t> buffer_;
  std::array<Operation, operation_limit> operations_{};
  size_t head_{}, count_{}, read_{}, write_{}, queued_bytes_{};
  bool closing_{};
  std::exception_ptr failure_;
  Stats stats_;
  std::function<void(uint64_t, std::span<const uint8_t>)> write_file_;
  std::thread worker_;
  static size_t checked_capacity(size_t capacity) {
    if (capacity < chunk_bytes || capacity > 256 * 1024 * 1024)
      throw std::invalid_argument("Invalid recording storage buffer");
    return capacity;
  }
  void run() noexcept {
    try {
      std::array<uint8_t, chunk_bytes> copy;
      for (;;) {
        Operation operation;
        {
          std::unique_lock lock(mutex_);
          available_.wait(lock, [&] { return count_ || closing_; });
          if (!count_) return;
          operation = operations_[head_];
          const auto first = (std::min)(size_t(operation.bytes), buffer_.size() - read_);
          std::memcpy(copy.data(), buffer_.data() + read_, first);
          if (first < operation.bytes) std::memcpy(copy.data() + first, buffer_.data(), operation.bytes - first);
          read_ = (read_ + operation.bytes) % buffer_.size();
          queued_bytes_ -= operation.bytes;
          head_ = (head_ + 1) % operations_.size(); --count_;
        }
        const auto begin = std::chrono::steady_clock::now();
        write_file_(operation.offset, {copy.data(), operation.bytes});
        const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
        {
          std::lock_guard lock(mutex_);
          stats_.maximum_write_ms = (std::max)(stats_.maximum_write_ms, elapsed);
          stats_.pending_bytes -= operation.bytes;
          stats_.completed_bytes += operation.bytes;
          ++stats_.completed_operations;
        }
        completed_.notify_all();
      }
    } catch (...) {
      { std::lock_guard lock(mutex_); failure_ = std::current_exception(); closing_ = true; }
      completed_.notify_all(); available_.notify_all();
    }
  }
public:
  BufferedWrites(size_t capacity, std::function<void(uint64_t, std::span<const uint8_t>)> writer)
      : buffer_(checked_capacity(capacity)), write_file_(std::move(writer)) {
    if (!write_file_)
      throw std::invalid_argument("Invalid recording storage buffer");
    stats_.capacity_bytes = capacity;
    worker_ = std::thread([this] { run(); });
  }
  BufferedWrites(const BufferedWrites &) = delete;
  BufferedWrites &operator=(const BufferedWrites &) = delete;
  ~BufferedWrites() { try { finish(); } catch (...) {} }
  void check() const {
    std::lock_guard lock(mutex_);
    if (failure_) std::rethrow_exception(failure_);
  }
  void write(uint64_t offset, std::span<const uint8_t> bytes) {
    std::unique_lock lock(mutex_);
    if (failure_) std::rethrow_exception(failure_);
    if (closing_) throw std::logic_error("Recording storage writer is closed");
    if (bytes.size() > UINT64_MAX - offset) throw std::invalid_argument("Recording storage offset overflow");
    const auto needed = bytes.size() / chunk_bytes + (bytes.size() % chunk_bytes != 0);
    if (bytes.size() > buffer_.size() - queued_bytes_ || needed > operations_.size() - count_) throw BufferFull();
    stats_.submitted_bytes += bytes.size();
    stats_.pending_bytes += bytes.size();
    stats_.peak_pending_bytes = (std::max)(stats_.peak_pending_bytes, stats_.pending_bytes);
    while (!bytes.empty()) {
      const auto count = (std::min)(chunk_bytes, bytes.size());
      const auto first = (std::min)(count, buffer_.size() - write_);
      std::memcpy(buffer_.data() + write_, bytes.data(), first);
      if (first < count) std::memcpy(buffer_.data(), bytes.data() + first, count - first);
      write_ = (write_ + count) % buffer_.size();
      queued_bytes_ += count;
      operations_[(head_ + count_) % operations_.size()] = {offset, static_cast<uint32_t>(count)};
      ++count_; ++stats_.submitted_operations;
      offset += count; bytes = bytes.subspan(count);
    }
    lock.unlock(); available_.notify_one();
  }
  void drain() {
    std::unique_lock lock(mutex_);
    completed_.wait(lock, [&] { return failure_ || stats_.submitted_operations == stats_.completed_operations; });
    if (failure_) std::rethrow_exception(failure_);
  }
  void finish() {
    { std::lock_guard lock(mutex_); closing_ = true; }
    available_.notify_all();
    if (worker_.joinable()) worker_.join();
    check();
  }
  Stats stats() const { std::lock_guard lock(mutex_); return stats_; }
};
} // namespace nessmuxer::io
