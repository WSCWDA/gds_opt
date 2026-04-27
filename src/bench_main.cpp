#include "cached_gds_runner.h"
#include "cpu_path_runner.h"
#include "naive_gds_runner.h"
#include "optimized_gds_runner.h"

#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>

namespace gds_bench {
namespace {

struct Args {
  std::string mode{"cpu"};
  std::string file_path;
  std::size_t io_size{0};
  std::size_t iterations{1};
  std::size_t buffers{1};
  bool is_read{true};
  std::string csv_path;
};

struct BenchmarkSummary {
  std::string mode;
  std::string file_path;
  std::size_t io_size_bytes{0};
  std::size_t iterations{0};
  std::size_t buffers{0};
  bool is_read{true};

  double latency_ms{0.0};
  double throughput_gbps{0.0};
  std::uint64_t register_count{0};
  std::uint64_t deregister_count{0};
  std::uint64_t cache_hit{0};
  std::uint64_t cache_miss{0};
};

bool parse_bool01(const std::string& text, bool* out) {
  if (text == "1") {
    *out = true;
    return true;
  }
  if (text == "0") {
    *out = false;
    return true;
  }
  return false;
}

bool parse_size(const std::string& text, std::size_t* out) {
  try {
    const std::size_t value = std::stoull(text);
    *out = value;
    return true;
  } catch (...) {
    return false;
  }
}

void print_usage(const char* prog) {
  std::cerr << "Usage: " << prog << " --mode=cpu|gds_naive|gds_opt|gds_cache --file=PATH "
            << "--size=BYTES --iters=N [--buffers=N] [--read=1|0] [--csv=OUTPUT.csv]\n";
}

bool parse_args(int argc, char** argv, Args* args) {
  std::unordered_map<std::string, std::string> kv;
  for (int i = 1; i < argc; ++i) {
    std::string token(argv[i]);
    if (token.rfind("--", 0) != 0) {
      std::cerr << "Invalid arg: " << token << "\n";
      return false;
    }

    const std::size_t eq = token.find('=');
    if (eq == std::string::npos || eq <= 2 || eq == token.size() - 1) {
      std::cerr << "Invalid key/value arg: " << token << "\n";
      return false;
    }

    const std::string key = token.substr(2, eq - 2);
    const std::string value = token.substr(eq + 1);
    kv[key] = value;
  }

  if (kv.count("mode") > 0) {
    args->mode = kv["mode"];
  }
  if (kv.count("file") > 0) {
    args->file_path = kv["file"];
  }
  if (kv.count("size") > 0 && !parse_size(kv["size"], &args->io_size)) {
    std::cerr << "Invalid --size value\n";
    return false;
  }
  if (kv.count("iters") > 0 && !parse_size(kv["iters"], &args->iterations)) {
    std::cerr << "Invalid --iters value\n";
    return false;
  }
  if (kv.count("buffers") > 0 && !parse_size(kv["buffers"], &args->buffers)) {
    std::cerr << "Invalid --buffers value\n";
    return false;
  }
  if (kv.count("read") > 0 && !parse_bool01(kv["read"], &args->is_read)) {
    std::cerr << "Invalid --read value, expected 1 or 0\n";
    return false;
  }
  if (kv.count("csv") > 0) {
    args->csv_path = kv["csv"];
  }

  if (args->file_path.empty() || args->io_size == 0 || args->iterations == 0) {
    std::cerr << "Missing or invalid required args: --file --size --iters\n";
    return false;
  }

  if (args->mode != "cpu" && args->mode != "gds_naive" && args->mode != "gds_opt" &&
      args->mode != "gds_cache") {
    std::cerr << "Invalid --mode. Use cpu|gds_naive|gds_opt|gds_cache\n";
    return false;
  }

  if (args->buffers == 0) {
    args->buffers = 1;
  }

  return true;
}

void print_summary(const BenchmarkSummary& s) {
  std::cout << "mode: " << s.mode << "\n"
            << "file: " << s.file_path << "\n"
            << "io_size_bytes: " << s.io_size_bytes << "\n"
            << "iterations: " << s.iterations << "\n"
            << "buffers: " << s.buffers << "\n"
            << "read: " << (s.is_read ? 1 : 0) << "\n"
            << "latency_ms: " << std::fixed << std::setprecision(6) << s.latency_ms << "\n"
            << "throughput_gbps: " << std::fixed << std::setprecision(6) << s.throughput_gbps << "\n"
            << "register_count: " << s.register_count << "\n"
            << "deregister_count: " << s.deregister_count << "\n"
            << "cache_hit: " << s.cache_hit << "\n"
            << "cache_miss: " << s.cache_miss << "\n";
}

bool append_csv(const std::string& csv_path, const BenchmarkSummary& s) {
  std::ofstream out(csv_path, std::ios::app);
  if (!out.is_open()) {
    std::cerr << "Failed to open CSV file: " << csv_path << "\n";
    return false;
  }

  std::ifstream in(csv_path);
  if (in.peek() == std::ifstream::traits_type::eof()) {
    out << "mode,file,io_size_bytes,iterations,buffers,read,latency_ms,throughput_gbps,"
           "register_count,deregister_count,cache_hit,cache_miss\n";
  }

  out << s.mode << ',' << s.file_path << ',' << s.io_size_bytes << ',' << s.iterations << ','
      << s.buffers << ',' << (s.is_read ? 1 : 0) << ',' << std::fixed << std::setprecision(6)
      << s.latency_ms << ',' << s.throughput_gbps << ',' << s.register_count << ','
      << s.deregister_count << ',' << s.cache_hit << ',' << s.cache_miss << '\n';
  return true;
}

bool run_cpu(const Args& args, BenchmarkSummary* out, std::string* err) {
  CPUPathRunner runner;
  if (!runner.init(args.file_path, args.io_size, args.is_read)) {
    *err = runner.last_error();
    return false;
  }
  if (!runner.run(args.iterations)) {
    *err = runner.last_error();
    runner.cleanup();
    return false;
  }

  const CPUPathMetrics& m = runner.metrics();
  out->latency_ms = m.total_latency_ms;
  out->throughput_gbps = m.throughput_gbps;
  out->register_count = 0;
  out->deregister_count = 0;
  out->cache_hit = 0;
  out->cache_miss = 0;

  runner.cleanup();
  return true;
}

bool run_naive(const Args& args, BenchmarkSummary* out, std::string* err) {
  NaiveGDSRunner runner;
  if (!runner.init(args.file_path, args.io_size, args.is_read)) {
    *err = runner.last_error();
    return false;
  }
  if (!runner.run(args.iterations)) {
    *err = runner.last_error();
    runner.cleanup();
    return false;
  }

  const NaiveGDSMetrics& m = runner.metrics();
  out->latency_ms = m.total_time_ms;
  out->throughput_gbps = m.throughput_gbps;
  out->register_count = m.register_count;
  out->deregister_count = m.deregister_count;
  out->cache_hit = 0;
  out->cache_miss = 0;

  runner.cleanup();
  return true;
}

bool run_optimized(const Args& args, BenchmarkSummary* out, std::string* err) {
  OptimizedGDSRunner runner;
  if (!runner.init(args.file_path, args.io_size, args.is_read)) {
    *err = runner.last_error();
    return false;
  }
  if (!runner.run(args.iterations)) {
    *err = runner.last_error();
    runner.cleanup();
    return false;
  }

  const OptimizedGDSMetrics& m = runner.metrics();
  out->latency_ms = m.total_time_ms;
  out->throughput_gbps = m.throughput_gbps;
  out->register_count = m.register_count;
  out->deregister_count = m.deregister_count;
  out->cache_hit = 0;
  out->cache_miss = 0;

  runner.cleanup();
  return true;
}

bool run_cached(const Args& args, BenchmarkSummary* out, std::string* err) {
  CachedGDSRunner runner;
  runner.set_buffer_count(args.buffers);

  if (!runner.init(args.file_path, args.io_size, args.is_read)) {
    *err = runner.last_error();
    return false;
  }
  if (!runner.run(args.iterations)) {
    *err = runner.last_error();
    runner.cleanup();
    return false;
  }

  const CachedGDSMetrics& m = runner.metrics();
  out->latency_ms = m.total_time_ms;
  out->throughput_gbps = m.throughput_gbps;
  out->register_count = m.register_count;
  out->deregister_count = m.deregister_count;
  out->cache_hit = m.cache_hit;
  out->cache_miss = m.cache_miss;

  runner.cleanup();
  return true;
}

}  // namespace
}  // namespace gds_bench

int main(int argc, char** argv) {
  gds_bench::Args args;
  if (!gds_bench::parse_args(argc, argv, &args)) {
    gds_bench::print_usage(argv[0]);
    return 1;
  }

  gds_bench::BenchmarkSummary summary;
  summary.mode = args.mode;
  summary.file_path = args.file_path;
  summary.io_size_bytes = args.io_size;
  summary.iterations = args.iterations;
  summary.buffers = args.buffers;
  summary.is_read = args.is_read;

  std::string error;
  bool ok = false;

  if (args.mode == "cpu") {
    ok = gds_bench::run_cpu(args, &summary, &error);
  } else if (args.mode == "gds_naive") {
    ok = gds_bench::run_naive(args, &summary, &error);
  } else if (args.mode == "gds_opt") {
    ok = gds_bench::run_optimized(args, &summary, &error);
  } else if (args.mode == "gds_cache") {
    ok = gds_bench::run_cached(args, &summary, &error);
  }

  if (!ok) {
    std::cerr << "Benchmark failed: " << error << "\n";
    return 2;
  }

  gds_bench::print_summary(summary);

  if (!args.csv_path.empty() && !gds_bench::append_csv(args.csv_path, summary)) {
    return 3;
  }

  return 0;
}
