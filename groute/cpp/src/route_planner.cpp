#include "groute/route_planner.hpp"

namespace groute {

RoutePlanner::RoutePlanner(PlannerConfig cfg) : cfg_(cfg) {}

RoutePlan RoutePlanner::plan(const FileMetadata& meta, const IORequest& req) const {
  if (!cfg_.cufile_available) {
    return {RoutePath::kPosix, "cuFile unavailable (stub build)"};
  }

  if (!meta.supports_direct_io) {
    return {RoutePath::kPosix, "filesystem/direct-io unsupported"};
  }

  if (req.size < cfg_.cufile_min_io_size) {
    return {RoutePath::kPosix, "I/O too small for cuFile path"};
  }

  if (meta.likely_hot_in_page_cache && req.pattern == AccessPattern::kSequential) {
    return {RoutePath::kPosix, "page-cache hot sequential read"};
  }

  return {RoutePath::kCuFile, "large/cold request, prefer cuFile"};
}

}  // namespace groute
