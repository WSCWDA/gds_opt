#include "cache.hpp"

#include <stdexcept>

namespace ghc::detail {

std::size_t KeyHash::operator()(const CacheKey& key) const noexcept {
  const auto a = std::hash<std::uint32_t>{}(key.file_id);
  const auto b = std::hash<std::uint64_t>{}(key.offset);
  return a ^ (b + 0x9e3779b97f4a7c15ULL + (a << 6U) + (a >> 2U));
}

BlockCache::BlockCache(std::size_t slots,
                       std::function<void(std::size_t)> on_evict)
    : slots_(slots), on_evict_(std::move(on_evict)) {
  if (slots_ == 0) throw std::invalid_argument("cache requires at least one slot");
  for (std::size_t i = 0; i < slots_; ++i) free_slots_.push_back(i);
}

LookupResult BlockCache::get_or_reserve(CacheKey key) {
  std::unique_lock<std::mutex> lock(mutex_);
  auto found = index_.find(key);
  if (found != index_.end()) {
    auto line = found->second;
    if (line->state == LineState::kFilling) {
      line->cv.wait(lock, [&] { return line->state != LineState::kFilling; });
      if (line->state == LineState::kValid) {
        ++line->users;
        touch_locked(line);
        return {line, false, true, true};
      }
      const int error = line->error;
      throw std::runtime_error("coalesced cache fill failed: " +
                               std::to_string(error));
    }
    if (line->state == LineState::kValid) {
      ++line->users;
      touch_locked(line);
      return {line, false, true, false};
    }
  }

  std::size_t slot = 0;
  if (!free_slots_.empty()) {
    slot = free_slots_.front();
    free_slots_.pop_front();
  } else {
    auto victim_it = lru_.end();
    while (victim_it != lru_.begin()) {
      --victim_it;
      if (index_.at(*victim_it)->users == 0) break;
    }
    if (victim_it == lru_.end() || index_.at(*victim_it)->users != 0)
      throw std::runtime_error("cache exhausted: all slots are in use");
    auto victim_key = *victim_it;
    lru_.erase(victim_it);
    auto victim = index_.at(victim_key);
    slot = victim->slot;
    index_.erase(victim_key);
    if (on_evict_) on_evict_(slot);
  }

  auto line = std::make_shared<CacheLine>();
  line->key = key;
  line->slot = slot;
  line->users = 1;
  lru_.push_front(key);
  line->lru_it = lru_.begin();
  index_[key] = line;
  return {line, true, false, false};
}

void BlockCache::release(const std::shared_ptr<CacheLine>& line) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (line->users == 0) throw std::logic_error("cache line release underflow");
  --line->users;
}

void BlockCache::publish(const std::shared_ptr<CacheLine>& line,
                         std::size_t valid_bytes, int error) {
  std::lock_guard<std::mutex> lock(mutex_);
  line->valid_bytes = valid_bytes;
  line->error = error;
  line->state = error == 0 ? LineState::kValid : LineState::kError;
  line->cv.notify_all();
}

void BlockCache::invalidate(CacheKey key) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = index_.find(key);
  if (it == index_.end()) return;
  auto line = it->second;
  if (line->state == LineState::kFilling) return;
  lru_.erase(line->lru_it);
  free_slots_.push_back(line->slot);
  index_.erase(it);
}

void BlockCache::touch_locked(const std::shared_ptr<CacheLine>& line) {
  lru_.splice(lru_.begin(), lru_, line->lru_it);
  line->lru_it = lru_.begin();
}

}  // namespace ghc::detail
