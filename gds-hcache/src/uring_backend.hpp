#pragma once

#include <cstddef>
#include <mutex>
#include <sys/types.h>

struct io_uring;
namespace ghc::detail {

class HostPool;

class UringBackend {
 public:
  UringBackend(HostPool& pool, unsigned queue_depth);
  ~UringBackend();
  UringBackend(const UringBackend&) = delete;
  UringBackend& operator=(const UringBackend&) = delete;

  ssize_t read(int fd, std::size_t slot, std::size_t length, off_t offset);

 private:
  HostPool& pool_;
  io_uring* ring_ = nullptr;
  bool fixed_ = false;
  std::mutex mutex_;
};

}  // namespace ghc::detail
