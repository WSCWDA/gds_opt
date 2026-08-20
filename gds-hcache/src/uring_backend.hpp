#pragma once

#include <cstddef>
#include <mutex>
#include <sys/types.h>

struct io_uring;
namespace ghc::detail {

class HostPool;

class UringBackend {
 public:
  UringBackend(HostPool& pool, unsigned queue_depth,
               std::size_t requested_region_size);
  ~UringBackend();
  UringBackend(const UringBackend&) = delete;
  UringBackend& operator=(const UringBackend&) = delete;

  ssize_t read(int fd, std::size_t slot, std::size_t length, off_t offset);

 private:
  HostPool& pool_;
  io_uring* ring_ = nullptr;
  bool fixed_ = false;
  std::size_t lines_per_region_ = 1;
  std::size_t registered_regions_ = 0;
  std::mutex mutex_;
};

}  // namespace ghc::detail
