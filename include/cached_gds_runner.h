#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <cufile.h>

#include "gds_cache.h"

namespace gds_bench {

struct CachedGDSMetrics {
  std::uint64_t cache_hit{0};
  std::uint64_t cache_miss{0};
  std::uint64_t register_count{0};
  std::uint64_t deregister_count{0};
  double total_time_ms{0.0};
  double latency_per_io_ms{0.0};
  double throughput_gbps{0.0};
  std::size_t iterations{0};
  std::size_t io_size_bytes{0};
  std::size_t buffer_count{0};
};

class CachedGDSRunner {
 public:
  CachedGDSRunner() = default;
  ~CachedGDSRunner();

  void set_buffer_count(std::size_t count);

  bool init(const std::string& file_path, std::size_t io_size, bool is_read);
  bool run(std::size_t iterations);
  void cleanup();

  const CachedGDSMetrics& metrics() const { return metrics_; }
  const std::string& last_error() const { return last_error_; }

 private:
  void set_error(const std::string& message);

  int fd_{-1};
  std::size_t io_size_{0};
  std::size_t file_size_{0};
  bool is_read_{true};
  bool initialized_{false};

  std::size_t buffer_count_{1};
  std::vector<void*> buffers_;

  bool cufile_driver_opened_{false};
  bool cufile_handle_registered_{false};
  CUfileHandle_t cfr_handle_{};

  GDSRegistrationCache cache_;
  CachedGDSMetrics metrics_{};
  std::string last_error_{};
};

}  // namespace gds_bench
