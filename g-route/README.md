# G-Route MVP mixed I/O prototype

`g_route_mvp.py` simulates the mixed I/O pattern of multi-epoch ImageNet-style training plus periodic checkpoint writes.  It follows the data-flow shape of NVIDIA DALI's PyTorch ResNet50 example (train/validation directory roots, epoch loop, periodic checkpointing), but this first version does **not** require DALI, CUDA, GDS, or GPUs.

## Routes

- Training samples from `/home/cwd/dataset/ImageNet/train` are modeled as **small random reads** that repeat across epochs and go through `HostCache`.
- Checkpoints are modeled as **large sequential writes** through `DirectPathStub`; they intentionally bypass `HostCache` and use `torch.save`/`torch.load` when PyTorch is installed.
- Each touched file is labeled in a JSONL profile log before use:
  - `training_data: small-random, repeated-epoch, cacheable`
  - `checkpoint: large-sequential-write, non-cacheable`

## Quick start

Run against the real ImageNet layout:

```bash
python g-route/g_route_mvp.py \
  --train-dir /home/cwd/dataset/ImageNet/train \
  --val-dir /home/cwd/dataset/ImageNet/val \
  --epochs 3 \
  --max-samples 1024 \
  --checkpoint-interval 1 \
  --checkpoint-mb 256
```

Run a self-contained smoke test if ImageNet is not mounted:

```bash
python g-route/g_route_mvp.py --synthetic-if-missing --epochs 2 --max-samples 16 --checkpoint-mb 1 --verify-checkpoint-load
```

## Metrics

The script prints and writes JSON metrics containing:

- `epoch_time`
- `checkpoint_time`
- `logical_reads`
- `physical_reads`
- `cache_hit_rate`
- `route_mix`
- `total_runtime`

By default outputs are written under `g-route/runs/latest/`.
