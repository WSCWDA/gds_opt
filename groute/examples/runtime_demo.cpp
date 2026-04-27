#include <fstream>
#include <iostream>
#include <vector>

#include "groute/runtime.hpp"

int main() {
  const std::string path = "./groute_demo_input.bin";
  {
    std::ofstream out(path, std::ios::binary);
    out << "hello-groute";
  }

  groute::PlannerConfig cfg;
  cfg.cufile_available = false;  // MVP stub build

  groute::Runtime rt(cfg);

  groute::FileMetadata meta;
  meta.path = path;
  meta.file_size = 12;
  meta.supports_direct_io = true;

  groute::IORequest req;
  req.offset = 0;
  req.size = 12;
  req.pattern = groute::AccessPattern::kSequential;

  std::vector<char> buf(req.size + 1, '\0');
  auto r = rt.read(meta, req, buf.data());
  if (!r.ok) {
    std::cerr << "read failed: " << r.error << "\n";
    return 1;
  }

  auto plan = rt.last_plan();
  std::cout << "route=" << (plan.path == groute::RoutePath::kPosix ? "posix" : "cufile")
            << " reason=" << plan.reason << " bytes=" << r.bytes << " payload=" << buf.data() << "\n";
  return 0;
}
