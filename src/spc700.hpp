// SPC700 disassembler (Sony syntax: destination first).
#pragma once

#include <cstdint>
#include <string>

namespace spc700 {
struct Insn {
    uint16_t    addr;
    uint8_t     len;         // 1..3
    uint8_t     bytes[3];
    std::string text;        // e.g. "MOV $14, #$46"
    int         target = -1; // branch/call/jump destination when static, else -1
};

Insn disassemble(const uint8_t* ram, uint16_t addr);

}
