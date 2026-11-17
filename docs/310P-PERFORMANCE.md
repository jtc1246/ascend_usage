# Ascend 310P3 性能特性实测 (Atlas 300I Duo)

对 Atlas 300I Duo (双 310P3) 的算力、带宽、延迟、指令吞吐等硬件特性的实测记录。除标注"标称"外均为本机实测 (驱动 26.1.1, CANN 9.0.0, 2026-11)。时钟按 1.08GHz 折算周期数。

测试代码在本机 (未入库): `/root/local_llm/npu_test/bandwidth` (带宽)、`/root/local_llm/tests/npu_vlat` (指令吞吐/延迟/启动开销)、`/root/local_llm/tests/npu_cube` (mmad 布局)、`/root/local_llm/tests/npu_l2` (片上缓存)、`/root/local_llm/tests/npu_api` (API 行为)、`/root/local_llm/tests/npu_p2p` (双芯互联/host 传输)、`/root/local_llm/tests/npu_sync` (同步原语延迟)、`/root/local_llm/tests/npu_doorbell` (GM 标志位 doorbell / host 内存映射探测)。

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
- host↔device (pinned 内存, aclrtMallocHost): d2h 25.1 GB/s / h2d 28.6 GB/s; 20KB 往返 (d2h+h2d) 单独压测 ~12.5µs, 但嵌在正常流程里的同步 aclrtMemcpy 实测只有 **~0.5µs/次** (单独压测的 6µs 大头不在拷贝本身); 非 pinned 内存会明显变慢

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
| [launch 空 kernel + 等完成] 往返, 默认流阻塞 sync | ~64 µs (等待期 CPU 占用 29%, 中断睡眠唤醒) |
| 同上, FAST_SYNC 流 (见下) | **~26 µs** (CPU 100%, 纯忙等) |
| 同上, 默认流 + `aclrtStreamQuery` 自旋 | ~26 µs (与 FAST_SYNC 等价) |
| 空流 sync / 空流 StreamQuery | 0.9 / 1.25 µs |
| GM 标志位 doorbell 往返 (预下发自旋 kernel, 见下) | **~12.2 µs** (blockdim=8 全 block 轮询 ~15) |
| 4B 同步 aclrtMemcpy 单独压测 | h2d 5.9 / d2h 6.1 µs |
| 向量指令发射 (Level-0, isSetMask=false) | ~14 ns/调用 |
| 同 V 管线内依赖链 | **0 停顿** (硬件保序, 与独立指令同速; 热循环 PIPE_V barrier 纯浪费) |
| PIPE_ALL barrier (管线空闲时) | ~3 ns |
| 向量指令后紧跟 PIPE_ALL | +~16 ns (排空) |
| MTE2 拷贝 + 前后 barrier 的串行化 | ≈ 完整 DMA 延迟 (16KB 约 250ns) |

- Level-2 API 每次调用重设 mask, 标量开销大; 热路径用 Level-0 + SetVectorMask 一次设满
- 手写跨管线 SetFlag/WaitFlag 的事件 id 必须用 `GetTPipePtr()->AllocEventID<>()` 分配, 写死数字会与框架内部冲突 (症状: 形状相关的确定性错数)
- **WaitFlag\<SRC_DST\> 由 DST 管线执行**: 事件类型按"等待方是谁"选 — 要挡 MTE2 重载必须用 MTE3_MTE2/V_MTE2; 误用 MTE3_V 时等待发生在 V 管线, MTE2 完全不被挡直接覆盖在用缓冲

### 同步原语 (host 侧, tests/npu_sync)

- 频繁 sync 的场景 (如多卡同步点) 用 `aclrtCreateStreamWithConfig(&s, 0, ACL_STREAM_FAST_LAUNCH | ACL_STREAM_FAST_SYNC)` 建流: **FAST_SYNC 让 aclrtSynchronizeStream 变成纯忙等** (等价于自己写 StreamQuery 自旋循环), 单次 sync 从 64µs 降到 26µs; FAST_LAUNCH 对该指标无可测影响
- **26µs 是 [launch + 执行 + 完成状态传播回 host] 的往返下限**, 只要走 runtime 的完成状态 (sync/StreamQuery) 就压不掉; 但 **kernel 自己写 GM 标志位可以绕开这条链路** (见下)。实测拆分表明 26µs 的大头在 [提交→设备侧派发→kernel 开跑] 的下行一侧: 只把"等完成"换成标志位轮询几乎无收益, 预下发 + 门自旋隐藏下发延迟才有收益
- **GM 标志位 doorbell** (tests/npu_doorbell): 预先 launch 一个自旋 kernel (开头轮询 GM 序号), host 用 4B 同步 aclrtMemcpy 写命令/读响应, 往返 **12.2µs** (blockdim=8 全 block 轮询同一标志 15.0µs), 自旋 kernel 对标志位的响应时间接近 0。kernel 侧轮询必须 volatile 读 + 每次 dcci (见第 8 节第 11 条); 写标志位前 PipeBarrier\<PIPE_ALL\> 之后, 该 kernel 此前 MTE3 写出的 GM 数据对 host d2h 完整可见 (无需整 cache flush)
- **host 内存映射 (zero-copy) 在 310P 完全不支持**: `aclrtHostMemMapCapabilities` 对所有引擎 (AIC/AIV/PCIEDMA/SDMA 等) 都返回 0, `aclrtHostRegister(MAPPED)` 报 207000, `aclrtHostGetDevicePointer` 对 aclrtMallocHost 内存返回成功但 devPtr=NULL (必须判空) — 标志位只能放设备 GM, host 侧轮询只能走同步 memcpy
- **aclrtMemcpyAsync 的小拷贝 (4B~20KB) 每次有 ~290µs 的任务调度开销**, 比阻塞 sync 还慢几倍 — 不能用异步小拷贝做 doorbell/序号自旋这类模式; 小数据同步 aclrtMemcpy 反而只要 ~0.5µs
- **同步 aclrtMemcpy (H2D/D2H) 不排队到 stream**: 会在已入队未执行的 kernel 之前落地, 覆盖其输入 (WAR 竞态); 对在用缓冲写入前必须先 sync。症状: 每个 build 内确定、跨 build 不同的"随机"差异

## 6. 双芯互联 (tests/npu_p2p)

- **两颗 310P3 之间 P2P 不可用**: `aclrtDeviceCanAccessPeer` 双向都返回 0, 跨 device 的 `aclrtMemcpy` D2D 报错 507899 (两个 ctx 下都试过) — 芯间数据交换只能经 host 中转 (d2h + h2d)
- host 中转往返 (pinned): 20KB 一来一回 ~12.5µs; [1][5120] f32 的 all-reduce 模拟 (sync + d2h×2 + barrier + h2d×2) ~19µs/次, [256][5120] ~832µs/次
- 两芯各自的 host 传输可同时进行, 实测互不明显干扰

## 7. mmad 数据通路与布局 (dav_m200 实测)

- 布局: L0A zZ `[M1][K1][16m][16k]`; L0B `[K1][N1][16n][16k]` (fractal 内 n 行 k 列; 若存成 [16k][16n] 则 LoadData `ifTranspose=1` 硬件转置); L0C `[N1][M1][16m][16n]`
- **m=1 为 gemv 模式**: A 按 k 连续读取 (不需要 fractal 化), 但按 512B fractal 粒度访问 — A 片偏移必须 512B 对齐, L0A 尾部要留余量, 否则设备异常 507015
- 数据通路全部可用: GM→L1, UB→L1 (`copy_ubuf_to_cbuf`), L1→L0A/L0B (LoadData, 可转置), L1→UB, CO1→UB
- **CO1→UB 的 DataCopy 必须显式 `BlockMode::BLOCK_MODE_MATRIX`** (默认 blockMode 不进任何分支, **静默不拷贝**); blockLen 单位是 fractal (16×16 元素); 该拷贝在 V 队列执行 (与后续向量 op 同队列保序, 跨管线用 M_V/V_M 事件护住即可)
- mmad 类型组合 (dav_m200): fp16 之外的整数组合只支持 (i8,i8)/(u8,i8)/(u8,u8); **b8 的 LoadData 转置是按 u16 做 16×16 转置** (一个 u16 的两个字节分别落到 k 和 k+16), 布局要按这个语义反推
- `LoadData2DParams` 的 `srcStride` (源 fractal 间隔, 单位 fractal) 与 `dstGap` (目的 fractal 间隙, 0=连续) 都可用, 配合可把 L1 的 [K1][M1]/[N1][K1] 序装成 L0 目标序; **L1→L0A 也支持 ifTranspose** (m200 实测)
- L0C 可划多个分区独立 init/累加共存, 跨很多条其他 mmad 后再 accumulate 同一分区也可靠

## 8. ISA 与运行时的坑 (会静默出错)

1. **没有整数移位指令** (ShiftLeft/Right 运行不报错、输出垃圾); 位运算只有 b16 的 And/Or; nibble 解包只能用 乘 2^-k + CAST_RINT + 回减 的乘法链
2. Cast 组合不全 (无 int16→float 直转、half→int16 无 CAST_FLOOR、无 uint16→half 等), 不支持的组合同样静默输出垃圾; 可用的组合: int8/uint8→half, int16↔half, half↔float 均精确, **half→int8 (CAST_NONE) 也可用** (±127 整数精确)
3. Reciprocal/Rsqrt 是低精度估计 (~4e-3), 精度敏感处用 Sqrt+Div (~1e-7, 向量 Div 与 host IEEE 除法逐位一致); 无 Cos 指令
4. 同 kernel 循环内, 下一轮 DataCopy (MTE2) 不会等上一轮向量读取完成, 覆盖缓冲前必须 PIPE_ALL 或 TQue; 此类 bug 只有随机数据能暴露
5. 未初始化 UB 可能是 inf/NaN 位型, 固定 repeat 扫到的 padding 区必须清零
6. aicore 函数里 float 与无符号整数标量直接互转编译报错, 经 int64 中转
7. **Level-0 API 的 mask 参数传单个 uint64 是 counter 模式** (每 repeat 处理的元素个数), 不是位掩码: 传 0xFFFF 意为 65535 个 → 被钳成整 repeat; 要位掩码必须 `SetVectorMask<T>(hi, lo)` + isSetMask=false (或传 uint64[2])。症状极具迷惑性: masked 写变整 repeat 写、相互踩踏、最后一个 repeat 越界写坏相邻 UB 缓冲
8. **Brcb 与 V 管线的依赖完全不保序, 前后都必须 PipeBarrier\<PIPE_ALL\> (PIPE_V 不够)**: 第 5 节"同 V 管线保序"对 Brcb 不成立, Brcb 读前序 V 写 / 后续 V 读 Brcb 输出都会撞旧值。症状: 低概率恰好 8 行一组 (= Brcb 一个 repeat) 用旧广播值, 位置随机, 源与 Brcb 相隔远时窗口极小可长期潜伏; 定位靠"坏行 8 行对齐"特征
9. **向量单元会把 fp16 次正规数 flush 成 0** (如 scale 预除后掉入 2^-24~2^-15 区间的值); 需要小 fp16 常量时在 f32 域派生, 或在 host 侧把次正规钳 0 保持两端一致
10. f32 Exp 对大正数溢出产生垃圾且 **垃圾 ×0 不保证得 0** (做 mask 乘之前要先 Mins 钳住); Ln(0) = -inf 会让后续差值变 NaN (前面先 Maxs(1e-30))。这类"数学上该是 0 的部分"只在真实数据分布下触发, 随机测试难覆盖
11. **GM 标量自旋轮询必须 `__gm__ volatile` 指针读 + 每次读前 dcci** (`DataCacheCleanAndInvalid<T, CacheLine::SINGLE_CACHE_LINE>`, m200 用 2 参数版): 非 volatile 的 `GlobalTensor::GetValue` 轮询会被编译器把读提升出循环 (dcci intrinsic 不带 memory clobber), 加了 dcci 也永久卡死; 有 volatile 但无 dcci 则首轮冷 cache 侥幸通过、第二轮起一直读缓存旧值。写标志位同样要在写后 dcci 才对 host 可见
