#pragma once
#include <stdexcept>
#include <string>
namespace diskann::iobench {
enum class IOBackendType { LIBAIO, PREAD_DIRECT, PREAD_BUFFERED, IO_URING, GDS };
inline IOBackendType parse_io_backend(const std::string &s) {
  if (s == "libaio") return IOBackendType::LIBAIO;
  if (s == "pread-direct") return IOBackendType::PREAD_DIRECT;
  if (s == "pread-buffered") return IOBackendType::PREAD_BUFFERED;
  if (s == "io-uring") return IOBackendType::IO_URING;
  if (s == "gds") return IOBackendType::GDS;
  throw std::invalid_argument("Unknown I/O backend: " + s);
}
inline const char *to_string(IOBackendType t) {
  switch (t) {
  case IOBackendType::LIBAIO: return "libaio";
  case IOBackendType::PREAD_DIRECT: return "pread-direct";
  case IOBackendType::PREAD_BUFFERED: return "pread-buffered";
  case IOBackendType::IO_URING: return "io-uring";
  case IOBackendType::GDS: return "gds";
  }
  return "unknown";
}
} // namespace diskann::iobench
