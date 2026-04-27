# G-Route (MVP)

G-Route 是一个用户态 GPU-aware I/O routing runtime 的第一版骨架。

## MVP 范围

当前版本包含：

- **metadata**: I/O 请求与文件元信息模型
- **route planner**: 根据 I/O 大小、缓存热度、可用性进行路径选择
- **POSIX backend**: 可运行的 `pread` 读取后端
- **cuFile backend stub**: 不依赖 GDS 的可编译占位实现（默认不可用，失败时返回清晰错误）
- **PyTorch Dataset wrapper**: Python 侧数据集包装骨架 `GRouteDatasetWrapper`

> 本版本不要求 GPU/GDS 环境，默认可以在纯 CPU 环境编译通过。

## 目录结构

```text
groute/
├── cpp/include/groute/
├── cpp/src/
├── python/groute/
├── examples/
├── tests/
├── CMakeLists.txt
├── pyproject.toml
└── README.md
```

## 构建（C++）

```bash
cmake -S groute -B groute/build
cmake --build groute/build -j
ctest --test-dir groute/build --output-on-failure
```

## 运行示例

```bash
./groute/build/groute_runtime_demo
```

## Python 包（本地安装）

```bash
python -m pip install -e groute
python -c "import groute; print(groute.__all__)"
```

## 后续迭代方向

- 引入真实 cuFile backend（替换 stub）
- 引入 GPU buffer 生命周期管理
- 增加策略学习/在线自适应路由
- 增加 Python/C++ 绑定（pybind11）
