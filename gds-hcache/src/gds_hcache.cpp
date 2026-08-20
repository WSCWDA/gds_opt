#include <ghc/ghc.hpp>

#include "cache.hpp"
#include "cufile_backend.hpp"
#include "host_pool.hpp"
#include "uring_backend.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <system_error>
#include <sys/stat.h>
#include <unistd.h>

#if GHC_ENABLE_CUDA
#include <cuda_runtime_api.h>
#endif

namespace ghc {
namespace {
std::atomic<std::uint32_t> next_file_id{1};

bool power_of_two(std::size_t value) {
  return value != 0 && (value & (value - 1)) == 0;
}
}  // namespace

struct Context::Impl {
  explicit Impl(Options opts)
      : options(std::move(opts)),
        pool(options.cache_capacity, options.cache_line_size),
        cache(pool.slots(), [this](std::size_t) { ++evictions; }),
        uring(pool, options.queue_depth, options.fixed_buffer_region_size) {
    if (!power_of_two(options.cache_line_size) || options.cache_line_size < 4096)
      throw std::invalid_argument("cache_line_size must be power-of-two and >= 4096");
    if (options.fixed_buffer_region_size < options.cache_line_size)
      throw std::invalid_argument(
          "fixed_buffer_region_size must be >= cache_line_size");
#if GHC_ENABLE_CUDA
    const auto rc = cudaSetDevice(options.cuda_device);
    if (rc != cudaSuccess) throw std::runtime_error(cudaGetErrorString(rc));
#endif
  }

  Options options;
  detail::HostPool pool;
  detail::BlockCache cache;
  detail::UringBackend uring;
  std::atomic<std::uint64_t> gds_reads{0}, host_hits{0}, host_misses{0};
  std::atomic<std::uint64_t> coalesced_waits{0}, evictions{0}, fallbacks{0};
  std::atomic<std::uint64_t> storage_bytes{0}, h2d_bytes{0};
};

struct File::Impl {
  Impl(Context::Impl& owner, std::string file_path)
      : ctx(owner), path(std::move(file_path)), file_id(next_file_id++) {
    host_fd = ::open(path.c_str(), O_RDONLY | O_DIRECT | O_CLOEXEC);
    if (host_fd < 0) throw std::system_error(errno, std::generic_category(), path);
    gds_fd = ::open(path.c_str(), O_RDONLY | O_DIRECT | O_CLOEXEC);
    if (gds_fd < 0) {
      const int saved = errno;
      ::close(host_fd);
      throw std::system_error(saved, std::generic_category(), path);
    }
    struct stat st {};
    if (::fstat(host_fd, &st) != 0) {
      const int saved = errno;
      ::close(gds_fd);
      ::close(host_fd);
      throw std::system_error(saved, std::generic_category(), path);
    }
    file_size = st.st_size;
    cufile = std::make_unique<detail::CufileBackend>(gds_fd);
  }

  ~Impl() {
    cufile.reset();
    if (gds_fd >= 0) ::close(gds_fd);
    if (host_fd >= 0) ::close(host_fd);
  }

  Context::Impl& ctx;
  std::string path;
  std::uint32_t file_id;
  int host_fd = -1;
  int gds_fd = -1;
  std::uint64_t file_size = 0;
  std::unique_ptr<detail::CufileBackend> cufile;
};

Context::Context(Options options) : impl_(std::make_unique<Impl>(std::move(options))) {}
Context::~Context() = default;

std::unique_ptr<File> Context::open(const std::string& path) {
  return std::unique_ptr<File>(new File(std::make_unique<File::Impl>(*impl_, path)));
}

Stats Context::stats() const {
  return {impl_->gds_reads.load(), impl_->host_hits.load(),
          impl_->host_misses.load(), impl_->coalesced_waits.load(),
          impl_->evictions.load(), impl_->fallbacks.load(),
          impl_->storage_bytes.load(), impl_->h2d_bytes.load()};
}

File::File(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
File::~File() = default;
std::uint64_t File::size() const noexcept { return impl_->file_size; }

std::ptrdiff_t File::read(void* gpu_dst, std::size_t length,
                          std::uint64_t file_offset, cudaStream_t stream) {
  if (!gpu_dst && length) throw std::invalid_argument("gpu_dst is null");
  if (length == 0 || file_offset >= impl_->file_size) return 0;
  length = std::min<std::uint64_t>(length, impl_->file_size - file_offset);
  auto& ctx = impl_->ctx;
  const auto line_size = ctx.options.cache_line_size;

  const bool cache_eligible = ctx.options.enable_cache &&
      length <= ctx.options.host_max_io_size &&
      length <= line_size &&
      (file_offset / line_size) == ((file_offset + length - 1) / line_size);

  if (!cache_eligible) {
    const auto rc = impl_->cufile->read(gpu_dst, length, file_offset);
    if (rc < 0) throw std::system_error(-rc, std::generic_category(), "cuFileRead");
    ++ctx.gds_reads;
    return rc;
  }

  const std::uint64_t block = file_offset & ~(line_size - 1);
  const std::size_t in_block = file_offset - block;
  auto lookup = ctx.cache.get_or_reserve({impl_->file_id, block});
  if (lookup.hit) ++ctx.host_hits;
  if (lookup.coalesced) ++ctx.coalesced_waits;

  if (lookup.owner) {
    ++ctx.host_misses;
    // Keep the DIO request length aligned. A request crossing EOF returns a short read.
    const auto rc = ctx.uring.read(impl_->host_fd, lookup.line->slot, line_size, block);
    if (rc < 0) {
      ctx.cache.publish(lookup.line, 0, -rc);
      ctx.cache.invalidate({impl_->file_id, block});
      ctx.cache.release(lookup.line);
      throw std::system_error(-rc, std::generic_category(), "O_DIRECT cache fill");
    }
    ctx.storage_bytes += rc;
    ctx.cache.publish(lookup.line, rc, 0);
  }

  if (in_block >= lookup.line->valid_bytes) {
    ctx.cache.release(lookup.line);
    return 0;
  }
  const auto copy_bytes = std::min(length, lookup.line->valid_bytes - in_block);
  const auto* source = static_cast<const unsigned char*>(ctx.pool.slot(lookup.line->slot)) + in_block;
#if GHC_ENABLE_CUDA
  auto rc = cudaMemcpyAsync(gpu_dst, source, copy_bytes, cudaMemcpyHostToDevice, stream);
  if (rc != cudaSuccess) {
    ctx.cache.release(lookup.line);
    throw std::runtime_error(cudaGetErrorString(rc));
  }
  if (ctx.options.sync_before_return) {
    rc = cudaStreamSynchronize(stream);
    if (rc != cudaSuccess) {
      ctx.cache.release(lookup.line);
      throw std::runtime_error(cudaGetErrorString(rc));
    }
  }
#else
  (void)stream;
  std::memcpy(gpu_dst, source, copy_bytes);
#endif
  ctx.h2d_bytes += copy_bytes;
  ctx.cache.release(lookup.line);
  return copy_bytes;
}

}  // namespace ghc
