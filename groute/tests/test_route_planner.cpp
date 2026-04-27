#include <cassert>

#include "groute/route_planner.hpp"

int main() {
  groute::PlannerConfig cfg;
  cfg.cufile_available = false;
  groute::RoutePlanner p(cfg);

  groute::FileMetadata m;
  m.path = "dummy";
  m.supports_direct_io = true;

  groute::IORequest r;
  r.size = 1024 * 1024;

  auto plan = p.plan(m, r);
  assert(plan.path == groute::RoutePath::kPosix);
  return 0;
}
