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
    {4, "Lp1", "Jump back unless counter 1 reached n", FxClass::Song, 2, true},  // 06
    {4, "Br1", "Jump when counter 1 reaches n", FxClass::Song, 2, true},         // 07
    {3, "Jmp", "Jump", FxClass::Song, 1, true},                           // 08
    {0, "Not", "Note", FxClass::Misc},                                    // 09
    {2, "EDl", "Echo delay", FxClass::Sys1},                              // 0A
    {0, "Blk", "Parameter block $04E0", FxClass::Misc},                   // 0B
    {2, "Prm", "Track parameter", FxClass::Misc},                         // 0C
    {2, "Ech", "Echo on/off", FxClass::Sys1},                             // 0D
    {1, "Rst", "Rest", FxClass::Misc},                                    // 0E
    {4, "Lp2", "Jump back unless counter 2 reached n", FxClass::Song, 2, true},  // 0F
    {4, "Br2", "Jump when counter 2 reaches n", FxClass::Song, 2, true},         // 10
    {2, "EFb", "Echo feedback", FxClass::Sys1},                           // 11
    {2, "FIR", "Echo filter set", FxClass::Sys1},                         // 12
    {2, "EVl", "Echo volume", FxClass::Sys1},                             // 13
    {2, "ESA", "Echo buffer address", FxClass::Sys1},                     // 14
};
const CmdSpec kBlock = {0, "Blk", "Parameter block", FxClass::Misc};
const CmdSpec kNop = {1, "Nop", "(no effect)", FxClass::Misc};

int popcount8(uint8_t m) { int n = 0; for (int i = 0; i < 8; ++i) if (m & (1 << i)) ++n; return n; }
int voice_bit(int v) { return 0x80 >> (v & 7); }   // the driver walks masks MSB first: bit 7 = voice 0

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
        if (base >= 0x1000 && base < 0xFF00 && ram[a + 7] + 1 == ram[a + 11]) {
            L.song_table = base;
            // BRA / MOV X,#lo / MOV $3C,X / MOV X,#hi / MOV $3D,X: the table for numbers past the compare
            if (ram[a + 12] == 0x2F && ram[a + 14] == 0xCD && ram[a + 16] == 0xD8 && ram[a + 18] == 0xCD && ram[a + 20] == 0xD8) {
                const uint16_t b2 = uint16_t(ram[a + 15] | (ram[a + 19] << 8));
                if (b2 >= 0x1000 && b2 < 0xFF00 && b2 != base) L.song_table2 = b2;
            }
            break;
        }
        p = a;
    }
    if (!L.song_table) return Layout{};

    // The 01 handler: ... / CALL fetch / MOV $09+X,A
    {
        const uint16_t h = uint16_t(ram[(L.cmd_table + 2) & 0xFFFF] | (ram[(L.cmd_table + 3) & 0xFFFF] << 8));
        for (int i = 0; i < 20; ++i)
            if (ram[(h + i) & 0xFFFF] == 0x3F && ram[(h + i + 3) & 0xFFFF] == 0xD4) { L.mask_zp = ram[(h + i + 4) & 0xFFFF]; break; }
    }

    // MOV X,#$00 / MOV $DF,X / MOV $DA,X / MOV $DB,X / MOV $DC,X / MOV A,$49+X
    const int state[] = {0xCD, 0x00, 0xD8, W, 0xD8, W, 0xD8, W, 0xD8, W, 0xF4, W};
    if (int a = find_pattern(ram, 0x200, 0x2000, state, 12); a >= 0) L.state_zp = ram[a + 11];

    // Records run until one stops looking like {track 0-3, address in ARAM}.
    auto count = [&](uint16_t table, int max) {
        int n = 0;
        for (int i = 0; i < max; ++i) {
            const int r = (table + i * 3) & 0xFFFF;
            const uint8_t trk = ram[r];
            const uint16_t addr = uint16_t(ram[r + 1] | (ram[r + 2] << 8));
            if (trk > 3) break;
            if (addr && (addr < 0x1000 || addr >= 0xFF00)) break;
            n = i + 1;
        }
        return n;
    };
    L.entries = count(L.song_table, 128);
    if (L.song_table2) L.entries2 = count(L.song_table2, 32);
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

// Walks a track's bytes; returns true when `ptr` lands inside one of its
// events (a rip can catch the pointer between an opcode and its operands).
namespace {
bool covers(const uint8_t* ram, uint16_t start, uint16_t ptr, int limit = 8000) {
    uint16_t p = start;
    uint16_t stack[8];
    int sp = 0, counter[2] = {0, 0};
    for (int n = 0; n < limit; ++n) {
        const uint8_t b = ram[p];
        if (b > 0x97 || (b > 0x14 && b < 0x18)) return false;
        const int sz = event_size(ram + p);
        if (ptr >= p && ptr < p + sz) return true;
        if (b == 0x03) { if (!sp) return false; p = stack[--sp]; continue; }
        if (b == 0x08) { p = uint16_t(ram[(p + 1) & 0xFFFF] | (ram[(p + 2) & 0xFFFF] << 8)); continue; }
        if (b == 0x06 || b == 0x0F || b == 0x07 || b == 0x10) {
            int& c = counter[b == 0x06 || b == 0x07 ? 0 : 1];
            c = (c + 1) & 0xFF;
            const bool back = b == 0x06 || b == 0x0F;
            const bool reached = c == ram[(p + 1) & 0xFFFF];
            if (reached) c = 0;
            if (back != reached) { p = uint16_t(ram[(p + 2) & 0xFFFF] | (ram[(p + 3) & 0xFFFF] << 8)); continue; }
        }
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

// The 01 masks a program sets: the first one and the union, over a bounded
// walk that follows calls and jumps.
void track_masks(const uint8_t* ram, uint16_t start, int& first, int& all) {
    first = -1; all = 0;
    uint16_t p = start, stack[8];
    int sp = 0;
    for (int n = 0; n < 3000; ++n) {
        const uint8_t b = ram[p];
        if (b > 0x97 || (b > 0x14 && b < 0x18)) return;
        const int sz = event_size(ram + p);
        if (b == 0x01) { const int m = ram[(p + 1) & 0xFFFF]; if (first < 0) first = m; all |= m; }
        if (b == 0x03) { if (!sp) return; p = stack[--sp]; continue; }
        if (b == 0x08) { p = uint16_t(ram[(p + 1) & 0xFFFF] | (ram[(p + 2) & 0xFFFF] << 8)); continue; }
        if (b == 0x02) {
            if (sp >= 8) return;
            stack[sp++] = uint16_t(p + sz);
            p = uint16_t(ram[(p + 1) & 0xFFFF] | (ram[(p + 2) & 0xFFFF] << 8));
            continue;
        }
        p = uint16_t(p + sz);
    }
}

// Which track drives each DSP voice: the first masks win, later ones fill
// in, a track without any 01 keeps its live mask (or its own bit).
std::array<int8_t, 8> owners_of(const uint8_t* ram, const Layout& L, const std::array<uint16_t, 4>& g) {
    int first[4], all[4];
    for (int t = 0; t < 4; ++t) {
        first[t] = -1; all[t] = 0;
        if (!g[size_t(t)]) continue;
        track_masks(ram, g[size_t(t)], first[t], all[t]);
        if (first[t] < 0) { first[t] = L.mask_zp ? ram[(L.mask_zp + t * 2) & 0xFF] : 0; if (!first[t]) first[t] = 1 << t; all[t] |= first[t]; }
    }
    std::array<int8_t, 8> o;
    for (int v = 0; v < 8; ++v) {
        o[size_t(v)] = -1;
        for (int t = 0; t < 4 && o[size_t(v)] < 0; ++t) if (g[size_t(t)] && (first[t] & voice_bit(v))) o[size_t(v)] = int8_t(t);
        for (int t = 0; t < 4 && o[size_t(v)] < 0; ++t) if (g[size_t(t)] && (all[t] & voice_bit(v))) o[size_t(v)] = int8_t(t);
    }
    return o;
}
}

std::vector<uint16_t> OzawaDriver::song_headers(const uint8_t* ram) const {
    groups_.clear();
    owners_.clear();
    headers_.clear();
    std::vector<std::pair<uint8_t, uint16_t>> rec;   // {track, start}
    std::vector<uint16_t> rec_at;                    // the record's address (the pseudo-header)
    for (int i = 0; i < L.entries + L.entries2; ++i) {
        const int r = (i < L.entries ? L.song_table + i * 3 : L.song_table2 + (i - L.entries) * 3) & 0xFFFF;
        rec.push_back({ram[r], uint16_t(ram[r + 1] | (ram[r + 2] << 8))});
        rec_at.push_back(uint16_t(r));
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
        if (live[size_t(v)]) continue;
        // Started by address, not through a table: the lowest program that
        // opens with a mask on a stream boundary and reaches the pointer.
        for (int a = 0x1000; a < 0xFFF0 && !live[size_t(v)]; ++a)
            if (ram[a] == 0x01 && (ram[a - 1] == 0x03 || ram[a - 1] == 0xFF || ram[a - 1] == 0x00) && covers(ram, uint16_t(a), ptr, 20000)) { live[size_t(v)] = uint16_t(a); any_live = true; }
    }
    if (any_live) {
        groups_.push_back(live);
        owners_.push_back(owners_of(ram, L, live));
        headers_.push_back(uint16_t(L.song_table));
    }
    // Then every table entry on its own, so the rest of the bank can be read.
    for (size_t i = 0; i < rec.size(); ++i) {
        const auto& [trk, addr] = rec[i];
        if (!addr) continue;
        if (any_live && addr == live[trk]) continue;
        std::array<uint16_t, 4> g{};
        g[trk] = addr;
        groups_.push_back(g);
        owners_.push_back(owners_of(ram, L, g));
        headers_.push_back(rec_at[i]);
    }
    return headers_;
}

// The group built from the running tracks is the song; stopped tracks park
// on event boundaries of whatever they last played and would outvote it.
int OzawaDriver::pick_current_song(const uint8_t* ram, const std::vector<seq::Song>& songs) const {
    for (size_t i = 0; i < songs.size(); ++i)
        if (songs[i].order_addr == L.song_table && !headers_.empty() && headers_[0] == L.song_table) { select_song(songs[i].order_addr); return int(i); }
    return stream::Driver::pick_current_song(ram, songs);
}

void OzawaDriver::select_song(uint16_t header) const {
    const int g = group_of(header);
    if (g >= 0) owner_ = owners_[size_t(g)];
}

seq::Position OzawaDriver::locate(const uint8_t* ram, const seq::Song& song, const seq::Position* prev) const {
    seq::Position pos = stream::Driver::locate(ram, song, prev);
    pos.track_ptr_base = L.ptr_zp;   // the four track pointers, whichever column maps where
    return pos;
}

State OzawaDriver::edit_state(const std::vector<Event>& ev) const {
    State s;
    for (const Event& e : ev) if (e.b[0] == 0x09 && (e.b[15] & 0x80)) { s.voice = e.b[15] & 7; break; }
    return s;
}

int OzawaDriver::group_of(uint16_t header) const {
    for (size_t i = 0; i < headers_.size(); ++i) if (headers_[i] == header) return int(i);
    return -1;
}

uint16_t OzawaDriver::track_start(const uint8_t* ram, uint16_t header, int v) const {
    (void)ram;
    if (v < 0 || v > 7) return 0;
    const int g = group_of(header);
    if (g < 0) return 0;
    const int t = owners_[size_t(g)][size_t(v)];
    return t < 0 ? 0 : groups_[size_t(g)][size_t(t)];
}

// Index of DSP voice v's value inside a 09's operands, -1 when the mask
// leaves the voice alone. Masks are consumed MSB first, so bit 7 is voice
// 0 and the values run in voice order.
static int value_index(const uint8_t* p, int v) {
    if (v < 0 || !(p[1] & voice_bit(v))) return -1;
    return 2 + popcount8(uint8_t(p[1] & ~(voice_bit(v) * 2 - 1)));
}

// State: len = note length, x[0] = length multiplier, x[1] = voice mask,
// x[2] / x[3] = the two repeat counters.
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
        // The column's own value; parsed events remember their column in
        // b[15] so edits and retimes decode the same way.
        int v = s.voice;
        if (v < 0 && (p[15] & 0x80)) v = p[15] & 7;
        if (v >= 0) e.b[15] = uint8_t(0x80 | v);
        int k = value_index(p, v);
        if (v < 0) for (int i = 2; i < e.size; ++i) if (p[i] != 0xFF) { k = i; break; }
        const int val = k < 0 ? -1 : p[k];
        if (val < 0) { e.type = v < 0 ? EventType::Rest : EventType::Tie; return; }
        if (val == 0xFF) { e.type = EventType::Rest; return; }
        if (val >= 0x80) { e.type = EventType::Percussion; return; }
        e.type = EventType::Note;
        e.pitch = note_semitone(uint8_t(val));
        return;
    }
    switch (b) {
        case 0x00: s.len = p[1]; break;
        case 0x04: s.x[0] = p[1]; break;
        case 0x01: s.x[1] = p[1]; break;
        case 0x02: f.kind = Flow::Call; f.count = 1; f.target = p[1] | (p[2] << 8); break;
        case 0x03: f.kind = Flow::Return; break;
        case 0x08: f.kind = Flow::Jump; f.target = p[1] | (p[2] << 8); break;
        // Both commands step a per-slot counter (06/07 slot 1, 0F/10 slot 2)
        // and compare it with n: 06/0F jump unless it got there, 07/10 jump
        // when it does; reaching n clears it. n = 0 means 256 passes, which
        // for 06/0F is the song loop.
        case 0x06: case 0x0F: {
            int& c = s.x[b == 0x06 ? 2 : 3];
            c = (c + 1) & 0xFF;
            f.target = p[2] | (p[3] << 8);
            if (p[1] == 0) { f.kind = Flow::Jump; f.count = 0; }
            else if (c != p[1]) { f.kind = Flow::Jump; f.count = 1; }
            else c = 0;
            break;
        }
        case 0x07: case 0x10: {
            int& c = s.x[b == 0x07 ? 2 : 3];
            c = (c + 1) & 0xFF;
            if (c == p[1]) { c = 0; f.kind = Flow::Jump; f.count = 1; f.target = p[2] | (p[3] << 8); }
            break;
        }
        default: break;
    }
    (void)pc;
}

int OzawaDriver::event_percussion(const Event& e) const {
    const int k = value_index(e.b, e.b[15] & 0x80 ? e.b[15] & 7 : -1);
    return k < 0 ? 0 : e.b[k] & 0x7F;
}

bool OzawaDriver::transpose_event(Event& e, int semis) const {
    if (e.type != EventType::Note || e.b[0] != 0x09) return false;
    const int k = value_index(e.b, e.b[15] & 0x80 ? e.b[15] & 7 : -1);
    if (k < 0) return false;
    const int val = e.b[k] + semis;
    if (val < 0 || val > 0x53) return false;
    e.b[k] = uint8_t(val);
    return true;
}

// Sets the column's value; a 09 that left the voice alone gains its bit,
// a rest (0E) becomes a one-voice 09.
void OzawaDriver::apply_note_byte(Event& e, uint8_t byte) const {
    const int v = e.b[15] & 0x80 ? e.b[15] & 7 : 0;
    if (e.b[0] == 0x0E) {
        e.b[0] = 0x09; e.b[1] = uint8_t(voice_bit(v)); e.b[2] = byte; e.size = 3;
    } else if (e.b[0] == 0x09) {
        int k = value_index(e.b, v);
        if (k < 0) {
            k = 2 + popcount8(uint8_t(e.b[1] & ~(voice_bit(v) * 2 - 1)));
            if (e.size >= 15) return;
            for (int i = e.size; i > k; --i) e.b[i] = e.b[i - 1];
            e.b[1] |= uint8_t(voice_bit(v));
            ++e.size;
        }
        e.b[k] = byte;
    } else return;
    e.b[15] = uint8_t(0x80 | v);
    e.type = byte == 0xFF ? EventType::Rest : byte >= 0x80 ? EventType::Percussion : EventType::Note;
    e.pitch = e.type == EventType::Note ? note_semitone(byte) : -1;
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
        for (int v = 0, k = 2; v < 8; ++v) {
            if (!(e.b[1] & voice_bit(v))) continue;
            const uint8_t val = e.b[k++];
            if (val < 0x54) std::snprintf(b, sizeof b, " %d:%s", v, note_name(val).c_str());
            else if (val == 0x54) std::snprintf(b, sizeof b, " %d:retrig", v);
            else if (val < 0x80) std::snprintf(b, sizeof b, " %d:noise%d", v, val & 0x1F);
            else if (val < 0xFF) std::snprintf(b, sizeof b, " %d:P%02d", v, val & 0x7F);
            else std::snprintf(b, sizeof b, " %d:off", v);
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
