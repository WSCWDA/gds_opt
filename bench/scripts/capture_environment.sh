#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
OUT=${1:-"${ROOT}/results/environment.txt"}
mkdir -p "$(dirname "${OUT}")"
{
  echo '=== benchmark repository ==='; git -C "${ROOT}/.." rev-parse HEAD
  echo '=== DiskANN repository ==='; git -C "${ROOT}/DiskANN" rev-parse HEAD
  echo '=== uname ==='; uname -a
  echo '=== lscpu ==='; lscpu
  echo '=== numactl ==='; numactl --hardware 2>&1 || true
  echo '=== lsblk ==='; lsblk -o NAME,TYPE,SIZE,FSTYPE,MOUNTPOINTS,MODEL,ROTA
  echo '=== CUDA ==='; nvidia-smi 2>&1 || true
  echo '=== cuFile ==='; ldconfig -p 2>/dev/null | awk '/libcufile/{print}' || true
} >"${OUT}"
