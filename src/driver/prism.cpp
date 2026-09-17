#include "prism.hpp"

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

namespace prism {
namespace {
const int W = 0x100;

const CmdSpec kCmds[0x40] = {
    {2, "Tmp", "Tempo", FxClass::Speed},                                  // C0
    {2, "Tmp", "Tempo", FxClass::Speed},                                  // C1
    {2, "Tmp", "Tempo", FxClass::Speed},                                  // C2
    {2, "Tmp", "Tempo", FxClass::Speed},                                  // C3
    {2, "Tmp", "Tempo", FxClass::Speed},                                  // C4
    {3, "CJp", "Jump when the condition is set", FxClass::Song, 1},       // C5
    {1, "Cnd", "Set the condition", FxClass::Song},                       // C6
    {2, "???", "Unknown", FxClass::Misc},                                 // C7
    {3, "???", "Unknown", FxClass::Misc},                                 // C8
    {3, "???", "Unknown", FxClass::Misc},                                 // C9
    {1, "EcR", "Restore echo parameters", FxClass::Sys1},                 // CA
    {1, "EcS", "Save echo parameters", FxClass::Sys1},                    // CB
    {2, "???", "Unknown", FxClass::Misc},                                 // CC
    {1, "SlF", "Slur off", FxClass::Time},                                // CD
    {1, "SlO", "Slur on", FxClass::Time},                                 // CE
    {3, "VEn", "Volume envelope (address)", FxClass::Volume},             // CF
    {1, "PT1", "Default pan table 1", FxClass::Panning},                  // D0
    {1, "PT2", "Default pan table 2", FxClass::Panning},                  // D1
    {1, "???", "Unknown", FxClass::Misc},                                 // D2
    {1, "P3+", "Increment APU port 3", FxClass::Sys2},                    // D3
    {1, "P2+", "Increment APU port 2", FxClass::Sys2},                    // D4
    {2, "Sg3", "Play song (slot 3)", FxClass::Song},                      // D5
    {2, "Sg2", "Play song (slot 2)", FxClass::Song},                      // D6
    {2, "Sg1", "Play song (slot 1)", FxClass::Song},                      // D7
    {2, "Tr+", "Transpose (relative)", FxClass::Pitch},                   // D8
    {4, "PnE", "Pan envelope (address, speed)", FxClass::Panning},        // D9
    {3, "PnT", "Pan table (address)", FxClass::Panning},                  // DA
    {3, "Nop", "(no effect)", FxClass::Misc},                             // DB
    {1, "DLF", "Default length off", FxClass::Time},                      // DC
    {2, "DLn", "Default length", FxClass::Time},                          // DD
    {4, "Lp1", "Repeat: jump back until played n times", FxClass::Song, 2, true},   // DE
    {4, "Lp2", "Repeat 2: jump back until played n times", FxClass::Song, 2, true}, // DF
    {1, "Ret", "Return", FxClass::Song},                                  // E0
    {3, "Cal", "Call subroutine", FxClass::Song, 1, true},                // E1
    {3, "Jmp", "Jump", FxClass::Song, 1},                                 // E2
    {2, "Trn", "Transpose", FxClass::Pitch},                              // E3
    {2, "Tun", "Tuning", FxClass::Pitch},                                 // E4
    {2, "VbD", "Vibrato delay", FxClass::Pitch},                          // E5
    {1, "VbF", "Vibrato off", FxClass::Pitch},                            // E6
    {4, "Vib", "Vibrato (delay, ...)", FxClass::Pitch},                   // E7
    {2, "???", "Unknown", FxClass::Misc},                                 // E8
    {3, "Sld", "Slide note (from, to, then a length)", FxClass::Pitch},    // E9
    {2, "Vl+", "Volume (relative)", FxClass::Volume},                     // EA
    {2, "Pan", "Panning", FxClass::Panning},                              // EB
    {2, "Vol", "Volume", FxClass::Volume},                                // EC
    {4, "???", "Unknown", FxClass::Misc},                                 // ED
    {1, "Rst", "Rest", FxClass::Time},                                    // EE
    {3, "GER", "Gain envelope (rest)", FxClass::Volume},                  // EF
    {3, "GDT", "Gain envelope decay time", FxClass::Volume},              // F0
    {1, "MGF", "Manual gates off", FxClass::Time},                        // F1
    {1, "MGO", "Manual gates on", FxClass::Time},                         // F2
    {2, "AGT", "Auto gate threshold", FxClass::Time},                     // F3
    {1, "Tie", "Tie with length", FxClass::Time},                         // F4
    {1, "Tie", "Tie", FxClass::Time},                                     // F5
    {3, "GES", "Gain envelope (sustain)", FxClass::Volume},               // F6
    {3, "EVE", "Echo volume envelope", FxClass::Sys1},                    // F7
    {4, "EcV", "Echo volume (L, R, mono)", FxClass::Sys1},                // F8
    {1, "EcF", "Echo off", FxClass::Sys1},                                // F9
    {1, "EcO", "Echo on", FxClass::Sys1},                                 // FA
    {5, "EcP", "Echo parameters", FxClass::Sys1},                         // FB
    {3, "ADS", "ADSR (a1, a2) / instrument (i < 80)", FxClass::Sys2},     // FC
    {3, "GED", "Gain envelope (decay)", FxClass::Volume},                 // FD
    {2, "Ins", "Instrument", FxClass::Instrument},                        // FE
    {1, "End", "End of track", FxClass::Song},                            // FF
};
const CmdSpec kPan = {1, "PT1", "Default pan table", FxClass::Panning};
const CmdSpec kCJp = {3, "CJp", "Jump when the condition is set", FxClass::Song, 1};
const CmdSpec kUnk2 = {3, "???", "Unknown", FxClass::Misc};
const CmdSpec kNop = {1, "Nop", "(no effect)", FxClass::Misc};
}

Layout detect_layout(const uint8_t* ram) {
    Layout L;
    const int load[] = {0xF6, W, W, 0xC4, W, 0xFC, 0xF6, W, W, 0xC4, W, 0x8D, 0x00, 0xF7, W, 0x30};
    int p = find_pattern(ram, 0x200, 0x4000, load, 16);
    if (p < 0 || rd16(ram, p + 1) != rd16(ram, p + 7)) return Layout{};
    L.song_list = rd16(ram, p + 1);
    const int disp[] = {0x80, 0xA8, 0xC0, 0x1C, 0x5D, 0x1F, W, W};
    int d = find_pattern(ram, 0x200, 0x4000, disp, 8);
    if (d < 0) return Layout{};
    const uint16_t table = rd16(ram, d + 6);
    if (rd16(ram, table) == rd16(ram, table + 32)) L.version = 1;
    else if (rd16(ram, table) == rd16(ram, table + 10)) L.version = 2;
    const int fetch[] = {0xF5, W, W, 0xC4, W, 0xF5, W, W, 0xC4, W, 0x8D, 0x00, 0xF7, W};
    int q = find_pattern(ram, 0x200, 0x4000, fetch, 14);
    if (q < 0 || ram[q + 4] + 1 != ram[q + 9] || ram[q + 13] != ram[q + 4]) return Layout{};
    L.ptr_lo = rd16(ram, q + 1); L.ptr_hi = rd16(ram, q + 6);
    const int h = rd16(ram, table + (0xE1 - 0xC0) * 2);   // call: stores the return address
    for (int i = 0; i < 24; ++i) if (ram[(h + i) & 0xFFFF] == 0xD5 && ram[(h + i + 3) & 0xFFFF] != 0xD5) { L.sub_ret = rd16(ram, h + i + 1); break; }
    int top = 0;
    for (int i = 0; i < 0x40; ++i) top = std::max(top, int(rd16(ram, table + i * 2)));
    for (int a = top, run = 0; a < 0x8000; ++a) {
        run = a > top && ram[a] == ram[a - 1] && (ram[a] == 0xFF || ram[a] == 0) ? run + 1 : 0;
        if (run == 31) { L.code_end = uint16_t(a - 31); break; }
    }
    return L;
}

std::unique_ptr<seq::Driver> detect(const uint8_t* ram) {
    Layout L = detect_layout(ram);
    if (!L.valid()) return nullptr;
    return std::make_unique<PrismDriver>(L);
}

std::string PrismDriver::name() const {
    static const char* const names[] = {"", "Cosmo Gang: The Video", "Dual Orb", "Dual Orb II"};
    return std::string("Prism Kikaku (") + names[L.version] + (L.version == 3 ? ")" : ") - untested");
}

const CmdSpec& PrismDriver::spec(uint8_t op) const {
    if (op < 0xC0) return kNop;
    if (L.version == 1 && op < 0xD0) return kPan;
    if (L.version == 1 && op == 0xDB) return kUnk2;
    if (L.version == 2 && op < 0xC5) return kCJp;
    return kCmds[op - 0xC0];
}

std::vector<uint16_t> PrismDriver::song_headers(const uint8_t* ram) const {
    std::vector<uint16_t> out;
    for (int i = 0; i < 0x60; ++i) {
        const uint16_t h = rd16(ram, L.song_list + i * 2);
        bool ok = h >= 0x200 && h < 0xFF00 && ram[h] < 8;
        for (int v = 0; v < 8 && ok; ++v) if (track_start(ram, h, v)) break; else if (v == 7) ok = false;
        if (!ok) continue;
        if (std::find(out.begin(), out.end(), h) == out.end()) out.push_back(h);
    }
    return out;
}

uint16_t PrismDriver::track_start(const uint8_t* ram, uint16_t header, int v) const {
    for (int i = 0; i < 8; ++i) {
        const int e = (header + i * 4) & 0xFFFF;
        if (ram[e] >= 0x80) return 0;
        if ((ram[e + 1] & 7) != v) continue;
        const uint16_t a = rd16(ram, e + 2);
        if (a < 0x200) return 0;
        logical_[uint32_t(header) << 8 | uint32_t(v)] = ram[e];
        slots_[uint32_t(header) << 8 | uint32_t(v)] = uint16_t(e + 2);
        return a;
    }
    return 0;
}

State PrismDriver::initial_state(const uint8_t* ram, uint16_t header, int voice) const {
    (void)voice;
    State s;
    data_lo_ = std::min(data_lo_ == 0x200 ? int(header) : data_lo_, int(header));
    for (int v = 0; v < 8; ++v) if (uint16_t a = track_start(ram, header, v)) data_lo_ = std::min(data_lo_, int(a));
    return s;
}

// State: len = default length (0 = read a byte), flags bit 0 = manual gates, bit 1 = condition, x[0] / x[1] = repeat counters.
void PrismDriver::decode(const uint8_t* p, int pc, State& s, Event& e, Flow& f) const {
    (void)pc;
    const uint8_t b = p[0];
    e.b[0] = b;
    e.size = 1;
    e.pitch = -1;
    e.type = EventType::Command;
    auto timed = [&](EventType type, bool gate, int n = 1) {
        int len = s.len;
        if (!s.len) len = p[n++];
        if (gate && (s.flags & 1)) ++n;
        e.size = uint8_t(n);
        for (int i = 1; i < n; ++i) e.b[i] = p[i];
        e.type = type;
        e.duration = len;
    };
    if (b < 0xA0) {
        timed(EventType::Note, true);
        e.pitch = (b & 0x7F) - 12 + s.trans;
        return;
    }
    if (b == 0xE9) { e.b[1] = p[1]; e.b[2] = p[2]; timed(EventType::Note, true, 3); e.pitch = (p[1] & 0x7F) - 12 + s.trans; return; }
    if (b == 0xEE) { timed(EventType::Rest, false); return; }
    if (b == 0xF4) { timed(EventType::Tie, true); return; }
    if (b == 0xF5) { e.type = EventType::Tie; e.duration = 0; return; }
    const CmdSpec& sp = spec(b);
    e.size = sp.size;
    if (b == 0xFC && p[1] < 0x80) e.size = 2;
    for (int i = 1; i < e.size; ++i) e.b[i] = p[i];
    const bool cjp = (L.version == 2 && b < 0xC5) || (L.version != 1 && b == 0xC5);
    if (cjp) { if (s.flags & 2) { f.kind = Flow::Jump; f.target = p[1] | (p[2] << 8); f.count = 1; } return; }
    if (L.version == 1 && b < 0xD0) return;
    switch (b) {
        case 0xC6: s.flags |= 2; break;
        case 0xD8: s.trans += int8_t(p[1]); break;
        case 0xDC: s.len = 0; break;
        case 0xDD: s.len = p[1]; break;
        case 0xDE: case 0xDF: {   // the section plays n + 1 times
            int& c = s.x[b - 0xDE];
            if (!c) c = (p[1] + 1) & 0xFF;
            if (--c != 0) { f.kind = Flow::Jump; f.target = p[2] | (p[3] << 8); f.count = 1; }
            break;
        }
        case 0xE0: f.kind = Flow::Return; f.count = 1; break;
        case 0xE1: f.kind = Flow::Call; f.target = p[1] | (p[2] << 8); f.count = 1; break;
        case 0xE2: f.kind = Flow::Jump; f.target = p[1] | (p[2] << 8); break;
        case 0xE3: s.trans = int8_t(p[1]); break;
        case 0xF1: s.flags &= uint8_t(~1); break;
        case 0xF2: s.flags |= 1; break;
        case 0xFF: f.kind = Flow::End; e.type = EventType::End; break;
        default: break;
    }
}

uint16_t PrismDriver::live_ptr(const uint8_t* ram, const seq::Position& pos, int v) const {
    (void)pos;
    return uint16_t(ram[(L.ptr_lo + logical(v)) & 0xFFFF] | (ram[(L.ptr_hi + logical(v)) & 0xFFFF] << 8));
}

void PrismDriver::live_state_writes(const uint8_t* ram, const seq::Position& pos, int voice, uint16_t ptr, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    (void)pos;
    int mapped = remap.find(live_ptr(ram, pos, voice));
    if (mapped >= 0) ptr = uint16_t(mapped);
    const int lv = logical(voice);
    out.push_back({uint16_t(L.ptr_lo + lv), uint8_t(ptr & 0xFF)});
    out.push_back({uint16_t(L.ptr_hi + lv), uint8_t(ptr >> 8)});
    if (L.sub_ret) seq::remap_word(ram, uint16_t(L.sub_ret + lv * 2), remap, out);
}

void PrismDriver::track_pointer_writes(const seq::Song& song, int pattern_idx, int voice, uint16_t dest, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    (void)pattern_idx;
    auto it = slots_.find(uint32_t(song.order_addr) << 8 | uint32_t(voice));
    if (it == slots_.end()) return;
    out.push_back({it->second, uint8_t(dest & 0xFF)});
    out.push_back({uint16_t(it->second + 1), uint8_t(dest >> 8)});
}

bool PrismDriver::transpose_event(Event& e, int semis) const {
    if (e.type != EventType::Note || e.b[0] == 0xE9) return false;
    const int k = (e.b[0] & 0x7F) + semis;
    if (k < 0 || k > 0x7F) return false;
    e.b[0] = uint8_t((e.b[0] & 0x80) | k);
    if (e.pitch >= 0) e.pitch += semis;
    return true;
}

void PrismDriver::apply_note_byte(Event& e, uint8_t byte) const {
    if (byte == 0xEE || byte == 0xF4) {   // rest / tie keep the length byte, a rest drops the gate byte
        const bool had_len = e.size >= 2;
        const uint8_t len = e.b[1];
        e.b[0] = byte;
        e.size = uint8_t(had_len ? 2 : 1);
        if (had_len) e.b[1] = len;
        e.type = byte == 0xEE ? EventType::Rest : EventType::Tie;
        e.pitch = -1;
        return;
    }
    if (e.type != EventType::Note) {
        const bool had_len = e.size >= 2;
        e.b[0] = byte & 0x7F;
        e.size = uint8_t(had_len ? 2 : 1);
        e.type = EventType::Note;
        e.pitch = note_semitone(byte);
        return;
    }
    if (e.b[0] == 0xE9) return;
    const int old = e.b[0] & 0x7F;
    e.b[0] = uint8_t((e.b[0] & 0x80) | (byte & 0x7F));
    if (e.pitch >= 0) e.pitch += (byte & 0x7F) - old;
}

bool PrismDriver::set_duration(std::vector<Event>& ev, int i, int dur) const {
    if (i < 0 || i >= int(ev.size()) || dur < 1 || dur > 255) return false;
    Event& e = ev[size_t(i)];
    if (e.duration <= 0 && e.type != EventType::Tie) return false;
    const State s = state_before(ev, i);
    if (s.len) return dur == s.len;   // default length in force: lengths are not per note
    e.b[1] = uint8_t(dur);
    if (e.size < 2) e.size = 2;
    e.duration = dur;
    e.addr = 0;
    return true;
}

bool PrismDriver::note_retriggers(const std::vector<Event>& ev, int i) const {
    if (i <= 0 || i >= int(ev.size()) || ev[size_t(i)].type != EventType::Note) return true;
    for (int k = i - 1; k >= 0; --k) {
        const Event& p = ev[size_t(k)];
        if (p.type == EventType::Tie) return false;
        if (p.duration > 0) return true;
    }
    return true;
}

std::string PrismDriver::event_text(const Event& e) const {
    char b[96];
    if (e.type == EventType::Note) { std::snprintf(b, sizeof b, "%s%s  %d ticks", note_name(e).c_str(), e.b[0] == 0xE9 ? " (slide)" : e.b[0] >= 0x80 ? " (noise)" : "", e.duration); return b; }
    if (e.type == EventType::Rest) { std::snprintf(b, sizeof b, "rest  %d ticks", e.duration); return b; }
    if (e.type == EventType::Tie) { std::snprintf(b, sizeof b, "tie  %d ticks", e.duration); return b; }
    if (e.type == EventType::Command) {
        switch (e.b[0]) {
            case 0xC0: case 0xC1: case 0xC2: case 0xC3: case 0xC4: if (L.version == 3) { std::snprintf(b, sizeof b, "Tempo %d (%.1f ticks/s)", e.b[1], e.b[1] ? 8000.0 / e.b[1] : 0); return b; } break;
            case 0xDD: std::snprintf(b, sizeof b, "Default length %d", e.b[1]); return b;
            case 0xDE: case 0xDF: std::snprintf(b, sizeof b, "Repeat x%d  $%02X%02X", e.b[1], e.b[3], e.b[2]); return b;
            case 0xE1: std::snprintf(b, sizeof b, "Call $%02X%02X", e.b[2], e.b[1]); return b;
            case 0xE2: std::snprintf(b, sizeof b, "Jump $%02X%02X", e.b[2], e.b[1]); return b;
            case 0xE3: std::snprintf(b, sizeof b, "Transpose %+d", int8_t(e.b[1])); return b;
            default: break;
        }
    }
    return seq::Driver::event_text(e);
}

bool PrismDriver::pitch_fx(const Event& e, seq::PitchFx& out) const {
    if (e.type == EventType::Note && e.b[0] == 0xE9) {
        out = seq::PitchFx{};
        out.kind = seq::PitchFx::Slide; out.target = e.pitch >= 0 ? e.pitch + ((e.b[2] & 0x7F) - (e.b[1] & 0x7F)) : (e.b[2] & 0x7F) - 12; out.length = e.duration;
        return true;
    }
    if (e.type != EventType::Command) return false;
    out = seq::PitchFx{};
    if (e.b[0] == 0xE7) { out.kind = seq::PitchFx::Vibrato; out.delay = e.b[1]; return true; }
    if (e.b[0] == 0xE6) { out.kind = seq::PitchFx::VibratoOff; return true; }
    return false;
}

bool PrismDriver::tempo_writes(const uint8_t* ram, double tps, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    (void)ram;
    if (tps <= 0) return false;
    out.push_back({0x00FA, uint8_t(std::clamp(int(8000.0 / tps + 0.5), 1, 255))});
    return true;
}

}
