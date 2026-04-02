#include <cuda_runtime.h>
#include <cufile.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <numeric>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

enum class PathType { Buffered, Direct, Gds };
enum class AccessPattern { Sequential, Random };

struct Config {
  std::string file_path;
  std::uint64_t file_size{0};
  std::vector<std::size_t> io_sizes{4096, 16384, 65536, 262144, 1048576, 4194304, 16777216};
  std::size_t requests{1000};
  std::size_t threads{1};
  std::size_t queue_depth{1};
  AccessPattern pattern{AccessPattern::Sequential};
  bool aligned{true};
  std::string csv_path;
  std::string path_arg{"all"};
};

struct Metrics {
  std::uint64_t total_bytes{0};
  double throughput_gbps{0.0};
  double avg_latency_us{0.0};
  double p50_latency_us{0.0};
  double p95_latency_us{0.0};
  double p99_latency_us{0.0};
  double cpu_time_us{0.0};
  double memcpy_time_us{0.0};
  double wall_time_us{0.0};
};

struct RunResult {
  bool ok{false};
  std::string error;
  Metrics metrics;
};

namespace {

constexpr std::size_t kAlign = 4096;

std::string to_string(PathType p) {
  switch (p) {
    case PathType::Buffered:
      return "posix_buffered";
    case PathType::Direct:
      return "posix_direct";
    case PathType::Gds:
      return "gds_direct";
  }
  return "unknown";
}

std::string to_string(AccessPattern p) {
  return p == AccessPattern::Sequential ? "sequential" : "random";
}

bool parse_bool01(const std::string& s, bool* out) {
  if (s == "1") {
    *out = true;
    return true;
  }
  if (s == "0") {
    *out = false;
    return true;
  }
  return false;
}

bool parse_u64(const std::string& s, std::uint64_t* out) {
  try {
    *out = std::stoull(s);
    return true;
  } catch (...) {
    return false;
  }
}

bool parse_sizes(const std::string& s, std::vector<std::size_t>* out) {
  std::vector<std::size_t> values;
  std::stringstream ss(s);
  std::string token;
  while (std::getline(ss, token, ',')) {
    if (token.empty()) {
      return false;
    }
    std::uint64_t v = 0;
    if (!parse_u64(token, &v) || v == 0) {
      return false;
    }
    values.push_back(static_cast<std::size_t>(v));
  }
  if (values.empty()) {
    return false;
  }
  *out = values;
  return true;
}

void print_usage(const char* argv0) {
  std::cerr
      << "Usage: " << argv0 << " --file=PATH [--file_size=BYTES] [--path=buffered|direct|gds|all]\\n"
      << "       [--io_sizes=4096,16384,...] [--requests=N] [--threads=N] [--qd=N]\\n"
      << "       [--pattern=seq|rand] [--aligned=1|0] [--csv=OUT.csv]\\n";
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
    std::string value = arg.substr(eq + 1);

    if (key == "file") {
      cfg->file_path = value;
    } else if (key == "file_size") {
      if (!parse_u64(value, &cfg->file_size)) {
        *err = "invalid --file_size";
        return false;
      }
    } else if (key == "io_sizes") {
      if (!parse_sizes(value, &cfg->io_sizes)) {
        *err = "invalid --io_sizes";
        return false;
      }
    } else if (key == "requests") {
      std::uint64_t v = 0;
      if (!parse_u64(value, &v) || v == 0) {
        *err = "invalid --requests";
        return false;
      }
      cfg->requests = static_cast<std::size_t>(v);
    } else if (key == "threads") {
      std::uint64_t v = 0;
      if (!parse_u64(value, &v) || v == 0) {
        *err = "invalid --threads";
        return false;
      }
      cfg->threads = static_cast<std::size_t>(v);
    } else if (key == "qd") {
      std::uint64_t v = 0;
      if (!parse_u64(value, &v) || v == 0) {
        *err = "invalid --qd";
        return false;
      }
      cfg->queue_depth = static_cast<std::size_t>(v);
    } else if (key == "pattern") {
      if (value == "seq") {
        cfg->pattern = AccessPattern::Sequential;
      } else if (value == "rand") {
        cfg->pattern = AccessPattern::Random;
      } else {
        *err = "invalid --pattern (seq|rand)";
        return false;
      }
    } else if (key == "aligned") {
      if (!parse_bool01(value, &cfg->aligned)) {
        *err = "invalid --aligned (1|0)";
        return false;
      }
    } else if (key == "csv") {
      cfg->csv_path = value;
    } else if (key == "path") {
      cfg->path_arg = value;
    } else {
      *err = "unknown arg: --" + key;
      return false;
    }
  }

  if (cfg->file_path.empty()) {
    *err = "--file is required";
    return false;
  }
  return true;
}

std::optional<std::uint64_t> get_file_size(const std::string& file_path) {
  struct stat st {};
  if (stat(file_path.c_str(), &st) != 0) {
    return std::nullopt;
  }
  return static_cast<std::uint64_t>(st.st_size);
}

std::vector<PathType> resolve_paths(const std::string& path_arg) {
  if (path_arg == "buffered") return {PathType::Buffered};
  if (path_arg == "direct") return {PathType::Direct};
  if (path_arg == "gds") return {PathType::Gds};
  return {PathType::Buffered, PathType::Direct, PathType::Gds};
}

double percentile_us(std::vector<double> vals, double pct) {
  if (vals.empty()) return 0.0;
  std::sort(vals.begin(), vals.end());
  const double idx = (pct / 100.0) * static_cast<double>(vals.size() - 1);
  const std::size_t lo = static_cast<std::size_t>(std::floor(idx));
  const std::size_t hi = static_cast<std::size_t>(std::ceil(idx));
  if (lo == hi) return vals[lo];
  const double frac = idx - static_cast<double>(lo);
  return vals[lo] * (1.0 - frac) + vals[hi] * frac;
}

void append_csv(const std::string& csv_path, PathType path, std::size_t io_size, const Config& cfg,
                const Metrics& m) {
  if (csv_path.empty()) return;

  const std::size_t slash = csv_path.find_last_of('/');
  if (slash != std::string::npos) {
    const std::string dir = csv_path.substr(0, slash);
    if (!dir.empty()) {
      ::mkdir(dir.c_str(), 0755);
    }
  }

  bool need_header = false;
  {
    std::ifstream in(csv_path);
    need_header = !in.good() || in.peek() == std::ifstream::traits_type::eof();
  }

  std::ofstream out(csv_path, std::ios::app);
  if (!out.is_open()) {
    std::cerr << "[WARN] failed to open csv: " << csv_path << "\n";
    return;
  }

  if (need_header) {
    out << "path_type,io_size,access_pattern,aligned,thread_num,total_bytes,throughput_gbps,avg_latency_us,p99_latency_us\n";
  }

  out << to_string(path) << ',' << io_size << ',' << to_string(cfg.pattern) << ',' << (cfg.aligned ? 1 : 0)
      << ',' << cfg.threads << ',' << m.total_bytes << ',' << std::fixed << std::setprecision(6)
      << m.throughput_gbps << ',' << m.avg_latency_us << ',' << m.p99_latency_us << '\n';
}

std::size_t aligned_offset(std::size_t req_idx, std::size_t io_size, std::uint64_t file_size,
                           AccessPattern pat, std::mt19937_64* rng) {
  const std::size_t slot_count = (file_size > io_size) ? static_cast<std::size_t>((file_size - io_size) / io_size + 1)
                                                        : 1;
  if (pat == AccessPattern::Sequential) {
    return (req_idx % slot_count) * io_size;
  }
  std::uniform_int_distribution<std::size_t> dist(0, slot_count - 1);
  return dist(*rng) * io_size;
}

std::size_t maybe_unalign(std::size_t offset, bool aligned) {
  if (aligned) return offset;
  return offset + 128;
}

RunResult run_one(PathType path, const Config& cfg, std::size_t io_size, std::uint64_t file_size) {
  RunResult rr;

  if (io_size > file_size) {
    rr.error = "io_size > file_size";
    return rr;
  }

  if (path == PathType::Direct && !cfg.aligned) {
    rr.error = "O_DIRECT path does not support non-aligned mode in this benchmark";
    return rr;
  }
  if (path == PathType::Gds && !cfg.aligned) {
    rr.error = "GDS path generally requires aligned offsets/sizes; non-aligned mode disabled";
    return rr;
  }

  int open_flags = O_RDONLY;
  if (path == PathType::Direct) {
    open_flags |= O_DIRECT;
  }

  int fd = ::open(cfg.file_path.c_str(), open_flags);
  if (fd < 0) {
    rr.error = "open failed, errno=" + std::to_string(errno);
    return rr;
  }

  CUfileHandle_t cfr{};
  bool gds_ready = false;
  if (path == PathType::Gds) {
    CUfileError_t st = cuFileDriverOpen();
    if (st.err != CU_FILE_SUCCESS) {
      rr.error = "GDS unavailable: cuFileDriverOpen failed (code=" + std::to_string(st.err) +
                 "). Please verify nvidia-fs/libcufile configuration.";
      ::close(fd);
      return rr;
    }

    CUfileDescr_t d{};
    d.type = CU_FILE_HANDLE_TYPE_OPAQUE_FD;
    d.handle.fd = fd;
    st = cuFileHandleRegister(&cfr, &d);
    if (st.err != CU_FILE_SUCCESS) {
      rr.error = "GDS unavailable: cuFileHandleRegister failed (code=" + std::to_string(st.err) + ")";
      cuFileDriverClose();
      ::close(fd);
      return rr;
    }
    gds_ready = true;
  }

  std::mutex lat_mu;
  std::vector<double> lat_us;
  lat_us.reserve(cfg.requests);
  std::atomic<std::uint64_t> total_bytes{0};
  double cpu_us_total = 0.0;
  double memcpy_us_total = 0.0;
  std::mutex stats_mu;

  auto t_wall0 = std::chrono::steady_clock::now();

  auto worker = [&](std::size_t tid, std::size_t begin, std::size_t end) {
    std::mt19937_64 rng(0x1234ULL + tid * 131ULL);

    void* host_buf = nullptr;
    void* gpu_buf = nullptr;

    if (path == PathType::Buffered) {
      host_buf = std::malloc(io_size + 256);
    } else if (path == PathType::Direct) {
      cudaHostAlloc(&host_buf, io_size + kAlign, cudaHostAllocDefault);
    }

    cudaMalloc(&gpu_buf, io_size + kAlign);

    if (path == PathType::Gds) {
      CUfileError_t st = cuFileBufRegister(gpu_buf, io_size, 0);
      if (st.err != CU_FILE_SUCCESS) {
        std::lock_guard<std::mutex> lk(lat_mu);
        rr.error = "cuFileBufRegister failed in worker (code=" + std::to_string(st.err) + ")";
      }
    }

    for (std::size_t i = begin; i < end; i += cfg.queue_depth) {
      std::size_t batch = std::min(cfg.queue_depth, end - i);
      for (std::size_t j = 0; j < batch; ++j) {
        std::size_t req_idx = i + j;
        std::size_t off = aligned_offset(req_idx, io_size, file_size, cfg.pattern, &rng);
        off = maybe_unalign(off, cfg.aligned);

        auto t0 = std::chrono::steady_clock::now();

        if (path == PathType::Buffered || path == PathType::Direct) {
          auto rc0 = std::chrono::steady_clock::now();
          ssize_t r = pread(fd, host_buf, io_size, static_cast<off_t>(off));
          auto rc1 = std::chrono::steady_clock::now();
          if (r != static_cast<ssize_t>(io_size)) {
            std::lock_guard<std::mutex> lk(lat_mu);
            rr.error = "pread failed or short read, ret=" + std::to_string(r) + " errno=" + std::to_string(errno);
            break;
          }

          auto mc0 = std::chrono::steady_clock::now();
          cudaMemcpy(gpu_buf, host_buf, io_size, cudaMemcpyHostToDevice);
          cudaDeviceSynchronize();
          auto mc1 = std::chrono::steady_clock::now();

          {
            std::lock_guard<std::mutex> lk(stats_mu);
            cpu_us_total += std::chrono::duration<double, std::micro>(rc1 - rc0).count();
            memcpy_us_total += std::chrono::duration<double, std::micro>(mc1 - mc0).count();
          }
        } else {
          auto rc0 = std::chrono::steady_clock::now();
          ssize_t r = cuFileRead(cfr, gpu_buf, io_size, static_cast<off_t>(off), 0);
          auto rc1 = std::chrono::steady_clock::now();
          if (r != static_cast<ssize_t>(io_size)) {
            std::lock_guard<std::mutex> lk(lat_mu);
            rr.error = "cuFileRead failed or short read, ret=" + std::to_string(r);
            break;
          }
          {
            std::lock_guard<std::mutex> lk(stats_mu);
            cpu_us_total += std::chrono::duration<double, std::micro>(rc1 - rc0).count();
          }
        }

        auto t1 = std::chrono::steady_clock::now();
        double l = std::chrono::duration<double, std::micro>(t1 - t0).count();

        {
          std::lock_guard<std::mutex> lk(lat_mu);
          lat_us.push_back(l);
        }
        total_bytes += io_size;
      }

      if (!rr.error.empty()) break;
    }

    if (path == PathType::Gds) {
      cuFileBufDeregister(gpu_buf);
    }

    cudaFree(gpu_buf);
    if (path == PathType::Buffered) {
      std::free(host_buf);
    } else if (path == PathType::Direct) {
      cudaFreeHost(host_buf);
    }
  };

  std::vector<std::thread> th;
  th.reserve(cfg.threads);
  std::size_t per = cfg.requests / cfg.threads;
  std::size_t rem = cfg.requests % cfg.threads;
  std::size_t idx = 0;
  for (std::size_t t = 0; t < cfg.threads; ++t) {
    std::size_t cnt = per + (t < rem ? 1 : 0);
    th.emplace_back(worker, t, idx, idx + cnt);
    idx += cnt;
  }
  for (auto& t : th) t.join();

  auto t_wall1 = std::chrono::steady_clock::now();

  if (gds_ready) {
    cuFileHandleDeregister(cfr);
    cuFileDriverClose();
  }
  ::close(fd);

  if (!rr.error.empty()) {
    rr.ok = false;
    return rr;
  }

  rr.ok = true;
  rr.metrics.total_bytes = total_bytes.load();
  rr.metrics.wall_time_us = std::chrono::duration<double, std::micro>(t_wall1 - t_wall0).count();
  rr.metrics.cpu_time_us = cpu_us_total;
  rr.metrics.memcpy_time_us = memcpy_us_total;
  rr.metrics.throughput_gbps =
      (rr.metrics.wall_time_us > 0.0)
          ? (static_cast<double>(rr.metrics.total_bytes) / (rr.metrics.wall_time_us / 1e6)) / 1e9
          : 0.0;

  if (!lat_us.empty()) {
    rr.metrics.avg_latency_us =
        std::accumulate(lat_us.begin(), lat_us.end(), 0.0) / static_cast<double>(lat_us.size());
    rr.metrics.p50_latency_us = percentile_us(lat_us, 50.0);
    rr.metrics.p95_latency_us = percentile_us(lat_us, 95.0);
    rr.metrics.p99_latency_us = percentile_us(lat_us, 99.0);
  }

  return rr;
}

void print_result(PathType path, std::size_t io_size, const Config& cfg, const RunResult& rr) {
  std::cout << "[" << to_string(path) << "] io_size=" << io_size
            << " pattern=" << to_string(cfg.pattern) << " aligned=" << (cfg.aligned ? 1 : 0)
            << " threads=" << cfg.threads << " qd=" << cfg.queue_depth << "\n";

  if (!rr.ok) {
    std::cout << "  status=FAILED error=" << rr.error << "\n";
    return;
  }

  std::cout << "  total_bytes=" << rr.metrics.total_bytes << " throughput_gbps=" << std::fixed
            << std::setprecision(6) << rr.metrics.throughput_gbps << "\n"
            << "  latency_us(avg/p50/p95/p99)=" << rr.metrics.avg_latency_us << "/"
            << rr.metrics.p50_latency_us << "/" << rr.metrics.p95_latency_us << "/"
            << rr.metrics.p99_latency_us << "\n"
            << "  cpu_time_us=" << rr.metrics.cpu_time_us << " memcpy_time_us="
            << rr.metrics.memcpy_time_us << " wall_time_us=" << rr.metrics.wall_time_us << "\n";
}

}  // namespace

int main(int argc, char** argv) {
  Config cfg;
  std::string err;
  if (!parse_args(argc, argv, &cfg, &err)) {
    std::cerr << "Error: " << err << "\n";
    print_usage(argv[0]);
    return 1;
  }

  std::optional<std::uint64_t> fs = get_file_size(cfg.file_path);
  if (!fs.has_value()) {
    std::cerr << "Error: failed to stat file: " << cfg.file_path << "\n";
    return 1;
  }
  if (cfg.file_size == 0) cfg.file_size = fs.value();
  if (cfg.file_size > fs.value()) cfg.file_size = fs.value();

  auto paths = resolve_paths(cfg.path_arg);

  for (PathType p : paths) {
    for (std::size_t io_size : cfg.io_sizes) {
      RunResult rr = run_one(p, cfg, io_size, cfg.file_size);
      print_result(p, io_size, cfg, rr);
      if (rr.ok) {
        append_csv(cfg.csv_path, p, io_size, cfg, rr.metrics);
      }
    }
  }

  return 0;
}
