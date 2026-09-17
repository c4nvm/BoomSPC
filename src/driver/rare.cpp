#include "rare.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>
#include <vector>

using seq::Event;
using seq::EventType;

namespace rare {
namespace {
inline uint16_t rd16(const uint8_t* ram, uint32_t a) { return uint16_t(ram[a & 0xFFFF] | (ram[(a + 1) & 0xFFFF] << 8)); }

int find_pattern(const uint8_t* ram, int lo, int hi, const int* pat, int n) {
    for (int a = lo; a + n <= hi; ++a) {
        bool ok = true;
        for (int i = 0; i < n && ok; ++i) ok = pat[i] == 0x100 || ram[a + i] == pat[i];
        if (ok) return a;
    }
    return -1;
}

using seq::FxClass;
const CmdSpec kCmdsDkc1[0x31] = {
    {1, "End", "End of voice", FxClass::Song},                                  // 00
    {2, "Ins", "Instrument", FxClass::Instrument},                              // 01
    {3, "Vol", "Volume left, right", FxClass::Volume},                          // 02
    {3, "Jmp", "Jump", FxClass::Song},                                          // 03
    {4, "Cal", "Call subroutine n times", FxClass::Song},                       // 04
    {1, "Ret", "Return", FxClass::Song},                                        // 05
    {2, "Dur", "Fixed duration", FxClass::Time},                                // 06
    {1, "DrX", "Explicit durations", FxClass::Time},                            // 07
    {6, "Env", "Pitch envelope / slide (5 bytes)", FxClass::Pitch},             // 08
    {6, "EnD", "Pitch envelope / slide down (5 bytes)", FxClass::Pitch},        // 09
    {1, "EnO", "Pitch envelope off", FxClass::Pitch},                           // 0A
    {2, "Tmp", "Tempo", FxClass::Speed},                                        // 0B
    {2, "Tm+", "Tempo add", FxClass::Speed},                                    // 0C
    {4, "Trm", "Tremolo (3 bytes)", FxClass::Volume},                           // 0D
    {1, "TrO", "Tremolo off", FxClass::Volume},                                 // 0E
    {5, "Tr4", "Tremolo (4 bytes)", FxClass::Volume},                           // 0F
    {3, "ADS", "ADSR", FxClass::Volume},                                        // 10
    {3, "MVl", "Main volume left, right", FxClass::Volume},                     // 11
    {2, "Det", "Detune", FxClass::Pitch},                                       // 12
    {2, "Trn", "Transpose", FxClass::Pitch},                                    // 13
    {2, "Tr+", "Transpose add", FxClass::Pitch},                                // 14
    {4, "Ech", "Echo feedback, volume left, right", FxClass::Sys1},             // 15
    {1, "EcO", "Echo on", FxClass::Sys1},                                       // 16
    {1, "EcF", "Echo off", FxClass::Sys1},                                      // 17
    {9, "FIR", "Echo FIR filter", FxClass::Sys1},                               // 18
    {2, "Noi", "Noise clock", FxClass::Sys1},                                   // 19
    {1, "PMO", "Pitch modulation on", FxClass::Sys1},                           // 1A
    {1, "PMF", "Pitch modulation off", FxClass::Sys1},                          // 1B
    {5, "Pr0", "Define preset 0 (vol L, R, ADSR)", FxClass::Volume},            // 1C
    {5, "Pr1", "Define preset 1", FxClass::Volume},                             // 1D
    {5, "Pr2", "Define preset 2", FxClass::Volume},                             // 1E
    {5, "Pr3", "Define preset 3", FxClass::Volume},                             // 1F
    {5, "Pr4", "Define preset 4", FxClass::Volume},                             // 20
    {1, "Us0", "Use preset 0", FxClass::Volume},                                // 21
    {1, "Us1", "Use preset 1", FxClass::Volume},                                // 22
    {1, "Us2", "Use preset 2", FxClass::Volume},                                // 23
    {1, "Us3", "Use preset 3", FxClass::Volume},                                // 24
    {1, "Us4", "Use preset 4", FxClass::Volume},                                // 25
    {5, "Vib", "Vibrato (4 bytes)", FxClass::Pitch},                            // 26
    {5, "VbD", "Vibrato, inverted (4 bytes)", FxClass::Pitch},                  // 27
    {4, "InV", "Instrument, volume left, right", FxClass::Instrument},          // 28
    {2, "Fad", "Master volume fade rate", FxClass::Volume},                     // 29
    {2, "Tim", "Timer period", FxClass::Speed},                                 // 2A
    {1, "D16", "Two-byte durations", FxClass::Time},                            // 2B
    {1, "D8", "One-byte durations", FxClass::Time},                             // 2C
    {3, "JmV", "Jump through table by variant", FxClass::Song},                 // 2D
    {2, "Var", "Set variant", FxClass::Song},                                   // 2E
    {5, "PnS", "Pan sweep (4 bytes)", FxClass::Panning},                        // 2F
    {1, "PnO", "Pan sweep off", FxClass::Panning},                              // 30
};

const CmdSpec kCmdsDkc2[0x33] = {
    {1, "End", "End of voice", FxClass::Song},                                  // 00
    {2, "Ins", "Instrument", FxClass::Instrument},                              // 01
    {3, "Vol", "Volume left, right", FxClass::Volume},                          // 02
    {3, "Jmp", "Jump", FxClass::Song},                                          // 03
    {4, "Cal", "Call subroutine n times", FxClass::Song},                       // 04
    {1, "Ret", "Return", FxClass::Song},                                        // 05
    {2, "Dur", "Fixed duration", FxClass::Time},                                // 06
    {1, "DrX", "Explicit durations", FxClass::Time},                            // 07
    {6, "Env", "Pitch envelope / slide (5 bytes)", FxClass::Pitch},             // 08
    {6, "EnD", "Pitch envelope / slide down (5 bytes)", FxClass::Pitch},        // 09
    {1, "EnO", "Pitch envelope off", FxClass::Pitch},                           // 0A
    {2, "Tmp", "Tempo", FxClass::Speed},                                        // 0B
    {2, "Tm+", "Tempo add", FxClass::Speed},                                    // 0C
    {4, "Trm", "Tremolo (3 bytes)", FxClass::Volume},                           // 0D
    {1, "TrO", "Tremolo off", FxClass::Volume},                                 // 0E
    {5, "Tr4", "Tremolo (4 bytes)", FxClass::Volume},                           // 0F
    {3, "ADS", "ADSR", FxClass::Volume},                                        // 10
    {0, "???", "(unused)", FxClass::Invalid},                                   // 11
    {2, "Det", "Detune", FxClass::Pitch},                                       // 12
    {2, "Trn", "Transpose", FxClass::Pitch},                                    // 13
    {2, "Tr+", "Transpose add", FxClass::Pitch},                                // 14
    {4, "Ech", "Echo feedback, volume left, right", FxClass::Sys1},             // 15
    {1, "EcO", "Echo on", FxClass::Sys1},                                       // 16
    {1, "EcF", "Echo off", FxClass::Sys1},                                      // 17
    {9, "FIR", "Echo FIR filter", FxClass::Sys1},                               // 18
    {2, "Noi", "Noise clock", FxClass::Sys1},                                   // 19
    {1, "PMO", "Pitch modulation on", FxClass::Sys1},                           // 1A
    {1, "PMF", "Pitch modulation off", FxClass::Sys1},                          // 1B
    {2, "NvA", "Note variable A (played by E0)", FxClass::Pitch},               // 1C
    {2, "NvB", "Note variable B (played by E1)", FxClass::Pitch},               // 1D
    {5, "GVl", "Global volume pairs 0 and 1", FxClass::Volume},                 // 1E
    {2, "EDl", "Echo delay (clears the buffer)", FxClass::Sys1},                // 1F
    {1, "GV0", "Use global volume pair 0", FxClass::Volume},                    // 20
    {3, "Cl1", "Call subroutine once", FxClass::Song},                          // 21
    {8, "InF", "Instrument, transpose, detune, volume, ADSR", FxClass::Instrument}, // 22
    {2, "VlM", "Volume (mono)", FxClass::Volume},                               // 23
    {2, "MVS", "Master volume scale (%)", FxClass::Volume},                     // 24
    {0, "???", "(unused)", FxClass::Invalid},                                   // 25
    {5, "VbD", "Vibrato, inverted (4 bytes)", FxClass::Pitch},                  // 26
    {5, "Vib", "Vibrato (4 bytes)", FxClass::Pitch},                            // 27
    {0, "???", "(unused)", FxClass::Invalid},                                   // 28
    {0, "???", "(unused)", FxClass::Invalid},                                   // 29
    {0, "???", "(unused)", FxClass::Invalid},                                   // 2A
    {1, "D16", "Two-byte durations", FxClass::Time},                            // 2B
    {1, "D8", "One-byte durations", FxClass::Time},                             // 2C
    {0, "???", "(unused)", FxClass::Invalid},                                   // 2D
    {0, "???", "(unused)", FxClass::Invalid},                                   // 2E
    {0, "???", "(unused)", FxClass::Invalid},                                   // 2F
    {1, "EcF", "Echo off", FxClass::Sys1},                                      // 30
    {1, "GV1", "Use global volume pair 1", FxClass::Volume},                    // 31
    {1, "EcF", "Echo off", FxClass::Sys1},                                      // 32
};

const CmdSpec kCmdsKi[0x31] = {
    {1, "End", "End of voice", FxClass::Song},                                  // 00
    {2, "Ins", "Instrument", FxClass::Instrument},                              // 01
    {3, "Vol", "Volume left, right", FxClass::Volume},                          // 02
    {3, "Jmp", "Jump", FxClass::Song},                                          // 03
    {4, "Cal", "Call subroutine n times", FxClass::Song},                       // 04
    {1, "Ret", "Return", FxClass::Song},                                        // 05
    {2, "Dur", "Fixed duration", FxClass::Time},                                // 06
    {1, "DrX", "Explicit durations", FxClass::Time},                            // 07
    {6, "Env", "Pitch envelope / slide (5 bytes)", FxClass::Pitch},             // 08
    {6, "EnD", "Pitch envelope / slide down (5 bytes)", FxClass::Pitch},        // 09
    {1, "EnO", "Pitch envelope off", FxClass::Pitch},                           // 0A
    {2, "Tmp", "Tempo", FxClass::Speed},                                        // 0B
    {0, "???", "(unused)", FxClass::Invalid},                                   // 0C
    {0, "???", "(unused)", FxClass::Invalid},                                   // 0D
    {1, "TrO", "Tremolo off", FxClass::Volume},                                 // 0E
    {5, "Tr4", "Tremolo (4 bytes)", FxClass::Volume},                           // 0F
    {3, "ADS", "ADSR", FxClass::Volume},                                        // 10
    {0, "???", "(unused)", FxClass::Invalid},                                   // 11
    {2, "Det", "Detune", FxClass::Pitch},                                       // 12
    {2, "Trn", "Transpose", FxClass::Pitch},                                    // 13
    {2, "Tr+", "Transpose add", FxClass::Pitch},                                // 14
    {0, "???", "(unused)", FxClass::Invalid},                                   // 15
    {1, "EcO", "Echo on", FxClass::Sys1},                                       // 16
    {1, "EcF", "Echo off", FxClass::Sys1},                                      // 17
    {0, "???", "(unused)", FxClass::Invalid},                                   // 18
    {0, "???", "(unused)", FxClass::Invalid},                                   // 19
    {0, "???", "(unused)", FxClass::Invalid},                                   // 1A
    {0, "???", "(unused)", FxClass::Invalid},                                   // 1B
    {0, "???", "(unused)", FxClass::Invalid},                                   // 1C
    {0, "???", "(unused)", FxClass::Invalid},                                   // 1D
    {2, "VlM", "Volume (mono)", FxClass::Volume},                               // 1E
    {3, "Cl1", "Call subroutine once", FxClass::Song},                          // 1F
    {1, "AdA", "ADSR preset 8F E0", FxClass::Volume},                           // 20
    {1, "AdB", "ADSR preset 8E E0", FxClass::Volume},                           // 21
    {4, "InT", "Instrument, transpose, detune", FxClass::Instrument},           // 22
    {2, "EDl", "Echo delay (clears the buffer)", FxClass::Sys1},                // 23
    {0, "???", "(unused)", FxClass::Invalid},                                   // 24
    {0, "???", "(unused)", FxClass::Invalid},                                   // 25
    {5, "VbD", "Vibrato, inverted (4 bytes)", FxClass::Pitch},                  // 26
    {5, "Vib", "Vibrato (4 bytes)", FxClass::Pitch},                            // 27
    {0, "???", "(unused)", FxClass::Invalid},                                   // 28
    {0, "???", "(unused)", FxClass::Invalid},                                   // 29
    {0, "???", "(unused)", FxClass::Invalid},                                   // 2A
    {1, "D16", "Two-byte durations", FxClass::Time},                            // 2B
    {1, "D8", "One-byte durations", FxClass::Time},                             // 2C
    {0, "???", "(unused)", FxClass::Invalid},                                   // 2D
    {0, "???", "(unused)", FxClass::Invalid},                                   // 2E
    {0, "???", "(unused)", FxClass::Invalid},                                   // 2F
    {0, "???", "(unused)", FxClass::Invalid},                                   // 30
};

Event make_event(EventType t, uint16_t addr, const uint8_t* ram, int size, int tick) {
    Event e{};
    e.type = t;
    e.addr = addr;
    e.size = uint8_t(std::min(size, 16));
    for (int i = 0; i < e.size; ++i) e.b[i] = ram[(addr + i) & 0xFFFF];
    e.tick = tick;
    return e;
}

}

Layout detect_layout(const uint8_t* ram) {
    Layout L;
    const int W = 0x100;
    const int disp1[] = {0xF7, 0x01, 0x68, 0x00, 0x30, W, 0x4D, 0x1C, 0x5D, 0x1F, W, W};
    const int disp2[] = {0xF7, 0x00, 0x30, W, 0x4D, 0x1C, 0x5D, 0x1F, W, W};
    if (int d = find_pattern(ram, 0x200, 0x2000, disp1, 12); d >= 0) {
        L.variant = Variant::DKC1; L.cmd_table = rd16(ram, d + 10);
        L.cmds = kCmdsDkc1; L.cmd_count = 0x31;
    } else if (int d2 = find_pattern(ram, 0x200, 0x2000, disp2, 10); d2 >= 0) {
        L.variant = Variant::DKC2; L.cmd_table = rd16(ram, d2 + 8);
        L.cmds = kCmdsDkc2; L.cmd_count = 0x33; L.call_once = 0x21;
        for (int op = 0; op < 0x33; ++op) {
            int h = rd16(ram, L.cmd_table + op * 2);
            if (h < 0x200 || h >= 0x2000) continue;
            if (ram[h] == 0x3F && ram[h + 3] == 0x8F && ram[h + 4] == 0x01 && ram[h + 5] == 0x04 && ram[h + 6] == 0x3F) { L.call_once = uint8_t(op); break; }
        }
        if (L.call_once == 0x1F) { L.variant = Variant::KI; L.cmds = kCmdsKi; L.cmd_count = 0x31; L.header_size = 18; }
    } else return Layout{};
    const int adv[] = {0xF4, W, 0xFB, W, 0x7A, W, 0xDB, W, 0xD4, W};
    int a = find_pattern(ram, 0x200, 0x2000, adv, 10);
    if (a < 0 || ram[a + 1] != ram[a + 9] || ram[a + 3] != ram[a + 7]) return Layout{};
    L.ptr_lo = ram[a + 1];
    L.ptr_hi = ram[a + 3];
    const int acc[] = {0x89, W, W, 0x6B};
    if (int p = find_pattern(ram, 0x200, 0x2000, acc, 4); p >= 0) L.tempo_addr = ram[p + 1];
    const int tm[] = {0xFA, W, 0xFA};
    if (int p = find_pattern(ram, 0x200, 0x2000, tm, 3); p >= 0) L.timer_addr = ram[p + 1];
    const int pt[] = {0xF5, W, W, 0xCF, 0xCB};
    if (int p = find_pattern(ram, 0x200, 0x2000, pt, 5); p >= 0) L.pitch_table = rd16(ram, p + 1);
    const int im[] = {0x5D, 0xF5, W, W, 0xCE, 0xD5, W, W};
    if (int p = find_pattern(ram, 0x200, 0x2000, im, 8); p >= 0) L.sample_map = rd16(ram, p + 2);
    const int tr[] = {0x95, W, W, 0x1C, 0x4D};
    if (int p = find_pattern(ram, 0x200, 0x2000, tr, 5); p >= 0) {
        L.transpose_base = rd16(ram, p + 1);
        if (ram[p - 2] == 0x88) L.note_offset = ram[p - 1];
        for (int q = p - 24; q < p; ++q)
            if (ram[q] == 0x68 && ram[q + 1] == 0xE1 && ram[q + 2] == 0xF0 && ram[q + 4] == 0xF4) { L.note_var_base = ram[q + 5]; break; }
    }
    const int va[] = {0xE4, W, 0x1C, 0xBC, 0xFD, 0xF7, 0x01};
    if (int p = find_pattern(ram, 0x200, 0x2000, va, 7); p >= 0) L.variant_addr = ram[p + 1];
    const int rt[] = {0xF6, W, W, 0xD4, L.ptr_hi, 0xF6, W, W, 0xD4, L.ptr_lo};
    if (int p = find_pattern(ram, 0x200, 0x2000, rt, 10); p >= 0) { L.stack_hi = rd16(ram, p + 1); L.stack_lo = rd16(ram, p + 6); }
    const int sf[] = {0xEE, 0xF6, W, W, 0xD4, L.ptr_lo};
    if (int p = find_pattern(ram, 0x200, 0x2000, sf, 6); p >= 0) L.sfx_table = rd16(ram, p + 2);
    const int st[] = {0xE5, W, W, 0x1C, 0xFD, 0xF6, W, W, 0xC4, W, 0xF6, W, W, 0xC4, W};
    if (int p = find_pattern(ram, 0x200, 0x2000, st, 15); p >= 0) { L.song_num_addr = rd16(ram, p + 1); L.song_table = rd16(ram, p + 6); }
    if (!L.pitch_table || !L.sample_map) return Layout{};
    return L;
}

Layout layout_for_tests(Variant variant) {
    Layout L;
    L.variant = variant;
    L.cmd_table = 0x100F; L.pitch_table = 0x11E1; L.sample_map = 0x04E0; L.transpose_base = 0x0140;
    if (variant == Variant::DKC2) { L.cmds = kCmdsDkc2; L.cmd_count = 0x33; L.call_once = 0x21; L.note_offset = 0x24; L.note_var_base = 0x0C; }
    else if (variant == Variant::KI) { L.cmds = kCmdsKi; L.cmd_count = 0x31; L.call_once = 0x1F; L.note_offset = 0x24; L.note_var_base = 0x0E; L.stack_lo = 0x0354; L.stack_hi = 0x03D4; L.header_size = 18; }
    else { L.cmds = kCmdsDkc1; L.cmd_count = 0x31; }
    return L;
}

std::unique_ptr<seq::Driver> detect(const uint8_t* ram) {
    Layout L = detect_layout(ram);
    if (!L.valid()) return nullptr;
    return std::make_unique<RareDriver>(L);
}

uint8_t RareDriver::note_byte(int semitone_from_c0) const { return uint8_t(0x80 + std::clamp(semitone_from_c0 - 11, 1, 0x3C)); }

bool RareDriver::transpose_event(Event& e, int semis) const {
    if (e.type != EventType::Note) return false;
    int n = (e.b[0] & 0x7F) + semis;
    if (n < 1 || n > 0x3C) return false;
    e.b[0] = uint8_t(0x80 + n);
    if (e.pitch >= 0) e.pitch += semis;
    return true;
}

int RareDriver::cmd_size(uint8_t op) const { return op < L.cmd_count ? L.cmds[op].size : 0; }
const char* RareDriver::cmd_name(uint8_t op) const { return op < L.cmd_count ? L.cmds[op].name : "?"; }
const char* RareDriver::cmd_code(uint8_t op) const { return op < L.cmd_count ? L.cmds[op].code : "???"; }
seq::FxClass RareDriver::cmd_class(uint8_t op) const { return op < L.cmd_count && L.cmds[op].size ? L.cmds[op].cls : seq::FxClass::Invalid; }

std::string RareDriver::event_text(const Event& e) const {
    char b[96];
    if (e.type == EventType::Note || e.type == EventType::Rest) {
        const char* var = L.note_var_base && (e.b[0] == 0xE0 || e.b[0] == 0xE1) ? (e.b[0] == 0xE0 ? " (note variable A)" : " (note variable B)") : "";
        std::snprintf(b, sizeof b, "%s  %d ticks%s%s", e.type == EventType::Rest ? "rest" : note_name(e).c_str(), e.duration,
                      e.size == 1 ? " (fixed)" : "", var);
        return b;
    }
    if (e.type == EventType::Command) {
        switch (e.b[0]) {
            case 0x03: std::snprintf(b, sizeof b, "Jump $%02X%02X", e.b[2], e.b[1]); return b;
            case 0x04: std::snprintf(b, sizeof b, "Call $%02X%02X  x%d", e.b[3], e.b[2], e.b[1]); return b;
            case 0x1F: case 0x21: if (L.call_once == e.b[0]) { std::snprintf(b, sizeof b, "Call $%02X%02X once", e.b[2], e.b[1]); return b; } break;
            case 0x06: if (e.size == 3) { std::snprintf(b, sizeof b, "Fixed duration %d", (e.b[1] << 8) | e.b[2]); return b; } break;
            case 0x13: std::snprintf(b, sizeof b, "Transpose %+d", int8_t(e.b[1])); return b;
            case 0x14: std::snprintf(b, sizeof b, "Transpose add %+d", int8_t(e.b[1])); return b;
            default: break;
        }
    }
    return seq::Driver::event_text(e);
}

int RareDriver::instrument_count(const uint8_t* ram) const {
    if (!L.sample_map) return 0;
    int n = 0;
    for (int i = 0; i < 64; ++i) if (ram[(L.sample_map + i) & 0xFFFF] < 0x80 && ram[(L.sample_map + i) & 0xFFFF] != 0) n = i + 1;
    return n;
}

seq::Instrument RareDriver::read_instrument(const uint8_t* ram, int index) const {
    seq::Instrument in{};
    in.srcn = ram[(L.sample_map + index) & 0xFFFF];
    in.adsr0 = 0x8F; in.adsr1 = 0xE0;
    in.gain = 0x7F;
    return in;
}

bool RareDriver::preview_regs(const uint8_t* ram, uint8_t note_byte, int instrument, uint8_t regs[8]) const {
    if (!L.pitch_table) return false;
    int n = (note_byte & 0x7F) + L.note_offset;
    int pitch = rd16(ram, L.pitch_table + n * 2);
    if (pitch > 0x3FFF) pitch = 0x3FFF;
    regs[0] = 0x30; regs[1] = 0x30;
    regs[2] = uint8_t(pitch & 0xFF); regs[3] = uint8_t(pitch >> 8);
    regs[4] = ram[(L.sample_map + instrument) & 0xFFFF]; regs[5] = 0x8F; regs[6] = 0xE0; regs[7] = 0x7F;
    return true;
}

seq::Track RareDriver::parse_track(const uint8_t* ram, uint16_t start, int variant, int voice, int budget) const {
    seq::Track t;
    t.addr = start;
    if (!start) return t;
    struct Frame { int call_addr; int count; int size; };
    std::vector<Frame> stack;
    std::vector<uint8_t> iter(0x10000, 0);
    int pc = start, tick = 0, fixed = -1, transpose = 0;
    int note_var[2] = {-1, -1};
    if (L.note_var_base && voice >= 0) { note_var[0] = ram[L.note_var_base + voice]; note_var[1] = ram[L.note_var_base + 8 + voice]; }
    bool twob = false;
    auto loop_target = [&](int addr) {
        for (size_t i = 0; i < t.events.size(); ++i) if (t.events[i].addr == addr) return int(i);
        return 0;
    };
    auto emit = [&](Event e) {
        e.in_sub = !stack.empty();
        e.nest = uint8_t(std::min<size_t>(stack.size(), 255));
        e.sub_iter = iter[e.addr];
        if (iter[e.addr] < 255) ++iter[e.addr];
        t.events.push_back(e);
    };
    std::set<std::pair<int, std::vector<int>>> jumped;
    auto jump = [&](int target) {
        std::vector<int> sig;
        for (const Frame& f : stack) { sig.push_back(f.call_addr); sig.push_back(f.count); }
        if (iter[target & 0xFFFF] > 0 && !jumped.insert({target, sig}).second) {
            t.loops = true; t.loop_event = loop_target(target); t.terminated = true; return false;
        }
        if (iter[target & 0xFFFF] > 0 && stack.empty()) { t.loops = true; t.loop_event = loop_target(target); t.terminated = true; return false; }
        pc = target;
        return true;
    };
    for (;;) {
        if (int(t.events.size()) >= budget) break;
        const uint8_t b = ram[pc & 0xFFFF];
        if (b >= 0x80) {
            int size = 1, dur;
            if (fixed >= 0) dur = fixed;
            else if (twob) { dur = (ram[(pc + 1) & 0xFFFF] << 8) | ram[(pc + 2) & 0xFFFF]; size = 3; }
            else { dur = ram[(pc + 1) & 0xFFFF]; size = 2; }
            if (dur == 0) dur = 1;
            Event e = make_event(b == 0x80 ? EventType::Rest : EventType::Note, uint16_t(pc), ram, size, tick);
            e.duration = dur;
            int n = int(b) - 0x80;
            if (L.note_var_base && (b == 0xE0 || b == 0xE1)) n = note_var[b - 0xE0];
            if (b != 0x80) e.pitch = n < 0 ? -1 : ((n + transpose) & 0x7F) + 11;
            emit(e);
            pc += size;
            tick += dur;
            continue;
        }
        if (b >= L.cmd_count || L.cmds[b].size == 0) { t.truncated = true; break; }
        int size = L.cmds[b].size;
        if (b == 0x06 && twob) size = 3;
        const uint8_t* args = ram + ((pc + 1) & 0xFFFF);
        if (b != 0x05) emit(make_event(EventType::Command, uint16_t(pc), ram, size, tick));
        int next = pc + size;
        bool stop = false;
        switch (b) {
            case 0x00: t.events.back().type = EventType::End; t.terminated = true; stop = true; break;
            case 0x03: if (!jump(rd16(ram, pc + 1))) stop = true; else continue; break;
            case 0x04:
                if (stack.size() > 16) { t.truncated = true; stop = true; break; }
                stack.push_back({pc, args[0] ? args[0] : 256, 4});
                pc = rd16(ram, pc + 2);
                continue;
            case 0x05: {
                if (stack.empty()) { t.truncated = true; stop = true; break; }
                Frame& f = stack.back();
                if (--f.count > 0) { pc = rd16(ram, f.call_addr + f.size - 2); continue; }
                pc = f.call_addr + f.size;
                stack.pop_back();
                continue;
            }
            case 0x06: fixed = twob ? ((args[0] << 8) | args[1]) : args[0]; break;
            case 0x07: fixed = -1; break;
            case 0x13: transpose = int8_t(args[0]); break;
            case 0x14: transpose += int8_t(args[0]); break;
            case 0x1C: if (L.note_var_base) note_var[0] = args[0]; break;
            case 0x1D: if (L.note_var_base) note_var[1] = args[0]; break;
            case 0x2B: twob = true; break;
            case 0x2C: twob = false; break;
            case 0x2D:
                if (L.variant == Variant::DKC1) { if (!jump(rd16(ram, pc + 1 + 2 * variant))) stop = true; else continue; }
                break;
            default:
                if (b == L.call_once && b) {
                    if (stack.size() > 16) { t.truncated = true; stop = true; break; }
                    stack.push_back({pc, 1, 3});
                    pc = rd16(ram, pc + 1);
                    continue;
                }
                break;
        }
        if (stop) break;
        pc = next;
    }
    int end = 0;
    for (const Event& e : t.events) end = std::max(end, e.tick + e.duration);
    t.total_ticks = end;
    t.used_events = int(t.events.size());
    t.end_addr = t.addr;
    return t;
}

bool RareDriver::parse_header(const uint8_t* ram, uint16_t header, int variant, seq::Pattern& out, int budget) const {
    out = seq::Pattern{};
    out.addr = header;
    bool any = false;
    for (int v = 0; v < 8; ++v) {
        uint16_t start = rd16(ram, header + v * 2);
        if (!start) continue;
        out.tracks[v] = parse_track(ram, start, variant, v, budget);
        if (out.tracks[v].truncated) return false;
        if (out.tracks[v].total_ticks > 0) any = true;
        out.length_ticks = std::max(out.length_ticks, out.tracks[v].total_ticks);
    }
    return any;
}

std::vector<seq::Song> RareDriver::find_songs(const uint8_t* ram, const uint8_t* dsp) const {
    (void)dsp;
    std::vector<seq::Song> songs;
    const int variant = L.variant == Variant::DKC1 ? ram[L.variant_addr] : 0;
    auto add_song = [&](int a, seq::Pattern&& pat, int number) {
        bool loops = false;
        for (int v = 0; v < 8; ++v) if (pat.tracks[v].loops) loops = true;
        seq::Song sg;
        sg.order_addr = uint16_t(a);
        sg.order_end = uint16_t(a + L.header_size);
        sg.orders.push_back({sg.order_addr, pat.addr});
        sg.patterns.push_back(std::move(pat));
        sg.loop_count = loops ? 0xFF : 0;
        sg.loop_to = loops ? 0 : -1;
        char b[64];
        if (number >= 0) std::snprintf(b, sizeof b, "song %d @%04X (%d ticks%s)", number, a, sg.patterns[0].length_ticks, loops ? ", loops" : "");
        else std::snprintf(b, sizeof b, "song @%04X (%d ticks%s)", a, sg.patterns[0].length_ticks, loops ? ", loops" : "");
        sg.label = b;
        songs.push_back(std::move(sg));
    };
    if (L.song_table) {
        int limit = 0x10000;
        for (int i = 0; i < 64 && L.song_table + i * 2 + 2 <= limit; ++i) {
            int e = rd16(ram, L.song_table + i * 2);
            if (e < 0x200 || e >= 0xFF00) break;
            if (e > L.song_table) limit = std::min(limit, e);
            seq::Pattern pat;
            if (!parse_header(ram, uint16_t(e), variant, pat)) continue;
            for (int v = 0; v < 8; ++v) if (pat.tracks[v].addr > L.song_table) limit = std::min(limit, int(pat.tracks[v].addr));
            add_song(e, std::move(pat), i);
        }
        if (!songs.empty()) return songs;
    }
    std::vector<bool> taken(0x10000, false);
    const int data_min = L.pitch_table + 61 * 2;
    if (L.sfx_table) for (int k = 0; k < 0x100; ++k) taken[(L.sfx_table + k) & 0xFFFF] = true;
    for (int a = 0x200; a + 16 <= 0xFFF0; a += 1) {
        if (taken[a]) continue;
        int nonzero = 0;
        bool ok = true;
        uint16_t w[8];
        for (int v = 0; v < 8 && ok; ++v) {
            w[v] = rd16(ram, a + v * 2);
            if (!w[v]) continue;
            ++nonzero;
            if (w[v] < data_min || w[v] >= 0xFFF0) { ok = false; break; }
            uint8_t first = ram[w[v]];
            if (first > 0x30 && first < 0x80) ok = false;
            for (int u = 0; u < v; ++u) if (w[u] == w[v]) ok = false;
        }
        if (!ok || nonzero < 4) continue;
        seq::Pattern probe;
        if (!parse_header(ram, uint16_t(a), variant, probe, 400)) continue;
        int notes = 0, voiced = 0;
        for (int v = 0; v < 8 && ok; ++v) {
            if (!w[v]) continue;
            int n = 0;
            for (const Event& e : probe.tracks[v].events) if (e.type == EventType::Note) ++n;
            if (n == 0 && !probe.tracks[v].terminated) ok = false;
            if (probe.tracks[v].total_ticks > 200000) ok = false;
            notes += n;
            if (n >= 4) ++voiced;
        }
        if (!ok || notes < 24 || voiced < 4) continue;
        seq::Pattern pat;
        if (!parse_header(ram, uint16_t(a), variant, pat)) continue;
        bool runaway = false;
        for (int v = 0; v < 8; ++v) {
            if (pat.tracks[v].addr && !pat.tracks[v].terminated && !pat.tracks[v].loops) runaway = true;
            if (pat.tracks[v].total_ticks > 200000) runaway = true;
        }
        if (runaway) continue;
        for (int k = 0; k < 16; ++k) taken[a + k] = true;
        add_song(a, std::move(pat), -1);
    }
    return songs;
}

uint16_t RareDriver::live_ptr(const uint8_t* ram, const seq::Position& pos, int v) const {
    (void)pos;
    return uint16_t(ram[L.ptr_lo + v] | (ram[L.ptr_hi + v] << 8));
}

int RareDriver::pick_current_song(const uint8_t* ram, const std::vector<seq::Song>& songs) const {
    int best = -1, best_hits = 0;
    seq::Position none;
    for (size_t i = 0; i < songs.size(); ++i) {
        const seq::Pattern& p = songs[i].patterns[0];
        int hits = 0;
        for (int v = 0; v < 8; ++v) {
            uint16_t ptr = live_ptr(ram, none, v);
            for (const Event& e : p.tracks[v].events)
                if (e.addr == ptr || uint16_t(e.addr + e.size) == ptr) { ++hits; break; }
        }
        if (hits > best_hits) { best_hits = hits; best = int(i); }
    }
    if (best < 0 && L.song_table && L.song_num_addr) {
        int a = rd16(ram, L.song_table + ram[L.song_num_addr] * 2);
        for (size_t i = 0; i < songs.size(); ++i) if (songs[i].order_addr == a) return int(i);
    }
    return best >= 0 ? best : songs.empty() ? -1 : 0;
}

seq::Position RareDriver::locate(const uint8_t* ram, const seq::Song& song, const seq::Position* prev) const {
    seq::Position pos;
    pos.track_ptr_base = L.ptr_lo;
    pos.track_ptr_span = uint8_t(L.ptr_hi + 8 - L.ptr_lo);
    pos.order_index = 0;
    uint16_t ptr[8];
    for (int v = 0; v < 8; ++v) ptr[v] = live_ptr(ram, pos, v);
    seq::resolve_stream_position(song.patterns[0], ptr, prev, false, pos);
    return pos;
}

double RareDriver::ticks_per_second(const uint8_t* ram) const {
    int latch = ram[0xFA], add = ram[L.tempo_addr];
    if (!latch || !add) return 0;
    return 8000.0 / latch * add / 256.0;
}

bool RareDriver::tempo_writes(const uint8_t* ram, double tps, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    int latch = ram[0xFA];
    if (!latch || tps <= 0) return false;
    int add = int(tps * latch / 8000.0 * 256.0 + 0.5);
    out.push_back({L.tempo_addr, uint8_t(std::clamp(add, 1, 255))});
    return true;
}

std::vector<uint8_t> RareDriver::serialize_track(const std::vector<Event>& events) const {
    std::vector<uint8_t> out;
    for (const Event& e : events) {
        if (e.in_sub) continue;
        for (int i = 0; i < e.size; ++i) out.push_back(e.b[i]);
    }
    return out;
}

void RareDriver::retime(std::vector<Event>& ev) const {
    int tick = 0, fixed = -1, transpose = 0, note_var[2] = {-1, -1};
    for (const Event& e : ev) if (e.type == EventType::Note && e.pitch >= 0 && L.note_var_base && (e.b[0] == 0xE0 || e.b[0] == 0xE1)) { note_var[e.b[0] - 0xE0] = ((e.pitch - 11) & 0x7F); break; }
    bool twob = false;
    for (Event& e : ev) {
        e.tick = tick;
        if (e.type == EventType::Note || e.type == EventType::Rest) {
            if (e.size == 1) e.duration = fixed >= 0 ? fixed : 1;
            else if (e.size == 3) e.duration = (e.b[1] << 8) | e.b[2];
            else e.duration = e.b[1];
            if (e.duration == 0) e.duration = 1;
            if (e.type == EventType::Note) {
                int n = int(e.b[0]) - 0x80;
                if (L.note_var_base && (e.b[0] == 0xE0 || e.b[0] == 0xE1)) n = note_var[e.b[0] - 0xE0];
                e.pitch = n < 0 ? -1 : ((n + transpose) & 0x7F) + 11;
            }
            tick += e.duration;
        } else {
            e.duration = 0;
            if (e.type == EventType::Command) {
                switch (e.b[0]) {
                    case 0x06: fixed = e.size == 3 ? ((e.b[1] << 8) | e.b[2]) : e.b[1]; break;
                    case 0x07: fixed = -1; break;
                    case 0x13: transpose = int8_t(e.b[1]); break;
                    case 0x14: transpose += int8_t(e.b[1]); break;
                    case 0x1C: if (L.note_var_base) note_var[0] = e.b[1]; break;
                    case 0x1D: if (L.note_var_base) note_var[1] = e.b[1]; break;
                    case 0x2B: twob = true; break;
                    case 0x2C: twob = false; break;
                    default: break;
                }
            }
        }
    }
    (void)twob;
}

namespace {
void duration_state(const std::vector<Event>& ev, int i, int& fixed, bool& twob) {
    fixed = -1; twob = false;
    for (int k = 0; k < i; ++k) {
        const Event& e = ev[size_t(k)];
        if (e.type != EventType::Command) continue;
        switch (e.b[0]) {
            case 0x06: fixed = e.size == 3 ? ((e.b[1] << 8) | e.b[2]) : e.b[1]; break;
            case 0x07: fixed = -1; break;
            case 0x2B: twob = true; break;
            case 0x2C: twob = false; break;
            default: break;
        }
    }
}
Event cmd_event(uint8_t op, int a = -1, int b = -1) {
    Event c{};
    c.type = EventType::Command;
    c.addr = 0;
    c.b[0] = op;
    c.size = 1;
    if (a >= 0) { c.b[1] = uint8_t(a); c.size = 2; }
    if (b >= 0) { c.b[2] = uint8_t(b); c.size = 3; }
    return c;
}
}

void RareDriver::apply_note_byte(Event& e, uint8_t byte) const {
    e.b[0] = byte;
    e.type = byte == 0x80 ? EventType::Rest : EventType::Note;
}

bool RareDriver::set_duration(std::vector<Event>& ev, int i, int dur) const {
    if (i < 0 || i >= int(ev.size()) || dur < 1 || dur > 0xFFFF) return false;
    int fixed; bool twob;
    duration_state(ev, i, fixed, twob);
    Event& e = ev[size_t(i)];
    if (e.size == 1 && fixed >= 0) {
        e.addr = 0;
        Event restore = twob ? cmd_event(0x06, fixed >> 8, fixed & 0xFF) : cmd_event(0x06, fixed & 0xFF);
        ev.insert(ev.begin() + i + 1, restore);
        ev.insert(ev.begin() + i, cmd_event(0x07));
        ++i;
    }
    Event& n = ev[size_t(i)];
    n.addr = 0;
    if (dur > 255 && !twob) {
        ev.insert(ev.begin() + i + 1, cmd_event(0x2C));
        ev.insert(ev.begin() + i, cmd_event(0x2B));
        ++i;
        twob = true;
    }
    Event& m = ev[size_t(i)];
    if (twob) { m.size = 3; m.b[1] = uint8_t(dur >> 8); m.b[2] = uint8_t(dur & 0xFF); }
    else { m.size = 2; m.b[1] = uint8_t(dur); }
    m.duration = dur;
    return true;
}

void RareDriver::track_pointer_writes(const seq::Song& song, int pattern_idx, int voice, uint16_t dest, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    (void)pattern_idx;
    uint16_t at = uint16_t(song.order_addr + voice * 2);   // the header word
    out.push_back({at, uint8_t(dest & 0xFF)});
    out.push_back({uint16_t(at + 1), uint8_t(dest >> 8)});
}

void RareDriver::live_state_writes(const uint8_t* ram, const seq::Position& pos, int voice, uint16_t ptr, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    (void)pos;
    int mapped = remap.find(uint16_t(ram[L.ptr_lo + voice] | (ram[L.ptr_hi + voice] << 8)));
    if (mapped >= 0) ptr = uint16_t(mapped);
    out.push_back({uint16_t(L.ptr_lo + voice), uint8_t(ptr & 0xFF)});
    out.push_back({uint16_t(L.ptr_hi + voice), uint8_t(ptr >> 8)});
    {
        for (int k = 0; k < 8; ++k) {
            uint16_t lo_at = uint16_t(L.stack_lo + voice * 8 + k), hi_at = uint16_t(L.stack_hi + voice * 8 + k);
            int m = remap.find(uint16_t(ram[lo_at] | (ram[hi_at] << 8)));
            if (m < 0) continue;
            out.push_back({lo_at, uint8_t(m & 0xFF)});
            out.push_back({hi_at, uint8_t(m >> 8)});
        }
    }
}

void RareDriver::reclaimable_ranges(const uint8_t* ram, std::vector<std::pair<uint16_t, uint16_t>>& out) const {
    if (!L.sfx_table) return;
    const int data_min = L.pitch_table + 61 * 2;
    for (int i = 0; i < 128; ++i) {
        uint16_t start = rd16(ram, L.sfx_table + i * 2);
        if (start < data_min || start >= 0xFF00) break;
        seq::Track t = parse_track(ram, start, 0, -1, 2000);
        if (t.truncated) continue;
        for (const Event& e : t.events) if (!e.in_sub) out.push_back({e.addr, uint16_t(e.addr + e.size)});
    }
}

bool RareDriver::set_note_at(std::vector<Event>& ev, int tick, uint8_t note_byte, int pattern_len) const {
    (void)pattern_len;
    return seq::stream_set_note_at(*this, ev, tick, note_byte, pattern_len);
}

bool RareDriver::remove_span(std::vector<Event>& ev, int tick, int ticks, bool keep_length) const {
    (void)keep_length;
    return seq::stream_remove_span(*this, ev, tick, ticks);
}

bool RareDriver::insert_span(std::vector<Event>& ev, int tick, int ticks, uint8_t byte) const {
    return seq::stream_insert_span(*this, ev, tick, ticks, byte);
}

bool RareDriver::enter_note(std::vector<Event>& ev, int tick, int semitone, int pattern_len) const {
    int transpose = 0;
    for (const Event& e : ev) {
        if (e.tick > tick) break;
        if (e.type == EventType::Command && e.b[0] == 0x13) transpose = int8_t(e.b[1]);
        else if (e.type == EventType::Command && e.b[0] == 0x14) transpose += int8_t(e.b[1]);
    }
    int n = semitone - 11 - transpose;
    if (n < 1 || n > 0x3C) return false;
    return set_note_at(ev, tick, uint8_t(0x80 + n), pattern_len);
}

bool RareDriver::insert_command_at(std::vector<Event>& ev, int tick, const uint8_t* bytes, int size) const {
    return seq::stream_insert_command_at(*this, ev, tick, bytes, size);
}

bool RareDriver::set_instrument(std::vector<Event>& ev, int tick0, int tick1, uint8_t ins) const {
    return seq::stream_set_instrument(*this, ev, tick0, tick1, ins);
}

}
