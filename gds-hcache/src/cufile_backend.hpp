#pragma once

#include <cstddef>
#include <cstdint>
#include <sys/types.h>

#if GHC_ENABLE_GDS
#include <cufile.h>
#endif

namespace ghc::detail {

class CufileBackend {
 public:
  explicit CufileBackend(int fd);
  ~CufileBackend();
  CufileBackend(const CufileBackend&) = delete;
  CufileBackend& operator=(const CufileBackend&) = delete;
  ssize_t read(void* gpu_dst, std::size_t length, std::uint64_t offset);

 private:
#if GHC_ENABLE_GDS
  CUfileHandle_t handle_{};
#endif
  bool registered_ = false;
};

}  // namespace ghc::detail
