# Test config for the ExecContext-based RegWritingRecord mechanism.
#
# The record path only exists on the O3 CPU (DynInst), so this uses O3CPU.
# Recording is enabled by setting the environment variable
# REG_WRITING_RECORD=<output file> before running gem5, e.g.:
#
#   REG_WRITING_RECORD=/tmp/reg_writing_record.jsonl \
#       build/ARM2/gem5.opt test_reg_writing_record.py
#
# Output is one JSON line per retired instruction.

import m5
from m5.objects import *

system = System()
system.clk_domain = SrcClockDomain(clock="1GHz", voltage_domain=VoltageDomain())
system.mem_mode = "timing"
system.mem_ranges = [AddrRange("512MB")]

system.cpu = O3CPU()
system.cpu.createInterruptController()

system.membus = SystemXBar()

system.l1i = Cache(size="32kB", assoc=2, tag_latency=2, data_latency=2,
                   response_latency=2)
system.l1d = Cache(size="32kB", assoc=2, tag_latency=2, data_latency=2,
                   response_latency=2)
system.l2 = Cache(size="256kB", assoc=8, tag_latency=8, data_latency=8,
                  response_latency=8)

system.cpu.connectAllPorts(system.membus, system.l1i, system.l1d, system.l2)

system.memory = SimpleMemory()
system.memory.range = system.mem_ranges[0]
system.memory.port = system.membus.mem_side_ports
system.system_port = system.membus.cpu_side_ports

system.workload = SEWorkload.init_compatible("/tmp/hello_arm64")

process = Process()
process.cmd = ["/tmp/hello_arm64"]
system.cpu.workload = process
system.cpu.createThreads()

root = Root(full_system=False, system=system)

m5.instantiate()
exit_event = m5.simulate()
print(f"Exit at tick {m5.curTick()}, cause: {exit_event.getCause()}")
