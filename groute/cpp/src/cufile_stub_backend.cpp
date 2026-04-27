#include "groute/cufile_stub_backend.hpp"

namespace groute {

IOResult CuFileStubBackend::read(const std::string&, std::uint64_t, std::size_t, void*) {
  return {false, 0, "cuFile backend is a stub in MVP build (no GDS dependency)"};
}

}  // namespace groute
