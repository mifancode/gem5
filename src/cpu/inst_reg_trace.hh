#ifndef __CPU_INST_REG_TRACE_HH__
#define __CPU_INST_REG_TRACE_HH__

#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#include "cpu/reg_class.hh"
#include "cpu/static_inst.hh"
#include "params/InstRegTrace.hh"
#include "sim/insttracer.hh"

namespace gem5
{

class ThreadContext;

namespace trace
{

class InstRegTrace;

class InstRegTraceRecord : public InstRecord
{
  public:
    struct Entry
    {
        int idx;
        size_t size;
        std::vector<uint8_t> data;
        bool isDest;
    };

    InstRegTraceRecord(Tick when, ThreadContext *thread,
                       const StaticInstPtr si, const PCStateBase &pc,
                       InstRegTrace &t,
                       const StaticInstPtr macroSi = nullptr);

    void recordReg(int idx, const void *val,
                   size_t size, bool isDest) override;

    void dump() override;

  protected:
    InstRegTrace &tracer;
    std::vector<Entry> entries;
};

class InstRegTrace : public InstTracer
{
  public:
    using Params = InstRegTraceParams;

    InstRegTrace(const Params &p);
    ~InstRegTrace();

    InstRecord *getInstRecord(Tick when, ThreadContext *tc,
        const StaticInstPtr si, const PCStateBase &pc,
        const StaticInstPtr macroSi = nullptr) override;

    void writeLine(const std::string &line);

  private:
    std::mutex fileMutex;
    std::ofstream traceFile;
    bool enabled;
};

} // namespace trace
} // namespace gem5

#endif // __CPU_INST_REG_TRACE_HH__
