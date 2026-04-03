#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN_PATH="${BIN_PATH:-${ROOT_DIR}/build/gds_bench}"
DATA_FILE="${DATA_FILE:-${ROOT_DIR}/test.dat}"
RESULTS_DIR="${RESULTS_DIR:-${ROOT_DIR}/results}"
OUTPUT_CSV="${OUTPUT_CSV:-${RESULTS_DIR}/raw_results.csv}"
READ_FLAG="${READ_FLAG:-1}"
CACHE_BUFFERS="${CACHE_BUFFERS:-4}"
REPEATS="${REPEATS:-3}"

MODES=(cpu gds_naive gds_opt gds_cache)
SIZES=(4096 16384 65536 262144 1048576 4194304)

mkdir -p "${RESULTS_DIR}"
rm -f "${OUTPUT_CSV}"

if [[ ! -x "${BIN_PATH}" ]]; then
  echo "error: benchmark binary not found/executable: ${BIN_PATH}" >&2
  echo "hint: build first, e.g. cmake -S . -B build && cmake --build build" >&2
  exit 1
fi

if [[ ! -f "${DATA_FILE}" ]]; then
  echo "error: data file not found: ${DATA_FILE}" >&2
  echo "hint: generate a test file (e.g., dd if=/dev/zero of=test.dat bs=1M count=1024)" >&2
  exit 1
fi

echo "Running benchmark matrix..."
echo "  bin: ${BIN_PATH}"
echo "  data: ${DATA_FILE}"
echo "  out: ${OUTPUT_CSV}"

total_runs=$(( ${#MODES[@]} * ${#SIZES[@]} * REPEATS ))
run_idx=0

for mode in "${MODES[@]}"; do
  for size in "${SIZES[@]}"; do
    for rep in $(seq 1 "${REPEATS}"); do
      run_idx=$((run_idx + 1))
      echo "[${run_idx}/${total_runs}] mode=${mode} size=${size} rep=${rep}"

      args=(
        "--mode=${mode}"
        "--file=${DATA_FILE}"
        "--size=${size}"
        "--iters=100"
        "--read=${READ_FLAG}"
        "--csv=${OUTPUT_CSV}"
      )

      if [[ "${mode}" == "gds_cache" ]]; then
        args+=("--buffers=${CACHE_BUFFERS}")
      else
        args+=("--buffers=1")
      fi

      "${BIN_PATH}" "${args[@]}"
    done
  done
done

echo "Done. Raw CSV: ${OUTPUT_CSV}"
