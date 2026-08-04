#include "cpu/inst_reg_trace.hh"

#include <iomanip>
#include <sstream>

#include "arch/generic/pcstate.hh"
#include "base/cprintf.hh"
#include "base/loader/symtab.hh"
#include "cpu/thread_context.hh"

namespace gem5
{
namespace trace
{

static std::string
jsonEscape(const std::string &s)
{
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;
        }
    }
    return out;
}

static std::string
bytesToHex(const std::vector<uint8_t> &bytes)
{
    std::ostringstream oss;
    oss << "0x" << std::hex << std::setfill('0');
    for (uint8_t b : bytes)
        oss << std::setw(2) << (unsigned)b;
    return oss.str();
}

InstRegTraceRecord::InstRegTraceRecord(
    Tick when, ThreadContext *thread,
    const StaticInstPtr si, const PCStateBase &pc,
    InstRegTrace &t, const StaticInstPtr macroSi)
    : InstRecord(when, thread, si, pc, macroSi),
      tracer(t)
{}

void
InstRegTraceRecord::recordReg(int idx, const void *val,
                              size_t size, bool isDest)
{
    Entry e;
    e.idx = idx;
    e.size = size;
    e.data.assign((const uint8_t *)val, (const uint8_t *)val + size);
    e.isDest = isDest;
    entries.push_back(std::move(e));
}

void
InstRegTraceRecord::dump()
{
    std::ostringstream js;

    js << "{";

    js << "\"tick\":" << when;

    js << ",\"pc\":\"" << csprintf("%#x", pc->instAddr()) << "\"";

    std::string disasm = tracer.disassemble(staticInst, *pc,
                                            &loader::debugSymbolTable);
    js << ",\"inst\":\"" << jsonEscape(disasm) << "\"";

    if (fetch_seq_valid)
        js << ",\"fetch_seq\":" << fetch_seq;
    if (cp_seq_valid)
        js << ",\"cp_seq\":" << cp_seq;

    js << ",\"thread\":" << thread->threadId();

    js << ",\"src\":[";
    bool first = true;
    for (auto &e : entries) {
        if (e.isDest) continue;
        const RegId &reg = staticInst->srcRegIdx(e.idx);
        if (!first) js << ",";
        js << "{\"name\":\"" << reg.regClass().regName(reg) << "\""
           << ",\"size\":" << e.size
           << ",\"val\":\"" << bytesToHex(e.data) << "\"}";
        first = false;
    }
    js << "]";

    js << ",\"dst\":[";
    first = true;
    for (auto &e : entries) {
        if (!e.isDest) continue;
        const RegId &reg = staticInst->destRegIdx(e.idx);
        if (!first) js << ",";
        js << "{\"name\":\"" << reg.regClass().regName(reg) << "\""
           << ",\"size\":" << e.size
           << ",\"val\":\"" << bytesToHex(e.data) << "\"}";
        first = false;
    }
    js << "]";

    js << ",\"mem\":{";
    js << "\"addr\":";
    if (mem_valid)
        js << "\"" << csprintf("%#x", addr) << "\"";
    else
        js << "null";
    js << ",\"size\":" << size;
    js << "}";

    js << ",\"fault\":" << (faulting ? "true" : "false");
    js << ",\"predicated\":" << (predicate ? "false" : "true");

    js << "}";

    tracer.writeLine(js.str());
}

InstRegTrace::InstRegTrace(const Params &p)
    : InstTracer(p),
      enabled(p.enabled)
{
    if (enabled) {
        traceFile.open(p.trace_file);
        if (!traceFile.is_open()) {
            warn("InstRegTrace: cannot open trace file %s",
                 p.trace_file.c_str());
            enabled = false;
        }
    }
}

InstRegTrace::~InstRegTrace()
{
    if (traceFile.is_open())
        traceFile.close();
}

InstRecord *
InstRegTrace::getInstRecord(Tick when, ThreadContext *tc,
    const StaticInstPtr si, const PCStateBase &pc,
    const StaticInstPtr macroSi)
{
    if (!enabled)
        return nullptr;
    return new InstRegTraceRecord(when, tc, si, pc, *this, macroSi);
}

void
InstRegTrace::writeLine(const std::string &line)
{
    std::lock_guard<std::mutex> lock(fileMutex);
    if (traceFile.is_open())
        traceFile << line << "\n";
}

} // namespace trace
} // namespace gem5
