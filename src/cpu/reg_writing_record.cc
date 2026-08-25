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

#include "cpu/reg_writing_record.hh"

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace gem5
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

static std::string
addrToHex(uint64_t v)
{
    std::ostringstream oss;
    oss << "0x" << std::hex << v;
    return oss.str();
}

bool
RegWritingRecord::enabled()
{
    static const bool on = []() {
        const char *e = std::getenv("REG_WRITING_RECORD");
        return e != nullptr && *e != '\0';
    }();
    return on;
}

const std::string &
RegWritingRecord::outputFile()
{
    static const std::string path = []() {
        const char *e = std::getenv("REG_WRITING_RECORD");
        return e ? std::string(e) : std::string("reg_writing_record.jsonl");
    }();
    return path;
}

void
RegWritingRecord::writeLine(const std::string &line)
{
    static std::mutex fileMutex;
    static std::ofstream traceFile;
    static bool opened = false;

    std::lock_guard<std::mutex> lock(fileMutex);
    if (!opened) {
        traceFile.open(outputFile());
        opened = true;
    }
    if (traceFile.is_open())
        traceFile << line << "\n";
}

void
RegWritingRecord::addEntry(const std::string &name, const void *val,
                           size_t size, bool isDest)
{
    Entry e;
    e.name = name;
    e.size = size;
    e.data.assign((const uint8_t *)val, (const uint8_t *)val + size);
    e.isDest = isDest;
    entries.push_back(std::move(e));
}

void
RegWritingRecord::setMem(uint64_t addr, uint64_t size)
{
    mem_valid = true;
    memAddr = addr;
    memSize = size;
}

void
RegWritingRecord::setFetchSeq(uint64_t seq)
{
    fetch_seq = seq;
    fetch_seq_valid = true;
}

void
RegWritingRecord::setFaulting(bool val)
{
    faulting = val;
}

void
RegWritingRecord::dump(uint64_t when, uint64_t pc, const std::string &inst,
                       int thread)
{
    std::ostringstream js;

    js << "{";

    js << "\"tick\":" << when;

    js << ",\"pc\":\"" << addrToHex(pc) << "\"";

    js << ",\"inst\":\"" << jsonEscape(inst) << "\"";

    if (fetch_seq_valid)
        js << ",\"fetch_seq\":" << fetch_seq;

    js << ",\"thread\":" << thread;

    js << ",\"src\":[";
    bool first = true;
    for (auto &e : entries) {
        if (e.isDest) continue;
        if (!first) js << ",";
        js << "{\"name\":\"" << e.name << "\""
           << ",\"size\":" << e.size
           << ",\"val\":\"" << bytesToHex(e.data) << "\"}";
        first = false;
    }
    js << "]";

    js << ",\"dst\":[";
    first = true;
    for (auto &e : entries) {
        if (!e.isDest) continue;
        if (!first) js << ",";
        js << "{\"name\":\"" << e.name << "\""
           << ",\"size\":" << e.size
           << ",\"val\":\"" << bytesToHex(e.data) << "\"}";
        first = false;
    }
    js << "]";

    js << ",\"mem\":{";
    js << "\"addr\":";
    if (mem_valid)
        js << "\"" << addrToHex(memAddr) << "\"";
    else
        js << "null";
    js << ",\"size\":" << memSize;
    js << "}";

    js << ",\"fault\":" << (faulting ? "true" : "false");

    js << "}";

    writeLine(js.str());
}

} // namespace gem5
