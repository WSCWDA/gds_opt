#include "gds_cache.h"

#include <cufile.h>

#include <chrono>

namespace gds_bench {

GDSRegistrationCache::~GDSRegistrationCache() { force_evict_all(); }

bool GDSRegistrationCache::acquire(void* dev_ptr, std::size_t len) {
  if (dev_ptr == nullptr || len == 0) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  const std::uint64_t ts = now_us();

  auto it = entries_.find(dev_ptr);
  if (it == entries_.end()) {
    CUfileError_t cufile_status = cuFileBufRegister(dev_ptr, len, 0);
    if (cufile_status.err != CU_FILE_SUCCESS) {
      return false;
    }

    CacheEntry entry;
    entry.base_ptr = dev_ptr;
    entry.length = len;
    entry.registered = true;
    entry.hit_count = 0;
    entry.use_count = 1;
    entry.last_used_time = ts;
    entry.ref_count = 1;
    entries_.emplace(dev_ptr, entry);

    stats_.miss_count += 1;
    stats_.register_count += 1;
    return true;
  }

  CacheEntry& entry = it->second;
  if (!entry.registered || entry.length != len) {
    if (entry.registered) {
      CUfileError_t dereg = cuFileBufDeregister(entry.base_ptr);
      if (dereg.err != CU_FILE_SUCCESS) {
        return false;
      }
      stats_.deregister_count += 1;
      entry.registered = false;
    }

    CUfileError_t reg = cuFileBufRegister(dev_ptr, len, 0);
    if (reg.err != CU_FILE_SUCCESS) {
      return false;
    }
    entry.length = len;
    entry.registered = true;
    stats_.register_count += 1;
    stats_.miss_count += 1;
  } else {
    entry.hit_count += 1;
    stats_.hit_count += 1;
  }

  entry.use_count += 1;
  entry.last_used_time = ts;
  entry.ref_count += 1;
  return true;
}

bool GDSRegistrationCache::release(void* dev_ptr) {
  if (dev_ptr == nullptr) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  auto it = entries_.find(dev_ptr);
  if (it == entries_.end()) {
    return false;
  }

  CacheEntry& entry = it->second;
  if (entry.ref_count <= 0) {
    return false;
  }

  entry.ref_count -= 1;
  entry.last_used_time = now_us();
  return true;
}

void GDSRegistrationCache::evict_idle(std::uint64_t idle_us) {
  std::lock_guard<std::mutex> lock(mutex_);
  const std::uint64_t ts = now_us();

  for (auto it = entries_.begin(); it != entries_.end();) {
    CacheEntry& entry = it->second;
    const bool in_use = entry.ref_count > 0;
    const bool idle_expired = (ts >= entry.last_used_time) && ((ts - entry.last_used_time) >= idle_us);

    if (!in_use && entry.registered && idle_expired) {
      CUfileError_t cufile_status = cuFileBufDeregister(entry.base_ptr);
      if (cufile_status.err == CU_FILE_SUCCESS) {
        stats_.deregister_count += 1;
        it = entries_.erase(it);
        continue;
      }
    }
    ++it;
  }
}

void GDSRegistrationCache::force_evict_all() {
  std::lock_guard<std::mutex> lock(mutex_);

  for (auto& kv : entries_) {
    CacheEntry& entry = kv.second;
    if (!entry.registered) {
      continue;
    }

    CUfileError_t cufile_status = cuFileBufDeregister(entry.base_ptr);
    if (cufile_status.err == CU_FILE_SUCCESS) {
      stats_.deregister_count += 1;
      entry.registered = false;
      entry.ref_count = 0;
    }
  }

  entries_.clear();
}

bool GDSRegistrationCache::is_registered(void* dev_ptr, std::size_t len) {
  if (dev_ptr == nullptr || len == 0) {
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  auto it = entries_.find(dev_ptr);
  if (it == entries_.end()) {
    return false;
  }

  const CacheEntry& entry = it->second;
  return entry.registered && entry.length == len;
}

CacheStats GDSRegistrationCache::get_stats() {
  std::lock_guard<std::mutex> lock(mutex_);

  CacheStats snapshot = stats_;
  snapshot.entry_count = entries_.size();

  std::size_t registered_entries = 0;
  for (const auto& kv : entries_) {
    if (kv.second.registered) {
      registered_entries += 1;
    }
  }
  snapshot.registered_entry_count = registered_entries;
  return snapshot;
}

std::uint64_t GDSRegistrationCache::now_us() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

}  // namespace gds_bench
