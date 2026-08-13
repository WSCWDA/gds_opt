#!/usr/bin/env bash
set -euo pipefail

# Fetch the exact upstream branch that owns the C++ implementation.  The
# benchmark never falls back to the Rust branch or to generated stand-in data.
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
DEST=${DISKANN_SRC:-"${ROOT}/DiskANN"}

if [[ -e "${DEST}" && ! -d "${DEST}/.git" ]]; then
  echo "error: ${DEST} exists but is not a git checkout" >&2
  exit 1
fi

if [[ ! -d "${DEST}/.git" ]]; then
  git clone --branch cpp_main --recurse-submodules \
    https://github.com/microsoft/DiskANN.git "${DEST}"
else
  git -C "${DEST}" fetch origin cpp_main
  git -C "${DEST}" checkout cpp_main
  git -C "${DEST}" pull --ff-only origin cpp_main
  git -C "${DEST}" submodule update --init --recursive
fi

git -C "${DEST}" rev-parse HEAD | tee "${ROOT}/DISKANN_COMMIT"
