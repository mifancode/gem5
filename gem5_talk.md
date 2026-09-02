# gem5 讲解讲义：从总体 → ARM → ARM O3

> 面向：**系统 / 算子方向**同事（对微架构有了解）
> 目标：讲清 gem5 是「什么、怎么工作、能用来干什么」，并落到 ARM 体系结构和 ARM O3 乱序流水线上。
> 本讲义与 5 份素材配套：`gem5_architecture.drawio` / `arm_architecture.drawio` / `arm_o3_architecture.drawio` / `event_tick_sequence.mmd` / `talk_demo.py`。文档中带 `[图]` 的地方对应打开对应图。

---

## 0. 一句话定位

**gem5 = 一个「模块化 + 事件驱动」的计算机系统模拟器**：CPU、缓存、内存、设备都是可替换的模块（SimObject），用一个全局事件队列按 tick 精确推进仿真。既能做处理器微架构研究，也能做系统级（OS、内存、外设）建模。

它解决的核心矛盾：**真实芯片跑不了还没造出来的设计、看不见内部信号、迭代慢成本高**。模拟器把「设计」变成可运行、可观测、可批量扫描参数的模型。

---

## 1. gem5 总体介绍

### 1.1 什么是 SimObject —— 一切皆模块

gem5 里每个部件都是一个 `SimObject`（由 Python 参数类 + C++ 实现类构成），通过 `Port` 相互连接，用 `Packet` / `Request` 通信。可替换是它最好的设计：

| 层次 | 源码目录 | 典型可选项 |
|---|---|---|
| CPU | `src/cpu/` | Atomic（功能快）· Timing（顺序）· Minor（顺序流水）· **O3（乱序）** · KVM（硬件虚拟化） |
| 内存 | `src/mem/` | 缓存层次（`cache/`）· 一致性协议（`ruby/`，如 MESI/MOESI）· DRAM（`dram_interface.cc`） |
| 设备 | `src/dev/` | 磁盘、网卡、PCI 等 |
| ISA | `src/arch/` | x86 · ARM · RISC-V · MIPS · Power · SPARC |

**同一条内存系统，你可以把 `AtomicCPU` 换成 `O3CPU`**——这正是「精度 ↔ 速度」权衡的核心：Atomic 快但几乎不建模时序；O3 精确但慢一个量级。

### 1.2 两种运行模式

- **SE（Syscall Emulation）**：不跑操作系统，guest 程序系统调用由 gem5 模拟处理。快，适合 CPU/内存微架构研究。
- **FS（Full System）**：跑完整 OS（Linux/Android）。真实、慢。

本讲义示例全部用 SE 模式。

### 1.3 核心机制：事件驱动 + tick 推进

gem5 **不是逐条指令线性执行**，而是维护一个全局**事件队列**（`src/sim/eventq.cc`），每个事件带时间戳 `tick`（`sim/cur_tick.cc`），调度器永远取「时间最早」的事件执行。CPU 每周期、cache 每次命中/缺失、DRAM 每个 bank 时序，都是往队列里塞事件——这就是精确时序（cycle-accurate）的来源。

**关键点：时间不是逐 cycle 慢走，而是「跳到下一个事件时刻」。** 详细过程见 `event_tick_sequence.mmd`：

```
主循环 ──取最早事件 e(t)──► curTick = t ──► e.process()
              CPU ──► 内存(延迟10cycle) ──► 调度响应事件 @ t+10
              CPU ──► 调度下一周期事件 @ t+1
              ──► 回到循环取下一个最早事件
```

队列始终按时间戳有序，所以天然实现精确时序而无需空转。

### 1.4 一次仿真的完整流程

```
Python 配置脚本(configs/*.py)  →  实例化 SimObject 树(System→CPU→Cache→MemCtrl)
        ↓ 交给内核
C++ 事件驱动仿真内核(build/*/gem5.opt)  →  逐 tick 推进
        ↓
输出 m5out/stats.txt(性能统计) + config.ini(参数树) + trace
```

**配置用 Python（灵活、脚本化），仿真用 C++（性能）**。`m5out/config.ini` 把整个系统参数树 dump 出来，是理解「某个模型到底配了什么」的最好入口。

### 1.5 [图] gem5 总体架构

打开 `gem5_architecture.drawio`。四层结构：
`①配置层(Python脚本)` → `②仿真内核(EventQueue)` → `③组件层(CPU/Cache/XBar/MemCtrl/Dev/ISA)` → `④输出(stats/config/trace)`。

---

## 2. ARM on gem5

> 那 ARM 是怎么「挂在」gem5 上的？答：ISA 只是一套可以被 CPU 调用的「译码 + 指令语义 + 系统支持」模块（`src/arch/arm/`），与 CPU 模型解耦。

### 2.1 ISA 是「自动生成」的

`src/arch/arm/isa/` 里有 ISA 描述语言 `.isa` 文件（`main.isa`、`bitfields.isa`、`operands.isa`、`formats/`、`templates/`、`decoder/`），在**编译期**由 `src/arch/isa_parser/isa_parser.py` 处理，自动生成：

- `decoder.cc` / `decoder.hh`（译码器）
- 各指令的 `StaticInst` 实现

**改 ISA 描述就能加指令**——这是 gem5 支持多 ISA 且易扩展的根源。

### 2.2 ARM 体系结构各部件（对应 `src/arch/arm/`）

| 部件 | 源码 | 作用 |
|---|---|---|
| 解码器 | `decoder.cc/hh` | 机器码 → StaticInst |
| 指令实现 | `insts/` | 标量（分支/数据/访存/杂项/加密）；向量 **NEON/SVE/SME**；事务内存 **TME** |
| 寄存器 | `regs/` | 整数 `int` · 条件码 `cc(NZCV)` · 向量 `vec` · 矩阵 `mat` · 系统 `misc` |
| 程序状态 | `pcstate.hh` | CPSR/SPSR · 异常级 **EL0-3** · 栈指针 `SP_ELx` |
| 地址翻译 | `mmu.cc` `tlb.cc` `table_walker.cc` `pagetable.cc` | MMU/TLB/页表遍历 |
| 虚拟化 | `stage2_lookup.cc` | **Stage2 二层翻译**（VHE/虚拟化关键） |
| 系统支持 | `system.cc` `ArmSystem.py` | **Generic Timer · GIC** 中断控制器 |
| 中断 / 异常 | `interrupts.cc` / `faults.cc` | 中断路由 / 异常(abort) |
| 性能 | `pmu.cc` | PMU 性能计数器 |
| 安全扩展 | `pauth_helpers.cc` `qarma.cc` | **指针认证 PAC** |
| 资源分区 | `mpam.cc` | **MPAM** 内存系统资源分区 |
| 事务内存 | `htm.cc` `tme*.cc` | **HTM/TME** |

### 2.3 外部接口（怎么和主机相接）

- **KVM**（`src/arch/arm/kvm/`）：调用主机 KVM 硬件虚拟化加速，可跑真实 OS。
- **FastModel**（`fastmodel/`）：集成 ARM 官方 Fast Models。
- **Semihosting**（`semihosting.cc`）：把设备 I/O 转发到主机 stdio。
- **remote_gdb / self_debug / tracers**：远程 GDB 调试、自调试寄存器、指令追踪。

### 2.4 [图] ARM 架构

打开 `arm_architecture.drawio`。五层：
`①指令侧(取指→Decoder→insts)` → `②执行状态(regs/PCState)` → `③MMU(TLB→Walker→PageTable→Stage2)` → `④系统与特权(GIC/timer/interrupts/faults/PMU/安全扩展)` → `⑤接口(KVM/FastModel/Semihosting/gdb)`。

**讲解强调**：①ISA 自动生成；②地址翻译是独立 Stage2 通道，支撑虚拟化；③特权级/安全扩展（PAC/MPAM/HTM）都在 `src/arch/arm` 里。

---

## 3. ARM O3 on gem5

> O3 是 gem5 的乱序超流水线 CPU 模型（`DerivO3CPU`，`src/cpu/o3/`），是微架构研究的主力。ARM 前端通过上面的 `src/arch/arm` 接入。

### 3.1 5 级流水线（gem5 的实际阶段）

```
取指 Fetch → 译码 Decode → 重命名 Rename → IEW(发射/执行/写回) → 提交 Commit
```

说明：Dispatch/Issue/Execute/Writeback 在 gem5 里由 `iew.cc` 统一处理，所以是 5 个阶段对象，而非教科书 8 级。

### 3.2 各阶段的支撑结构（对应 `src/cpu/o3/`）

| 流水级 | 支撑结构 |
|---|---|
| 取指 | 分支预测器 BTB/RAS（`src/cpu/pred/`）· FTQ（`ftq.cc`） |
| 译码 | ARM Decoder（指令 → 微操作）· 译码缓冲 |
| 重命名 | RenameMap（`rename_map.cc`）· FreeList 物理寄存器自由列表（`free_list.cc`）· 物理寄存器堆 RegFile（`regfile.cc`） |
| IEW | InstQueue 发射队列（`inst_queue.cc`）· LSQ 访存队列（`lsq.cc`）· FUPool 功能单元（`fu_pool.cc`）· Scoreboard/MemDepUnit（`scoreboard.cc`/`mem_dep_unit.cc`） |
| 提交 | ROB 重排序缓冲（`rob.cc`）· 分支恢复/异常提交（EL 切换、PC 回滚） |

### 3.3 乱序的核心逻辑（一句讲透）

**Rename 解依赖 + ROB 保顺序**。Rename 用映射表把逻辑寄存器改名到物理寄存器，消除 WAR/WAW 假依赖；IEW 里的 InstQueue 乱序发射；最后 Commit 靠 ROB 按程序顺序提交，实现精确状态（异常、分支预测失败靠它回滚）。

**对算子/系统方向的共鸣点**：LSQ + FUPool 里的访存/ALU 单元正是「算子怎么跑、内存依赖怎么预测」所在；改 `fu_pool.cc`（`FuncUnitConfig.py`）就能给 ARM 加自定义执行单元。

### 3.4 ARM 特色在 O3 里

- ARM 指令经 Decoder 转为**微操作**再进流水。
- 物理寄存器堆含**向量/浮点**寄存器（FP · NEON · SVE），不只是整数。
- 提交/异常处理负责**异常级 EL 切换**。
- 分支预测含 BTB + RAS。

### 3.5 [图] ARM O3 流水与架构

打开 `arm_o3_architecture.drawio`。上半为 5 级流水，下半为各阶段的支撑结构与缓冲，底部为 ARM 特色说明。

---

## 4. 现场演示（`talk_demo.py`，已实测通过）

> 环境：仓库 `build/ARM2/gem5.opt`（ARM ISA 构建），用本地 ARM hello，无需联网/交叉编译。

### 4.1 演示一：换 CPU 模型看性能变化

```bash
./build/ARM2/gem5.opt talk_demo.py run --cpu-type timing --cache l1l2
./build/ARM2/gem5.opt talk_demo.py run --cpu-type o3     --cache l1l2
```

实测结果（hello，ARM32）：

| CPU 模型 | 实测 IPC | 讲解点 |
|---|---|---|
| atomic | 0.86（无时序，仅供参考） | 功能模型：只求跑对，不建模时序 |
| timing | 0.082 | 顺序、时序精确（SimpleCPU） |
| minor | 0.082 | 顺序、带流水线 |
| **o3** | **0.147** | 乱序，ILP 更高 |

> 讲解：即使 hello 这种极小程序，乱序(O3) 的 IPC 也约为顺序(timing/minor) 的 **1.8 倍**，直观展示乱序提取 ILP 的能力。atomic 的 IPC 是「假」的，因它不建时序。

### 4.2 演示二：访存模式 vs DRAM 带宽（对算子方向最有冲击力）

```bash
./build/ARM2/gem5.opt talk_demo.py traffic --pattern linear   # 顺序
./build/ARM2/gem5.opt talk_demo.py traffic --pattern random   # 随机
```

实测：

| 访存模式 | 实测读带宽 |
|---|---|
| 顺序 linear | **17.08 GiB/s** |
| 随机 random | **15.01 GiB/s** |

> 讲解：**访存模式决定 DRAM 带宽**——顺序访存命中 row-buffer 局部性，随机访存频繁打散页，带宽下探。一句就能让算子同事抓到关键。

### 4.3 关键统计怎么看

脚本结束会自动打印摘要；完整统计在进程退出后写进 `m5out/stats.txt`：

```bash
grep -E 'simSeconds|simInsts|ipc|missRate|bw_total' m5out/stats.txt
less m5out/config.ini      # SimObject 参数树
```

---

## 5. 预判提问 / 互动点

- **Q：gem5 和 QEMU/真实硬件区别？** A：gem5 面向建模/可配置/可观测，微架构级精确；QEMU 只求功能正确、速度优先；真实硬件无法跑未投产设计。
- **Q：速度太慢怎么办？** A：用更粗的 CPU 模型（Atomic/Timing）、用 checkpoint、用 KVM 硬件加速、用 Ruby 网络模拟做网络部分。
- **Q：能加自定义算子/执行单元吗？** A：能。改 `fu_pool.cc` / `FuncUnitConfig.py` 加执行单元，改 `src/arch/arm/isa/` 加指令，用 `.isa` DSL 描述。
- **Q：SE 够真实吗？** A：SE 不做 OS 调度/系统调用计时，研究 CPU/缓存足够；要做 OS/设备交互需 FS。
- **Q：你们在做的寄存器追踪是什么？** A：`reg_writing_record` + `inst_reg_trace_viewer`——利用 gem5 的可观测性，精确到「每条指令写回了哪个寄存器的什么值」，用于调试、性能分析和依赖分析，这是真实硬件给不了的。

---

## 附录：素材清单

| 文件 | 内容 |
|---|---|
| `gem5_architecture.drawio` | gem5 总体架构（配置→内核→组件→输出） |
| `arm_architecture.drawio` | ARM 体系结构实现（src/arch/arm） |
| `arm_o3_architecture.drawio` | ARM O3 乱序流水与架构（src/cpu/o3） |
| `event_tick_sequence.mmd` | 事件驱动 + tick 推进时序图 |
| `talk_demo.py` | 可运行演示脚本（run / traffic 两个子命令，已实测） |
