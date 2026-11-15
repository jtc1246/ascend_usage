# Ascend 310P3 性能特性实测 (Atlas 300I Duo)

对 Atlas 300I Duo (双 310P3) 的算力、带宽、延迟、指令吞吐等硬件特性的实测记录。除标注"标称"外均为本机实测 (驱动 26.1.1, CANN 9.0.0, 2026-11)。时钟按 1.08GHz 折算周期数。

测试代码在本机 (未入库): `/root/local_llm/npu_test/bandwidth` (带宽)、`/root/local_llm/tests/npu_vlat` (指令吞吐/延迟/启动开销)、`/root/local_llm/tests/npu_cube` (mmad 布局)、`/root/local_llm/tests/npu_l2` (片上缓存)、`/root/local_llm/tests/npu_api` (API 行为)。

## 1. 基本结构

- 一张卡 = 2 颗独立的 310P3, 显存独立、互不影响, ACL 中是两个 device
- 每颗: 8 个 AI Core (架构 dav_m200), cube / vector / scalar 单元耦合在同一核内 (不像 910B 分 AIC/AIV)
- 每核片上存储: UB 256KB (TPipe 实际可分配 ~248KB)、L1 1MB、L0A 64KB、L0B 64KB、L0C 256KB
- 另有全片共享缓存 (见第 3 节), 实测有效容量 ≥16MB 且 <64MB

## 2. 显存与容量

- 每颗 48GB LPDDR4X; 标称带宽 204.8 GB/s
- **关闭 DDR ECC** (`npu-smi set -t ecc-enable -i <id> -d 0`, 重启生效) 后: 可见容量 +3GB (inline ECC 占 1/16), 两颗分别为 47301/46717 MB; ACL 实际可分配 ~46.2GB (设备侧 OS/固件预留 ~2GB + 常驻 ~1.4GB, 无法消除)
- 开 ECC 时带宽也明显缩水 (见下)

## 3. 带宽

| 项目 | 实测 | 说明 |
|---|---|---|
| 纯读 (kernel GM→UB DataCopy, ECC 关) | 204 GB/s | 标称的 99.6% |
| D2D 拷贝 (读+写, ECC 关) | 200 GB/s | |
| 纯读 / D2D (ECC 开) | 182 / 186.5 GB/s | ECC 的代价 ~10% |
| 片上共享缓存重复读 (区域 ≤16MB, 8 核错相) | **1010 GB/s** | 64MB 以上回落到 ~195; 32MB 未测 |
| 8 核锁步读相同地址 | ~1000 GB/s | 与区域大小无关 (256MB 也一样), 请求被合并 |
| 单核 MTE2 (从缓存) | ~126 GB/s | 1010/8 |

- 拷贝粒度 64KB 与 192KB 无差别; 两颗芯片同时压测互不影响
- 含义: 流式读大数据 (如每 token 过一遍的权重) 只有 204; 反复读的小数据 (≤16MB) 接近免费

## 4. 算力

### cube (每核 16×16×16 mmad)

- fp16 理论: 8 核 × 4096 MAC × 2 × 1.08GHz ≈ **70.8 TFLOPS**; 实测含反量化流水的真实 GEMM (m=256) 达 25-35 TFLOPS
- 支持的 mmad 类型: f16×f16→f32/f16, int8×int8→int32, u8 组合, int4×int4→int32; **没有 fp32, 没有 bf16**
- fractal 粒度固定 512B: f16 为 [16][16], int8 为 [16][32], int4 为 [16][64] (k 维分别 16/32/64)

### vector (每核 256B/周期操作宽度)

fp32 乘加折算全片峰值仅 **~0.55 TFLOPS** (与 cube 差 128 倍)。各指令实测吞吐 (ns/repeat, 1 repeat = 256B 目标宽度, 即 f32 64 元素 / f16 128 元素):

| 指令 | ns/rep | 周期/rep |
|---|---|---|
| Mul / Add f32 | 1.86 | 2 |
| Mul f16 | 1.30 | 1.4 |
| Cast int8→f16 | 1.19 | 1.3 |
| BlockReduceSum f32 | 0.93 | 1 |
| MulAddDst f16 | 3.7 | 4 |
| **MulAddDst (f32,f16,f16)** | **4.63** | **5 (慢, 避免用于吞吐路径)** |
| **Brcb f32** | 26.8 (8 块) | **3.35/块 (慢)** |
| **Brcb f16** | 71 (8 块) | **9.6/块 (不可用级)** |
| **Gather f16** | 22.2 | **24 (不可用级)** |

- Brcb/Gather 慢到任何"按块广播/查表"都不可行; 块广播只能靠数据布局 + 二元指令 `src1BlkStride=0`(块内复用)/`src1RepStride`(逐块前进) 实现
- 标量单元: GetValue/SetValue 走 S 管线, 与向量交互必须 PipeBarrier\<PIPE_ALL\>, 热路径应完全避免标量读

## 5. 延迟与固定开销

| 项目 | 实测 |
|---|---|
| kernel 连续提交 (同一 kernel 或两个交替, block_dim 8) | ~5 µs/个 |
| 空 kernel 提交 (不等完成) | ~3.9 µs/个 |
| kernel 提交 + 同步 | ~36-63 µs |
| 向量指令发射 (Level-0, isSetMask=false) | ~14 ns/调用 |
| 同 V 管线内依赖链 | **0 停顿** (硬件保序, 与独立指令同速; 热循环 PIPE_V barrier 纯浪费) |
| PIPE_ALL barrier (管线空闲时) | ~3 ns |
| 向量指令后紧跟 PIPE_ALL | +~16 ns (排空) |
| MTE2 拷贝 + 前后 barrier 的串行化 | ≈ 完整 DMA 延迟 (16KB 约 250ns) |

- Level-2 API 每次调用重设 mask, 标量开销大; 热路径用 Level-0 + SetVectorMask 一次设满
- 手写跨管线 SetFlag/WaitFlag 的事件 id 必须用 `GetTPipePtr()->AllocEventID<>()` 分配, 写死数字会与框架内部冲突 (症状: 形状相关的确定性错数)

## 6. mmad 数据通路与布局 (dav_m200 实测)

- 布局: L0A zZ `[M1][K1][16m][16k]`; L0B `[K1][N1][16n][16k]` (fractal 内 n 行 k 列; 若存成 [16k][16n] 则 LoadData `ifTranspose=1` 硬件转置); L0C `[N1][M1][16m][16n]`
- **m=1 为 gemv 模式**: A 按 k 连续读取 (不需要 fractal 化), 但按 512B fractal 粒度访问 — A 片偏移必须 512B 对齐, L0A 尾部要留余量, 否则设备异常 507015
- 数据通路全部可用: GM→L1, UB→L1 (`copy_ubuf_to_cbuf`), L1→L0A/L0B (LoadData, 可转置), L1→UB, CO1→UB
- **CO1→UB 的 DataCopy 必须显式 `BlockMode::BLOCK_MODE_MATRIX`** (默认 blockMode 不进任何分支, **静默不拷贝**); blockLen 单位是 fractal (16×16 元素)

## 7. ISA 限制 (会静默出错的坑)

1. **没有整数移位指令** (ShiftLeft/Right 运行不报错、输出垃圾); 位运算只有 b16 的 And/Or; nibble 解包只能用 乘 2^-k + CAST_RINT + 回减 的乘法链
2. Cast 组合不全 (无 int16→float 直转、half→int16 无 CAST_FLOOR、无 uint16→half 等), 不支持的组合同样静默输出垃圾
3. Reciprocal/Rsqrt 是低精度估计 (~4e-3), 精度敏感处用 Sqrt+Div (~1e-7); 无 Cos 指令
4. 同 kernel 循环内, 下一轮 DataCopy (MTE2) 不会等上一轮向量读取完成, 覆盖缓冲前必须 PIPE_ALL 或 TQue; 此类 bug 只有随机数据能暴露
5. 未初始化 UB 可能是 inf/NaN 位型, 固定 repeat 扫到的 padding 区必须清零
6. aicore 函数里 float 与无符号整数标量直接互转编译报错, 经 int64 中转
