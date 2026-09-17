#include "chun.hpp"

#include <algorithm>
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

namespace chun {
namespace {
const int W = 0x100;

const CmdSpec kCmds[0x25] = {
    {3, "Br2", "Repeat 2: skip on the last pass", FxClass::Song, 1, true},   // DB
    {3, "Lp2", "Repeat 2: play again", FxClass::Song, 1, true},              // DC
    {2, "RlS", "Release rate", FxClass::Sys2},                                // DD
    {4, "ADR", "ADSR + release rate", FxClass::Sys2},                         // DE
    {2, "Sur", "Surround", FxClass::Panning},                                 // DF
    {4, "CJp", "Conditional jump (address, value)", FxClass::Song, 1},        // E0
    {1, "Cnt", "Increment condition counter", FxClass::Song},                 // E1
    {2, "PEn", "Pitch envelope", FxClass::Pitch},                             // E2
    {1, "NzO", "Noise on", FxClass::Sys1},                                    // E3
    {1, "NzF", "Noise off", FxClass::Sys1},                                   // E4
    {3, "MVF", "Master volume fade", FxClass::Volume},                        // E5
    {3, "ExF", "Expression fade", FxClass::Volume},                           // E6
    {2, "FVF", "Full volume fade", FxClass::Volume},                          // E7
    {3, "PnF", "Pan fade", FxClass::Panning},                                 // E8
    {2, "Tun", "Tuning", FxClass::Pitch},                                     // E9
    {3, "Jmp", "Jump", FxClass::Song, 1},                                     // EA
    {2, "Tmp", "Tempo", FxClass::Speed},                                      // EB
    {2, "Gat", "Gate ratio", FxClass::Time},                                  // EC
    {2, "Vol", "Volume", FxClass::Volume},                                    // ED
    {2, "Pan", "Panning", FxClass::Panning},                                  // EE
    {3, "ADS", "ADSR", FxClass::Sys2},                                        // EF
    {2, "Ins", "Instrument", FxClass::Instrument},                            // F0
    {2, "???", "Unknown", FxClass::Misc},                                     // F1
    {1, "SyO", "Sync note length on", FxClass::Time},                         // F2
    {1, "SyF", "Sync note length off", FxClass::Time},                        // F3
    {3, "Lp1", "Play the section again once", FxClass::Song, 1, true},        // F4
    {4, "LpN", "Repeat the section n times", FxClass::Song, 2, true},         // F5
    {2, "Exp", "Expression", FxClass::Volume},                                // F6
    {1, "Nop", "(no effect)", FxClass::Misc},                                 // F7
    {3, "Cal", "Call subroutine", FxClass::Song, 1, true},                    // F8
    {1, "Ret", "Return", FxClass::Song},                                      // F9
    {2, "Trn", "Transpose", FxClass::Pitch},                                  // FA
    {3, "Sld", "Pitch slide (semitones, length)", FxClass::Pitch},            // FB
    {1, "EcO", "Echo on", FxClass::Sys1},                                     // FC
    {1, "EcF", "Echo off", FxClass::Sys1},                                    // FD
    {2, "Pre", "Load preset", FxClass::Misc},                                 // FE
    {1, "End", "End / return", FxClass::Song},                                // FF
};
const CmdSpec kGate = {1, "GtT", "Gate ratio from table", FxClass::Time};
const CmdSpec kNop = {1, "Nop", "(no effect)", FxClass::Misc};
const CmdSpec kSync = {1, "SyO", "Sync note length on", FxClass::Time};
const CmdSpec kUnk1 = {2, "???", "Unknown", FxClass::Misc};
}

Layout detect_layout(const uint8_t* ram) {
    Layout L;
    const int winter[] = {0x8F, W, 0xA0, 0x8F, W, 0xA1, 0x8D, 0x06, 0xCF, 0x7A, 0xA0, 0xDA, 0xA0};
    const int winter3[] = {0x8D, 0x06, 0xCF, 0x7A, 0xA0, 0xDA, 0xA0, 0x8D, 0x05, 0xF7, 0xA0, 0x08, 0x08, 0xD7, 0xA0};
    const int summer[] = {0xD5, W, W, 0xC9, W, W, 0x2D, 0xE5, W, W, 0xC4, 0xC0, 0xE5, W, W, 0xC4, 0xC1};
    if (int p = find_pattern(ram, 0x200, 0x4000, winter, 13); p >= 0) {
        L.song_list = uint16_t(ram[p + 1] | (ram[p + 4] << 8));
    } else if (int q = find_pattern(ram, 0x200, 0x4000, winter3, 15); q >= 0) {
        const int base[] = {0x8F, W, 0xA0, 0x8F, W, 0xA1};
        int r = find_pattern(ram, q - 40, q, base, 6);
        if (r < 0) return Layout{};
        L.song_list = uint16_t(ram[r + 1] | (ram[r + 4] << 8));
    } else if (int s = find_pattern(ram, 0x200, 0x4000, summer, 17); s >= 0) {
        L.summer = true;
        L.entry_size = 2; L.entry_header = 0;
        const uint16_t table = rd16(ram, s + 8);
        L.song_list = rd16(ram, table);
    } else return Layout{};
    if (L.song_list < 0x200) return Layout{};
    const int disp[] = {0xA8, W, 0x90, W, 0x4D, 0x1C, 0x5D, 0xF5, W, W};
    int d = find_pattern(ram, 0x200, 0x4000, disp, 10);
    if (d < 0) return Layout{};
    L.first_cmd = ram[d + 1];
    L.cmd_table = rd16(ram, d + 8);
    auto handler = [&](uint8_t op) { return op < L.first_cmd ? -1 : int(rd16(ram, L.cmd_table + (op - L.first_cmd) * 2)); };
    const int cond[] = {0xF5, W, W, 0xFD, 0xF6, W, W, 0x28, 0x7F};
    if (int h = handler(0xE0); h >= 0) if (int c = find_pattern(ram, h, h + 24, cond, 9); c >= 0) { L.slot_table = rd16(ram, c + 1); L.cond_base = rd16(ram, c + 5); }
    const int tempo[] = {0xF5, W, W, 0xFD, 0x3F, W, W, 0xD6, W, W};
    if (int h = handler(0xEB); h >= 0) if (int t = find_pattern(ram, h, h + 12, tempo, 10); t >= 0) { L.tempo_base = rd16(ram, t + 8); if (!L.slot_table) L.slot_table = rd16(ram, t + 1); }
    const int depth[] = {0xF5, W, W, 0x68, 0x06, 0xB0, W, 0x2D, 0x3F, W, W};
    if (int h = handler(0xF8); h >= 0) if (int c = find_pattern(ram, h, h + 24, depth, 11); c >= 0) {
        L.stack_depth = rd16(ram, c + 1);
        const int r = rd16(ram, c + 9);
        const int base[] = {0xE8, W, 0xC4, W, 0xE8, W, 0xC4, W, 0x7D, 0x8D, 0x03, 0xCF};
        if (find_pattern(ram, r, r + 12, base, 12) == r) L.stack_base = uint16_t(ram[r + 1] | (ram[r + 5] << 8));
    }
    return L;
}

std::unique_ptr<seq::Driver> detect(const uint8_t* ram) {
    Layout L = detect_layout(ram);
    if (!L.valid()) return nullptr;
    return std::make_unique<ChunDriver>(L);
}

const CmdSpec& ChunDriver::spec(uint8_t op) const {
    if (op < 0xA0) return kNop;
    if (op < 0xDB) return (!L.summer && op <= 0xB5) ? kGate : kNop;
    if (L.summer) {
        if (op == 0xDB || op == 0xDC) return kNop;
        if (op == 0xF1) return kSync;
        if (op == 0xF7) return kUnk1;
    }
    return kCmds[op - 0xDB];
}

std::vector<uint16_t> ChunDriver::song_headers(const uint8_t* ram) const {
    std::vector<uint16_t> out;
    for (int i = 0, misses = 0; i < 0x80 && misses < 8; ++i) {
        const int at = L.song_list + i * L.entry_size;
        if (at + L.entry_size > 0x10000) break;
        const uint16_t h = rd16(ram, at + L.entry_header);
        const int n = h >= 0x200 && h < 0xFF00 ? ram[h + 1] : 0;
        bool ok = n >= 1 && n <= 8;
        for (int v = 0; v < n && ok; ++v) ok = track_start(ram, h, v) != 0;
        if (!ok) { ++misses; continue; }
        misses = 0;
        out.push_back(h);
    }
    if (out.empty()) {
        // No list (songs uploaded straight into slots): find a header whose
        // tracks lead up to where the voices are playing.
        uint16_t live[8];
        for (int v = 0; v < 8; ++v) live[v] = rd16(ram, L.ptr_zp + v * 2);
        int best = -1, best_n = 1;
        for (int h = 0x200; h < 0xFF00; ++h) {
            const int n = ram[h + 1];
            if (n < 2 || n > 8 || !ram[h]) continue;
            bool ok = true;
            for (int v = 0; v < n && ok; ++v) {
                const uint16_t a = track_start(ram, uint16_t(h), v);
                ok = a && live[v] >= a && live[v] < a + 0x800;
            }
            if (ok && n > best_n) { best = h; best_n = n; }
        }
        if (best >= 0) out.push_back(uint16_t(best));
    }
    return out;
}

uint16_t ChunDriver::track_start(const uint8_t* ram, uint16_t header, int v) const {
    const int n = ram[(header + 1) & 0xFFFF];
    if (v >= n || n > 8) return 0;
    const uint16_t raw = rd16(ram, header + 2 + v * 2);
    const uint16_t a = L.summer ? raw : uint16_t(header + raw);
    return a >= 0x200 ? a : 0;
}

State ChunDriver::initial_state(const uint8_t* ram, uint16_t header, int voice) const {
    State s;
    if (L.slot_table && L.cond_base) s.x[2] = ram[(L.cond_base + ram[(L.slot_table + voice * 2) & 0xFFFF]) & 0xFFFF];
    data_lo_ = std::min(data_lo_ == 0x200 ? int(header) : data_lo_, int(header));
    for (int v = 0; v < 8; ++v) if (uint16_t a = track_start(ram, header, v)) data_lo_ = std::min(data_lo_, int(a));
    return s;
}

void ChunDriver::decode(const uint8_t* p, int pc, State& s, Event& e, Flow& f) const {
    const uint8_t b = p[0];
    e.b[0] = b;
    e.size = 1;
    e.pitch = -1;
    e.type = EventType::Command;
    if (b < 0xA0) {
        int idx = b;
        if ((s.flags & 1) && s.voice > 0) {   // synced: the previous voice's length at this tick
            const auto& tl = lens_[s.voice - 1];
            for (size_t k = 0; k < tl.size(); ++k) if (tl[k].first <= s.tick && (k + 1 == tl.size() || tl[k + 1].first > s.tick)) { s.len = tl[k].second; break; }
        }
        if (b >= 0x50) { s.len = p[1]; e.b[1] = p[1]; e.size = 2; idx -= 0x50; }
        e.type = idx == 0 ? EventType::Rest : idx == 0x4F ? EventType::Tie : EventType::Note;
        e.duration = s.len;
        if (e.type == EventType::Note) e.pitch = idx - 1 - 12 + s.trans;
        return;
    }
    const CmdSpec& sp = spec(b);
    e.size = sp.size;
    for (int i = 1; i < e.size; ++i) e.b[i] = p[i];
    auto rel = [&](int at) { return (pc + at + 2 + int16_t(p[at] | (p[at + 1] << 8))) & 0xFFFF; };
    switch (b) {
        case 0xDB: if (L.summer) break; if (s.x[1]) { f.kind = Flow::Jump; f.target = rel(1); f.count = 1; } break;
        case 0xDC: if (L.summer) break; if (!s.x[1]) s.x[1] = 2; if (--s.x[1]) { f.kind = Flow::Jump; f.target = rel(1); f.count = 1; } break;
        case 0xE0: if ((s.x[2] & 0x7F) == p[3]) { f.kind = Flow::Jump; f.target = rel(1); f.count = 1; } else s.x[2] |= 0x80; break;
        case 0xE1: ++s.x[3]; break;
        case 0xEA: f.kind = Flow::Jump; f.target = rel(1); break;
        case 0xF4: if (!s.x[0]) s.x[0] = 2; if (--s.x[0]) { f.kind = Flow::Jump; f.target = rel(1); f.count = 1; } break;
        case 0xF5: if (!s.x[0]) s.x[0] = p[1]; if (--s.x[0]) { f.kind = Flow::Jump; f.target = rel(2); f.count = 1; } break;
        case 0xF8: f.kind = Flow::Call; f.target = rel(1); f.count = 1; break;
        case 0xF9: f.kind = Flow::Return; f.count = 1; break;
        case 0xFF: f.kind = Flow::Return; break;
        case 0xFA: s.trans = int8_t(p[1]); break;
        case 0xF2: s.flags |= 1; break;
        case 0xF3: s.flags &= uint8_t(~1); break;
        case 0xF1: if (L.summer) s.flags |= 1; break;
        case 0xEC: s.ratio = p[1]; break;
        default: if (b >= 0xA0 && b <= 0xB5 && !L.summer) s.ratio = 1; break;
    }
}

void ChunDriver::track_parsed(int v, const seq::Track& t) const {
    lens_[v].clear();
    for (const Event& e : t.events) if (e.duration > 0) lens_[v].push_back({e.tick, e.duration});
}

int ChunDriver::jump_target(const Event& e) const {
    if (e.type != EventType::Command || !e.addr) return -1;
    const CmdSpec& sp = spec(e.b[0]);
    if (!sp.addr_at) return -1;
    return (e.addr + sp.addr_at + 2 + int16_t(e.b[sp.addr_at] | (e.b[sp.addr_at + 1] << 8))) & 0xFFFF;
}

void ChunDriver::set_jump_target(Event& e, uint16_t addr) const {
    const CmdSpec& sp = spec(e.b[0]);
    if (!sp.addr_at || !e.addr) return;
    const int rel = int(addr) - int(e.addr + sp.addr_at + 2);
    e.b[sp.addr_at] = uint8_t(rel & 0xFF);
    e.b[sp.addr_at + 1] = uint8_t((rel >> 8) & 0xFF);
}

std::vector<uint8_t> ChunDriver::serialize_relocated(const std::vector<Event>& ev, uint16_t dest, std::vector<int>* offsets) const {
    std::vector<int> off;
    std::vector<uint8_t> out = stream::Driver::serialize_relocated(ev, dest, &off);
    // Relative jumps: re-encode from the command's new address.
    for (size_t i = 0; i < ev.size(); ++i) {
        if (ev[i].in_sub || off[i] < 0 || ev[i].type != EventType::Command) continue;
        const CmdSpec& sp = spec(ev[i].b[0]);
        if (!sp.addr_at) continue;
        const int t = jump_target(ev[i]);
        const int j = relocated_target(ev, int(i), off);
        if (t < 0 && j < 0) continue;
        const int target = j >= 0 ? dest + off[size_t(j)] : t;
        const int rel = target - int(dest + off[i] + sp.addr_at + 2);
        out[size_t(off[i] + sp.addr_at)] = uint8_t(rel & 0xFF);
        out[size_t(off[i] + sp.addr_at + 1)] = uint8_t((rel >> 8) & 0xFF);
    }
    if (offsets) *offsets = off;
    return out;
}

void ChunDriver::track_pointer_writes(const seq::Song& song, int pattern_idx, int voice, uint16_t dest, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    (void)pattern_idx;
    const uint16_t at = uint16_t(song.order_addr + 2 + voice * 2);
    const uint16_t raw = L.summer ? dest : uint16_t(dest - song.order_addr);
    out.push_back({at, uint8_t(raw & 0xFF)});
    out.push_back({uint16_t(at + 1), uint8_t(raw >> 8)});
}

void ChunDriver::live_extra_writes(const uint8_t* ram, int voice, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    if (!L.stack_base || !L.stack_depth) return;
    const int depth = ram[(L.stack_depth + voice * 2) & 0xFFFF];
    for (int k = 0; k + 1 < depth && k < 6; k += 2) seq::remap_word(ram, uint16_t(L.stack_base + voice * 6 + k), remap, out);
}

bool ChunDriver::transpose_event(Event& e, int semis) const {
    if (e.type != EventType::Note) return false;
    const int idx = (e.b[0] % 0x50) + semis;
    if (idx < 1 || idx > 0x4E) return false;
    e.b[0] = uint8_t((e.b[0] >= 0x50 ? 0x50 : 0) + idx);
    if (e.pitch >= 0) e.pitch += semis;
    return true;
}

void ChunDriver::apply_note_byte(Event& e, uint8_t byte) const {
    const int idx = byte % 0x50, old = e.b[0] % 0x50;
    e.b[0] = uint8_t((e.b[0] >= 0x50 ? 0x50 : 0) + idx);
    e.type = idx == 0 ? EventType::Rest : idx == 0x4F ? EventType::Tie : EventType::Note;
    if (e.type == EventType::Note) {
        if (e.pitch >= 0 && old >= 1 && old <= 0x4E) e.pitch += idx - old;
        else if (e.pitch < 0) e.pitch = note_semitone(byte);
    } else e.pitch = -1;
}

bool ChunDriver::set_duration(std::vector<Event>& ev, int i, int dur) const {
    if (i < 0 || i >= int(ev.size()) || dur < 1) return false;
    Event& e = ev[size_t(i)];
    if (e.duration <= 0) return false;
    // Notes without a length byte use the last one: give the next such note its own.
    for (size_t k = size_t(i) + 1; k < ev.size(); ++k) {
        Event& n = ev[k];
        if (n.duration <= 0) continue;
        if (n.b[0] >= 0x50) break;
        if (n.in_sub) return false;
        n.b[1] = uint8_t(n.duration); n.b[0] = uint8_t(n.b[0] + 0x50); n.size = 2; n.addr = 0;
        break;
    }
    if (e.b[0] < 0x50) { e.b[0] = uint8_t(e.b[0] + 0x50); e.size = 2; }
    const int first = std::min(dur, 255);
    e.b[1] = uint8_t(first);
    e.duration = first;
    e.addr = 0;
    std::vector<Event> tail;
    for (int left = dur - first; left > 0;) {
        const int n = std::min(left, 255);
        Event t{};
        t.type = e.type == EventType::Rest ? EventType::Rest : EventType::Tie;
        t.b[0] = uint8_t(0x50 + (t.type == EventType::Rest ? 0 : 0x4F)); t.b[1] = uint8_t(n); t.size = 2; t.duration = n;
        tail.push_back(t);
        left -= n;
    }
    ev.insert(ev.begin() + i + 1, tail.begin(), tail.end());
    return true;
}

bool ChunDriver::note_retriggers(const std::vector<Event>& ev, int i) const {
    if (i <= 0 || i >= int(ev.size()) || ev[size_t(i)].type != EventType::Note) return true;
    for (int k = i - 1; k >= 0; --k) {
        const Event& p = ev[size_t(k)];
        if (p.duration <= 0) continue;
        if (p.type == EventType::Tie) return false;
        if (p.type != EventType::Note) return true;
        return !(state_before(ev, k + 1).ratio == 0 && (p.b[0] % 0x50) == (ev[size_t(i)].b[0] % 0x50));
    }
    return true;
}

std::string ChunDriver::event_text(const Event& e) const {
    char b[96];
    if (e.type == EventType::Note) { std::snprintf(b, sizeof b, "%s  %d ticks%s", note_name(e).c_str(), e.duration, e.size == 2 ? "" : " (last length)"); return b; }
    if (e.type == EventType::Rest) { std::snprintf(b, sizeof b, "rest  %d ticks", e.duration); return b; }
    if (e.type == EventType::Tie) { std::snprintf(b, sizeof b, "tie  %d ticks", e.duration); return b; }
    if (e.type == EventType::Command) {
        const int t = jump_target(e);
        switch (e.b[0]) {
            case 0xEB: std::snprintf(b, sizeof b, "Tempo %d", e.b[1]); return b;
            case 0xFA: std::snprintf(b, sizeof b, "Transpose %+d", int8_t(e.b[1])); return b;
            case 0xEA: case 0xF4: case 0xF8: case 0xDB: case 0xDC:
                if (t >= 0) { std::snprintf(b, sizeof b, "%s $%04X", cmd_name(e.b[0]), t); return b; }
                break;
            case 0xF5: if (t >= 0) { std::snprintf(b, sizeof b, "Repeat x%d  $%04X", e.b[1], t); return b; } break;
            case 0xE0: if (t >= 0) { std::snprintf(b, sizeof b, "Jump $%04X when condition = %d", t, e.b[3]); return b; } break;
            default: break;
        }
    }
    return seq::Driver::event_text(e);
}

bool ChunDriver::pitch_fx(const Event& e, seq::PitchFx& out) const {
    if (e.type != EventType::Command || e.b[0] != 0xFB) return false;
    out = seq::PitchFx{};
    out.kind = seq::PitchFx::Slide; out.delta = int8_t(e.b[1]); out.length = e.b[2]; out.next_note = true;
    return true;
}

uint16_t ChunDriver::tempo_addr(const uint8_t* ram) const {
    if (!L.tempo_base) return 0;
    const int slot = L.slot_table ? ram[L.slot_table] : 0;
    return uint16_t(L.tempo_base + slot);
}

double ChunDriver::ticks_per_second(const uint8_t* ram) const {
    const uint16_t at = tempo_addr(ram);
    return at ? ram[at] * 48 / 60.0 : 0;
}

bool ChunDriver::tempo_writes(const uint8_t* ram, double tps, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    const uint16_t at = tempo_addr(ram);
    if (!at || tps <= 0) return false;
    out.push_back({at, uint8_t(std::clamp(int(tps * 60.0 / 48 + 0.5), 1, 255))});
    return true;
}

}
