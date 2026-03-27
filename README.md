# GPUDirect Storage Benchmark Prototype

## 1) Project overview

This project is a C++17 benchmark prototype for evaluating file I/O paths that move data to/from GPU memory, with a focus on GPUDirect Storage (GDS) registration overhead.

The benchmark compares four modes:

- **`cpu`**: host `pread`/`read` + `cudaMemcpy` to GPU
- **`gds_naive`**: `cuFileBufRegister`/`cuFileBufDeregister` on every I/O
- **`gds_opt`**: register once, reuse for all iterations, deregister at end
- **`gds_cache`**: lifecycle-aware registration cache with acquire/release semantics

The repository also includes experiment scripts for matrix runs, CSV summarization, plotting, and tracepoint capture/parsing.

---

## 2) System requirements

### Hardware / OS

- Linux (ext4 or XFS for GDS data path testing)
- NVIDIA GPU with a compatible driver
- NVMe/local storage for realistic GDS runs

### Software

- CMake >= 3.20
- C++ compiler with C++17 support (e.g., `g++` 10+)
- CUDA Toolkit with:
  - CUDA Runtime (`cudart`)
  - cuFile (`libcufile`)
- Python 3.8+
- Python packages:
  - `matplotlib` (for plotting)

### Privileges for tracing

- Root/sudo access is generally required to control tracefs (`/sys/kernel/tracing`).

---

## 3) Build instructions

```bash
cmake -S . -B build
cmake --build build -j
```

This builds:

- `gds_runners` (static library)
- `gds_bench` (CLI benchmark executable)

If CMake cannot find CUDA, set `CUDAToolkit_ROOT` to your toolkit install path.

---

## 4) Run examples

### Basic CPU baseline

```bash
CUDA_VISIBLE_DEVICES=3 ./build/gds_bench \
  --mode=cpu \
  --file=/mnt/gds2/cwd_test/test.dat \
  --size=1048576 \
  --iters=100 \
  --read=1
```

### Naive GDS mode

```bash
CUDA_VISIBLE_DEVICES=3 ./build/gds_bench \
  --mode=gds_naive \
  --file=/mnt/gds2/cwd_test/test.dat \
  --size=1048576 \
  --iters=100 \
  --read=1
```

### Optimized GDS mode

```bash
CUDA_VISIBLE_DEVICES=3 ./build/gds_bench \
  --mode=gds_opt \
  --file=/mnt/gds2/cwd_test/test.dat \
  --size=1048576 \
  --iters=100 \
  --read=1
```

### Cached GDS mode (rotating buffers)

```bash
CUDA_VISIBLE_DEVICES=3 ./build/gds_bench \
  --mode=gds_cache \
  --file=/mnt/gds2/cwd_test/test.dat \
  --size=1048576 \
  --iters=100 \
  --buffers=4 \
  --read=1
```

### Append benchmark output to CSV

```bash
CUDA_VISIBLE_DEVICES=3 ./build/gds_bench \
  --mode=gds_cache \
  --file=/mnt/gds2/cwd_test/test.dat \
  --size=1048576 \
  --iters=100 \
  --buffers=4 \
  --read=1 \
  --csv=results/raw_results.csv
```

---

## 5) Benchmark modes explanation

### `cpu`

- Reads file data into host memory (`pread`/`read`)
- Copies host buffer to GPU via `cudaMemcpy`
- Useful as a non-GDS baseline

### `gds_naive`

- Per iteration:
  1. `cuFileBufRegister`
  2. `cuFileRead` / `cuFileWrite`
  3. `cuFileBufDeregister`
- Captures worst-case registration overhead

### `gds_opt`

- Registers GPU buffer once during init
- Reuses same registration across all I/O iterations
- Deregisters once at end

### `gds_cache`

- Uses `GDSRegistrationCache`
- Iteration flow:
  1. `acquire(buffer)`
  2. `cuFileRead` / `cuFileWrite`
  3. `release(buffer)`
- Supports single-buffer reuse and multi-buffer rotation

---

## 6) Output format explanation

The benchmark prints a summary and can append CSV rows.

### Console summary fields

- `mode`
- `file`
- `io_size_bytes`
- `iterations`
- `buffers`
- `read`
- `latency_ms`
- `throughput_gbps`
- `register_count`
- `deregister_count`
- `cache_hit`
- `cache_miss`

### Raw CSV columns (`--csv=...`)

```text
mode,file,io_size_bytes,iterations,buffers,read,latency_ms,throughput_gbps,register_count,deregister_count,cache_hit,cache_miss
```

---

## 7) How to reproduce figures

### Step A: Run matrix experiments

```bash
scripts/run_all.sh
```

Defaults:

- modes: `cpu`, `gds_naive`, `gds_opt`, `gds_cache`
- sizes: `4K, 16K, 64K, 256K, 1M, 4M`
- repeats: `3`
- output: `results/raw_results.csv`

Useful overrides:

```bash
BIN_PATH=./build/gds_bench \
DATA_FILE=/mnt/gds2/cwd_test/test.dat \
RESULTS_DIR=results \
CACHE_BUFFERS=4 \
REPEATS=3 \
scripts/run_all.sh
```

### Step B: Summarize runs

```bash
python3 scripts/summarize.py \
  --input results/raw_results.csv \
  --output results/summary.csv
```

### Step C: Generate plots

```bash
python3 scripts/plot.py \
  --input results/summary.csv \
  --outdir results
```

Generated figures:

- `results/latency_vs_io_size.png`
- `results/throughput_vs_io_size.png`

---

## 8) Tracepoint usage

Use trace scripts to inspect registration behavior, bounce buffering, and I/O completion events.

### Start tracing

```bash
sudo scripts/start_trace.sh results/trace
```

Optional environment variables:

- `TRACEFS` (default: `/sys/kernel/tracing`)
- `GDS_EVENT_GROUPS` (default: `nvfs,cufile`)
- `GDS_EVENT_FILTERS` (default: `map,bounce,io,complete`)

### Run workload

```bash
CUDA_VISIBLE_DEVICES=3 ./build/gds_bench --mode=gds_cache --file=/mnt/gds2/cwd_test/test.dat --size=1048576 --iters=100 --buffers=4 --read=1
```

### Stop tracing

```bash
sudo scripts/stop_trace.sh results/trace
```

Raw trace output is saved to:

- `results/trace/trace_raw.txt`

### Parse trace and export structured stats

```bash
python3 scripts/parse_trace.py \
  --input results/trace/trace_raw.txt \
  --output results/trace/trace_summary.csv
```

The parser prints a text summary and writes CSV containing:

- summary counts for `map_event`, `bounce_event`, `io_completion_event`
- per-event counts and category mapping

---

## Repository layout

```text
.
├── CMakeLists.txt
├── README.md
├── bench/
├── include/
├── scripts/
└── src/
```
