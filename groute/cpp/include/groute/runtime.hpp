#pragma once

#include <memory>

#include "groute/backend.hpp"
#include "groute/cufile_stub_backend.hpp"
#include "groute/posix_backend.hpp"
#include "groute/route_planner.hpp"

namespace groute {

class Runtime {
 public:
  explicit Runtime(PlannerConfig cfg);

  RoutePlan last_plan() const { return last_plan_; }

  IOResult read(const FileMetadata& meta, const IORequest& req, void* dst);

 private:
  RoutePlanner planner_;
  PosixBackend posix_backend_;
  CuFileStubBackend cufile_stub_backend_;
  RoutePlan last_plan_{};
};

}  // namespace groute
