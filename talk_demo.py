#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gem5 讲解演示脚本 —— 面向系统 / 算子方向同事（对微架构也有了解）。

一句话定位 gem5:
    一个「模块化 + 事件驱动」的计算机系统模拟器：CPU、缓存、内存、设备都是
    可替换的模块（SimObject），用一个全局事件队列按 tick 精确推进仿真。

这个脚本演示两条主线，对应讲解时最重要的两个概念：

  1. run     —— 配置 -> 实例化 -> 仿真 -> 统计 的完整闭环（SE 模式，无 OS）。
               展示「换 CPU 模型 / 换缓存层次」如何直接改变性能统计。
  2. traffic —— 内存流量发生器，直接压内存系统（访存模式 + 带宽）。
               对「系统和算子」方向最直观：顺序 vs 随机访存的带宽差异。

用法（在仓库根目录执行，二进制为 build/ARM2/gem5.opt）:

  # ① 最简 SE 仿真：AtomicCPU，无缓存，跑本地 ARM hello
  ./build/ARM2/gem5.opt talk_demo.py run --cpu-type atomic --cache nocache

  # ② 换成乱序 CPU + L1/L2 缓存，对比 IPC / 运行时间
  ./build/ARM2/gem5.opt talk_demo.py run --cpu-type o3 --cache l1l2

  # ③ CPU 模型横向对比（atomic / timing / minor / o3）
  for cpu in atomic timing minor o3; do
      ./build/ARM2/gem5.opt talk_demo.py run --cpu-type $cpu --cache l1l2
  done

  # ④ 内存系统演示：顺序 vs 随机访存，看 DRAM 带宽
  ./build/ARM2/gem5.opt talk_demo.py traffic --pattern linear
  ./build/ARM2/gem5.opt talk_demo.py traffic --pattern random

运行后，讲解时对应去看:
  m5out/stats.txt    全量统计（进程退出后写入，见脚本结尾的 grep 提示）
  m5out/config.ini   实例化后的系统参数树（SimObject 树，逐行可读）
  m5out/config.json  机器可读的配置
"""

import argparse
import sys

# gem5 以非交互方式退出时可能跳过 Python stdout 缓冲区的 flush，
# 导致脚本里的 print 丢失。这里强制行缓冲，保证讲解时输出可见。
try:
    sys.stdout.reconfigure(line_buffering=True)
except Exception:
    pass

from gem5.components.boards.simple_board import SimpleBoard
from gem5.components.boards.test_board import TestBoard
from gem5.components.cachehierarchies.classic.no_cache import NoCache
from gem5.components.cachehierarchies.classic.private_l1_cache_hierarchy import (
    PrivateL1CacheHierarchy,
)
from gem5.components.cachehierarchies.classic.private_l1_private_l2_cache_hierarchy import (
    PrivateL1PrivateL2CacheHierarchy,
)
from gem5.components.memory import SingleChannelDDR3_1600, SingleChannelDDR4_2400
from gem5.components.memory.hbm import HighBandwidthMemory
from gem5.components.memory.dram_interfaces.hbm import HBM_2000_4H_1x64
from gem5.components.processors.cpu_types import CPUTypes
from gem5.components.processors.simple_processor import SimpleProcessor
from gem5.components.processors.linear_generator import LinearGenerator
from gem5.components.processors.random_generator import RandomGenerator
from gem5.isas import ISA
from gem5.resources.resource import BinaryResource
from gem5.simulate.simulator import Simulator
from gem5.utils.requires import requires

# 本地 ARM hello 二进制（仓库自带，无需下载 / 交叉编译）
DEFAULT_BINARY = "tests/test-progs/hello/bin/arm/linux/hello"

CPU_MAP = {
    "atomic": CPUTypes.ATOMIC,   # 近似、快：不建模时序，只求功能正确
    "timing": CPUTypes.TIMING,   # 顺序、时序精确但不乱序（SimpleCPU）
    "minor": CPUTypes.MINOR,     # 顺序、时序精确、带流水线（MinorCPU）
    "o3": CPUTypes.O3,           # 乱序、时序精确（O3CPU，微架构研究主力）
}


def build_cache_hierarchy(kind: str):
    if kind == "nocache":
        return NoCache()
    if kind == "l1":
        return PrivateL1CacheHierarchy(l1d_size="64KiB", l1i_size="64KiB")
    if kind == "l1l2":
        return PrivateL1PrivateL2CacheHierarchy(
            l1d_size="64KiB", l1i_size="64KiB", l2_size="512KiB"
        )
    raise ValueError(f"未知缓存层次: {kind}")


def build_memory(kind: str):
    # 8GiB 与 DDR3_1600_8x8 / DDR4_2400_8x8 的 DIMM 容量一致，避免告警。
    if kind == "ddr3":
        return SingleChannelDDR3_1600(size="8GiB")
    if kind == "ddr4":
        return SingleChannelDDR4_2400(size="8GiB")
    raise ValueError(f"未知内存: {kind}")


def get_stat(simstat, dotted: str, default="n/a"):
    """
    按点分路径安全地取出一个统计量。

    说明：gem5 的 SimStat 树里，向量用「名字+下标」的约定访问
    （例如 `cores0` 表示 `cores[0]`）。本函数对缺失/异常一律返回 default，
    保证在 AtomicCPU（无时序统计）等场景下也不会崩。
    """
    node = simstat
    try:
        for part in dotted.split("."):
            node = node[part]
        return node.value if hasattr(node, "value") else node
    except Exception:
        return default


def print_run_summary(simulator, cpu_type="timing"):
    """从内存里的 SimStat 树即时读取几个关键统计（无需等 stats.txt 落盘）。"""
    print("\n===== 仿真统计（即时读取） =====")
    s = simulator.get_simstats()

    sim_ticks = get_stat(s, "simTicks")
    sim_freq = get_stat(s, "simFreq")
    sim_insts = get_stat(s, "simInsts")
    sim_ops = get_stat(s, "simOps")
    host_secs = get_stat(s, "hostSeconds")
    num_cycles = get_stat(s, "board.processor.cores0.core.numCycles")

    print(f"  simTicks   = {sim_ticks}    # 仿真推进了多少个 tick")
    print(f"  simInsts   = {sim_insts}    # 提交的指令数")
    print(f"  simOps     = {sim_ops}    # 提交的微操作数")
    print(f"  hostSeconds= {host_secs}    # 主机实际耗时(秒)")

    try:
        print(f"  simSeconds = {sim_ticks / sim_freq:.9f}  # 模拟时间(秒)")
    except Exception:
        pass

    try:
        ipc = sim_insts / num_cycles
        print(f"  numCycles  = {num_cycles}")
        print(f"  IPC        = {ipc:.4f}   # 指令/周期")
    except Exception:
        print("  numCycles / IPC = n/a")

    if cpu_type == "atomic":
        print("  (注: AtomicCPU 是功能模型，不做精确时序，上面的 cycle/IPC 仅供参考)")

    print("\n完整统计在 m5out/stats.txt（进程退出后写入），运行结束后可看:")
    print("  grep -E 'simSeconds|simInsts|ipc|missRate|bw_total' m5out/stats.txt")


def print_traffic_summary(simulator):
    print("\n===== 内存系统统计（即时读取） =====")
    s = simulator.get_simstats()
    sim_ticks = get_stat(s, "simTicks")
    sim_freq = get_stat(s, "simFreq")
    host_secs = get_stat(s, "hostSeconds")
    bytes_read = get_stat(s, "board.memory.mem_ctrl0.bytesReadSys")
    bytes_written = get_stat(s, "board.memory.mem_ctrl0.bytesWrittenSys")

    print(f"  simTicks     = {sim_ticks}")
    print(f"  hostSeconds  = {host_secs}")
    print(f"  bytesReadSys = {bytes_read}    # 系统侧读总字节")
    print(f"  bytesWrittenSys = {bytes_written}    # 系统侧写总字节")
    # 平均带宽 = 字节数 / 模拟时长（avgRdBWSys 为派生统计，树里没有，直接算）
    try:
        sim_secs = sim_ticks / sim_freq
        print(f"  平均读带宽 = {bytes_read / sim_secs / (1024**3):.2f} GiB/s")
        print(f"  平均写带宽 = {bytes_written / sim_secs / (1024**3):.2f} GiB/s")
    except Exception:
        pass

    print("\n完整统计在 m5out/stats.txt，运行结束后可看:")
    print("  grep -E 'avgRdBWSys|avgWrBWSys|bytesReadSys|bytesWrittenSys|avgRdBW|avgWrBW' m5out/stats.txt")


def run_workload(args):
    """主线 1：SE 模式跑一个真实二进制，展示 配置 -> 仿真 -> 统计。"""
    requires(isa_required=ISA.ARM)

    cpu_type = CPU_MAP[args.cpu_type]
    cache_hierarchy = build_cache_hierarchy(args.cache)
    memory = build_memory(args.memory)

    print(f"[配置] ISA=ARM  CPU={args.cpu_type}  cache={args.cache}  "
          f"mem={args.memory}  binary={args.binary}")

    processor = SimpleProcessor(cpu_type=cpu_type, isa=ISA.ARM, num_cores=1)

    board = SimpleBoard(
        clk_freq="3GHz",
        processor=processor,
        memory=memory,
        cache_hierarchy=cache_hierarchy,
    )
    board.set_se_binary_workload(
        binary=BinaryResource(local_path=args.binary)
    )

    simulator = Simulator(board=board)
    print("[仿真] 开始推进事件队列 ...")
    simulator.run()
    print("[仿真] 完成。")

    print_run_summary(simulator, cpu_type=args.cpu_type)


def run_traffic(args):
    """主线 2：内存流量发生器，直接观察访存模式对内存系统的影响。"""
    memory = HighBandwidthMemory(HBM_2000_4H_1x64, 1, 128)

    rate = "32GiB/s"
    if args.pattern == "linear":
        generator = LinearGenerator(
            duration="1ms", rate=rate, max_addr=memory.get_size(),
            rd_perc=args.rd_perc,
        )
    else:
        generator = RandomGenerator(
            duration="1ms", rate=rate, max_addr=memory.get_size(),
            rd_perc=args.rd_perc,
        )

    # cache_hierarchy=None 时，发生器直连内存；也可换成 cache 层次做对比。
    cache_hierarchy = None
    if args.cache != "none":
        cache_hierarchy = build_cache_hierarchy(args.cache)

    print(f"[配置] 访存模式={args.pattern}  读占比={args.rd_perc}%  "
          f"速率={rate}  cache={args.cache}")

    board = TestBoard(
        clk_freq="1GHz",
        generator=generator,
        memory=memory,
        cache_hierarchy=cache_hierarchy,
    )

    simulator = Simulator(board=board)
    print("[仿真] 开始压内存 ...")
    simulator.run()
    print("[仿真] 完成。")

    print_traffic_summary(simulator)


def main():
    parser = argparse.ArgumentParser(
        description="gem5 讲解演示脚本（系统/算子方向）"
    )
    sub = parser.add_subparsers(dest="command", required=True)

    p_run = sub.add_parser("run", help="SE 模式跑二进制（CPU/缓存可切换）")
    p_run.add_argument("--cpu-type", default="timing",
                       choices=list(CPU_MAP), help="CPU 模型")
    p_run.add_argument("--cache", default="l1",
                       choices=["nocache", "l1", "l1l2"], help="缓存层次")
    p_run.add_argument("--memory", default="ddr3",
                       choices=["ddr3", "ddr4"], help="内存类型")
    p_run.add_argument("--binary", default=DEFAULT_BINARY, help="二进制路径")
    p_run.set_defaults(func=run_workload)

    p_traffic = sub.add_parser("traffic", help="内存流量发生器")
    p_traffic.add_argument("--pattern", default="linear",
                           choices=["linear", "random"], help="访存模式")
    p_traffic.add_argument("--rd-perc", type=int, default=70,
                           help="读请求占比(0-100)")
    p_traffic.add_argument("--cache", default="none",
                           choices=["none", "nocache", "l1", "l1l2"],
                           help="缓存层次(none=直连内存)")
    p_traffic.set_defaults(func=run_traffic)

    args = parser.parse_args()
    args.func(args)


# 注意：gem5 通过 `exec` 运行脚本时会把 __name__ 设为 "__m5_main__"
# （而不是 "__main__"），因此这里直接调用 main()，与 gem5 官方 configs 一致。
main()
