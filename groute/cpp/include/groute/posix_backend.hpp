#pragma once

#include "groute/backend.hpp"

namespace groute {

class PosixBackend : public Backend {
 public:
  IOResult read(const std::string& path, std::uint64_t offset, std::size_t size, void* dst) override;
  const char* name() const override { return "posix"; }
};

}  // namespace groute
