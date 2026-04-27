#include "groute/metadata.h"

#include <functional>

namespace groute {

namespace {
inline std::size_t hash_combine(std::size_t seed, std::size_t value) {
  return seed ^ (value + 0x9e3779b97f4a7c15ULL + (seed << 6U) + (seed >> 2U));
}
}  // namespace

std::size_t ChunkKeyHash::operator()(const ChunkKey& key) const noexcept {
  std::size_t h = std::hash<std::uint64_t>{}(key.file_id);
  h = hash_combine(h, std::hash<std::uint64_t>{}(key.chunk_id));
  h = hash_combine(h, std::hash<std::uint32_t>{}(key.generation));
  return h;
}

bool GlobalDirectory::insert_or_update(const ChunkMeta& meta) {
  std::lock_guard<std::mutex> lock(mu_);
  table_[meta.key] = meta;
  return true;
}

std::optional<ChunkMeta> GlobalDirectory::lookup(const ChunkKey& key) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = table_.find(key);
  if (it == table_.end()) {
    return std::nullopt;
  }
  return it->second;
}

void GlobalDirectory::update_reuse_score(const ChunkKey& key, float delta) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = table_.find(key);
  if (it == table_.end()) {
    return;
  }
  it->second.reuse_score += delta;
}

std::size_t GlobalDirectory::size() const {
  std::lock_guard<std::mutex> lock(mu_);
  return table_.size();
}

}  // namespace groute
