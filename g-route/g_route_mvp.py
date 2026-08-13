#!/usr/bin/env python3
"""G-Route MVP: mixed ImageNet-style training reads + checkpoint writes.

This prototype is intentionally GDS-free.  It routes repeated small random
training-file reads through an in-process HostCache and routes checkpoint
writes through DirectPathStub, which uses normal sequential file I/O while
preserving the same API boundary a future GDS/cuFile backend can replace.
"""

from __future__ import annotations

import argparse
import json
import os
import pickle
import random
import time
from collections import OrderedDict
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, Iterable, List, MutableMapping, Optional

try:
    import torch
except ImportError:  # keep the MVP runnable in minimal CI images
    torch = None  # type: ignore[assignment]

TRAINING_LABEL = "training_data: small-random, repeated-epoch, cacheable"
CHECKPOINT_LABEL = "checkpoint: large-sequential-write, non-cacheable"


@dataclass
class Metrics:
    logical_reads: int = 0
    physical_reads: int = 0
    training_bytes: int = 0
    checkpoint_bytes: int = 0
    checkpoint_time: float = 0.0
    epoch_times: List[float] = field(default_factory=list)
    route_counts: Dict[str, int] = field(default_factory=lambda: {"HostCache": 0, "DirectPathStub": 0})

    @property
    def cache_hit_rate(self) -> float:
        if self.logical_reads == 0:
            return 0.0
        return (self.logical_reads - self.physical_reads) / self.logical_reads

    @property
    def route_mix(self) -> Dict[str, float]:
        total = sum(self.route_counts.values())
        if total == 0:
            return {k: 0.0 for k in self.route_counts}
        return {k: v / total for k, v in self.route_counts.items()}


class ProfileLogger:
    """Writes one label per touched file before that file is used."""

    def __init__(self, path: Path):
        self.path = path
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._seen: set[tuple[str, str]] = set()
        self._fh = self.path.open("w", encoding="utf-8")

    def label_once(self, file_path: Path, label: str) -> None:
        key = (str(file_path), label)
        if key in self._seen:
            return
        self._seen.add(key)
        self._fh.write(json.dumps({"file": str(file_path), "label": label}, ensure_ascii=False) + "\n")
        self._fh.flush()

    def close(self) -> None:
        self._fh.close()


class HostCache:
    """Tiny LRU cache for small random training-data reads."""

    def __init__(self, capacity_bytes: int, metrics: Metrics):
        self.capacity_bytes = capacity_bytes
        self.metrics = metrics
        self._items: MutableMapping[Path, bytes] = OrderedDict()
        self._bytes = 0

    def read(self, path: Path) -> bytes:
        self.metrics.logical_reads += 1
        self.metrics.route_counts["HostCache"] += 1
        if path in self._items:
            data = self._items.pop(path)
            self._items[path] = data
            return data

        data = path.read_bytes()
        self.metrics.physical_reads += 1
        self.metrics.training_bytes += len(data)
        self._insert(path, data)
        return data

    def _insert(self, path: Path, data: bytes) -> None:
        if len(data) > self.capacity_bytes:
            return
        while self._bytes + len(data) > self.capacity_bytes and self._items:
            _, evicted = self._items.popitem(last=False)
            self._bytes -= len(evicted)
        self._items[path] = data
        self._bytes += len(data)


class DirectPathStub:
    """Sequential checkpoint backend that bypasses HostCache.

    The write/load operations use torch.save and torch.load when PyTorch is
    installed.  A pickle fallback keeps the prototype smoke-testable without
    changing the route contract.
    """

    def __init__(self, metrics: Metrics):
        self.metrics = metrics

    def save(self, obj: object, path: Path) -> float:
        path.parent.mkdir(parents=True, exist_ok=True)
        start = time.perf_counter()
        if torch is not None:
            torch.save(obj, path)
        else:
            with path.open("wb") as fh:
                pickle.dump(obj, fh, protocol=pickle.HIGHEST_PROTOCOL)
        elapsed = time.perf_counter() - start
        self.metrics.route_counts["DirectPathStub"] += 1
        self.metrics.checkpoint_bytes += path.stat().st_size
        self.metrics.checkpoint_time += elapsed
        return elapsed

    def load(self, path: Path) -> object:
        if torch is not None:
            return torch.load(path, map_location="cpu")
        with path.open("rb") as fh:
            return pickle.load(fh)


def discover_files(root: Path, limit: int) -> List[Path]:
    files = [p for p in root.rglob("*") if p.is_file()]
    files.sort()
    return files[:limit] if limit > 0 else files


def create_synthetic_imagenet(root: Path, samples: int, bytes_per_sample: int) -> List[Path]:
    class_dir = root / "synthetic_class"
    class_dir.mkdir(parents=True, exist_ok=True)
    files = []
    for i in range(samples):
        path = class_dir / f"sample_{i:05d}.bin"
        if not path.exists() or path.stat().st_size != bytes_per_sample:
            path.write_bytes(os.urandom(bytes_per_sample))
        files.append(path)
    return files


def checkpoint_payload(epoch: int, payload_mb: int) -> object:
    size = payload_mb * 1024 * 1024
    if torch is not None:
        return {"epoch": epoch, "weights": torch.ones(size // 4, dtype=torch.float32)}
    return {"epoch": epoch, "weights": bytearray(size)}


def run(args: argparse.Namespace) -> Dict[str, object]:
    random.seed(args.seed)
    out_dir = Path(args.output_dir)
    metrics = Metrics()
    logger = ProfileLogger(Path(args.profile_log))
    cache = HostCache(args.host_cache_mb * 1024 * 1024, metrics)
    direct = DirectPathStub(metrics)
    start_total = time.perf_counter()

    train_root = Path(args.train_dir)
    samples = discover_files(train_root, args.max_samples) if train_root.exists() else []
    if not samples and args.synthetic_if_missing:
        samples = create_synthetic_imagenet(out_dir / "synthetic_imagenet" / "train", args.max_samples, args.synthetic_sample_bytes)
    if not samples:
        raise FileNotFoundError(f"No training samples found under {train_root}; pass --synthetic-if-missing for a self-contained smoke run")

    for sample in samples:
        logger.label_once(sample, TRAINING_LABEL)

    for epoch in range(args.epochs):
        epoch_start = time.perf_counter()
        order = list(samples)
        random.shuffle(order)
        for sample in order:
            data = cache.read(sample)
            # Minimal CPU-side work to model a consumed training sample.
            _ = data[0] if data else 0
        metrics.epoch_times.append(time.perf_counter() - epoch_start)

        if (epoch + 1) % args.checkpoint_interval == 0:
            ckpt = out_dir / "checkpoints" / f"epoch_{epoch + 1:04d}.pt"
            logger.label_once(ckpt, CHECKPOINT_LABEL)
            direct.save(checkpoint_payload(epoch + 1, args.checkpoint_mb), ckpt)
            if args.verify_checkpoint_load:
                direct.load(ckpt)

    logger.close()
    total_runtime = time.perf_counter() - start_total
    return {
        "epoch_time": metrics.epoch_times,
        "checkpoint_time": metrics.checkpoint_time,
        "logical_reads": metrics.logical_reads,
        "physical_reads": metrics.physical_reads,
        "cache_hit_rate": metrics.cache_hit_rate,
        "route_mix": metrics.route_mix,
        "total_runtime": total_runtime,
        "training_bytes_read_from_ssd": metrics.training_bytes,
        "checkpoint_bytes_written": metrics.checkpoint_bytes,
        "profile_log": str(Path(args.profile_log)),
    }


def parse_args(argv: Optional[Iterable[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="G-Route MVP mixed I/O simulator")
    parser.add_argument("--train-dir", default="/home/cwd/dataset/ImageNet/train")
    parser.add_argument("--val-dir", default="/home/cwd/dataset/ImageNet/val", help="Reserved for parity with ImageNet train/val layouts")
    parser.add_argument("--epochs", type=int, default=2)
    parser.add_argument("--max-samples", type=int, default=128)
    parser.add_argument("--host-cache-mb", type=int, default=256)
    parser.add_argument("--checkpoint-interval", type=int, default=1)
    parser.add_argument("--checkpoint-mb", type=int, default=64)
    parser.add_argument("--output-dir", default="g-route/runs/latest")
    parser.add_argument("--profile-log", default="g-route/runs/latest/profile_labels.jsonl")
    parser.add_argument("--metrics-json", default="g-route/runs/latest/metrics.json")
    parser.add_argument("--synthetic-if-missing", action="store_true")
    parser.add_argument("--synthetic-sample-bytes", type=int, default=4096)
    parser.add_argument("--verify-checkpoint-load", action="store_true")
    parser.add_argument("--seed", type=int, default=1234)
    return parser.parse_args(argv)


def main(argv: Optional[Iterable[str]] = None) -> None:
    args = parse_args(argv)
    metrics = run(args)
    metrics_path = Path(args.metrics_json)
    metrics_path.parent.mkdir(parents=True, exist_ok=True)
    metrics_path.write_text(json.dumps(metrics, indent=2), encoding="utf-8")
    print(json.dumps(metrics, indent=2))


if __name__ == "__main__":
    main()
