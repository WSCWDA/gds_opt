#include "cufile_backend.hpp"

#include <cerrno>
#include <stdexcept>

namespace ghc::detail {

CufileBackend::CufileBackend(int fd) {
#if GHC_ENABLE_GDS
  CUfileDescr_t desc{};
  desc.handle.fd = fd;
  desc.type = CU_FILE_HANDLE_TYPE_OPAQUE_FD;
  const auto rc = cuFileHandleRegister(&handle_, &desc);
  if (rc.err != CU_FILE_SUCCESS)
    throw std::runtime_error("cuFileHandleRegister failed: " +
                             std::to_string(rc.err));
  registered_ = true;
#else
  (void)fd;
#endif
}

CufileBackend::~CufileBackend() {
#if GHC_ENABLE_GDS
  if (registered_) cuFileHandleDeregister(handle_);
#endif
}

ssize_t CufileBackend::read(void* gpu_dst, std::size_t length,
                            std::uint64_t offset) {
#if GHC_ENABLE_GDS
  return cuFileRead(handle_, gpu_dst, length, offset, 0);
#else
  (void)gpu_dst;
  (void)length;
  (void)offset;
  return -ENOTSUP;
#endif
}

}  // namespace ghc::detail
