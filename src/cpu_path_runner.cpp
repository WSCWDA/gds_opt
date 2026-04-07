#include "cpu_path_runner.h"

#include <cuda_runtime.h>

#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace gds_bench {
namespace {

std::string cuda_error_to_string(const char* prefix, cudaError_t err) {
  std::ostringstream oss;
  oss << prefix << ": " << cudaGetErrorString(err);
  return oss.str();
}

double ns_to_ms(const std::chrono::nanoseconds& ns) {
  return static_cast<double>(ns.count()) / 1e6;
}

}  // namespace

CPUPathRunner::~CPUPathRunner() { cleanup(); }

bool CPUPathRunner::init(const std::string& file_path, std::size_t io_size, bool is_read) {
  cleanup();

  if (io_size == 0) {
    set_error("init failed: io_size must be > 0");
    return false;
  }
  if (!is_read) {
    set_error("init failed: CPUPathRunner currently supports read flow only");
    return false;
  }

  fd_ = open(file_path.c_str(), O_RDONLY);
  if (fd_ < 0) {
    set_error("init failed: unable to open file " + file_path + ", errno=" +
              std::to_string(errno));
    return false;
  }

  struct stat st {};
  if (fstat(fd_, &st) != 0) {
    set_error("init failed: fstat failed, errno=" + std::to_string(errno));
    cleanup();
    return false;
  }
  if (st.st_size <= 0) {
    set_error("init failed: file is empty");
    cleanup();
    return false;
  }

  file_size_ = static_cast<std::size_t>(st.st_size);
  io_size_ = io_size;
  is_read_ = is_read;

  if (posix_memalign(&host_buffer_, 4096, io_size_) != 0) {
    set_error("init failed: posix_memalign for host buffer failed");
    cleanup();
    return false;
  }

  cudaError_t cerr = cudaMalloc(&device_buffer_, io_size_);
  if (cerr != cudaSuccess) {
    set_error(cuda_error_to_string("init failed: cudaMalloc failed", cerr));
    cleanup();
    return false;
  }

  metrics_ = CPUPathMetrics{};
  metrics_.io_size_bytes = io_size_;
  last_error_.clear();
  initialized_ = true;
  return true;
}

bool CPUPathRunner::run(std::size_t iterations) {
  if (!initialized_) {
    set_error("run failed: runner is not initialized");
    return false;
  }
  if (iterations == 0) {
    set_error("run failed: iterations must be > 0");
    return false;
  }

  std::chrono::nanoseconds read_ns{0};
  std::chrono::nanoseconds memcpy_ns{0};
  const auto total_start = std::chrono::steady_clock::now();

  for (std::size_t i = 0; i < iterations; ++i) {
    const std::size_t max_offset = (file_size_ > io_size_) ? (file_size_ - io_size_) : 0;
    const off_t offset = static_cast<off_t>((i * io_size_) % (max_offset + 1));

    const auto read_start = std::chrono::steady_clock::now();
    if (!read_once(offset)) {
      return false;
    }
    const auto read_end = std::chrono::steady_clock::now();
    read_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(read_end - read_start);

    const auto memcpy_start = std::chrono::steady_clock::now();
    if (!memcpy_once()) {
      return false;
    }
    const auto memcpy_end = std::chrono::steady_clock::now();
    memcpy_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(memcpy_end - memcpy_start);
  }

  const auto total_end = std::chrono::steady_clock::now();
  const auto total_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(total_end - total_start);

  metrics_.iterations = iterations;
  metrics_.read_time_ms = ns_to_ms(read_ns);
  metrics_.memcpy_time_ms = ns_to_ms(memcpy_ns);
  metrics_.total_latency_ms = ns_to_ms(total_ns);

  const double total_seconds = static_cast<double>(total_ns.count()) / 1e9;
  const double total_bytes = static_cast<double>(iterations) * static_cast<double>(io_size_);
  metrics_.throughput_gbps = (total_seconds > 0.0) ? (total_bytes / total_seconds) / 1e9 : 0.0;

  last_error_.clear();
  return true;
}

void CPUPathRunner::cleanup() {
  initialized_ = false;

  if (device_buffer_ != nullptr) {
    cudaFree(device_buffer_);
    device_buffer_ = nullptr;
  }

  if (host_buffer_ != nullptr) {
    free(host_buffer_);
    host_buffer_ = nullptr;
  }

  if (fd_ >= 0) {
    close(fd_);
    fd_ = -1;
  }

  io_size_ = 0;
  file_size_ = 0;
}

bool CPUPathRunner::read_once(off_t offset_bytes) {
  std::size_t total_read = 0;
  while (total_read < io_size_) {
    const ssize_t n = pread(fd_, static_cast<char*>(host_buffer_) + total_read,
                            io_size_ - total_read, offset_bytes + total_read);
    if (n < 0) {
      set_error("read_once failed: pread error, errno=" + std::to_string(errno));
      return false;
    }
    if (n == 0) {
      set_error("read_once failed: unexpected EOF");
      return false;
    }
    total_read += static_cast<std::size_t>(n);
  }
  return true;
}

bool CPUPathRunner::memcpy_once() {
  cudaError_t cerr = cudaMemcpy(device_buffer_, host_buffer_, io_size_, cudaMemcpyHostToDevice);
  if (cerr != cudaSuccess) {
    set_error(cuda_error_to_string("memcpy_once failed: cudaMemcpy", cerr));
    return false;
  }

  cerr = cudaDeviceSynchronize();
  if (cerr != cudaSuccess) {
    set_error(cuda_error_to_string("memcpy_once failed: cudaDeviceSynchronize", cerr));
    return false;
  }
  return true;
}

void CPUPathRunner::set_error(const std::string& message) { last_error_ = message; }

}  // namespace gds_bench
