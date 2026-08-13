#pragma once
#include <atomic>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>
namespace diskann::iobench {
struct IOTraceRecord {
  uint64_t seq = 0, timestamp_ns = 0, query_id = 0, thread_id = 0;
  uint32_t batch_id = 0, request_in_batch = 0, batch_size = 0;
  uint64_t offset = 0, len = 0, latency_ns = 0;
  uint8_t cache_miss = 0;
};
class IOTraceWriter {
public:
  IOTraceWriter() = default;
  explicit IOTraceWriter(const std::string &path) { open(path); }
  ~IOTraceWriter() { close(); }
  void open(const std::string &path); void close(); void append(const IOTraceRecord &record);
  bool enabled() const { return enabled_.load(std::memory_order_acquire); }
private:
  std::ofstream out_; std::mutex mutex_; std::atomic<uint64_t> seq_{0}; std::atomic<bool> enabled_{false};
};
std::vector<IOTraceRecord> read_io_trace(const std::string &path);
uint64_t monotonic_time_ns();
} // namespace diskann::iobench
