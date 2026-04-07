#!/usr/bin/env python3
import argparse
import csv
from collections import defaultdict
from pathlib import Path


def to_int(v: str) -> int:
    return int(float(v))


def to_float(v: str) -> float:
    return float(v)


def summarize(input_csv: Path, output_csv: Path) -> None:
    groups = defaultdict(list)

    with input_csv.open("r", newline="") as f:
        reader = csv.DictReader(f)
        required = {
            "mode",
            "io_size_bytes",
            "latency_ms",
            "throughput_gbps",
            "register_count",
            "deregister_count",
            "cache_hit",
            "cache_miss",
        }
        missing = required - set(reader.fieldnames or [])
        if missing:
            raise ValueError(f"missing required columns: {sorted(missing)}")

        for row in reader:
            key = (row["mode"], to_int(row["io_size_bytes"]))
            groups[key].append(row)

    output_csv.parent.mkdir(parents=True, exist_ok=True)

    with output_csv.open("w", newline="") as f:
        fieldnames = [
            "mode",
            "io_size_bytes",
            "samples",
            "avg_latency_ms",
            "avg_throughput_gbps",
            "avg_register_count",
            "avg_deregister_count",
            "avg_cache_hit",
            "avg_cache_miss",
        ]
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()

        for (mode, io_size), rows in sorted(groups.items(), key=lambda x: (x[0][0], x[0][1])):
            n = len(rows)
            writer.writerow(
                {
                    "mode": mode,
                    "io_size_bytes": io_size,
                    "samples": n,
                    "avg_latency_ms": sum(to_float(r["latency_ms"]) for r in rows) / n,
                    "avg_throughput_gbps": sum(to_float(r["throughput_gbps"]) for r in rows) / n,
                    "avg_register_count": sum(to_float(r["register_count"]) for r in rows) / n,
                    "avg_deregister_count": sum(to_float(r["deregister_count"]) for r in rows) / n,
                    "avg_cache_hit": sum(to_float(r["cache_hit"]) for r in rows) / n,
                    "avg_cache_miss": sum(to_float(r["cache_miss"]) for r in rows) / n,
                }
            )


def main() -> None:
    parser = argparse.ArgumentParser(description="Summarize raw GDS benchmark CSV results")
    parser.add_argument("--input", default="results/raw_results.csv", help="Input raw CSV path")
    parser.add_argument("--output", default="results/summary.csv", help="Output summary CSV path")
    args = parser.parse_args()

    summarize(Path(args.input), Path(args.output))
    print(f"Wrote summary: {args.output}")


if __name__ == "__main__":
    main()
