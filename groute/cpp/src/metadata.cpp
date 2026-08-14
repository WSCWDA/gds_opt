#include "groute/metadata.h"

namespace groute {

bool GlobalDirectory::insert_or_update(const ChunkMeta& meta) {
  std::lock_guard<std::mutex> lock(mu_);
  const auto [it, inserted] = table_.insert_or_assign(meta.key, meta);
  (void)it;
  return inserted;
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
