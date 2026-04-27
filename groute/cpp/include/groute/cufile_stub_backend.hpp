#pragma once

#include "groute/backend.hpp"

namespace groute {

class CuFileStubBackend : public Backend {
 public:
  IOResult read(const std::string& path, std::uint64_t offset, std::size_t size, void* dst) override;
  const char* name() const override { return "cufile_stub"; }
};

}  // namespace groute
