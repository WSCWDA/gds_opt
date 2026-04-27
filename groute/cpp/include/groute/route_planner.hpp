#pragma once

#include <string>

#include "groute/metadata.hpp"

namespace groute {

enum class RoutePath {
  kPosix,
  kCuFile,
};

struct RoutePlan {
  RoutePath path{RoutePath::kPosix};
  std::string reason;
};

struct PlannerConfig {
  std::size_t cufile_min_io_size{256 * 1024};
  bool cufile_available{false};
};

class RoutePlanner {
 public:
  explicit RoutePlanner(PlannerConfig cfg);

  RoutePlan plan(const FileMetadata& meta, const IORequest& req) const;

 private:
  PlannerConfig cfg_;
};

}  // namespace groute
