# 只有驱动时：纯 C++ 用户态编译环境安装

本文说明在**驱动已经安装且 `npu-smi info` 正常**的前提下，如何以普通用户准备本仓库所需的 CANN、Ascend C 编译器、ACLNN 和 CMake。

不涉及 Torch/TorchNPU，不重装驱动，不刷写固件，也不要求修改系统目录。

## 1. 需要哪些组件

| 组件 | 用途 | 是否随 CANN Toolkit 提供 |
|---|---|---|
| AscendCL / `libascendcl` | Device、Stream、Event、内存、Kernel Launch | 是 |
| ACLNN / `libopapi`、`libnnopbase` | 现成 MatMul 等算子接口 | Toolkit 与对应 ops 包共同提供 |
| `ccec` / Bisheng Compiler | 编译 Ascend C Device Kernel | 是 |
| Ascend C CMake 文件和头文件 | Kernel 构建、打包和 Host Stub | 是 |
| 310P ops | 310P3 的内置算子实现与配置 | 单独的 `Ascend-cann-310p-ops` 包 |
| Host `g++` | 编译 C++ 主程序 | 否，使用系统或已有用户工具链 |
| CMake / Make | 组织 Host 与 Device 构建 | 否 |
| Python 3 | CANN 安装器和部分构建工具的内部依赖；不是运行 Demo 的 AI 框架 | 否 |

**只有驱动无法编译 Ascend C，也没有 ACL/ACLNN 用户态 API。** 最低限度仍需安装 CANN 用户态组件。

## 2. 安装前检查

```bash
npu-smi info
uname -m
g++ --version
make --version
python3 --version
```

当前验证环境为 x86_64、GCC 11.5、Python 3.9.21。其他硬件、CPU 架构和 CANN 版本必须重新核对官方配套关系，不能直接套用本文包名和 SHA-256。

CANN 9.0.0 的本次实测磁盘占用：

- Toolkit 安装包约 1.2 GiB；
- 310P ops 安装包约 2.1 GiB；
- Toolkit + ops 安装完成后约 9.2 GiB；
- 安装解包和构建还需要额外临时空间。

建议预留至少 20 GiB，并在安装前用 `df -h` 检查目标文件系统。

## 3. 下载并校验 CANN

本文脚本固定下载并校验：

- `Ascend-cann-toolkit_9.0.0_linux-x86_64.run`
- `Ascend-cann-310p-ops_9.0.0_linux-x86_64.run`

使用一个全新的或空目录：

```bash
./scripts/download_cann_9.0.0_x86_64.sh /path/to/new-cann-packages
```

脚本只向该目录写入两个包，拒绝使用非空目录，并在下载后执行 SHA-256 校验。校验清单为 [cann-9.0.0-x86_64.sha256](../scripts/cann-9.0.0-x86_64.sha256)。

也可以从官方页面手动选择对应版本，不要仅根据文件名推断兼容性：

- [CANN 下载入口](https://www.hiascend.com/cann/download)
- [CANN 9.0.0 社区版文档](https://www.hiascend.com/document/detail/zh/CANNCommunityEdition/900/index/index.html)
- [固件与驱动配套入口](https://www.hiascend.com/hardware/firmware-drivers)

## 4. 普通用户隔离安装 CANN

选择一个新的或空的安装前缀，例如 `$HOME/opt/ascend-cann-9.0.0`：

```bash
./scripts/install_cann_9.0.0_x86_64.sh \
    /path/to/new-cann-packages/Ascend-cann-toolkit_9.0.0_linux-x86_64.run \
    /path/to/new-cann-packages/Ascend-cann-310p-ops_9.0.0_linux-x86_64.run \
    "$HOME/opt/ascend-cann-9.0.0"
```

脚本执行顺序：

1. 检查两个包的精确文件名和 SHA-256；
2. 拒绝覆盖非空安装前缀；
3. 以当前用户运行 Toolkit 的 `--full` 安装；
4. 将 310P ops 安装到**同一个前缀**；
5. 把安装器的 `HOME` 指向前缀内的 `installer-home`，尽量隔离安装日志和缓存；
6. 安装结束后打印 `CANN_HOME` 设置方法。

不需要 `sudo`，脚本也不会主动修改 `~/.bashrc`。

**注意：**脚本使用安装器的 `--quiet`，其帮助明确说明这会接受安装包 EULA。运行安装脚本前应先阅读并接受对应许可；如果不希望非交互安装，请按官方文档手动执行安装器。

不要对已有 CANN 目录重复运行此脚本。升级、覆盖和多版本共存是不同操作，应另做备份并按官方升级文档处理。

## 5. 用户态安装 CMake

如果系统已有 CMake 3.16 或更新版本：

```bash
cmake --version
```

无需另外安装。如果没有全局 CMake，而 Python 已支持 `venv`，可以创建独立环境：

```bash
./scripts/install_cmake_venv.sh "$HOME/opt/cmake-venv"
export CMAKE_BIN="$HOME/opt/cmake-venv/bin/cmake"
```

该脚本通过 pip 安装 `cmake==4.4.4`，不写系统 Python 包。若系统缺少 `venv`，需要先通过发行版安装相应用户环境支持，或改用官方 CMake 预编译包；这不属于 CANN 本身的依赖问题。

## 6. 检查环境并编译 Demo

假设安装脚本最后输出的实际 CANN 目录为 `$HOME/opt/ascend-cann-9.0.0/cann-9.0.0`：

```bash
export CANN_HOME="$HOME/opt/ascend-cann-9.0.0/cann-9.0.0"
./scripts/check_cpp_env.sh
./build_demos.sh
./run_demos.sh
```

`check_cpp_env.sh` 会检查：

- Host 编译器、Make、Python 和 `npu-smi`；
- ACL、ACLNN 头文件及三个运行库；
- `set_env.sh` 和 `ASCEND_OPP_PATH`；
- Ascend C 编译器 `ccec` 是否进入 PATH；
- `CMAKE_BIN` 是否可用。

如果只需要 ACL + 手写 Ascend C，不想使用 ACLNN，可以不调用 `acl_gemm_demo`；但 Toolkit、Ascend C 编译器和 AscendCL 仍然是必要的。跳过 ACLNN 不等于可以跳过 CANN。

## 7. 安装和运行注意事项

1. **驱动、Toolkit、ops、固件需要配套。** 不要因为本机 Demo 通过，就把同一包组合推广到所有昇腾设备。
2. **Toolkit 与 ops 使用同一安装前缀。** 分装到无关目录会导致头文件、OPP 和运行库不匹配。
3. **安装器可能写用户目录。** 即使设置了临时 `HOME`，子组件仍可能尝试写真实用户目录下的 `var/log`。本次出现过日志路径不存在警告，但最终安装成功；不能承诺完全无外部副作用。
4. **CANN 的 `set_env.sh` 与 `set -u` 不兼容。** 本仓库脚本采用临时关闭 nounset 的方式，不修改官方脚本。
5. **保留安装日志和包校验值。** 出现算子缺失时，先确认安装的是完整 Toolkit 和匹配 ops，而不是仅 NNRT 运行组件。
6. **无需为纯 C++ Demo 安装 Torch。** Python 在这里只是安装器、CMake venv 和部分 CANN 工具的基础依赖。

## 8. 本次实际安装记录

- [Toolkit 完整用户态安装](../logs/cann-full-local-install.log)
- [310P ops 安装到同一前缀](../logs/cann-310p-ops-local-install.log)
- [实际安装包 SHA-256](../logs/cann-package-sha256.txt)
- [安装后独立仓库构建](../logs/standalone-build.log)
- [安装后双 Device 运行](../logs/standalone-run.log)
