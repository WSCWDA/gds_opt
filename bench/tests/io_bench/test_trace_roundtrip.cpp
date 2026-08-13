#include "io_bench/io_trace.h"
#include <cassert>
#include <cstdio>
int main() {
  const char *path="trace_roundtrip.csv";
  diskann::iobench::IOTraceRecord expected{9,11,13,17,19,23,29,4096,8192,31,1};
  { diskann::iobench::IOTraceWriter writer(path); writer.append(expected); }
  auto rows=diskann::iobench::read_io_trace(path); assert(rows.size()==1); const auto &r=rows[0];
  // seq is assigned by the writer; every workload-derived field round-trips.
  assert(r.seq==0 && r.timestamp_ns==expected.timestamp_ns && r.query_id==expected.query_id && r.thread_id==expected.thread_id);
  assert(r.batch_id==expected.batch_id && r.request_in_batch==expected.request_in_batch && r.batch_size==expected.batch_size);
  assert(r.offset==expected.offset && r.len==expected.len && r.latency_ns==expected.latency_ns && r.cache_miss==expected.cache_miss);
  std::remove(path); return 0;
}
