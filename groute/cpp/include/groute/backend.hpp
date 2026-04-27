#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace groute {

struct IOResult {
  bool ok{false};
  std::size_t bytes{0};
  std::string error;
};

class Backend {
 public:
  virtual ~Backend() = default;

  virtual IOResult read(const std::string& path, std::uint64_t offset, std::size_t size,
                        void* dst) = 0;
  virtual const char* name() const = 0;
};

}  // namespace groute
