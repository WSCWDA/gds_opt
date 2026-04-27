#include <cuda_runtime.h>
#include <cufile.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

enum class PathType { Buffered, Direct, Gds };
enum class Pattern { Sequential, Random };

struct Config {
  std::string root{"/home/cwd/dataset/ImageNet/train/"};
  std::size_t min_size{4096};
  std::size_t max_size{262144};
  std::size_t max_files{50000};
  Pattern pattern{Pattern::Sequential};
  std::size_t runs{2};
  bool drop_cache_before_cold{false};
  std::string path_arg{"all"};
  std::string csv_path{"results/imagenet_compare.csv"};
  std::uint64_t seed{42};
};

struct Stats {
  std::size_t file_count{0};
  std::uint64_t total_bytes{0};
  std::size_t failed_files{0};
  double throughput_gbps{0.0};
  double avg_latency_us{0.0};
  double p50_latency_us{0.0};
  double p95_latency_us{0.0};
  double p99_latency_us{0.0};
};

struct FileItem {
  std::string path;
  std::size_t size{0};
};

namespace {

constexpr std::size_t kAlign = 4096;

std::string to_string(PathType p) {
  switch (p) {
    case PathType::Buffered:
      return "buffered";
    case PathType::Direct:
      return "direct";
    case PathType::Gds:
      return "gds";
  }
  return "unknown";
}

std::string to_string(Pattern p) { return p == Pattern::Sequential ? "sequential" : "random"; }

std::size_t align_up(std::size_t v) { return ((v + kAlign - 1) / kAlign) * kAlign; }

bool parse_u64(const std::string& s, std::uint64_t* out) {
  try {
    *out = std::stoull(s);
    return true;
  } catch (...) {
    return false;
  }
}

bool parse_args(int argc, char** argv, Config* cfg, std::string* err) {
  for (int i = 1; i < argc; ++i) {
    std::string arg(argv[i]);
    if (arg.rfind("--", 0) != 0) {
      *err = "invalid arg: " + arg;
      return false;
    }
    auto eq = arg.find('=');
    if (eq == std::string::npos) {
      *err = "invalid arg format: " + arg;
      return false;
    }
    std::string key = arg.substr(2, eq - 2);
    std::string val = arg.substr(eq + 1);

    if (key == "root") cfg->root = val;
    else if (key == "min_size") {
      std::uint64_t v; if (!parse_u64(val, &v)) { *err = "invalid --min_size"; return false; }
      cfg->min_size = static_cast<std::size_t>(v);
    } else if (key == "max_size") {
      std::uint64_t v; if (!parse_u64(val, &v)) { *err = "invalid --max_size"; return false; }
      cfg->max_size = static_cast<std::size_t>(v);
    } else if (key == "max_files") {
      std::uint64_t v; if (!parse_u64(val, &v)) { *err = "invalid --max_files"; return false; }
      cfg->max_files = static_cast<std::size_t>(v);
    } else if (key == "pattern") {
      if (val == "seq") cfg->pattern = Pattern::Sequential;
      else if (val == "rand") cfg->pattern = Pattern::Random;
      else { *err = "invalid --pattern (seq|rand)"; return false; }
    } else if (key == "runs") {
      std::uint64_t v; if (!parse_u64(val, &v) || v == 0) { *err = "invalid --runs"; return false; }
      cfg->runs = static_cast<std::size_t>(v);
    } else if (key == "drop_cache_before_cold") {
      cfg->drop_cache_before_cold = (val == "1");
    } else if (key == "path") {
      cfg->path_arg = val;
    } else if (key == "csv") {
      cfg->csv_path = val;
    } else if (key == "seed") {
      std::uint64_t v; if (!parse_u64(val, &v)) { *err = "invalid --seed"; return false; }
      cfg->seed = v;
    } else {
      *err = "unknown arg: --" + key;
      return false;
    }
  }
  return true;
}

void usage(const char* prog) {
  std::cerr << "Usage: " << prog
            << " [--root=/home/cwd/dataset/ImageNet/train/] [--path=buffered|direct|gds|all]"
            << " [--pattern=seq|rand] [--min_size=4096] [--max_size=262144]"
            << " [--max_files=50000] [--runs=2] [--drop_cache_before_cold=0|1]"
            << " [--csv=results/imagenet_compare.csv] [--seed=42]\n";
}

std::vector<PathType> resolve_paths(const std::string& arg) {
  if (arg == "buffered") return {PathType::Buffered};
  if (arg == "direct") return {PathType::Direct};
  if (arg == "gds") return {PathType::Gds};
  return {PathType::Buffered, PathType::Direct, PathType::Gds};
}

std::vector<FileItem> collect_files(const Config& cfg) {
  std::vector<FileItem> files;
  for (const auto& e : fs::recursive_directory_iterator(cfg.root)) {
    if (!e.is_regular_file()) continue;
    std::uint64_t sz = e.file_size();
    if (sz < cfg.min_size || sz > cfg.max_size) continue;
    files.push_back({e.path().string(), static_cast<std::size_t>(sz)});
  }
  return files;
}

double percentile(std::vector<double> v, double pct) {
  if (v.empty()) return 0.0;
  std::sort(v.begin(), v.end());
  double idx = pct / 100.0 * static_cast<double>(v.size() - 1);
  std::size_t lo = static_cast<std::size_t>(idx);
  std::size_t hi = std::min(lo + 1, v.size() - 1);
  double frac = idx - static_cast<double>(lo);
  return v[lo] * (1.0 - frac) + v[hi] * frac;
}

bool ensure_parent(const std::string& path) {
  auto p = fs::path(path).parent_path();
  if (p.empty()) return true;
  std::error_code ec;
  fs::create_directories(p, ec);
  return !ec;
}

void append_csv(const std::string& csv, const std::string& run_state, const Config& cfg, PathType path,
                const Stats& s) {
  ensure_parent(csv);
  bool header = false;
  {
    std::ifstream in(csv);
    header = !in.good() || in.peek() == std::ifstream::traits_type::eof();
  }
  std::ofstream out(csv, std::ios::app);
  if (!out.is_open()) return;
  if (header) {
    out << "path_type,access_pattern,run_state,file_count,total_bytes,throughput_gbps,avg_latency_us,p50_latency_us,p95_latency_us,p99_latency_us,failed_files\n";
  }
  out << to_string(path) << ',' << to_string(cfg.pattern) << ',' << run_state << ',' << s.file_count << ','
      << s.total_bytes << ',' << std::fixed << std::setprecision(6) << s.throughput_gbps << ','
      << s.avg_latency_us << ',' << s.p50_latency_us << ',' << s.p95_latency_us << ','
      << s.p99_latency_us << ',' << s.failed_files << '\n';
}

std::optional<std::string> try_drop_cache() {
  int rc = std::system("sync && sh -c 'echo 3 > /proc/sys/vm/drop_caches' >/dev/null 2>&1");
  if (rc != 0) {
    return std::string("drop_caches failed (need root?). continue without cache drop");
  }
  return std::nullopt;
}

std::optional<std::string> run_one(const Config& cfg, const std::vector<FileItem>& files, PathType path,
                                   const std::string& run_state, Stats* out) {
  out->file_count = files.size();

  void* host_buf = nullptr;
  void* gpu_buf = nullptr;

  std::size_t max_size = 0;
  for (const auto& f : files) max_size = std::max(max_size, f.size);
  std::size_t max_aligned = align_up(max_size);

  if (path == PathType::Buffered) {
    host_buf = std::malloc(max_aligned);
  } else if (path == PathType::Direct) {
    if (cudaHostAlloc(&host_buf, max_aligned, cudaHostAllocDefault) != cudaSuccess) {
      return "cudaHostAlloc failed";
    }
  }

  if (cudaMalloc(&gpu_buf, max_aligned) != cudaSuccess) {
    if (path == PathType::Buffered && host_buf) std::free(host_buf);
    if (path == PathType::Direct && host_buf) cudaFreeHost(host_buf);
    return "cudaMalloc failed";
  }

  CUfileError_t st{};
  if (path == PathType::Gds) {
    st = cuFileDriverOpen();
    if (st.err != CU_FILE_SUCCESS) {
      cudaFree(gpu_buf);
      return "GDS unavailable: cuFileDriverOpen failed code=" + std::to_string(st.err);
    }
    st = cuFileBufRegister(gpu_buf, max_aligned, 0);
    if (st.err != CU_FILE_SUCCESS) {
      cuFileDriverClose();
      cudaFree(gpu_buf);
      return "GDS unavailable: cuFileBufRegister failed code=" + std::to_string(st.err);
    }
  }

  std::vector<double> lat;
  lat.reserve(files.size());
  auto t0 = std::chrono::steady_clock::now();

  for (const auto& file : files) {
    auto r0 = std::chrono::steady_clock::now();

    if (path == PathType::Buffered || path == PathType::Direct) {
      int flags = O_RDONLY | ((path == PathType::Direct) ? O_DIRECT : 0);
      int fd = ::open(file.path.c_str(), flags);
      if (fd < 0) {
        out->failed_files++;
        continue;
      }

      std::size_t want = (path == PathType::Direct) ? align_up(file.size) : file.size;
      ssize_t n = pread(fd, host_buf, want, 0);
      close(fd);

      if (n < static_cast<ssize_t>(file.size)) {
        out->failed_files++;
        continue;
      }

      if (cudaMemcpy(gpu_buf, host_buf, file.size, cudaMemcpyHostToDevice) != cudaSuccess) {
        out->failed_files++;
        continue;
      }
      cudaDeviceSynchronize();
    } else {
      int fd = ::open(file.path.c_str(), O_RDONLY | O_DIRECT);
      if (fd < 0) {
        out->failed_files++;
        continue;
      }

      CUfileDescr_t d{};
      d.type = CU_FILE_HANDLE_TYPE_OPAQUE_FD;
      d.handle.fd = fd;
      CUfileHandle_t h{};
      st = cuFileHandleRegister(&h, &d);
      if (st.err != CU_FILE_SUCCESS) {
        close(fd);
        out->failed_files++;
        continue;
      }

      ssize_t n = cuFileRead(h, gpu_buf, align_up(file.size), 0, 0);
      cuFileHandleDeregister(h);
      close(fd);

      if (n < static_cast<ssize_t>(file.size)) {
        out->failed_files++;
        continue;
      }
    }

    auto r1 = std::chrono::steady_clock::now();
    lat.push_back(std::chrono::duration<double, std::micro>(r1 - r0).count());
    out->total_bytes += file.size;
  }

  auto t1 = std::chrono::steady_clock::now();

  if (path == PathType::Gds) {
    cuFileBufDeregister(gpu_buf);
    cuFileDriverClose();
  }

  cudaFree(gpu_buf);
  if (path == PathType::Buffered) std::free(host_buf);
  if (path == PathType::Direct) cudaFreeHost(host_buf);

  if (lat.empty()) {
    return "no successful samples";
  }

  double wall_us = std::chrono::duration<double, std::micro>(t1 - t0).count();
  out->throughput_gbps = (static_cast<double>(out->total_bytes) / (wall_us / 1e6)) / 1e9;
  out->avg_latency_us = std::accumulate(lat.begin(), lat.end(), 0.0) / static_cast<double>(lat.size());
  out->p50_latency_us = percentile(lat, 50);
  out->p95_latency_us = percentile(lat, 95);
  out->p99_latency_us = percentile(lat, 99);
  return std::nullopt;
}

void print_stats(PathType path, const std::string& run_state, const Stats& s) {
  std::cout << "[" << to_string(path) << "] run_state=" << run_state
            << " files=" << s.file_count
            << " failed=" << s.failed_files
            << " throughput_gbps=" << std::fixed << std::setprecision(6) << s.throughput_gbps
            << " avg/p50/p95/p99(us)=" << s.avg_latency_us << "/" << s.p50_latency_us
            << "/" << s.p95_latency_us << "/" << s.p99_latency_us << "\n";
}

}  // namespace

int main(int argc, char** argv) {
  Config cfg;
  std::string err;
  if (!parse_args(argc, argv, &cfg, &err)) {
    std::cerr << "Error: " << err << "\n";
    usage(argv[0]);
    return 1;
  }

  if (!fs::exists(cfg.root)) {
    std::cerr << "Error: dataset root not found: " << cfg.root << "\n";
    return 1;
  }

  std::vector<FileItem> files = collect_files(cfg);
  if (files.empty()) {
    std::cerr << "Error: no files matched size filter in: " << cfg.root << "\n";
    return 1;
  }

  std::mt19937_64 rng(cfg.seed);
  std::shuffle(files.begin(), files.end(), rng);
  if (files.size() > cfg.max_files) files.resize(cfg.max_files);
  if (cfg.pattern == Pattern::Sequential) {
    std::sort(files.begin(), files.end(), [](const FileItem& a, const FileItem& b) { return a.path < b.path; });
  }

  auto paths = resolve_paths(cfg.path_arg);

  std::cout << "Dataset root: " << cfg.root << "\n"
            << "Selected files: " << files.size() << " (size range " << cfg.min_size << "~" << cfg.max_size << ")\n"
            << "Pattern: " << to_string(cfg.pattern) << "\n";

  for (std::size_t run = 0; run < cfg.runs; ++run) {
    std::string run_state = (run == 0) ? "cold" : "warm";

    if (run == 0 && cfg.drop_cache_before_cold) {
      auto warn = try_drop_cache();
      if (warn.has_value()) std::cerr << "[WARN] " << warn.value() << "\n";
    }

    for (auto p : paths) {
      Stats s;
      auto run_err = run_one(cfg, files, p, run_state, &s);
      if (run_err.has_value()) {
        std::cout << "[" << to_string(p) << "] run_state=" << run_state << " FAILED: " << run_err.value()
                  << "\n";
        continue;
      }
      print_stats(p, run_state, s);
      append_csv(cfg.csv_path, run_state, cfg, p, s);
    }
  }

  return 0;
}
