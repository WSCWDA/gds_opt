#include "io_bench/io_trace.h"
#include <chrono>
#include <sstream>
#include <stdexcept>
namespace diskann::iobench {
uint64_t monotonic_time_ns() { return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
void IOTraceWriter::open(const std::string &path) {
  std::lock_guard<std::mutex> lock(mutex_); out_.open(path, std::ios::out|std::ios::trunc);
  if (!out_) throw std::runtime_error("Cannot open trace: "+path);
  out_ << "seq,timestamp_ns,query_id,thread_id,batch_id,request_in_batch,batch_size,offset,len,latency_ns,cache_miss\n";
  seq_=0; enabled_.store(true,std::memory_order_release);
}
void IOTraceWriter::close() { std::lock_guard<std::mutex> lock(mutex_); enabled_.store(false,std::memory_order_release); if(out_.is_open()) out_.close(); }
void IOTraceWriter::append(const IOTraceRecord &r) {
  if(!enabled()) return; std::lock_guard<std::mutex> lock(mutex_); if(!enabled()) return;
  out_ << seq_.fetch_add(1) << ',' << r.timestamp_ns << ',' << r.query_id << ',' << r.thread_id << ',' << r.batch_id << ',' << r.request_in_batch << ',' << r.batch_size << ',' << r.offset << ',' << r.len << ',' << r.latency_ns << ',' << static_cast<unsigned>(r.cache_miss) << '\n';
}
std::vector<IOTraceRecord> read_io_trace(const std::string &path) {
  std::ifstream in(path); if(!in) throw std::runtime_error("Cannot open trace: "+path); std::string line; std::getline(in,line);
  const std::string expected="seq,timestamp_ns,query_id,thread_id,batch_id,request_in_batch,batch_size,offset,len,latency_ns,cache_miss";
  if(line!=expected) throw std::runtime_error("Unsupported trace header"); std::vector<IOTraceRecord> out;
  while(std::getline(in,line)) { if(line.empty()) continue; std::stringstream ss(line); std::string f; uint64_t v[11]{}; for(auto &x:v){if(!std::getline(ss,f,',')) throw std::runtime_error("Truncated trace row"); x=std::stoull(f);} out.push_back({v[0],v[1],v[2],v[3],static_cast<uint32_t>(v[4]),static_cast<uint32_t>(v[5]),static_cast<uint32_t>(v[6]),v[7],v[8],v[9],static_cast<uint8_t>(v[10])}); }
  return out;
}
} // namespace diskann::iobench
