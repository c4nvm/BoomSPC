#include "spc700.hpp"

#include <cstdio>

namespace spc700 {
namespace {
struct Entry { const char* fmt; uint8_t len; };
const Entry kTable[256] = {
#include "spc700_table.inc"
};

}

Insn disassemble(const uint8_t* ram, uint16_t addr) {
    Insn in;
    in.addr = addr;
    const uint8_t op = ram[addr];
    const Entry& e = kTable[op];
    in.len = e.len;
    for (int i = 0; i < 3; ++i) in.bytes[i] = ram[uint16_t(addr + i)];
    const uint8_t b1 = in.bytes[1], b2 = in.bytes[2];
    const uint16_t next = uint16_t(addr + e.len);

    std::string out;
    for (const char* p = e.fmt; *p; ++p) {
        if (*p != '{') { out += *p; continue; }
        std::string key;
        for (++p; *p && *p != '}'; ++p) key += *p;
        char buf[24];
        if (key == "i1")       std::snprintf(buf, sizeof buf, "$%02X", b1);
        else if (key == "d1")  std::snprintf(buf, sizeof buf, "$%02X", b1);
        else if (key == "d2")  std::snprintf(buf, sizeof buf, "$%02X", b2);
        else if (key == "a12") { int a = b1 | (b2 << 8); std::snprintf(buf, sizeof buf, "!$%04X", a); if (op == 0x3F || op == 0x5F) in.target = a; }
        else if (key == "r1")  { int t = uint16_t(next + int8_t(b1)); std::snprintf(buf, sizeof buf, "$%04X", t); in.target = t; }
        else if (key == "r2")  { int t = uint16_t(next + int8_t(b2)); std::snprintf(buf, sizeof buf, "$%04X", t); in.target = t; }
        else if (key == "m12") { int m = b1 | (b2 << 8); std::snprintf(buf, sizeof buf, "$%04X.%d", m & 0x1FFF, m >> 13); }
        else if (key == "n")   std::snprintf(buf, sizeof buf, "%d", op >> 4);
        else if (key == "b")   std::snprintf(buf, sizeof buf, "%d", op >> 5);
        else                   std::snprintf(buf, sizeof buf, "?");
        out += buf;
    }
    if (op == 0x4F) in.target = 0xFF00 | b1;
    if ((op & 0x0F) == 0x01) { int v = 0xFFDE - 2 * (op >> 4); in.target = ram[v] | (ram[v + 1] << 8); }
    in.text = out;
    return in;
}

}
