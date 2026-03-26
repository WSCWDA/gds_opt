#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <cufile.h>

namespace gds_bench {

struct OptimizedGDSMetrics {
  double total_time_ms{0.0};
  double latency_per_io_ms{0.0};
  double throughput_gbps{0.0};
  std::size_t register_count{0};
  std::size_t deregister_count{0};
  std::size_t iterations{0};
  std::size_t io_size_bytes{0};
};

class OptimizedGDSRunner {
 public:
  OptimizedGDSRunner() = default;
  ~OptimizedGDSRunner();

  bool init(const std::string& file_path, std::size_t io_size, bool is_read);
  bool run(std::size_t iterations);
  void cleanup();

  const OptimizedGDSMetrics& metrics() const { return metrics_; }
  const std::string& last_error() const { return last_error_; }

 private:
  void set_error(const std::string& message);
  bool deregister_buffer_once();

  int fd_{-1};
  void* device_buffer_{nullptr};
  std::size_t io_size_{0};
  std::size_t file_size_{0};
  bool is_read_{true};
  bool initialized_{false};
  bool run_completed_{false};

  bool cufile_driver_opened_{false};
  bool cufile_handle_registered_{false};
  bool cufile_buffer_registered_{false};
  CUfileHandle_t cfr_handle_{};

  OptimizedGDSMetrics metrics_{};
  std::string last_error_{};
};

}  // namespace gds_bench
