# GDS Benchmark Prototype

This repository currently contains **Task 1 project scaffolding only**.
No functional C++ implementation is included yet.

## Required structure

- `include/`
- `src/`
- `bench/`
- `scripts/`
- `CMakeLists.txt`
- `README.md`

## Modules (scaffolded)

1. `gds_common`
   - error handling
   - timing utilities
   - CUDA/cuFile initialization lifecycle
2. `cpu_path_runner`
3. `naive_gds_runner`
4. `optimized_gds_runner`
5. `gds_cache` (lifecycle-aware GPU buffer registration cache)
6. `cached_gds_runner`
7. `bench_main`

## File map

### include/
- `include/gds_common/error.hpp`
- `include/gds_common/timing.hpp`
- `include/gds_common/runtime.hpp`
- `include/cpu_path_runner.h`
- `include/naive_gds_runner.hpp`
- `include/optimized_gds_runner.hpp`
- `include/gds_cache.hpp`
- `include/cached_gds_runner.hpp`

### src/
- `src/gds_common/error.cpp`
- `src/gds_common/timing.cpp`
- `src/gds_common/runtime.cpp`
- `src/cpu_path_runner.cpp`
- `src/naive_gds_runner.cpp`
- `src/optimized_gds_runner.cpp`
- `src/gds_cache.cpp`
- `src/cached_gds_runner.cpp`
- `src/bench_main.cpp`

### bench/
- `bench/run_bench.sh`

### scripts/
- `scripts/gen_test_file.sh`
