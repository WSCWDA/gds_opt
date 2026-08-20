#include "uring_backend.hpp"

#include "host_pool.hpp"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

#if GHC_ENABLE_URING
#include <liburing.h>
#include <sys/uio.h>
#endif

namespace ghc::detail {

UringBackend::UringBackend(HostPool& pool, unsigned queue_depth,
                           std::size_t requested_region_size)
    : pool_(pool) {
#if GHC_ENABLE_URING
  ring_ = new io_uring{};
  int rc = io_uring_queue_init(queue_depth, ring_, 0);
  if (rc < 0) {
    delete ring_;
    ring_ = nullptr;
    throw std::runtime_error("io_uring_queue_init: " + std::to_string(-rc));
  }
  long max_iov_value = ::sysconf(_SC_IOV_MAX);
  const std::size_t max_iov = max_iov_value > 0
      ? static_cast<std::size_t>(max_iov_value)
      : 1024;
  const std::size_t line_size = pool_.line_size();
  const std::size_t min_region_size =
      ((pool_.capacity() + max_iov - 1) / max_iov + line_size - 1) /
      line_size * line_size;
  const std::size_t region_size =
      (std::max(requested_region_size, min_region_size) + line_size - 1) /
      line_size * line_size;
  constexpr std::size_t kMaxRegisteredRegion = 1ULL << 30;
  if (region_size > kMaxRegisteredRegion) {
    io_uring_queue_exit(ring_);
    delete ring_;
    ring_ = nullptr;
    throw std::invalid_argument(
        "fixed-buffer region exceeds io_uring 1 GiB iovec limit");
  }

  lines_per_region_ = region_size / line_size;
  registered_regions_ =
      (pool_.slots() + lines_per_region_ - 1) / lines_per_region_;
  if (registered_regions_ == 0 || registered_regions_ > max_iov ||
      registered_regions_ > std::numeric_limits<unsigned short>::max()) {
    io_uring_queue_exit(ring_);
    delete ring_;
    ring_ = nullptr;
    throw std::invalid_argument(
        "fixed-buffer region count exceeds kernel/index limit");
  }

  std::vector<iovec> iov(registered_regions_);
  for (std::size_t i = 0; i < registered_regions_; ++i) {
    const std::size_t first_slot = i * lines_per_region_;
    const std::size_t remaining_slots = pool_.slots() - first_slot;
    const std::size_t region_lines =
        std::min(lines_per_region_, remaining_slots);
    iov[i] = {pool_.slot(first_slot), region_lines * line_size};
  }
  rc = io_uring_register_buffers(ring_, iov.data(), iov.size());
  if (rc < 0) {
    io_uring_queue_exit(ring_);
    delete ring_;
    ring_ = nullptr;
    throw std::runtime_error(
        "io_uring_register_buffers: " + std::to_string(-rc) + " (" +
        std::strerror(-rc) + "), regions=" +
        std::to_string(registered_regions_) + ", region_bytes=" +
        std::to_string(region_size));
  }
  fixed_ = true;
#else
  (void)queue_depth;
  (void)requested_region_size;
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
  const std::size_t buffer_index = slot / lines_per_region_;
  io_uring_prep_read_fixed(sqe, fd, pool_.slot(slot), length, offset,
                           static_cast<int>(buffer_index));
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
