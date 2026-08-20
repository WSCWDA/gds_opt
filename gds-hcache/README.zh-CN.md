# GDS-HCache MVP：安装、运行与验证教程

本目录实现“GDS大块直通 + Host DRAM热点小读缓存”的第一版MVP。

## 1. 第一版支持范围

- Linux、本地NVMe、单进程、单GPU；
- 只读且运行期间不可修改的普通文件；
- 大块或不适合缓存的请求调用`cuFileRead`；
- 小请求由`io_uring READ_FIXED + O_DIRECT`填充固定容量的pinned Host DRAM；
- cache hit通过`cudaMemcpyAsync`复制到GPU；
- `(file_handle, aligned block offset)`作为cache key；
- 相同块并发miss采用single-flight，只提交一次存储I/O；
- LRU clean-line淘汰和容量硬上限。
- 多个cache line聚合到同一个io_uring fixed-buffer注册区，默认每区64MiB；
  因此1GiB cache只占16个注册项，不会触发`UIO_MAXIOV`限制。

暂不支持写入、多进程共享、外部writer一致性、跨cache-line小读、非对齐GDS拆分和在线代价模型。

## 2. 软件依赖

推荐Ubuntu 20.04/22.04：

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config liburing-dev
```

还需要：

- NVIDIA驱动；
- CUDA Toolkit；
- GPUDirect Storage用户态库`libcufile.so`；
- 已加载且配置正确的`nvidia-fs`；
- 支持GDS的NVMe、文件系统和PCIe拓扑。

检查环境：

```bash
nvidia-smi
/usr/local/cuda/gds/tools/gdscheck -p
ldconfig -p | grep -E 'libcufile|liburing'
```

如果`gdscheck`显示compatibility mode，程序仍可能运行，但此时不能把结果解释为真实GDS直通性能。

## 3. 完整GDS版本编译

```bash
cd gds_opt/gds-hcache
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DGHC_ENABLE_CUDA=ON \
  -DGHC_ENABLE_GDS=ON \
  -DGHC_ENABLE_URING=ON
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

找不到CUDA时指定：

```bash
cmake -S . -B build -DCUDAToolkit_ROOT=/usr/local/cuda
```

找不到cuFile时确认以下文件存在：

```bash
ls /usr/local/cuda/include/cufile.h
find /usr/local/cuda -name 'libcufile.so*'
```

## 4. 无GPU开发机验证缓存状态机

这个模式不构建真实GDS数据路径，只用于检查cache的命中、淘汰与并发miss合并：

```bash
cmake -S . -B build-core \
  -DGHC_ENABLE_CUDA=OFF \
  -DGHC_ENABLE_GDS=OFF \
  -DGHC_ENABLE_URING=OFF
cmake --build build-core -j
ctest --test-dir build-core --output-on-failure
```

## 5. 准备测试文件

测试文件必须位于目标NVMe文件系统，不能放在tmpfs：

```bash
TEST_FILE=/mnt/gds2/cwd_test/gds_hcache_8g.bin
sudo mkdir -p "$(dirname "$TEST_FILE")"
sudo fallocate -l 8G "$TEST_FILE"
sudo chown "$(id -u):$(id -g)" "$TEST_FILE"
```

若文件系统对未实际写入的extent有特殊处理，可写入一次随机数据：

```bash
dd if=/dev/urandom of="$TEST_FILE" bs=4M count=2048 status=progress oflag=direct
```

## 6. 单次运行

热点小读缓存测试：

```bash
CUDA_VISIBLE_DEVICES=0 ./build/ghc_bench \
  --file="$TEST_FILE" \
  --io-size=4096 \
  --line-size=65536 \
  --host-max=65536 \
  --cache-bytes=1073741824 \
  --fixed-buffer-bytes=67108864 \
  --hot-bytes=67108864 \
  --requests=100000 \
  --cache=1
```

关闭缓存作为大路径/基线测试：

```bash
CUDA_VISIBLE_DEVICES=0 ./build/ghc_bench \
  --file="$TEST_FILE" --io-size=1048576 \
  --requests=10000 --cache=0
```

输出字段：

- `gds_reads`：直接进入cuFile的大块请求数；
- `host_hits/host_misses`：Host cache命中与缺失；
- `coalesced`：等待已有fill、未重复访问SSD的请求数；
- `storage_bytes`：Host miss实际从SSD读取的字节数；
- `h2d_bytes`：Host cache复制到GPU的字节数；
- `evictions`：cache容量不足产生的clean-line淘汰数。

`--fixed-buffer-bytes`控制一个io_uring注册区的大小，而不是cache fill
大小。即使注册区为64MiB，4KiB请求仍只按`--line-size`指定的64KiB
进行cache fill，不会一次读取64MiB。

## 7. 运行最小实验矩阵

```bash
chmod +x scripts/run_mvp_matrix.sh
GHC_BIN=./build/ghc_bench \
  scripts/run_mvp_matrix.sh "$TEST_FILE" results.csv
```

正式实验必须另外采集：

```bash
iostat -x 1
pidstat -p "$(pidof ghc_bench)" 1
nvidia-smi dmon -s putcm
```

重点确认cache warm后SSD IOPS和`storage_bytes`确实下降，而不是只看应用输出的hit ratio。

## 8. 已知限制与正确性边界

1. 文件在handle打开后必须保持不可变；程序不会感知其他进程的写入、truncate或mmap修改。
2. Host pool会长期pin住配置容量，先从256MiB或1GiB开始，不要直接占满DRAM。
3. 公开API当前同步返回；这样容易验证正确性，但4KiB hit可能受CUDA同步开销影响。
4. 一次可缓存读取不能跨越cache line；跨line请求当前回到GDS。
5. `O_DIRECT`实际对齐依赖内核和文件系统；本版要求line size至少4KiB且为2的幂，后续加入`STATX_DIOALIGN`自动探测。
6. 当前io_uring对象采用同步提交/等待，MVP用于证明缓存收益，后续版本再实现多in-flight异步队列。
7. 每个fixed-buffer注册区最大1GiB；程序会按`IOV_MAX`自动增大注册区，
   避免大cache产生过多iovec。注册失败时会输出errno文字、注册区数量和大小。

## 9. MVP验收标准

- 1MiB顺序读取相对原生GDS回退不超过3%；
- 热点4/16/64KiB读取在warm后显著减少SSD IOPS；
- cache hit数据校验正确；
- working set超过cache容量时没有死锁和内存增长；
- 至少在DiskANN真实trace replay中改善P99或端到端查询时间。
