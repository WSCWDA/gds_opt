#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace gds_bench {

struct CacheEntry {
  void* base_ptr{nullptr};
  std::size_t length{0};
  bool registered{false};
  std::uint64_t hit_count{0};
  std::uint64_t use_count{0};
  std::uint64_t last_used_time{0};
  int ref_count{0};
};

struct CacheStats {
  std::uint64_t hit_count{0};
  std::uint64_t miss_count{0};
  std::uint64_t register_count{0};
  std::uint64_t deregister_count{0};
  std::size_t entry_count{0};
  std::size_t registered_entry_count{0};
};

class GDSRegistrationCache {
 public:
  GDSRegistrationCache() = default;
  ~GDSRegistrationCache();

  bool acquire(void* dev_ptr, std::size_t len);
  bool release(void* dev_ptr);
  void evict_idle(std::uint64_t idle_us);
  void force_evict_all();

  bool is_registered(void* dev_ptr, std::size_t len);
  CacheStats get_stats();

 private:
  static std::uint64_t now_us();

  mutable std::mutex mutex_;
  std::unordered_map<void*, CacheEntry> entries_;
  CacheStats stats_{};
};

}  // namespace gds_bench
