#include <ghc/ghc.hpp>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>

#if GHC_ENABLE_CUDA
#include <cuda_runtime_api.h>
#endif

namespace {
std::string value(int argc, char** argv, const std::string& name,
                  const std::string& fallback = {}) {
  const std::string prefix = "--" + name + "=";
  for (int i = 1; i < argc; ++i) {
    std::string arg(argv[i]);
    if (arg.rfind(prefix, 0) == 0) return arg.substr(prefix.size());
  }
  return fallback;
}
}  // namespace

int main(int argc, char** argv) try {
  const auto path = value(argc, argv, "file");
  if (path.empty()) throw std::invalid_argument("required: --file=PATH");
  const auto io_size = std::stoull(value(argc, argv, "io-size", "4096"));
  const auto requests = std::stoull(value(argc, argv, "requests", "10000"));
  const auto hot_bytes = std::stoull(value(argc, argv, "hot-bytes", "67108864"));

  ghc::Options options;
  options.cache_capacity = std::stoull(value(argc, argv, "cache-bytes", "268435456"));
  options.cache_line_size = std::stoull(value(argc, argv, "line-size", "65536"));
  options.host_max_io_size = std::stoull(value(argc, argv, "host-max", "65536"));
  options.cuda_device = std::stoi(value(argc, argv, "device", "0"));
  options.enable_cache = value(argc, argv, "cache", "1") != "0";

  ghc::Context context(options);
  auto file = context.open(path);
  const auto working_set = std::min<std::uint64_t>(file->size(), hot_bytes);
  if (working_set < io_size) throw std::runtime_error("working set smaller than I/O");

  void* buffer = nullptr;
#if GHC_ENABLE_CUDA
  if (cudaMalloc(&buffer, io_size) != cudaSuccess) throw std::runtime_error("cudaMalloc failed");
#else
  buffer = std::malloc(io_size);
#endif

  std::mt19937_64 rng(42);
  std::uniform_int_distribution<std::uint64_t> dist(0, working_set / io_size - 1);
  const auto begin = std::chrono::steady_clock::now();
  std::uint64_t completed = 0;
  for (std::uint64_t i = 0; i < requests; ++i) {
    const auto off = dist(rng) * io_size;
    if (file->read(buffer, io_size, off) == static_cast<std::ptrdiff_t>(io_size)) ++completed;
  }
  const auto end = std::chrono::steady_clock::now();
  const auto seconds = std::chrono::duration<double>(end - begin).count();
  const auto stats = context.stats();

  std::cout << "requests=" << completed << " seconds=" << seconds
            << " iops=" << completed / seconds
            << " gds_reads=" << stats.gds_reads
            << " host_hits=" << stats.host_hits
            << " host_misses=" << stats.host_misses
            << " coalesced=" << stats.coalesced_waits
            << " evictions=" << stats.evictions
            << " storage_bytes=" << stats.storage_bytes
            << " h2d_bytes=" << stats.h2d_bytes << '\n';
#if GHC_ENABLE_CUDA
  cudaFree(buffer);
#else
  std::free(buffer);
#endif
  return 0;
} catch (const std::exception& e) {
  std::cerr << "ghc_bench: " << e.what() << '\n';
  return 1;
}
