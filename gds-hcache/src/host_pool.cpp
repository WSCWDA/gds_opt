#include "host_pool.hpp"

#include <cstdlib>
#include <cstring>
#include <stdexcept>

#if GHC_ENABLE_CUDA
#include <cuda_runtime_api.h>
#endif

namespace ghc::detail {

HostPool::HostPool(std::size_t capacity, std::size_t line_size)
    : capacity_(capacity), line_size_(line_size) {
  if (line_size_ == 0 || capacity_ < line_size_ || capacity_ % line_size_ != 0)
    throw std::invalid_argument("capacity must be a positive multiple of line size");
  if (posix_memalign(&base_, 4096, capacity_) != 0)
    throw std::bad_alloc();
  std::memset(base_, 0, capacity_);
#if GHC_ENABLE_CUDA
  const auto rc = cudaHostRegister(base_, capacity_, cudaHostRegisterDefault);
  if (rc != cudaSuccess) {
    std::free(base_);
    base_ = nullptr;
    throw std::runtime_error(std::string("cudaHostRegister: ") +
                             cudaGetErrorString(rc));
  }
  cuda_registered_ = true;
#endif
}

HostPool::~HostPool() {
#if GHC_ENABLE_CUDA
  if (cuda_registered_) cudaHostUnregister(base_);
#endif
  std::free(base_);
}

void* HostPool::slot(std::size_t id) const noexcept {
  return static_cast<unsigned char*>(base_) + id * line_size_;
}

}  // namespace ghc::detail
