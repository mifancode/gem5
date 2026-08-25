/*
 * Copyright (c) 2026
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __CPU_REG_WRITING_RECORD_HH__
#define __CPU_REG_WRITING_RECORD_HH__

/**
 * RegWritingRecord: a self-contained, per-instruction recorder for
 * register values (sources and destinations).
 *
 * The class deliberately depends only on the C++ standard library so it
 * can be moved to other gem5-based projects (different ISA, different
 * gem5 vintage, non-CPU models such as NPUs) without dragging in the
 * gem5 cpu/ and trace/ machinery.  All gem5-specific context (tick, pc,
 * disassembly, thread id) is supplied by the host at dump() time, and
 * register names are resolved by the host before addEntry().
 *
 * Enable/disable and output location are controlled by the environment
 * variable REG_WRITING_RECORD: if set to a non-empty value, recording is
 * enabled and the value is the output file path (JSON Lines format,
 * one object per retired instruction).  If unset or empty, recording is
 * disabled and records are never allocated.
 */

#include <cstdint>
#include <string>
#include <vector>

namespace gem5
{

class RegWritingRecord
{
  public:
    struct Entry
    {
        std::string name;         ///< architectural register name
        size_t size;              ///< value size in bytes
        std::vector<uint8_t> data;///< raw register bits (little-endian copy)
        bool isDest;              ///< true: destination, false: source
    };

    /**
     * Append one register access.  \a name is resolved by the host
     * (e.g. from the instruction's operand table) so this class never
     * needs to know about gem5 instruction types.
     */
    void addEntry(const std::string &name, const void *val,
                  size_t size, bool isDest);

    /** Record a memory access (optional; mirrors setMem on InstRecord). */
    void setMem(uint64_t addr, uint64_t size);

    void setFetchSeq(uint64_t seq);
    void setFaulting(bool val);

    /**
     * Serialize the record as one JSON line and append it to the output
     * file.  All instruction context is passed in by the host so that
     * this class stays independent of gem5's instruction model.
     */
    void dump(uint64_t when, uint64_t pc, const std::string &inst,
              int thread);

    /** Whether recording is enabled (see header comment). */
    static bool enabled();
    /** Output file path (env var REG_WRITING_RECORD or default). */
    static const std::string &outputFile();

  private:
    std::vector<Entry> entries;

    bool mem_valid = false;
    uint64_t memAddr = 0;
    uint64_t memSize = 0;

    bool fetch_seq_valid = false;
    uint64_t fetch_seq = 0;

    bool faulting = false;

    static void writeLine(const std::string &line);
};

} // namespace gem5

#endif // __CPU_REG_WRITING_RECORD_HH__
