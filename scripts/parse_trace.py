#!/usr/bin/env python3
import argparse
import csv
import re
from collections import Counter, defaultdict
from pathlib import Path

CATEGORY_PATTERNS = {
    "map_event": [r"\bmap\b", r"\bregister\b", r"bufregister", r"pin"],
    "bounce_event": [r"bounce", r"staging", r"compat"],
    "io_completion_event": [r"io.*complete", r"complete", r"done", r"end_io"],
}


def classify(line: str):
    l = line.lower()
    matched = []
    for category, patterns in CATEGORY_PATTERNS.items():
        for p in patterns:
            if re.search(p, l):
                matched.append(category)
                break
    return matched


def parse_trace(path: Path):
    counts = Counter()
    event_counts = Counter()
    categories_by_event = defaultdict(set)

    with path.open("r", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue

            # typical ftrace format includes "group:event:" token.
            m = re.search(r"\s([a-zA-Z0-9_\-]+:[a-zA-Z0-9_\-]+):", line)
            event = m.group(1) if m else "unknown"
            event_counts[event] += 1

            categories = classify(line)
            if not categories:
                continue
            for cat in categories:
                counts[cat] += 1
                categories_by_event[event].add(cat)

    return counts, event_counts, categories_by_event


def write_csv(output: Path, counts: Counter, event_counts: Counter, categories_by_event):
    output.parent.mkdir(parents=True, exist_ok=True)

    with output.open("w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["section", "key", "value", "categories"])

        for key in ["map_event", "bounce_event", "io_completion_event"]:
            writer.writerow(["summary", key, counts.get(key, 0), ""])

        for event, n in event_counts.most_common():
            cats = sorted(categories_by_event.get(event, set()))
            writer.writerow(["event", event, n, "|".join(cats)])


def print_text(counts: Counter, event_counts: Counter):
    print("Trace Summary")
    print("=============")
    print(f"map_event: {counts.get('map_event', 0)}")
    print(f"bounce_event: {counts.get('bounce_event', 0)}")
    print(f"io_completion_event: {counts.get('io_completion_event', 0)}")
    print("\nTop events:")
    for event, n in event_counts.most_common(10):
        print(f"  {event}: {n}")


def main():
    parser = argparse.ArgumentParser(description="Parse ftrace output for GDS-related events")
    parser.add_argument("--input", required=True, help="Path to raw trace text")
    parser.add_argument(
        "--output",
        default="results/trace/trace_summary.csv",
        help="Output summary CSV",
    )
    args = parser.parse_args()

    input_path = Path(args.input)
    output_path = Path(args.output)

    counts, event_counts, categories_by_event = parse_trace(input_path)
    write_csv(output_path, counts, event_counts, categories_by_event)
    print_text(counts, event_counts)
    print(f"\nWrote CSV: {output_path}")


if __name__ == "__main__":
    main()
