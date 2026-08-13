#include "io_bench/io_stats.h"
namespace diskann::iobench {
double IOStats::cache_hit_ratio() const { const auto n=cache_hits+cache_misses; return n ? static_cast<double>(cache_hits)/n : 0.0; }
void IOStats::merge(const IOStats &o) { cache_hits+=o.cache_hits; cache_misses+=o.cache_misses; io_batches+=o.io_batches; io_requests+=o.io_requests; io_bytes+=o.io_bytes; io_submit_ns+=o.io_submit_ns; io_wait_ns+=o.io_wait_ns; }
} // namespace diskann::iobench
