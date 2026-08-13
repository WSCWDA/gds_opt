# DiskANN Real-I/O Path Benchmark

This benchmark is an integration layer for Microsoft's **`cpp_main`** branch.
It must not be run against generated vectors or synthetic offsets. Every trace
record must originate at `PQFlashIndex::cached_beam_search`, become an upstream
`AlignedRead`, and be recorded by the selected `AlignedFileReader` immediately
around the actual read operation.

## Reproducibility contract

Record hardware, OS, kernel, filesystem, SSD model/device, CUDA/cuFile version,
DiskANN commit, dataset checksums, build/search parameters, page-cache state,
NUMA binding, and CPU affinity. `scripts/capture_environment.sh` captures the
discoverable environment in `results/environment.txt`; experiment metadata must
add the remaining operator-selected values.

For backend comparisons, dataset, disk index, query file/order, `L`, beam width,
cached-node count, thread count, SSD, filesystem, CPU affinity, and NUMA policy
must remain identical. The sole variable is the I/O path. Trace-enabled runs
characterize work; trace-disabled runs measure performance. Do not report trace
latencies as final performance.

## Bootstrap and data

```bash
bench/scripts/bootstrap_diskann.sh
bench/scripts/download_real_datasets.sh
# Configure the upstream checkout with DISKANN_IO_BENCH=ON after applying the
# integration implementation, then:
bench/scripts/build_real_indexes.sh
```

Only TexMex SIFT1M/GIST1M files are downloaded. Indexes are produced exclusively
with upstream `build_disk_index`; no script generates vectors or index sectors.

## Cache interpretation

`requested_cache_nodes` is a configuration value, not a hit ratio. Figures must
use cache hits and misses observed in DiskANN's real neighborhood/coordinate
cache branches. Page-cache `cold` means Linux page cache was dropped; it does
not claim that an NVMe controller cache was flushed.

## GDS interpretation

GDS is optional and must be disabled by default. A GDS replay compares SSD to
GPU-memory transfer against SSD to CPU-DRAM transfer for the same real trace; it
does **not** establish GPU DiskANN end-to-end speed because graph traversal and
distance computation remain CPU work.
