#pragma once
#include <cstdint>
namespace diskann::iobench {
struct IOStats {
  uint64_t cache_hits = 0, cache_misses = 0;
  uint64_t io_batches = 0, io_requests = 0, io_bytes = 0;
  uint64_t io_submit_ns = 0, io_wait_ns = 0;
  double cache_hit_ratio() const;
  void merge(const IOStats &other);
};
} // namespace diskann::iobench
