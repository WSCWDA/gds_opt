#pragma once
#include <cstdint>
#include <limits>
namespace diskann::iobench {
struct QueryIOContext { uint64_t query_id = std::numeric_limits<uint64_t>::max(); uint32_t batch_id = 0; };
extern thread_local QueryIOContext current_query_io_context;
inline void begin_query(uint64_t id) { current_query_io_context = {id, 0}; }
inline uint32_t next_batch() { return current_query_io_context.batch_id++; }
inline void end_query() { current_query_io_context.query_id = std::numeric_limits<uint64_t>::max(); }
} // namespace diskann::iobench
