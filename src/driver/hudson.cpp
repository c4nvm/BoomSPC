#include "hudson.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

using seq::Event;
using seq::EventType;
using seq::FxClass;
using stream::CmdSpec;
using stream::Flow;
using stream::State;
using stream::find_pattern;
using stream::rd16;

namespace hudson {
namespace {
const int W = 0x100;
const uint8_t kLens[8] = {0xC0, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01};

const CmdSpec kCmds[0x30] = {
    {1, "Nop", "(no effect)", FxClass::Misc},                           // D0
    {2, "Tmp", "Tempo (BPM)", FxClass::Speed},                          // D1
    {2, "Oct", "Octave", FxClass::Pitch},                               // D2
    {1, "Oc+", "Octave up", FxClass::Pitch},                            // D3
    {1, "Oc-", "Octave down", FxClass::Pitch},                          // D4
    {2, "Gat", "Gate (1-8 = eighths, 9+ = ticks cut)", FxClass::Time},  // D5
    {2, "Ins", "Instrument", FxClass::Instrument},                      // D6
    {2, "Nop", "(no effect)", FxClass::Misc},                           // D7
    {2, "Nop", "(no effect)", FxClass::Misc},                           // D8
    {2, "Vol", "Volume", FxClass::Volume},                              // D9
    {2, "Pan", "Panning", FxClass::Panning},                            // DA
    {2, "Phs", "Reverse phase", FxClass::Sys1},                         // DB
    {2, "Vl+", "Volume relative", FxClass::Volume},                     // DC
    {2, "Lp[", "Repeat start (count)", FxClass::Song, 0, true},         // DD
    {1, "Lp]", "Repeat end", FxClass::Song, 0, true},                   // DE
    {3, "Cal", "Call subroutine", FxClass::Song, 1, true},              // DF
    {3, "Jmp", "Jump", FxClass::Song, 1},                               // E0
    {2, "Tun", "Tuning", FxClass::Pitch},                               // E1
    {3, "Vib", "Vibrato (rate, depth)", FxClass::Pitch},                // E2
    {2, "VbD", "Vibrato delay", FxClass::Pitch},                        // E3
    {3, "EcV", "Echo volume", FxClass::Sys1},                           // E4
    {4, "EcP", "Echo parameters (delay, feedback, filter)", FxClass::Sys1},   // E5
    {1, "EcO", "Echo on", FxClass::Sys1},                               // E6
    {2, "Trn", "Transpose", FxClass::Pitch},                            // E7
    {2, "Tr+", "Transpose relative", FxClass::Pitch},                   // E8
    {4, "PEn", "Pitch attack envelope (speed, depth, direction)", FxClass::Pitch},   // E9
    {1, "PEf", "Pitch attack envelope off", FxClass::Pitch},            // EA
    {1, "LpP", "Loop point", FxClass::Song},                            // EB
    {1, "LpJ", "Jump to loop point", FxClass::Song},                    // EC
    {1, "Lp1", "Loop point (first pass only)", FxClass::Song},          // ED
    {2, "VlT", "Volume from table", FxClass::Volume},                   // EE
    {3, "???", "Unknown", FxClass::Misc},                               // EF
    {2, "???", "Unknown", FxClass::Misc},                               // F0
    {3, "Por", "Portamento (time, depth)", FxClass::Pitch},             // F1
    {1, "Nop", "(no effect)", FxClass::Misc},                           // F2
    {1, "Nop", "(no effect)", FxClass::Misc},                           // F3
    {1, "Nop", "(no effect)", FxClass::Misc},                           // F4
    {1, "Nop", "(no effect)", FxClass::Misc},                           // F5
    {1, "Nop", "(no effect)", FxClass::Misc},                           // F6
    {1, "Nop", "(no effect)", FxClass::Misc},                           // F7
    {1, "Nop", "(no effect)", FxClass::Misc},                           // F8
    {1, "Nop", "(no effect)", FxClass::Misc},                           // F9
    {1, "Nop", "(no effect)", FxClass::Misc},                           // FA
    {1, "Nop", "(no effect)", FxClass::Misc},                           // FB
    {1, "Nop", "(no effect)", FxClass::Misc},                           // FC
    {1, "Nop", "(no effect)", FxClass::Misc},                           // FD
    {2, "Sub", "Sub-command", FxClass::Sys2},                           // FE
    {1, "End", "End / return", FxClass::Song},                          // FF
};
const CmdSpec kUnk1 = {2, "???", "Unknown", FxClass::Misc};
const CmdSpec kVib3 = {4, "Vib", "Vibrato", FxClass::Pitch};
const CmdSpec kNop = {1, "Nop", "(no effect)", FxClass::Misc};

// FE xx: bytes after the sub-opcode.
int sub_args(int version, uint8_t sub) {
    if (sub <= 0x09) return 0;
    if (version < 2) return 0;
    if (sub == 0x0D) return 1;
    if (sub >= 0x10 && sub <= 0x13) return 2;
    if (sub >= 0x14 && sub <= 0x19) return 2;
    if (sub >= 0x1A && sub <= 0x1E) return 1;
    return 0;
}
}

Layout detect_layout(const uint8_t* ram) {
    Layout L;
    const int lens[] = {0xC0, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01};
    if (find_pattern(ram, 0x200, 0x8000, lens, 8) < 0) return Layout{};
    const int v12[] = {0xE5, W, W, 0xEC, W, W, 0xDA, W, 0xE5, W, W, 0xEC, W, W, 0xDA, W, 0xE5, W, W, 0xEC, W, W, 0xDA, W, 0xE5, W, W, 0xC5, W, W, 0xE5, W, W, 0xC4, W};
    const int v0[] = {0xF6, W, W, 0xC4, W, 0xFC, 0xF6, W, W, 0xC4, W, 0x2F, W, 0xF6, W, W, 0xC4, W, 0xFC, 0xF6, W, W, 0xC4, W, 0x2F, W, 0xF6, W, W, 0xC5, W, W};
    if (int p = find_pattern(ram, 0x200, 0x4000, v12, 35); p >= 0) {
        const uint16_t engine = rd16(ram, p + 1);
        L.version = engine == 0x07C2 ? 1 : 2;
        const uint16_t table = rd16(ram, engine);
        if (table + 2 > 0x10000) return Layout{};
        L.song_list = rd16(ram, table);
    } else if (int q = find_pattern(ram, 0x200, 0x4000, v0, 32); q >= 0) {
        L.version = 0;
        const uint16_t table = rd16(ram, ram[q + 4]);
        if (table + 2 > 0x10000) return Layout{};
        L.song_list = rd16(ram, table);
    } else return Layout{};
    if (L.song_list < 0x200) return Layout{};
    const int tr[] = {0xE8, 0x00, 0xD4, W, 0xD4, W, 0xD5, W, W, 0x4B, W, 0x90, W, 0xFC, 0xF7, W, 0xD5, W, W, 0xD5, W, W, 0xFC, 0xF7, W, 0xD5, W, W, 0xD5, W, W};
    int t = find_pattern(ram, 0x200, 0x4000, tr, 31);
    if (t < 0) return Layout{};
    L.ptr_lo = rd16(ram, t + 17); L.loop_lo = rd16(ram, t + 20);
    L.ptr_hi = rd16(ram, t + 26); L.loop_hi = rd16(ram, t + 29);
    const int disp[] = {0xA8, 0xD0, 0x1C, 0x5D, 0x1F, W, W};
    if (int d = find_pattern(ram, 0x200, 0x4000, disp, 7); d >= 0) L.cmd_table = rd16(ram, d + 5);
    if (L.cmd_table) {
        const int h = rd16(ram, L.cmd_table + 2);   // D1 tempo
        const int st[] = {0x65, W, W, 0xF0, W, 0xC5, W, W};
        if (int s = find_pattern(ram, h, h + 16, st, 8); s >= 0) L.tempo_addr = rd16(ram, s + 1);
        const int hs = rd16(ram, L.cmd_table + 0x1F * 2);   // DF call: the stack helper it calls
        const int call[] = {0x3F, W, W};
        if (int c = find_pattern(ram, hs, hs + 6, call, 3); c >= 0) {
            const int helper = rd16(ram, c + 1);
            const int stk[] = {0xF5, W, W, 0xC4, W, 0xF5, W, W, 0xC4, W, 0xF5, W, W, 0xFD};
            if (find_pattern(ram, helper, helper + 14, stk, 14) == helper) { L.stack_lo = rd16(ram, helper + 1); L.stack_hi = rd16(ram, helper + 6); L.stack_sp = rd16(ram, helper + 11); }
        }
    }
    return L;
}

std::unique_ptr<seq::Driver> detect(const uint8_t* ram) {
    Layout L = detect_layout(ram);
    if (!L.valid()) return nullptr;
    auto d = std::make_unique<HudsonDriver>(L);
    return d;
}

std::string HudsonDriver::name() const {
    static const char* const names[] = {"Super Bomberman 2", "Super Bomberman 3", "Super Bomberman 4 / Tengai Makyou Zero"};
    return std::string("Hudson SFX SOUND DRIVER v") + char('0' + L.version) + " (" + names[L.version] + ")";
}

const CmdSpec& HudsonDriver::spec(uint8_t op) const {
    if (op < 0xD0) return kNop;
    if (L.version == 2) {
        if (op == 0xD7 || op == 0xD8 || op == 0xEE) return kNop;
        if (op == 0xE2) return kVib3;
        if (op == 0xF2 || op == 0xF3) return kUnk1;
    }
    return kCmds[op - 0xD0];
}

bool HudsonDriver::parse_song_header(const uint8_t* ram, uint16_t addr, SongInfo& info) const {
    info = SongInfo{};
    int p = addr;
    auto read_tracks = [&]() {
        const uint8_t bits = ram[p & 0xFFFF];
        ++p;
        for (int v = 0; v < 8; ++v)
            if (bits & (1 << v)) { info.tracks[v] = rd16(ram, p); info.slots[v] = uint16_t(p); p += 2; }
        return bits != 0;
    };
    if (L.version < 2 && !read_tracks()) return false;
    for (int guard = 0; guard < 32; ++guard) {
        const uint8_t tag = ram[p & 0xFFFF];
        ++p;
        if (tag == 0) break;
        if (L.version < 2) {
            switch (tag) {
                case 1: info.shift = ram[p & 0xFFFF] & 3; ++p; break;
                case 2: { int n = ram[p & 0xFFFF]; ++p; info.ins_table = uint16_t(p); info.ins_count = n / 4; p += n; break; }
                case 3: { int n = ram[p & 0xFFFF]; ++p; p += n; break; }
                case 4: { int n = ram[p & 0xFFFF]; ++p; info.ins_table = uint16_t(p); info.ins_count = n; p += n * 4; break; }
                case 5: { int n = ram[p & 0xFFFF]; ++p; p += n * 2; break; }
                default: return false;
            }
        } else {
            switch (tag) {
                case 1: if (!read_tracks()) return false; break;
                case 2: info.shift = ram[p & 0xFFFF] & 3; ++p; break;
                case 3: { int n = ram[p & 0xFFFF]; ++p; info.ins_table = uint16_t(p); info.ins_count = n; p += n * 4; break; }
                case 4: { int n = ram[p & 0xFFFF]; ++p; p += n * 4; break; }
                case 5: case 6: case 9: { int n = ram[p & 0xFFFF]; ++p; p += n * 2; break; }
                case 7: { bool def = ram[p & 0xFFFF] != 0; ++p; if (!def) p += 6; break; }
                case 8: info.velocity = ram[p & 0xFFFF] != 0; ++p; break;
                default: return false;
            }
        }
        if (p > 0xFFFF) return false;
    }
    info.end = uint16_t(p);
    bool any = false;
    for (int v = 0; v < 8; ++v) if (info.tracks[v]) any = true;
    if (any) infos_[addr] = info;
    return any;
}

std::vector<uint16_t> HudsonDriver::song_headers(const uint8_t* ram) const {
    std::vector<uint16_t> out;
    int cutoff = 0x10000;
    for (int i = 0; i < 0x80; ++i) {
        const int at = L.song_list + i * 2;
        if (at + 2 > 0x10000 || at >= cutoff) break;
        const uint16_t h = rd16(ram, at);
        if (h && h < cutoff) cutoff = h;
        SongInfo info;
        if (h >= 0x200 && parse_song_header(ram, h, info)) out.push_back(h);
    }
    if (out.empty() && L.loop_lo) {
        // The list was overwritten (a rip taken after other uploads):
        // look for a header whose tracks start where the voices loop.
        uint16_t loops[8];
        for (int v = 0; v < 8; ++v) loops[v] = uint16_t(ram[(L.loop_lo + v) & 0xFFFF] | (ram[(L.loop_hi + v) & 0xFFFF] << 8));
        for (int a = 0x200; a < 0xFF00; ++a) {
            if (ram[a] != (L.version < 2 ? ram[a] : 0x01)) continue;
            SongInfo info;
            if (!parse_song_header(ram, uint16_t(a), info)) continue;
            int hits = 0;
            for (int v = 0; v < 8; ++v) if (info.tracks[v] && info.tracks[v] == loops[v]) ++hits;
            if (hits >= 3) { out.push_back(uint16_t(a)); break; }
        }
    }
    return out;
}

uint16_t HudsonDriver::track_start(const uint8_t* ram, uint16_t header, int v) const {
    SongInfo info;
    if (!parse_song_header(ram, header, info)) return 0;
    return info.tracks[v] >= 0x200 ? info.tracks[v] : 0;
}

State HudsonDriver::initial_state(const uint8_t* ram, uint16_t header, int voice) const {
    (void)voice;
    State s;
    SongInfo info;
    if (parse_song_header(ram, header, info)) {
        shift_ = info.shift; velocity_ = info.velocity;
        ins_table_ = info.ins_table; ins_count_ = info.ins_count;
        int lo = header;
        for (int v = 0; v < 8; ++v) if (info.tracks[v]) lo = std::min(lo, int(info.tracks[v]));
        data_lo_ = std::min(data_lo_ == 0x200 ? lo : data_lo_, lo);
    }
    s.x[7] = shift_ + 1;
    s.flags = uint8_t(2 | (velocity_ ? 1 : 0));
    s.oct = 2;
    return s;
}

std::string HudsonDriver::song_label(const uint8_t* ram, uint16_t header, const seq::Pattern& p, int index) const {
    (void)ram;
    bool loops = false;
    for (int v = 0; v < 8; ++v) if (p.tracks[v].loops) loops = true;
    char b[64];
    std::snprintf(b, sizeof b, "song %d @%04X (%d ticks%s)", index, header, p.length_ticks, loops ? ", loops" : "");
    return b;
}

int HudsonDriver::note_ticks(uint8_t byte, int shift) const {
    const int idx = byte & 7;
    if (!idx) return 0;
    return kLens[std::min(idx - 1 + shift, 7)];
}

void HudsonDriver::decode(const uint8_t* p, int pc, State& s, Event& e, Flow& f) const {
    (void)pc;
    const int shift = s.x[7] ? s.x[7] - 1 : shift_;
    const bool velocity = (s.flags & 2) ? (s.flags & 1) != 0 : velocity_;
    const uint8_t b = p[0];
    e.b[0] = b;
    e.size = 1;
    e.pitch = -1;
    e.type = EventType::Command;
    if (b < 0xD0) {
        int n = 1;
        int len = note_ticks(b, shift);
        if (!(b & 7)) len = p[n++];
        if (velocity) ++n;
        const int key = b >> 4;
        e.type = key ? EventType::Note : EventType::Rest;
        e.size = uint8_t(n);
        for (int i = 1; i < n; ++i) e.b[i] = p[i];
        e.duration = len;
        if (key) e.pitch = s.oct * 12 + key - 1 + s.trans - 12;
        return;
    }
    const CmdSpec& sp = spec(b);
    e.size = sp.size;
    if (b == 0xFE) e.size = uint8_t(2 + sub_args(L.version, p[1]));
    for (int i = 1; i < e.size; ++i) e.b[i] = p[i];
    switch (b) {
        case 0xD2: s.oct = p[1]; break;
        case 0xD3: ++s.oct; break;
        case 0xD4: --s.oct; break;
        case 0xDD: f.kind = Flow::RepStart; f.count = p[1] ? p[1] : 256; break;
        case 0xDE: f.kind = Flow::RepEnd; break;
        case 0xDF: f.kind = Flow::Call; f.target = p[1] | (p[2] << 8); f.count = 1; break;
        case 0xE0: f.kind = Flow::Jump; f.target = p[1] | (p[2] << 8); break;
        case 0xE7: s.trans = int8_t(p[1]); break;
        case 0xE8: s.trans += int8_t(p[1]); break;
        case 0xEB: s.x[0] = pc + 1; break;
        case 0xED: if (!s.x[1]) { s.x[0] = pc + 1; s.x[1] = 1; } break;
        case 0xEC: f.kind = Flow::Jump; f.target = s.x[0] ? s.x[0] : s.start; break;
        case 0xFE:
            if (p[1] == 0x0C) s.flags = uint8_t((s.flags | 2) & ~1);
            else if (p[1] == 0x15 || p[1] == 0x17 || p[1] == 0x19) { f.kind = Flow::Jump; f.target = p[2] | (p[3] << 8); f.count = 1; }
            break;
        case 0xFF: f.kind = Flow::Return; break;
        default: break;
    }
}

uint16_t HudsonDriver::live_ptr(const uint8_t* ram, const seq::Position& pos, int v) const {
    (void)pos;
    return uint16_t(ram[(L.ptr_lo + v) & 0xFFFF] | (ram[(L.ptr_hi + v) & 0xFFFF] << 8));
}

void HudsonDriver::live_state_writes(const uint8_t* ram, const seq::Position& pos, int voice, uint16_t ptr, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    (void)pos;
    int mapped = remap.find(live_ptr(ram, pos, voice));
    if (mapped >= 0) ptr = uint16_t(mapped);
    out.push_back({uint16_t(L.ptr_lo + voice), uint8_t(ptr & 0xFF)});
    out.push_back({uint16_t(L.ptr_hi + voice), uint8_t(ptr >> 8)});
    if (L.loop_lo) {
        int loop = remap.find(uint16_t(ram[(L.loop_lo + voice) & 0xFFFF] | (ram[(L.loop_hi + voice) & 0xFFFF] << 8)));
        if (loop >= 0) { out.push_back({uint16_t(L.loop_lo + voice), uint8_t(loop & 0xFF)}); out.push_back({uint16_t(L.loop_hi + voice), uint8_t(loop >> 8)}); }
    }
    if (L.stack_lo) {
        const uint16_t base = uint16_t(ram[(L.stack_lo + voice) & 0xFFFF] | (ram[(L.stack_hi + voice) & 0xFFFF] << 8));
        const int sp = ram[(L.stack_sp + voice) & 0xFFFF];
        for (int i = 0; i + 1 < sp; ++i) seq::remap_word(ram, uint16_t(base + i), remap, out);
    }
}

void HudsonDriver::track_pointer_writes(const seq::Song& song, int pattern_idx, int voice, uint16_t dest, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    (void)pattern_idx;
    auto it = infos_.find(song.order_addr);
    if (it == infos_.end() || !it->second.slots[voice]) return;
    const uint16_t at = it->second.slots[voice];
    out.push_back({at, uint8_t(dest & 0xFF)});
    out.push_back({uint16_t(at + 1), uint8_t(dest >> 8)});
}

bool HudsonDriver::transpose_event(Event& e, int semis) const {
    if (e.type != EventType::Note) return false;
    int key = (e.b[0] >> 4) - 1 + semis;
    if (key < 0 || key > 11) return false;
    e.b[0] = uint8_t(((key + 1) << 4) | (e.b[0] & 0x0F));
    if (e.pitch >= 0) e.pitch += semis;
    return true;
}

void HudsonDriver::apply_note_byte(Event& e, uint8_t byte) const {
    const int key = byte >> 4, old = e.b[0] >> 4;
    e.b[0] = uint8_t((key << 4) | (e.b[0] & 0x0F));
    e.type = key ? EventType::Note : EventType::Rest;
    if (key && e.pitch >= 0 && old) e.pitch += key - old;
    else if (key && e.pitch < 0) e.pitch = note_semitone(byte);
    else if (!key) e.pitch = -1;
}

bool HudsonDriver::enter_note(std::vector<Event>& ev, int tick, int semitone, int pattern_len) const {
    semitone += 12;
    seq::stream_prepare(*this, ev, tick, pattern_len);
    int idx = seq::stream_timed_covering(ev, tick, true);
    if (idx < 0) return false;
    if (ev[size_t(idx)].tick != tick) {
        if (ev[size_t(idx)].in_sub || !seq::stream_split_at(*this, ev, tick)) return false;
        idx = -1;
        for (size_t i = 0; i < ev.size(); ++i) if (ev[i].duration > 0 && !ev[i].in_sub && ev[i].tick == tick) { idx = int(i); break; }
        if (idx < 0) return false;
    }
    if (ev[size_t(idx)].in_sub) return false;
    const State s = state_before(ev, idx);
    semitone -= pitch_base(ev);
    int key = semitone - s.trans - s.oct * 12, oct = s.oct;
    if (key < 0 || key > 11) {
        oct = (semitone - s.trans) / 12;
        key = semitone - s.trans - oct * 12;
        if (oct < 0 || oct > 7 || key < 0) return false;
    }
    if (!set_note_at(ev, tick, uint8_t((key + 1) << 4), pattern_len)) return false;
    if (oct == s.oct) return true;
    idx = -1;
    for (size_t i = 0; i < ev.size(); ++i) if (ev[i].duration > 0 && !ev[i].in_sub && ev[i].tick == tick) { idx = int(i); break; }
    if (idx < 0) return false;
    bool later = false;
    for (size_t i = size_t(idx) + 1; i < ev.size(); ++i) if (ev[i].type == EventType::Note && !ev[i].in_sub) { later = true; break; }
    if (later) ev.insert(ev.begin() + idx + 1, stream::cmd_event(0xD2, s.oct));
    ev.insert(ev.begin() + idx, stream::cmd_event(0xD2, oct));
    retime(ev);
    return true;
}

bool HudsonDriver::set_duration(std::vector<Event>& ev, int i, int dur) const {
    if (i < 0 || i >= int(ev.size()) || dur < 1) return false;
    Event& e = ev[size_t(i)];
    if (e.type != EventType::Note && e.type != EventType::Rest) return false;
    const State s = state_before(ev, i);
    const int shift = s.x[7] ? s.x[7] - 1 : shift_;
    const bool velocity = (s.flags & 2) ? (s.flags & 1) != 0 : velocity_;
    const uint8_t vel = velocity ? e.b[e.size - 1] : 0;
    const uint8_t head = uint8_t(e.b[0] & 0xF8);
    std::vector<Event> pieces;
    int left = dur;
    while (left > 0) {
        int idx = 0, take = 0;
        for (int k = 1; k <= 7; ++k) if (kLens[std::min(k - 1 + shift, 7)] <= left) { idx = k; take = kLens[std::min(k - 1 + shift, 7)]; break; }
        if (!idx || take < left && left <= 255) { idx = 0; take = std::min(left, 255); }
        Event n = e;
        n.addr = 0;
        n.b[0] = uint8_t(head | idx);
        n.size = 1;
        if (!idx) n.b[n.size++] = uint8_t(take);
        if (velocity) n.b[n.size++] = vel;
        n.duration = take;
        if (!pieces.empty() && e.type == EventType::Note) pieces.back().b[0] |= 0x08;
        pieces.push_back(n);
        left -= take;
    }
    ev.erase(ev.begin() + i);
    ev.insert(ev.begin() + i, pieces.begin(), pieces.end());
    return true;
}

bool HudsonDriver::note_retriggers(const std::vector<Event>& ev, int i) const {
    if (i <= 0 || i >= int(ev.size()) || ev[size_t(i)].type != EventType::Note) return true;
    for (int k = i - 1; k >= 0; --k) {
        const Event& p = ev[size_t(k)];
        if (p.duration <= 0) continue;
        return !(p.type == EventType::Note && (p.b[0] & 0x08) && (p.b[0] >> 4) == (ev[size_t(i)].b[0] >> 4));
    }
    return true;
}

std::string HudsonDriver::event_text(const Event& e) const {
    char b[96];
    if (e.type == EventType::Note) { std::snprintf(b, sizeof b, "%s  %d ticks%s", note_name(e).c_str(), e.duration, (e.b[0] & 8) ? "  (held into the next note)" : ""); return b; }
    if (e.type == EventType::Rest) { std::snprintf(b, sizeof b, "rest  %d ticks", e.duration); return b; }
    if (e.type == EventType::Command) {
        switch (e.b[0]) {
            case 0xD1: std::snprintf(b, sizeof b, "Tempo %d BPM", e.b[1]); return b;
            case 0xD2: std::snprintf(b, sizeof b, "Octave %d", e.b[1]); return b;
            case 0xDD: std::snprintf(b, sizeof b, "Repeat x%d", e.b[1]); return b;
            case 0xDF: std::snprintf(b, sizeof b, "Call $%02X%02X", e.b[2], e.b[1]); return b;
            case 0xE0: std::snprintf(b, sizeof b, "Jump $%02X%02X", e.b[2], e.b[1]); return b;
            case 0xE7: std::snprintf(b, sizeof b, "Transpose %+d", int8_t(e.b[1])); return b;
            case 0xFE: {
                static const char* const subs[] = {"end", "echo off", "?", "percussion on", "percussion off", "vibrato type 0", "vibrato type 1", "vibrato type 2", "?", "?",
                                                   "?", "?", "note velocity off", "?", "nop", "nop", "mov reg, imm", "mov reg, reg", "cmp reg, imm", "cmp reg, reg",
                                                   "bne", "beq", "bcs", "bcc", "bmi", "bpl", "attack rate", "decay rate", "sustain level", "sustain rate", "release rate"};
                std::string s = std::string("Sub: ") + (e.b[1] < 31 ? subs[e.b[1]] : "?");
                for (int i = 2; i < e.size; ++i) { std::snprintf(b, sizeof b, " %02X", e.b[i]); s += b; }
                return s;
            }
            default: break;
        }
    }
    return seq::Driver::event_text(e);
}

bool HudsonDriver::pitch_fx(const Event& e, seq::PitchFx& out) const {
    if (e.type != EventType::Command) return false;
    out = seq::PitchFx{};
    if (e.b[0] == 0xE2) { out.kind = e.b[2] ? seq::PitchFx::Vibrato : seq::PitchFx::VibratoOff; out.rate = e.b[1]; out.depth = e.b[2]; return true; }
    if (e.b[0] == 0xF1) { out.kind = e.b[1] ? seq::PitchFx::Portamento : seq::PitchFx::SlideOff; out.length = e.b[1]; return true; }
    return false;
}

int HudsonDriver::instrument_count(const uint8_t* ram) const { (void)ram; return ins_table_ ? ins_count_ : 0; }

seq::Instrument HudsonDriver::read_instrument(const uint8_t* ram, int index) const {
    seq::Instrument in{};
    const uint8_t* e = ram + ((ins_table_ + index * 4) & 0xFFFF);
    in.srcn = e[0]; in.adsr0 = e[1]; in.adsr1 = e[2]; in.gain = e[3];
    in.pitch_hi = 0x10;
    return in;
}

bool HudsonDriver::preview_regs(const uint8_t* ram, uint8_t note_byte, int instrument, uint8_t regs[8]) const {
    if (!ins_table_ || instrument < 0 || instrument >= ins_count_) return false;
    const uint8_t* e = ram + ((ins_table_ + instrument * 4) & 0xFFFF);
    const int pv = std::min(int(4096.0 * std::pow(2.0, (note_semitone(note_byte) - 60) / 12.0) + 0.5), 0x3FFF);
    regs[0] = 0x40; regs[1] = 0x40;
    regs[2] = uint8_t(pv & 0xFF); regs[3] = uint8_t(pv >> 8);
    regs[4] = e[0]; regs[5] = e[1]; regs[6] = e[2]; regs[7] = e[3];
    return true;
}

double HudsonDriver::ticks_per_second(const uint8_t* ram) const {
    if (!L.tempo_addr) return 0;
    const int bpm = ram[L.tempo_addr];
    return bpm * (48 >> shift_) / 60.0;
}

bool HudsonDriver::tempo_writes(const uint8_t* ram, double tps, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    (void)ram;
    if (!L.tempo_addr || tps <= 0) return false;
    out.push_back({L.tempo_addr, uint8_t(std::clamp(int(tps * 60.0 / (48 >> shift_) + 0.5), 1, 255))});
    return true;
}

}
