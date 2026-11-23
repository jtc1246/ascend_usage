# 宿主机运维清单 (重启 / 新卡接入)

适用于 Atlas 300I Duo (双 310P3), 记录宿主机常规重启后和新卡接入后需要的操作, 以权限类为主。

权限类操作必须在宿主机以真实 root 执行: 若业务跑在容器里 (如 Singularity, 容器内是 user namespace 的 root), 宿主机 root 拥有的设备文件在容器内显示为 nobody, 打不开也无法 chmod。

通用说明:

- 卡 ID 用 `npu-smi info` 第一列查询, 下文记为 `<card_id>`; 卡的 PCIe 地址用 `lspci` 查 (Processing accelerators: Huawei, 一张 Duo 卡两颗芯是同一个端点), 下文记为 `<bdf>`
- npu-smi 直接运行若报 libc_sec.so 缺失, 需要:

```bash
LD_LIBRARY_PATH=/usr/local/Ascend/driver/lib64/driver:/usr/local/Ascend/driver/lib64/common npu-smi info
```

## 1. 常规重启后 (每次必做)

宿主机每次重启, udev 会按默认权限重建 /dev 设备节点, 对 sysfs 文件的 chmod 也会复位, 以下操作全部要重做。

### 1.1 NPU 设备节点权限 (必须)

```bash
chmod 666 /dev/davinci? /dev/davinci_manager /dev/devmm_svm /dev/hisi_hdc
```

默认是 660, 容器内完全打不开。不做的症状 (容器内):

- 任何 ACL 程序初始化即崩溃: `terminate called after throwing an instance of 'std::logic_error'`, `what(): basic_string::_S_construct null not valid`, 进程返回码 -6 (SIGABRT)
- 伴随驱动打印 `Failed to obtain the console log level. The possible causes are as follows: ...`
- `npu-smi info` 报 `dcmi module initialize failed. ret is -8005`

注: 666 是单人机器的简单做法, 多用户环境应考虑按用户组放权。

### 1.2 PCIe BAR 窗口权限 (使用 BAR MMIO 直访时必须)

```bash
chmod 666 /sys/bus/pci/devices/<bdf>/resource4 /sys/bus/pci/devices/<bdf>/resource4_wc
```

默认 600。不做不影响正确性 (程序 mmap 失败应自动回退), 只损失 doorbell 延迟 (1.2µs → 12.2µs, 见 [310P-PERFORMANCE.md](310P-PERFORMANCE.md))。

### 1.3 验证

```bash
npu-smi info    # 两颗芯都识别、显存总量正常 (关 ECC 时每颗比开 ECC 多 3GB)
for f in /dev/davinci? /dev/davinci_manager /dev/devmm_svm /dev/hisi_hdc; do
    ( : <> "$f" ) && echo "✅ $f" || echo "❌ $f"    # 在容器内执行, 验证可读写打开
done
```

ECC 开关掉电保存 (见第 3 节), 常规重启后不需要重设; 但若发现显存变少、带宽变低或 BAR 变小, 优先怀疑 ECC 状态。

## 2. 新卡接入后 (一次性)

1. 宿主机安装 NPU 驱动, `npu-smi info` 确认两颗芯都识别 (注意上面的 LD_LIBRARY_PATH 问题)
2. 用户态安装 CANN toolkit + 310P ops 包: 见 [USER-SPACE-INSTALL.md](USER-SPACE-INSTALL.md)
3. **关闭 DDR ECC (新卡出厂默认是开启的, 每张新卡都要做一次)**:

```bash
npu-smi info -t ecc-enable -i <card_id>     # 新卡通常为 True
npu-smi set -t ecc-enable -i <card_id> -d 0 # 关闭, 重启宿主机后生效
```

   收益见第 3 节。注意: 卡正被进程占用时 set 会失败 (`Status : Fail / Message : Failed to set card ecc-enable`), 先停掉占用 NPU 的进程再执行; 若容器内仍失败, 到宿主机上执行。生效需要的那次重启之后, 第 1 节的权限要重新设置
4. 按第 1 节设置权限
5. 跑本仓库 demos 验证: `./build_demos.sh && ./run_demos.sh`

## 3. DDR ECC 开关说明

查询/设置:

```bash
npu-smi info -t ecc-enable -i <card_id>      # DDR ECC-Enable Status : True/False
npu-smi set -t ecc-enable -i <card_id> -d 0  # 0 关 / 1 开, 重启生效
```

存储位置: 配置保存在**卡上** (设备侧管理固件), 不在宿主机 —— set 成功后重启前即可读回新值, 且跨宿主机重启、掉电都保持 (实测)。因此换宿主机不需要重设; 反过来, 换新卡必须重新关一次 (出厂默认开启), 不要误以为是"机器的设置"。官方文档 (npu-smi 命令参考) 只说明"重启系统生效", 未说明存储位置。

开启 ECC 的代价 (310P 实测, 详见 [310P-PERFORMANCE.md](310P-PERFORMANCE.md)):

- 每颗可见显存少 3GB (inline ECC 占 1/16)
- 显存带宽低 ~10% (D2D 200 → 186.5 GB/s)
- resource4 BAR 窗口从 8GB 缩成 1GB (疑似固件按 ECC 布局缩窗, 机制未确认), BAR MMIO 直访 GM 基本不可用

纯推理场景 (权重只读、出错可重跑) 建议关闭; 对数据完整性敏感的场景自行权衡。
