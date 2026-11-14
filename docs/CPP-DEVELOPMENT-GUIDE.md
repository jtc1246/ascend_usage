# Atlas 300I Duo 纯 C++ 使用说明与注意事项

验证日期：2026-11-14
验证设备：Atlas 300I Duo，两颗 Ascend 310P3
验证基线：Ascend HDK 26.1.1、CANN 9.0.0、x86_64、GCC 11.5

## 1. 用途与验证边界

本仓库在不使用 Torch/TorchNPU 的条件下，提供三个 C++ Demo：

| Demo | 验证内容 |
|---|---|
| `acl_memory_demo` | ACL 初始化、Device/Stream/Event、Device 内存、H2D/D2D/D2H 拷贝 |
| `acl_gemm_demo` | ACLNN Tensor、FP16 MatMul 两阶段调用、结果检查 |
| `ascendc_add_demo` | Ascend C Kernel 编译、打包、Host 链接、FP16 向量加法 |

这些 Demo 只证明基础编译和运行链路，**不验证模型推理、ATC/OM、性能、长时间稳定性或其他 CANN/硬件版本兼容性**。

## 2. 环境准备

需要：

1. 已安装驱动，普通用户对设备有访问权限，`npu-smi info` 能正常显示设备；
2. 已安装 CANN Toolkit 和与设备匹配的 ops 包；
3. CMake 3.16 或更新版本；
4. 支持 C++17 的 Host C++ 编译器。

默认 CANN 目录：

```text
/usr/local/Ascend/ascend-toolkit/latest
```

如果使用隔离安装，通过 `CANN_HOME` 指定包含 `set_env.sh` 的目录。无需修改用户全局 shell 配置。

官方下载入口：

- [固件与驱动](https://www.hiascend.com/hardware/firmware-drivers)
- [CANN 文档中心](https://www.hiascend.com/document)
- [CANN 9.0.0 社区版文档](https://www.hiascend.com/document/detail/zh/CANNCommunityEdition/900/index/index.html)

## 3. 构建与运行

仓库结构：

```text
ascend_usage/
├── demos/
│   ├── CMakeLists.txt
│   ├── acl_check.h
│   ├── acl_memory_demo.cpp
│   ├── acl_gemm_demo.cpp
│   ├── ascendc_add_kernel.cpp
│   └── ascendc_add_demo.cpp
├── build_demos.sh
├── run_demos.sh
├── docs/
├── logs/
└── licenses/
```

使用默认环境：

```bash
./build_demos.sh
./run_demos.sh
```

指定隔离的 CANN 和 CMake：

```bash
export CANN_HOME=/path/to/cann
export CMAKE_BIN=/path/to/cmake
export BUILD_DIR="$PWD/demos/build"
./build_demos.sh
./run_demos.sh
```

`run_demos.sh` 默认依次运行 Device 0、1。也可以明确指定 Device 及顺序：

```bash
./run_demos.sh 0
./run_demos.sh 1 0
```

脚本在 `source CANN_HOME/set_env.sh` 时暂时关闭 `set -u`，原因是 CANN 9.0.0 的环境脚本会读取可能未定义的 `LD_LIBRARY_PATH`、`PYTHONPATH` 和 `CMAKE_PREFIX_PATH`。

## 4. AscendCL：设备、内存与异步拷贝

源码：[acl_memory_demo.cpp](../demos/acl_memory_demo.cpp)

基本生命周期：

```text
aclInit
→ aclrtSetDevice
→ aclrtCreateStream
→ aclrtMalloc
→ H2D / D2D 异步拷贝
→ aclrtRecordEvent + aclrtSynchronizeEvent
→ D2H 取回结果
→ 释放 Event、内存、Stream
→ aclrtResetDevice
→ aclFinalize
```

注意事项：

1. 同一 Stream 中的操作按顺序执行；读取结果前仍需同步。
2. Host 内存和 Device 内存是不同地址空间，Device 指针必须来自 `aclrtMalloc`。
3. Demo 使用 `ACL_MEM_MALLOC_HUGE_FIRST`；ACLNN Workspace 使用 `ACL_MEM_MALLOC_NORMAL_ONLY`。
4. 所有 ACL 返回码都应检查。示例会打印失败操作和数值错误码。
5. 两颗 310P3 是两个独立 Device，不是自动统一编址的 96 GB 单设备。

## 5. ACLNN：FP16 MatMul

源码：[acl_gemm_demo.cpp](../demos/acl_gemm_demo.cpp)

示例使用 32×32 矩阵，A 为已知 FP16 数据，B 为单位矩阵，因此 C 必须等于 A。

ACLNN 常用两阶段调用：

```cpp
aclnnMatmulGetWorkspaceSize(tensorA, tensorB, tensorC, cubeMathType,
                            &workspaceSize, &executor);
aclnnMatmul(workspace, workspaceSize, executor, stream);
```

关键概念：

1. `aclCreateTensor` 创建的是**描述符**，数据指针指向已分配的 Device 内存，不会复制矩阵。
2. Shape、Stride、Format、DataType 必须匹配。本例为二维、ND、FP16、连续行优先布局。
3. 第一段接口返回 Workspace 大小和执行器；第二段接口向指定 Stream 提交计算。
4. `workspaceSize == 0` 是合法结果，此时无需分配 Workspace。
5. 本环境需要显式链接 `nnopbase`，否则 `aclCreateTensor` 会出现 `DSO missing from command line`。

```cmake
target_link_libraries(acl_gemm_demo PRIVATE ascendcl opapi nnopbase)
```

相关声明位于 CANN 安装目录：

```text
${CANN_HOME}/x86_64-linux/include/aclnnop/aclnn_matmul.h
${CANN_HOME}/x86_64-linux/include/aclnn/acl_meta.h
```

其他 CPU 架构的目录名需相应调整；CMake 示例使用 `CMAKE_SYSTEM_PROCESSOR` 生成该路径。

## 6. Ascend C：自定义向量加法

Kernel：[ascendc_add_kernel.cpp](../demos/ascendc_add_kernel.cpp)
Host：[ascendc_add_demo.cpp](../demos/ascendc_add_demo.cpp)

Kernel 使用固定的 8 个 Block，每个 Block 分 Tile 处理 FP16 数据：

```text
Global Memory（GM）
→ DataCopy 到片上 LocalTensor
→ AscendC::Add
→ DataCopy 回 GM
```

`TPipe + TQue` 管理输入和输出队列。此示例 Shape、Block 数和 Tile 固定且对齐，不是通用 Add；生产代码还需要处理尾块、任意 Shape、Tiling 和数据类型。

Host 包含构建时自动生成的 `aclrtlaunch_vector_add.h`，使用：

```cpp
ACLRT_LAUNCH_KERNEL(vector_add)(BLOCK_DIM, stream, deviceX, deviceY, deviceZ);
```

CMake 关键配置：

```cmake
set(SOC_VERSION "Ascend310P3")
set(RUN_MODE "npu")
set(ASCEND_KERNEL_LAUNCH_ONLY ON)
include(/path/to/ascendc_kernel_cmake/ascendc.cmake)
ascendc_library(ascendc_add_kernel STATIC ascendc_add_kernel.cpp)
```

仓库中的 [CMakeLists.txt](../demos/CMakeLists.txt) 会依次查找几种常见 CANN 安装布局。Ascend C 构建不是单纯调用 g++，还包括预处理、Device 编译、Kernel 打包、Host Stub 生成和最终链接。

## 7. 常见问题

### 7.1 找不到 `acl/acl.h`

为普通 Host Target 显式添加 CANN 的头文件和库目录。包含 Ascend C 的 CMake 文件不代表普通 ACL Target 会自动继承这些路径。

```cmake
target_include_directories(target PRIVATE "${ASCEND_ARCH_DIR}/include")
target_link_directories(target PRIVATE "${ASCEND_ARCH_DIR}/lib64")
```

### 7.2 `aclblasGemmEx` 返回 `100024`

在本次 CANN 9.0.0/310P3 环境中，`100024` 对应 `ACL_ERROR_OP_NOT_FOUND`。2×2 和 32×32 均失败；将 `alpha/beta` 改为 Device 内存后仍失败。详细日志为：

```text
MatchDynamicOpModel: No match dynamic model, opName = GEMM
MatchOpModel ... opName = GEMM fail from static map or dynamic map
```

本仓库因此改用 ACLNN MatMul。新项目应优先评估 ACLNN 或 Ascend C，不应直接把归档 Samples 中的旧 `aclop/aclblas` 代码作为新实现基线。

相关记录：

- [旧 CBLAS 32×32 失败](../logs/gemm-32x32-device0.log)
- [Device 标量参数仍失败](../logs/gemm-device-scalars-device0.log)
- [ACLNN 成功](../logs/aclnn-matmul-device0.log)

### 7.3 官网文档直接下载得到 WAF 页面

部分官网文档 URL 直接通过 `curl` 下载时可能得到 JavaScript/WAF 验证页，而不是正文。离线排障应同时保留对应版本的头文件、可工作的示例、构建日志和在线文档链接。

### 7.4 新版 Samples 不一定支持当前设备

截至本次验证，新版 `cann-samples` 的部分快速入门主要面向 Ascend 950/910B/C，部分样例要求更高 CANN 版本。旧版官方 Samples 又可能包含废弃 API。使用任何示例前都应核对：

- SoC；
- CANN 版本；
- Host 架构；
- API 是否废弃；
- 官方文档和实际运行结果。

示例入口：

- [新版 CANN Samples](https://gitcode.com/cann/cann-samples)
- [归档 Ascend Samples](https://gitee.com/ascend/samples)
- [归档 Ascend C Add 参考](https://gitee.com/ascend/samples/tree/master/operator/ascendc/0_introduction/3_add_kernellaunch/AddKernelInvocationAcl)

## 8. 实施建议

1. 固定驱动、CANN、文档和示例版本，不混用最新 Weekly 示例。
2. 普通算子优先评估 ACLNN；特殊量化、融合或数据布局再使用 Ascend C。
3. 每种新算子先用小 Shape 做 CPU 对照，再接入完整模型。
4. 多芯片模型需要显式划分和通信设计，不能把双芯片当作统一显存。
5. 保留失败日志；`$HOME/ascend/log/` 下的 plog 通常比终端错误码更具体。
6. 升级内核、驱动、固件或 CANN 后，重新进行干净构建和双 Device 验证。
7. 如果希望类似“CUDA Driver API + 全部手写 Kernel”的依赖方式，可以使用 AscendCL + Ascend C，不调用 ACLNN；不建议直接依赖未公开的内核驱动 ioctl。

## 9. 验证记录

- [干净构建](../logs/build-clean-verified.log)
- [双 Device 最终运行](../logs/run-final-both-devices.log)
- [独立仓库版本构建](../logs/standalone-build.log)
- [独立仓库版本运行](../logs/standalone-run.log)

成功输出的格式为：

```text
ACL memory demo: device=0 ... result=PASS
ACLNN FP16 MatMul demo: device=0 ... result=PASS
Ascend C Add demo: device=0 ... result=PASS
```
