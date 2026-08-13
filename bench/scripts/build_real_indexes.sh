#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
D=${DISKANN_SRC:-"${ROOT}/DiskANN"}
B=${DISKANN_BUILD:-"${D}/build"}
DATA=${DATA_DIR:-"${ROOT}/data"}
IDX=${INDEX_DIR:-"${ROOT}/indexes"}
mkdir -p "${IDX}"

convert() {
  local src=$1 dst=$2
  [[ -s "${dst}" ]] || "${B}/apps/utils/fvecs_to_bin" float "${src}" "${dst}"
}
build() {
  local base=$1 prefix=$2
  [[ -s "${prefix}_disk.index" ]] || "${B}/apps/build_disk_index" \
    --data_type float --dist_fn l2 --data_path "${base}" \
    --index_path_prefix "${prefix}" -R 64 -L 100 -B 0.05 -M 8 -T "${BUILD_THREADS:-32}"
}

convert "${DATA}/sift/sift_base.fvecs" "${DATA}/sift/sift_base.fbin"
convert "${DATA}/sift/sift_query.fvecs" "${DATA}/sift/sift_query.fbin"
convert "${DATA}/gist/gist_base.fvecs" "${DATA}/gist/gist_base.fbin"
convert "${DATA}/gist/gist_query.fvecs" "${DATA}/gist/gist_query.fbin"
build "${DATA}/sift/sift_base.fbin" "${IDX}/sift1m_R64_L100"
build "${DATA}/gist/gist_base.fbin" "${IDX}/gist1m_R64_L100"
