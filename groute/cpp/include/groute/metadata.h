#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace groute {

enum class AccessPattern {
  kSequential,
  kRandom,
};

enum class LocationKind {
  LocalSSD_GDS,
  PosixCompat,
  HostCache,
  PeerGPU,
};

struct ChunkKey {
  std::uint64_t file_id{0};
  std::uint64_t chunk_id{0};
  std::uint32_t generation{0};

  bool operator==(const ChunkKey& other) const {
    return file_id == other.file_id && chunk_id == other.chunk_id && generation == other.generation;
  }
};

struct ChunkKeyHash {
  std::size_t operator()(const ChunkKey& key) const noexcept;
};

struct LocationRef {
  LocationKind kind{LocationKind::PosixCompat};
  int gpu_id{-1};
  int numa_id{-1};
  std::uint64_t offset{0};
  std::uint64_t length{0};
  std::uint32_t score_bias{0};
};

struct ChunkMeta {
  ChunkKey key;
  std::uint64_t logical_size{0};
  std::uint64_t dio_align{4096};
  float reuse_score{0.0F};
  std::uint64_t last_access_ns{0};
  std::vector<LocationRef> replicas;
};

struct FileMetadata {
  std::string path;
  std::uint64_t file_size{0};
  std::uint32_t block_size{4096};
  bool supports_direct_io{true};
  bool likely_hot_in_page_cache{false};
};

struct IORequest {
  std::uint64_t offset{0};
  std::size_t size{0};
  AccessPattern pattern{AccessPattern::kSequential};
};

class GlobalDirectory {
 public:
  bool insert_or_update(const ChunkMeta& meta);
  std::optional<ChunkMeta> lookup(const ChunkKey& key);
  void update_reuse_score(const ChunkKey& key, float delta);
  std::size_t size() const;

 private:
  mutable std::mutex mu_;
  std::unordered_map<ChunkKey, ChunkMeta, ChunkKeyHash> table_;
};

}  // namespace groute
