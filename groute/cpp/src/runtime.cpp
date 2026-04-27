#include "groute/runtime.hpp"

namespace groute {

Runtime::Runtime(PlannerConfig cfg) : planner_(cfg) {}

IOResult Runtime::read(const FileMetadata& meta, const IORequest& req, void* dst) {
  last_plan_ = planner_.plan(meta, req);

  if (last_plan_.path == RoutePath::kCuFile) {
    auto r = cufile_stub_backend_.read(meta.path, req.offset, req.size, dst);
    if (r.ok) {
      return r;
    }
    // Fallback to POSIX path when stub fails.
    auto fb = posix_backend_.read(meta.path, req.offset, req.size, dst);
    if (!fb.ok) {
      fb.error = std::string("cuFile path failed: ") + r.error + "; fallback failed: " + fb.error;
    }
    return fb;
  }

  return posix_backend_.read(meta.path, req.offset, req.size, dst);
}

}  // namespace groute
