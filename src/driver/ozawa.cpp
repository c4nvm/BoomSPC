#include "ozawa.hpp"

#include <cstdio>
#include <cstring>

using seq::Event;
using seq::EventType;
using seq::FxClass;
using stream::CmdSpec;
using stream::Flow;
using stream::State;
using stream::find_pattern;

namespace ozawa {
namespace {
const int W = 0x100;   // wildcard byte for find_pattern

// $00-$14. A size of 0 means the operand count comes from a mask byte.
const CmdSpec kCmds[0x15] = {
    {2, "Len", "Note length in ticks", FxClass::Time},                    // 00
    {2, "Vce", "DSP voices this track drives", FxClass::Sys1},            // 01
    {3, "Cal", "Call", FxClass::Song, 1, true},                           // 02
    {1, "Ret", "Return, or end of track", FxClass::Song, 0, true},        // 03
    {2, "LnM", "Length multiplier", FxClass::Time},                       // 04
    {2, "Prm", "Track parameter", FxClass::Misc},                         // 05
    {4, "Lp1", "Repeat slot 1 until the count", FxClass::Song, 2, true},  // 06
    {4, "Lp1", "Repeat slot 1 on the count", FxClass::Song, 2, true},     // 07
    {3, "Jmp", "Jump", FxClass::Song, 1, true},                           // 08
    {0, "Not", "Note", FxClass::Misc},                                    // 09
    {2, "EDl", "Echo delay", FxClass::Sys1},                              // 0A
    {0, "Blk", "Parameter block $04E0", FxClass::Misc},                   // 0B
    {2, "Prm", "Track parameter", FxClass::Misc},                         // 0C
    {2, "Ech", "Echo on/off", FxClass::Sys1},                             // 0D
    {1, "Rst", "Rest", FxClass::Misc},                                    // 0E
    {4, "Lp2", "Repeat slot 2 until the count", FxClass::Song, 2, true},  // 0F
    {4, "Lp2", "Repeat slot 2 on the count", FxClass::Song, 2, true},     // 10
    {2, "EFb", "Echo feedback", FxClass::Sys1},                           // 11
    {2, "FIR", "Echo filter set", FxClass::Sys1},                         // 12
    {2, "EVl", "Echo volume", FxClass::Sys1},                             // 13
    {2, "ESA", "Echo buffer address", FxClass::Sys1},                     // 14
};
const CmdSpec kBlock = {0, "Blk", "Parameter block", FxClass::Misc};
const CmdSpec kNop = {1, "Nop", "(no effect)", FxClass::Misc};

int popcount8(uint8_t m) { int n = 0; for (int i = 0; i < 8; ++i) if (m & (1 << i)) ++n; return n; }

// Bytes an event occupies, given the byte at p[0] and its mask operand.
int event_size(const uint8_t* p) {
    const uint8_t b = p[0];
    if (b >= 0x18) return b <= 0x97 ? 2 + popcount8(p[1]) : 1;
    if (b == 0x09 || b == 0x0B) return 2 + popcount8(p[1]);
    return b <= 0x14 ? kCmds[b].size : 1;
}
}

Layout detect_layout(const uint8_t* ram) {
    Layout L;
    // MOV X,$DE / MOV A,[$00+X] / INC $00+X / BNE +2 / INC $01+X / RET
    const int fetch[] = {0xE9, W, 0x00, 0xE7, W, 0xBB, W, 0xD0, 0x02, 0xBB, W, 0x6F};
    const int f = find_pattern(ram, 0x200, 0x2000, fetch, 12);
    if (f < 0) return Layout{};
    L.ptr_zp = ram[f + 4];
    if (ram[f + 6] != L.ptr_zp || ram[f + 10] != L.ptr_zp + 1) return Layout{};

    // CMP A,#$18 / BPL / ASL A / MOV X,A / JMP [!table+X]
    const int disp[] = {0x68, 0x18, 0x10, W, 0x1C, 0x5D, 0x1F, W, W};
    const int d = find_pattern(ram, 0x200, 0x2000, disp, 9);
    if (d < 0) return Layout{};
    L.cmd_table = uint16_t(ram[d + 7] | (ram[d + 8] << 8));

    // CMP A,#n / BCS / MOV X,#lo / MOV $3C,X / MOV X,#hi / MOV $3D,X
    const int tbl[] = {0x68, W, 0xB0, W, 0xCD, W, 0xD8, W, 0xCD, W, 0xD8, W};
    for (int p = 0x200; p < 0x2000; ++p) {
        const int a = find_pattern(ram, p, 0x2000, tbl, 12);
        if (a < 0) break;
        const uint16_t base = uint16_t(ram[a + 5] | (ram[a + 9] << 8));
        if (base >= 0x1000 && base < 0xFF00 && ram[a + 7] + 1 == ram[a + 11]) { L.song_table = base; break; }
        p = a;
    }
    if (!L.song_table) return Layout{};

    // MOV X,#$00 / MOV $DF,X / MOV $DA,X / MOV $DB,X / MOV $DC,X / MOV A,$49+X
    const int state[] = {0xCD, 0x00, 0xD8, W, 0xD8, W, 0xD8, W, 0xD8, W, 0xF4, W};
    if (int a = find_pattern(ram, 0x200, 0x2000, state, 12); a >= 0) L.state_zp = ram[a + 11];

    // Records run until one stops looking like {track 0-3, address in ARAM}.
    for (int i = 0; i < 128; ++i) {
        const int r = (L.song_table + i * 3) & 0xFFFF;
        const uint8_t trk = ram[r];
        const uint16_t addr = uint16_t(ram[r + 1] | (ram[r + 2] << 8));
        if (trk > 3) break;
        if (addr && (addr < 0x1000 || addr >= 0xFF00)) break;
        L.entries = i + 1;
    }
    return L;
}

std::unique_ptr<seq::Driver> detect(const uint8_t* ram) {
    Layout L = detect_layout(ram);
    if (!L.valid()) return nullptr;
    return std::make_unique<OzawaDriver>(L);
}

const CmdSpec& OzawaDriver::spec(uint8_t op) const {
    if (op <= 0x14) return kCmds[op];
    if (op >= 0x18 && op <= 0x97) return kBlock;
    return kNop;
}

// Walks a track's bytes; returns true when `ptr` lands on an event boundary.
namespace {
bool covers(const uint8_t* ram, uint16_t start, uint16_t ptr, int limit = 8000) {
    uint16_t p = start;
    uint16_t stack[8];
    int sp = 0;
    for (int n = 0; n < limit; ++n) {
        if (p == ptr) return true;
        const uint8_t b = ram[p];
        if (b > 0x97 || (b > 0x14 && b < 0x18)) return false;
        const int sz = event_size(ram + p);
        if (b == 0x03) { if (!sp) return false; p = stack[--sp]; continue; }
        if (b == 0x08) { p = uint16_t(ram[(p + 1) & 0xFFFF] | (ram[(p + 2) & 0xFFFF] << 8)); continue; }
        if (b == 0x02) {
            if (sp >= 8) return false;
            stack[sp++] = uint16_t(p + sz);
            p = uint16_t(ram[(p + 1) & 0xFFFF] | (ram[(p + 2) & 0xFFFF] << 8));
            continue;
        }
        p = uint16_t(p + sz);
    }
    return false;
}
}

std::vector<uint16_t> OzawaDriver::song_headers(const uint8_t* ram) const {
    groups_.clear();
    headers_.clear();
    std::vector<std::pair<uint8_t, uint16_t>> rec;
    for (int i = 0; i < L.entries; ++i) {
        const int r = (L.song_table + i * 3) & 0xFFFF;
        rec.push_back({ram[r], uint16_t(ram[r + 1] | (ram[r + 2] << 8))});
    }
    // The playing set first: each live pointer belongs to the table entry whose
    // program reaches it.
    std::array<uint16_t, 4> live{};
    bool any_live = false;
    for (int v = 0; v < 4; ++v) {
        if (L.state_zp && !ram[(L.state_zp + v * 2) & 0xFF]) continue;   // that track is stopped
        const uint16_t ptr = uint16_t(ram[(L.ptr_zp + v * 2) & 0xFF] | (ram[(L.ptr_zp + v * 2 + 1) & 0xFF] << 8));
        if (ptr < 0x1000 || ptr >= 0xFF00) continue;
        for (const auto& [trk, addr] : rec)
            if (trk == v && addr && covers(ram, addr, ptr)) { live[size_t(v)] = addr; any_live = true; break; }
    }
    if (any_live) {
        groups_.push_back(live);
        headers_.push_back(uint16_t(L.song_table));
    }
    // Then every table entry on its own, so the rest of the bank can be read.
    for (int i = 0; i < L.entries; ++i) {
        const auto& [trk, addr] = rec[size_t(i)];
        if (!addr) continue;
        if (any_live && addr == live[trk]) continue;
        std::array<uint16_t, 4> g{};
        g[trk] = addr;
        groups_.push_back(g);
        headers_.push_back(uint16_t((L.song_table + i * 3) & 0xFFFF));
    }
    return headers_;
}

int OzawaDriver::group_of(uint16_t header) const {
    for (size_t i = 0; i < headers_.size(); ++i) if (headers_[i] == header) return int(i);
    return -1;
}

uint16_t OzawaDriver::track_start(const uint8_t* ram, uint16_t header, int v) const {
    (void)ram;
    if (v < 0 || v > 3) return 0;
    const int g = group_of(header);
    return g < 0 ? 0 : groups_[size_t(g)][size_t(v)];
}

// State: len = note length, x[0] = length multiplier, x[1] = voice mask.
void OzawaDriver::decode(const uint8_t* p, int pc, State& s, Event& e, Flow& f) const {
    const uint8_t b = p[0];
    e.b[0] = b;
    e.pitch = -1;
    e.type = EventType::Command;
    e.size = uint8_t(std::min(event_size(p), 16));
    for (int i = 1; i < e.size; ++i) e.b[i] = p[i];

    if (b == 0x09 || b == 0x0E) {
        const int mul = s.x[0] ? s.x[0] : 1;
        e.duration = std::max(1, s.len * mul);
        if (b == 0x0E) { e.type = EventType::Tie; return; }
        // The first value in the mask is the voice the grid shows; the rest of
        // the chord stays in the bytes.
        int first = -1;
        for (int i = 2; i < e.size; ++i) if (p[i] != 0xFF) { first = p[i]; break; }
        if (first < 0) { e.type = EventType::Rest; return; }
        e.type = EventType::Note;
        e.pitch = note_semitone(uint8_t(first));
        return;
    }
    switch (b) {
        case 0x00: s.len = p[1]; break;
        case 0x04: s.x[0] = p[1]; break;
        case 0x01: s.x[1] = p[1]; break;
        case 0x02: f.kind = Flow::Call; f.count = 1; f.target = p[1] | (p[2] << 8); break;
        case 0x03: f.kind = Flow::Return; break;
        case 0x08: f.kind = Flow::Jump; f.target = p[1] | (p[2] << 8); break;
        case 0x06: case 0x0F: f.kind = Flow::RepStart; f.count = p[1] ? p[1] : 256; f.slot = b == 0x06 ? 0 : 1; break;
        case 0x07: case 0x10: f.kind = Flow::RepEnd; f.count = p[1] ? p[1] : 256; f.slot = b == 0x07 ? 0 : 1; break;
        default: break;
    }
    (void)pc;
}

int OzawaDriver::jump_target(const Event& e) const {
    if (e.type != EventType::Command) return -1;
    const uint8_t op = e.b[0];
    if (op == 0x02 || op == 0x08) return e.b[1] | (e.b[2] << 8);
    if (op == 0x06 || op == 0x07 || op == 0x0F || op == 0x10) return e.b[2] | (e.b[3] << 8);
    return -1;
}

std::string OzawaDriver::event_text(const Event& e) const {
    char b[96];
    if (e.b[0] == 0x09) {
        std::string s = "note";
        for (int i = 2; i < e.size; ++i) {
            std::snprintf(b, sizeof b, " %s", e.b[i] == 0xFF ? "--" : note_name(e.b[i]).c_str());
            s += b;
        }
        return s;
    }
    if (e.b[0] == 0x0E) return "rest";
    const CmdSpec& sp = spec(e.b[0]);
    std::string s = sp.name;
    for (int i = 1; i < e.size; ++i) { std::snprintf(b, sizeof b, " %02X", e.b[i]); s += b; }
    return s;
}

double OzawaDriver::ticks_per_second(const uint8_t* ram) const {
    // Timer 0 drives the tick; the driver leaves its divider in $FA.
    return ram[0xFA] ? 8000.0 / ram[0xFA] : 0;
}

}
