#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#if GHC_ENABLE_CUDA
#include <cuda_runtime_api.h>
#else
using cudaStream_t = void*;
#endif

namespace ghc {

struct Options {
  std::size_t cache_capacity = 1ULL << 30;
  std::size_t cache_line_size = 64ULL << 10;
  std::size_t host_max_io_size = 64ULL << 10;
  // One io_uring fixed-buffer registration covers many cache lines.
  // The backend may increase this value to stay below IOV_MAX.
  std::size_t fixed_buffer_region_size = 64ULL << 20;
  unsigned queue_depth = 64;
  int cuda_device = 0;
  bool enable_cache = true;
  bool sync_before_return = true;
};

struct Stats {
  std::uint64_t gds_reads = 0;
  std::uint64_t host_hits = 0;
  std::uint64_t host_misses = 0;
  std::uint64_t coalesced_waits = 0;
  std::uint64_t evictions = 0;
  std::uint64_t fallbacks = 0;
  std::uint64_t storage_bytes = 0;
  std::uint64_t h2d_bytes = 0;
};

class File;

class Context {
 public:
  explicit Context(Options options = {});
  ~Context();

  Context(const Context&) = delete;
  Context& operator=(const Context&) = delete;

  std::unique_ptr<File> open(const std::string& path);
  Stats stats() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  friend class File;
};

class File {
 public:
  ~File();
  File(const File&) = delete;
  File& operator=(const File&) = delete;

  // Read-only synchronous MVP. Returns bytes copied or throws std::system_error.
  std::ptrdiff_t read(void* gpu_dst, std::size_t length,
                      std::uint64_t file_offset,
                      cudaStream_t stream = nullptr);
  std::uint64_t size() const noexcept;

 private:
  struct Impl;
  explicit File(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
  friend class Context;
};

}  // namespace ghc
