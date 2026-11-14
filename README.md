# Ascend C++ Usage

Atlas 300I Duo / Ascend 310P3 的纯 C++ 使用示例。包含 AscendCL 运行时、ACLNN MatMul 和 Ascend C 自定义 Kernel，不使用 Torch/TorchNPU。

## 示例

| 程序 | 内容 |
|---|---|
| `acl_memory_demo` | Device、Stream、Event、Device 内存和 H2D/D2D/D2H 拷贝 |
| `acl_gemm_demo` | ACLNN FP16 MatMul 两阶段接口与结果检查 |
| `ascendc_add_demo` | Ascend C FP16 向量加法，覆盖 Device 编译、Kernel 打包和 Host 启动 |

示例只用于验证编译、运行和数值正确性，不用于性能测试，也不是通用生产算子。

## 环境要求

- 已安装并能识别设备的昇腾驱动；
- CANN Toolkit 和与设备匹配的 ops 包；
- CMake 3.16 或更新版本；
- 支持 C++17 的 Host C++ 编译器；
- 当前验证环境：Atlas 300I Duo、两颗 310P3、CANN 9.0.0、x86_64、GCC 11.5。

默认 CANN 路径为 `/usr/local/Ascend/ascend-toolkit/latest`。自定义或隔离安装时，通过 `CANN_HOME` 指定包含 `set_env.sh` 的目录。

**如果机器目前只有驱动**，先阅读[纯 C++ 用户态编译环境安装](docs/USER-SPACE-INSTALL.md)。其中包含 CANN/310P ops 下载、SHA-256 校验、普通用户隔离安装、CMake venv 和环境检查脚本。

## 构建和运行

```bash
./build_demos.sh
./run_demos.sh
```

默认依次在 Device 0 和 Device 1 上运行。只验证一个 Device：

```bash
./run_demos.sh 0
```

使用非默认 CANN、CMake 或构建目录：

```bash
export CANN_HOME=/path/to/cann
export CMAKE_BIN=/path/to/cmake
export BUILD_DIR="$PWD/demos/build"
./build_demos.sh
./run_demos.sh
```

所有 Demo 都检查实际结果，成功时打印 `PASS`，失败时返回非零退出码。

## 文档

- [只有驱动时的用户态编译环境安装](docs/USER-SPACE-INSTALL.md)
- [详细使用说明、关键实现和注意事项](docs/CPP-DEVELOPMENT-GUIDE.md)
- [最终双 Device 运行记录](logs/run-final-both-devices.log)
- [第三方样例来源与许可说明](THIRD_PARTY_NOTICES.md)

## 官方资源

- [CANN 文档中心](https://www.hiascend.com/document)
- [CANN Samples](https://gitcode.com/cann/cann-samples)
- [归档 Ascend Samples](https://gitee.com/ascend/samples)
- [固件与驱动](https://www.hiascend.com/hardware/firmware-drivers)
