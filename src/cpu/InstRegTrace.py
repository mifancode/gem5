from m5.objects.InstTracer import InstTracer
from m5.params import *
from m5.SimObject import SimObject

class InstRegTrace(InstTracer):
    type = "InstRegTrace"
    cxx_class = "gem5::trace::InstRegTrace"
    cxx_header = "cpu/inst_reg_trace.hh"

    trace_file = Param.String("inst_reg_trace.jsonl", "Output trace file")
    enabled = Param.Bool(True, "Enable instruction register tracing")
