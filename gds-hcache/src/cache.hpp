#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace ghc::detail {

struct CacheKey {
  std::uint32_t file_id;
  std::uint64_t offset;
  bool operator==(const CacheKey& rhs) const noexcept {
    return file_id == rhs.file_id && offset == rhs.offset;
  }
};

struct KeyHash {
  std::size_t operator()(const CacheKey& key) const noexcept;
};

enum class LineState { kFilling, kValid, kError };

struct CacheLine {
  CacheKey key{};
  std::size_t slot = 0;
  std::size_t valid_bytes = 0;
  LineState state = LineState::kFilling;
  int error = 0;
  std::size_t users = 0;
  std::condition_variable cv;
  std::list<CacheKey>::iterator lru_it;
};

struct LookupResult {
  std::shared_ptr<CacheLine> line;
  bool owner = false;
  bool hit = false;
  bool coalesced = false;
};

class BlockCache {
 public:
  BlockCache(std::size_t slots, std::function<void(std::size_t)> on_evict = {});
  LookupResult get_or_reserve(CacheKey key);
  void publish(const std::shared_ptr<CacheLine>& line,
               std::size_t valid_bytes, int error);
  void release(const std::shared_ptr<CacheLine>& line);
  void invalidate(CacheKey key);

 private:
  void touch_locked(const std::shared_ptr<CacheLine>& line);

  std::mutex mutex_;
  std::condition_variable free_cv_;
  std::size_t slots_;
  std::list<std::size_t> free_slots_;
  std::list<CacheKey> lru_;
  std::unordered_map<CacheKey, std::shared_ptr<CacheLine>, KeyHash> index_;
  std::function<void(std::size_t)> on_evict_;
};

}  // namespace ghc::detail
