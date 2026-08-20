#include "uring_backend.hpp"

#include "host_pool.hpp"

#include <cerrno>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

#if GHC_ENABLE_URING
#include <liburing.h>
#include <sys/uio.h>
#endif

namespace ghc::detail {

UringBackend::UringBackend(HostPool& pool, unsigned queue_depth) : pool_(pool) {
#if GHC_ENABLE_URING
  ring_ = new io_uring{};
  int rc = io_uring_queue_init(queue_depth, ring_, 0);
  if (rc < 0) {
    delete ring_;
    ring_ = nullptr;
    throw std::runtime_error("io_uring_queue_init: " + std::to_string(-rc));
  }
  std::vector<iovec> iov(pool_.slots());
  for (std::size_t i = 0; i < pool_.slots(); ++i)
    iov[i] = {pool_.slot(i), pool_.line_size()};
  rc = io_uring_register_buffers(ring_, iov.data(), iov.size());
  if (rc < 0) {
    io_uring_queue_exit(ring_);
    delete ring_;
    ring_ = nullptr;
    throw std::runtime_error("io_uring_register_buffers: " + std::to_string(-rc));
  }
  fixed_ = true;
#else
  (void)queue_depth;
#endif
}

UringBackend::~UringBackend() {
#if GHC_ENABLE_URING
  if (ring_) {
    if (fixed_) io_uring_unregister_buffers(ring_);
    io_uring_queue_exit(ring_);
    delete ring_;
  }
#endif
}

ssize_t UringBackend::read(int fd, std::size_t slot, std::size_t length,
                           off_t offset) {
  std::lock_guard<std::mutex> lock(mutex_);
#if GHC_ENABLE_URING
  auto* sqe = io_uring_get_sqe(ring_);
  if (!sqe) return -EAGAIN;
  io_uring_prep_read_fixed(sqe, fd, pool_.slot(slot), length, offset, slot);
  int rc = io_uring_submit(ring_);
  if (rc < 0) return rc;
  io_uring_cqe* cqe = nullptr;
  rc = io_uring_wait_cqe(ring_, &cqe);
  if (rc < 0) return rc;
  const int result = cqe->res;
  io_uring_cqe_seen(ring_, cqe);
  return result;
#else
  return ::pread(fd, pool_.slot(slot), length, offset);
#endif
}

}  // namespace ghc::detail
