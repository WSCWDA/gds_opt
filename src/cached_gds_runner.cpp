#include "cached_gds_runner.h"

#include <cuda_runtime.h>
#include <cufile.h>

#include <chrono>
#include <cerrno>
#include <cstdint>
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

std::string cufile_error_to_string(const char* prefix, CUfileError_t err) {
  std::ostringstream oss;
  oss << prefix << ": cuFile error code=" << err.err;
  return oss.str();
}

double ns_to_ms(const std::chrono::nanoseconds& ns) {
  return static_cast<double>(ns.count()) / 1e6;
}

}  // namespace

CachedGDSRunner::~CachedGDSRunner() { cleanup(); }

void CachedGDSRunner::set_buffer_count(std::size_t count) {
  if (initialized_) {
    return;
  }
  buffer_count_ = (count == 0) ? 1 : count;
}

bool CachedGDSRunner::init(const std::string& file_path, std::size_t io_size, bool is_read) {
  cleanup();

  if (io_size == 0) {
    set_error("init failed: io_size must be > 0");
    return false;
  }

  const int open_flags = is_read ? O_RDONLY : O_WRONLY;
  fd_ = open(file_path.c_str(), open_flags | O_DIRECT);
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

  if (is_read && st.st_size <= 0) {
    set_error("init failed: file is empty");
    cleanup();
    return false;
  }

  file_size_ = static_cast<std::size_t>(st.st_size);
  io_size_ = io_size;
  is_read_ = is_read;

  buffers_.clear();
  buffers_.reserve(buffer_count_);
  for (std::size_t i = 0; i < buffer_count_; ++i) {
    void* dev_ptr = nullptr;
    cudaError_t cuda_status = cudaMalloc(&dev_ptr, io_size_);
    if (cuda_status != cudaSuccess) {
      set_error(cuda_error_to_string("init failed: cudaMalloc", cuda_status));
      cleanup();
      return false;
    }
    buffers_.push_back(dev_ptr);
  }

  CUfileError_t cufile_status = cuFileDriverOpen();
  if (cufile_status.err != CU_FILE_SUCCESS) {
    set_error(cufile_error_to_string("init failed: cuFileDriverOpen", cufile_status));
    cleanup();
    return false;
  }
  cufile_driver_opened_ = true;

  CUfileDescr_t cufile_descr{};
  cufile_descr.handle.fd = fd_;
  cufile_descr.type = CU_FILE_HANDLE_TYPE_OPAQUE_FD;

  cufile_status = cuFileHandleRegister(&cfr_handle_, &cufile_descr);
  if (cufile_status.err != CU_FILE_SUCCESS) {
    set_error(cufile_error_to_string("init failed: cuFileHandleRegister", cufile_status));
    cleanup();
    return false;
  }
  cufile_handle_registered_ = true;

  metrics_ = CachedGDSMetrics{};
  metrics_.io_size_bytes = io_size_;
  metrics_.buffer_count = buffer_count_;
  initialized_ = true;
  last_error_.clear();
  return true;
}

bool CachedGDSRunner::run(std::size_t iterations) {
  if (!initialized_) {
    set_error("run failed: runner is not initialized");
    return false;
  }
  if (iterations == 0) {
    set_error("run failed: iterations must be > 0");
    return false;
  }

  const CacheStats before = cache_.get_stats();
  const auto start = std::chrono::steady_clock::now();

  for (std::size_t i = 0; i < iterations; ++i) {
    void* dev_ptr = buffers_[i % buffer_count_];

    if (!cache_.acquire(dev_ptr, io_size_)) {
      set_error("run failed: cache acquire failed");
      return false;
    }

    const std::size_t max_offset = (is_read_ && file_size_ > io_size_) ? (file_size_ - io_size_) : 0;
    const off_t file_offset = is_read_
                                  ? static_cast<off_t>((i * io_size_) % (max_offset + 1))
                                  : static_cast<off_t>(i * io_size_);

    ssize_t io_ret = 0;
    if (is_read_) {
      io_ret = cuFileRead(cfr_handle_, dev_ptr, io_size_, file_offset, 0);
      if (io_ret < 0) {
        (void)cache_.release(dev_ptr);
        set_error("run failed: cuFileRead returned error=" + std::to_string(io_ret));
        return false;
      }
    } else {
      io_ret = cuFileWrite(cfr_handle_, dev_ptr, io_size_, file_offset, 0);
      if (io_ret < 0) {
        (void)cache_.release(dev_ptr);
        set_error("run failed: cuFileWrite returned error=" + std::to_string(io_ret));
        return false;
      }
    }

    if (static_cast<std::size_t>(io_ret) != io_size_) {
      (void)cache_.release(dev_ptr);
      set_error("run failed: short I/O, transferred=" + std::to_string(io_ret) +
                " expected=" + std::to_string(io_size_));
      return false;
    }

    if (!cache_.release(dev_ptr)) {
      set_error("run failed: cache release failed");
      return false;
    }
  }

  const auto end = std::chrono::steady_clock::now();
  const auto total_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
  const CacheStats after = cache_.get_stats();

  metrics_.iterations = iterations;
  metrics_.total_time_ms = ns_to_ms(total_ns);
  metrics_.latency_per_io_ms = metrics_.total_time_ms / static_cast<double>(iterations);

  const double total_seconds = static_cast<double>(total_ns.count()) / 1e9;
  const double total_bytes = static_cast<double>(iterations) * static_cast<double>(io_size_);
  metrics_.throughput_gbps = (total_seconds > 0.0) ? (total_bytes / total_seconds) / 1e9 : 0.0;

  metrics_.cache_hit = after.hit_count - before.hit_count;
  metrics_.cache_miss = after.miss_count - before.miss_count;
  metrics_.register_count = after.register_count - before.register_count;
  metrics_.deregister_count = after.deregister_count - before.deregister_count;

  last_error_.clear();
  return true;
}

void CachedGDSRunner::cleanup() {
  initialized_ = false;

  cache_.force_evict_all();

  if (cufile_handle_registered_) {
    (void)cuFileHandleDeregister(cfr_handle_);
    cufile_handle_registered_ = false;
  }

  if (cufile_driver_opened_) {
    (void)cuFileDriverClose();
    cufile_driver_opened_ = false;
  }

  for (void* dev_ptr : buffers_) {
    if (dev_ptr != nullptr) {
      (void)cudaFree(dev_ptr);
    }
  }
  buffers_.clear();

  if (fd_ >= 0) {
    (void)close(fd_);
    fd_ = -1;
  }

  io_size_ = 0;
  file_size_ = 0;
}

void CachedGDSRunner::set_error(const std::string& message) { last_error_ = message; }

}  // namespace gds_bench
