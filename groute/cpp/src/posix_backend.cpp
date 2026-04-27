#include "groute/posix_backend.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace groute {

IOResult PosixBackend::read(const std::string& path, std::uint64_t offset, std::size_t size, void* dst) {
  IOResult out;
  int fd = ::open(path.c_str(), O_RDONLY);
  if (fd < 0) {
    out.error = std::string("open failed: ") + std::strerror(errno);
    return out;
  }

  ssize_t n = ::pread(fd, dst, size, static_cast<off_t>(offset));
  ::close(fd);

  if (n < 0) {
    out.error = std::string("pread failed: ") + std::strerror(errno);
    return out;
  }

  out.ok = true;
  out.bytes = static_cast<std::size_t>(n);
  return out;
}

}  // namespace groute
