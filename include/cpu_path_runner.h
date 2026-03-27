#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <sys/types.h>

namespace gds_bench {

struct CPUPathMetrics {
  double read_time_ms{0.0};
  double memcpy_time_ms{0.0};
  double total_latency_ms{0.0};
  double throughput_gbps{0.0};
  std::size_t iterations{0};
  std::size_t io_size_bytes{0};
};

class CPUPathRunner {
 public:
  CPUPathRunner() = default;
  ~CPUPathRunner();

  // Initialize file descriptor and host/device buffers.
  bool init(const std::string& file_path, std::size_t io_size, bool is_read);

  // Execute N I/O iterations and collect timing statistics.
  bool run(std::size_t iterations);

  // Release all allocated resources; safe to call multiple times.
  void cleanup();

  const CPUPathMetrics& metrics() const { return metrics_; }
  const std::string& last_error() const { return last_error_; }

 private:
  bool read_once(off_t offset_bytes);
  bool memcpy_once();
  void set_error(const std::string& message);

  int fd_{-1};
  void* host_buffer_{nullptr};
  void* device_buffer_{nullptr};
  std::size_t io_size_{0};
  std::size_t file_size_{0};
  bool is_read_{true};
  bool initialized_{false};

  CPUPathMetrics metrics_{};
  std::string last_error_{};
};

}  // namespace gds_bench
