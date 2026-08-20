#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "用法: $0 /绝对路径/测试文件 [结果文件]" >&2
  exit 2
fi

data_file=$1
result_file=${2:-gds_hcache_results.csv}
binary=${GHC_BIN:-./build/ghc_bench}

echo "cache,io_size,line_size,run,output" > "$result_file"
for cache in 0 1; do
  for io_size in 4096 16384 65536 1048576; do
    for line_size in 4096 16384 65536; do
      if (( io_size > line_size && io_size <= 65536 )); then
        continue
      fi
      for run in 1 2 3 4 5; do
        output=$($binary --file="$data_file" --cache="$cache" \
          --io-size="$io_size" --line-size="$line_size" \
          --host-max=65536 --requests=10000)
        printf '%s,%s,%s,%s,"%s"\n' \
          "$cache" "$io_size" "$line_size" "$run" "$output" >> "$result_file"
      done
    done
  done
done
echo "结果已写入: $result_file"
