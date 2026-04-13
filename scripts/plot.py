#!/usr/bin/env python3
import argparse
import csv
from collections import defaultdict
from pathlib import Path

import matplotlib.pyplot as plt


MODE_LABELS = {
    "cpu": "CPU",
    "gds_naive": "GDS Naive",
    "gds_opt": "GDS Optimized",
    "gds_cache": "GDS Cached",
}


def load_summary(path: Path):
    data = defaultdict(list)
    with path.open("r", newline="") as f:
        reader = csv.DictReader(f)
        required = {"mode", "io_size_bytes", "avg_latency_ms", "avg_throughput_gbps"}
        missing = required - set(reader.fieldnames or [])
        if missing:
            raise ValueError(f"missing required columns: {sorted(missing)}")

        for row in reader:
            mode = row["mode"]
            data[mode].append(
                {
                    "io_size_bytes": int(float(row["io_size_bytes"])),
                    "avg_latency_ms": float(row["avg_latency_ms"]),
                    "avg_throughput_gbps": float(row["avg_throughput_gbps"]),
                }
            )

    for mode in data:
        data[mode].sort(key=lambda r: r["io_size_bytes"])
    return data


def human_size(bytes_count: int) -> str:
    units = [(1 << 20, "M"), (1 << 10, "K")]
    for scale, suffix in units:
        if bytes_count % scale == 0 and bytes_count >= scale:
            return f"{bytes_count // scale}{suffix}"
    return str(bytes_count)


def plot_metric(data, metric_key: str, ylabel: str, title: str, output_path: Path):
    plt.figure(figsize=(9, 5))

    all_sizes = set()
    for mode, rows in data.items():
        x = [r["io_size_bytes"] for r in rows]
        y = [r[metric_key] for r in rows]
        all_sizes.update(x)
        plt.plot(x, y, marker="o", label=MODE_LABELS.get(mode, mode))

    if all_sizes:
        ticks = sorted(all_sizes)
        plt.xticks(ticks, [human_size(v) for v in ticks])

    plt.xlabel("I/O Size")
    plt.ylabel(ylabel)
    plt.title(title)
    plt.grid(True, linestyle="--", alpha=0.4)
    plt.legend()
    plt.tight_layout()

    output_path.parent.mkdir(parents=True, exist_ok=True)
    plt.savefig(output_path, dpi=150)
    plt.close()


def main() -> None:
    parser = argparse.ArgumentParser(description="Plot GDS benchmark summary metrics")
    parser.add_argument("--input", default="results/summary.csv", help="Input summary CSV")
    parser.add_argument("--outdir", default="results", help="Output directory for plots")
    args = parser.parse_args()

    summary_path = Path(args.input)
    outdir = Path(args.outdir)

    data = load_summary(summary_path)

    plot_metric(
        data,
        metric_key="avg_latency_ms",
        ylabel="Latency (ms)",
        title="Latency vs I/O Size",
        output_path=outdir / "latency_vs_io_size.png",
    )
    plot_metric(
        data,
        metric_key="avg_throughput_gbps",
        ylabel="Throughput (GB/s)",
        title="Throughput vs I/O Size",
        output_path=outdir / "throughput_vs_io_size.png",
    )

    print(f"Saved plots to: {outdir}")


if __name__ == "__main__":
    main()
