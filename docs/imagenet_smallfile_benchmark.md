# ImageNet 小文件场景基准设计（buffered / direct / gds）

> 目标数据集目录：`/home/cwd/dataset/ImageNet/train/`

## 1. 目标问题

围绕以下三个问题设计对照实验：

1. 在 ImageNet 小文件场景下，`buffered / direct / gds` 三条路径的性能差异是什么？
2. 在重复读场景下，`buffered` 是否因为 page cache 明显受益？
3. 与 `direct` 和 `gds` 相比，`buffered` 的 warm run 是否显著改善？

---

## 2. 核心假设（待实验验证）

- **H1（小文件）**：小文件（例如 <=256KB）场景中，`buffered` 可能优于 `direct/gds`，因为系统调用与注册开销占比更高，且 page cache 有机会命中。
- **H2（重复读）**：重复读时 `buffered` 的 warm run 会显著提升（延迟下降、吞吐上升），因为数据可由 page cache 命中。
- **H3（对比）**：`direct/gds` 绕过 page cache（或不直接受其影响），warm/cold 差异通常小于 `buffered`。

---

## 3. 数据集抽样与分层

从 `ImageNet/train` 抽样构建固定文件列表，建议每次实验使用同一列表以保证可复现。

### 3.1 只测小文件（推荐）

- 文件大小范围：`4KB ~ 256KB`
- 样本量建议：`20k ~ 100k` 个文件
- 访问模式：顺序 + 随机各一组

示例（生成固定列表）：

```bash
find /home/cwd/dataset/ImageNet/train -type f -size -256k > results/imagenet_small_all.txt
shuf results/imagenet_small_all.txt | head -n 50000 > results/imagenet_small_50k.txt
```

---

## 4. 实验矩阵

每个组合至少重复 3~5 次，输出均值和 P95/P99。

- Path：`buffered`, `direct`, `gds`
- Access pattern：`sequential`, `random`
- Run state：`cold`, `warm`
- 线程数：`1, 4, 8`（按机器能力调整）

输出指标建议：

- 吞吐（GB/s，或 files/s）
- 平均延迟、P50/P95/P99 延迟
- 总 wall-clock 时间
- 失败请求数（若有）

---

## 5. Cold / Warm 定义（关键）

- **Cold run**：尽可能清空 page cache 后执行（仅用于评估缓存影响）
- **Warm run**：在同一文件列表上连续再跑一次

清缓存示例（需要 root）：

```bash
sync
sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches'
```

> 注意：生产环境谨慎执行 `drop_caches`。

---

## 6. 执行建议（与现有工具对接）

仓库已提供 `gds_imagenet_bench`（`src/imagenet_compare_main.cpp`），用于直接回放 ImageNet 目录小文件并对比 `buffered/direct/gds` 三条路径。

### 6.1 构建

```bash
cmake -S . -B build
cmake --build build -j
```

### 6.2 运行示例

#### 顺序读，cold+warm 两轮

```bash
CUDA_VISIBLE_DEVICES=3 ./build/gds_imagenet_bench \
  --root=/home/cwd/dataset/ImageNet/train/ \
  --path=all \
  --pattern=seq \
  --min_size=4096 \
  --max_size=262144 \
  --max_files=50000 \
  --runs=2 \
  --drop_cache_before_cold=0 \
  --csv=results/imagenet_compare.csv
```

#### 随机读，cold 前主动清缓存（需要 root）

```bash
CUDA_VISIBLE_DEVICES=3 ./build/gds_imagenet_bench \
  --root=/home/cwd/dataset/ImageNet/train/ \
  --path=all \
  --pattern=rand \
  --min_size=4096 \
  --max_size=262144 \
  --max_files=50000 \
  --runs=2 \
  --drop_cache_before_cold=1 \
  --csv=results/imagenet_compare_rand.csv
```

### 6.3 参数说明（核心）

- `--path=buffered|direct|gds|all`：选择路径
- `--pattern=seq|rand`：顺序/随机访问
- `--min_size --max_size`：小文件大小过滤
- `--max_files`：采样文件上限
- `--runs`：连续运行轮次（第 1 轮记作 cold，其后记作 warm）
- `--drop_cache_before_cold=1`：cold 前尝试 `drop_caches`（失败会告警但不中断）
- `--csv`：结果输出路径

---

## 7. 判定规则（回答三个问题）

### Q1: 小文件场景三路径差异

统计每组的 `throughput` 与 `p99 latency`，按 path 画箱线图/折线图。

### Q2: buffered 是否受益于 page cache

对比 `buffered-cold` 与 `buffered-warm`：

- 若 warm 吞吐显著上升、延迟显著下降，则说明 page cache 受益明显。

### Q3: buffered warm vs direct/gds

同一访问模式下比较：

- `buffered-warm` vs `direct-warm`
- `buffered-warm` vs `gds-warm`

若 `buffered-warm` 在小文件下显著更好（尤其 p99 下降明显），则可认为 warm cache 对其有显著提升。

---

## 8. 结果记录模板（建议）

CSV 字段（最小集）：

```text
path_type,access_pattern,run_state,thread_num,file_count,total_bytes,throughput_gbps,avg_latency_us,p50_latency_us,p95_latency_us,p99_latency_us
```

并在报告中给出：

- 样本文件大小分布（中位数/P95）
- 机器配置（CPU/GPU/NVMe/文件系统）
- CUDA/cuFile/驱动版本

---

## 9. 实验注意事项

- 固定 `CUDA_VISIBLE_DEVICES`（例如 `CUDA_VISIBLE_DEVICES=3`）
- 固定 CPU 频率策略与负载背景
- 每次实验保持相同文件列表与随机种子
- 避免不同实验间共享缓存污染（必要时执行 cold 流程）
