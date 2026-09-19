#include "ascii.hpp"

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

namespace ascii {
namespace {
const int W = 0x100;

// Ardy Lightfoot build (commands 80-9F).
const CmdSpec kCmds1[0x20] = {
    {1, "End", "End of track", FxClass::Song},                           // 80
    {1, "Lp{", "Song loop point", FxClass::Song},                        // 81
    {1, "Lp}", "Jump to the song loop point", FxClass::Song},            // 82
    {1, "Lp[", "Repeat start", FxClass::Song, 0, true},                  // 83
    {2, "Lp]", "Repeat end (count, 0 = 256)", FxClass::Song, 0, true},   // 84
    {1, "Brk", "Repeat break flag", FxClass::Song},                      // 85
    {3, "Cal", "Call", FxClass::Song, 1, true},                          // 86
    {1, "Ret", "Return", FxClass::Song, 0, true},                        // 87
    {1, "Syn", "Wait for the SNES", FxClass::Misc},                      // 88
    {2, "Ins", "Instrument", FxClass::Instrument},                       // 89
    {2, "Tmp", "Tempo", FxClass::Speed},                                 // 8A
    {2, "Trn", "Transpose", FxClass::Pitch},                             // 8B
    {2, "NtP", "Note parameter (stored per note, never read by this build)", FxClass::Misc},   // 8C
    {2, "Dtn", "Detune (signed, added to the pitch)", FxClass::Pitch},   // 8D
    {2, "Gat", "Gate: a note sounds length * n / 256 ticks (0 = full)", FxClass::Time},   // 8E
    {2, "MVl", "Master volume", FxClass::Volume},                        // 8F
    {3, "VlP", "Volume and pan", FxClass::Volume},                       // 90
    {2, "Vol", "Volume", FxClass::Volume},                               // 91
    {2, "Pan", "Panning", FxClass::Panning},                             // 92
    {3, "VlF", "Volume fade", FxClass::Volume},                          // 93
    {4, "PnF", "Pan fade", FxClass::Panning},                            // 94
    {3, "EVl", "Echo volume L, R", FxClass::Sys1},                       // 95
    {3, "Ech", "Echo parameters", FxClass::Sys1},                        // 96
    {2, "EcV", "Echo voices", FxClass::Sys1},                            // 97
    {5, "Vib", "Vibrato (delay, rate, depth, step)", FxClass::Pitch},    // 98
    {1, "VbF", "Vibrato off", FxClass::Pitch},                           // 99
    {4, "Sld", "Pitch slide (rate, depth, note)", FxClass::Pitch},       // 9A
    {1, "Rst", "Rest", FxClass::Time},                                   // 9B
    {1, "NoO", "Noise on", FxClass::Sys1},                               // 9C
    {1, "NoF", "Noise off", FxClass::Sys1},                              // 9D
    {9, "FIR", "Echo filter", FxClass::Sys1},                            // 9E
    {1, "Off", "Key off", FxClass::Time},                                // 9F
};
// Wizardry VI build (commands 80-AB), after VGMTrans.
const CmdSpec kCmds2[0x2C] = {
    {1, "End", "End of track", FxClass::Song},                           // 80
    {1, "Lp{", "Song loop point", FxClass::Song},                        // 81
    {1, "Lp}", "Jump to the song loop point", FxClass::Song},            // 82
    {1, "Lp[", "Repeat start", FxClass::Song, 0, true},                  // 83
    {2, "Lp]", "Repeat end (count, 0 = 256)", FxClass::Song, 0, true},   // 84
    {1, "Brk", "Leave the repeat on its last pass", FxClass::Song, 0, true},    // 85
    {3, "Cal", "Call", FxClass::Song, 1, true},                          // 86
    {1, "Ret", "Return", FxClass::Song, 0, true},                        // 87
    {1, "???", "Unknown", FxClass::Misc},                                // 88
    {2, "Ins", "Instrument", FxClass::Instrument},                       // 89
    {2, "Rel", "Release ADSR", FxClass::Sys2},                           // 8A
    {2, "Tmp", "Tempo", FxClass::Speed},                                 // 8B
    {2, "Trn", "Transpose", FxClass::Pitch},                             // 8C
    {2, "Tr+", "Transpose (relative)", FxClass::Pitch},                  // 8D
    {2, "Tun", "Tuning", FxClass::Pitch},                                // 8E
    {3, "Sld", "Pitch bend slide", FxClass::Pitch},                      // 8F
    {2, "Gat", "Gate (n/256)", FxClass::Time},                           // 90
    {1, "???", "Unknown", FxClass::Misc},                                // 91
    {3, "VlP", "Volume and pan", FxClass::Volume},                       // 92
    {2, "Vol", "Volume", FxClass::Volume},                               // 93
    {2, "Vl+", "Volume (relative)", FxClass::Volume},                    // 94
    {2, "Vl~", "Volume (relative, one note)", FxClass::Volume},          // 95
    {2, "Pan", "Panning", FxClass::Panning},                             // 96
    {1, "???", "Unknown", FxClass::Misc},                                // 97
    {4, "PnF", "Pan fade", FxClass::Panning},                            // 98
    {5, "VlF", "Volume fade", FxClass::Volume},                          // 99
    {3, "MVl", "Master volume", FxClass::Volume},                        // 9A
    {4, "Ech", "Echo parameters", FxClass::Sys1},                        // 9B
    {2, "EcV", "Echo voices", FxClass::Sys1},                            // 9C
    {5, "Vib", "Vibrato", FxClass::Pitch},                               // 9D
    {1, "VbF", "Vibrato off", FxClass::Pitch},                           // 9E
    {1, "Rst", "Rest", FxClass::Time},                                   // 9F
    {1, "NoO", "Noise on", FxClass::Sys1},                               // A0
    {1, "NoF", "Noise off", FxClass::Sys1},                              // A1
    {1, "???", "Unknown", FxClass::Misc},                                // A2
    {2, "Mut", "Mute voices", FxClass::Sys1},                            // A3
    {5, "IVP", "Instrument, volume, pan, transpose", FxClass::Instrument},   // A4
    {2, "Gt#", "Gate (ticks)", FxClass::Time},                           // A5
    {2, "Prt", "Write to port", FxClass::Misc},                          // A6
    {1, "???", "Unknown", FxClass::Misc},                                // A7
    {1, "???", "Unknown", FxClass::Misc},                                // A8
    {1, "???", "Unknown", FxClass::Misc},                                // A9
    {1, "???", "Unknown", FxClass::Misc},                                // AA
    {1, "???", "Unknown", FxClass::Misc},                                // AB
};
const CmdSpec kNop = {1, "Nop", "(no effect)", FxClass::Misc};
const CmdSpec kUnknown = {1, "???", "Unknown opcode (not in the driver's table)", FxClass::Misc};
const CmdSpec kSlur = {1, "Slr", "Slur into the next note", FxClass::Time};
}

Layout detect_layout(const uint8_t* ram) {
    Layout L;
    const int load[] = {0xE8, W, 0x3F, W, W, 0xF5, W, W, 0xD5, W, W, 0xF5, W, W, 0xD4, W, 0x1D, 0x10, 0xED};
    int p = find_pattern(ram, 0x200, 0x4000, load, 19);
    if (p < 0) return Layout{};
    // MOV X,#n-1 before the loop counts the voices; the MOV A,#5 the pattern
    // starts on is the init argument.
    L.voices = ram[p - 2] == 0xCD ? uint8_t(ram[p - 1] + 1) : uint8_t(ram[p + 1] + 1);
    L.header_lo = rd16(ram, p + 6); L.ptr_lo = rd16(ram, p + 9);
    L.header_hi = rd16(ram, p + 12); L.ptr_hi_zp = ram[p + 15];
    if (L.header_hi != L.header_lo + 8 || L.voices > 8) return Layout{};
    const int disp[] = {0x80, 0xA8, W, 0x10, W, 0x60, 0x88, W, 0x1C, 0x5D};
    int d = find_pattern(ram, 0x200, 0x4000, disp, 10);
    if (d < 0) return Layout{};
    L.note_base = ram[d + 2];
    L.version = L.note_base == 0xA0 ? 1 : 2;
    const int call[] = {0xFB, W, 0xBB, W, 0x20, 0xE4, W, 0x60, 0x88, 0x02, 0xD6, W, W, 0xE4, W, 0x88, 0x00, 0xD6, W, W};
    if (int c = find_pattern(ram, 0x200, 0x4000, call, 20); c >= 0) { L.call_sp = uint16_t(0x100 + ram[c + 1]); L.call_lo = rd16(ram, c + 11); L.call_hi = rd16(ram, c + 18); }
    const int rep[] = {0xFB, W, 0xD6, W, W, 0xE4, W, 0xD6, W, W, 0xBB, W};
    if (int r = find_pattern(ram, 0x200, 0x4000, rep, 12); r >= 0 && ram[r + 1] == ram[r + 11]) { L.rep_sp = uint16_t(0x100 + ram[r + 1]); L.rep_lo = rd16(ram, r + 3); L.rep_hi = rd16(ram, r + 8); }
    const int end[] = {0xFB, W, 0xD6, W, W, 0xE5, W, 0x00, 0xD6, W, W, 0xE5, W, 0x00, 0xD6, W, W, 0xBB, W};
    if (int e = find_pattern(ram, 0x200, 0x4000, end, 19); e >= 0 && ram[e + 1] == ram[e + 18]) { L.end_sp = uint16_t(0x100 + ram[e + 1]); L.end_count = rd16(ram, e + 3); L.end_lo = rd16(ram, e + 9); L.end_hi = rd16(ram, e + 15); }
    const int loop[] = {0xBA, W, 0xD5, W, W, 0xDB, W};
    if (int l = find_pattern(ram, 0x200, 0x4000, loop, 7); l >= 0) { L.loop_lo = rd16(ram, l + 3); L.loop_hi_zp = ram[l + 6]; }
    const int tempo[] = {0xCB, W, 0x3F, W, W, 0x5F, W, W, 0xBA, W, 0xCF, 0xCD, 0x10, 0x9E, 0xC4, W};
    if (int t = find_pattern(ram, 0x200, 0x4000, tempo, 16); t >= 0 && ram[t + 1] == ram[t + 9]) { L.tempo_zp = ram[t + 1]; L.tick_zp = ram[t + 15]; }
    return L;
}

std::unique_ptr<seq::Driver> detect(const uint8_t* ram) {
    Layout L = detect_layout(ram);
    if (!L.valid()) return nullptr;
    return std::make_unique<AsciiDriver>(L);
}

const CmdSpec& AsciiDriver::spec(uint8_t op) const {
    if (op == 0xFF) return kSlur;
    if (op < 0x80 || op >= L.note_base) return kUnknown;
    return L.version == 1 ? kCmds1[op - 0x80] : kCmds2[op - 0x80];
}

uint16_t AsciiDriver::track_start(const uint8_t* ram, uint16_t header, int v) const {
    if (v >= L.voices) return 0;
    const uint16_t a = uint16_t(ram[(header + v) & 0xFFFF] | (ram[(header + 8 + v) & 0xFFFF] << 8));
    return a >= 0x200 && a < 0xFF00 ? a : 0;
}

State AsciiDriver::initial_state(const uint8_t* ram, uint16_t header, int voice) const {
    (void)voice;
    State s;
    s.len = 24;
    data_lo_ = std::min(data_lo_ == 0x200 ? int(header) : data_lo_, int(header));
    for (int v = 0; v < 8; ++v) if (uint16_t a = track_start(ram, header, v)) data_lo_ = std::min(data_lo_, int(a));
    return s;
}

double AsciiDriver::ticks_per_second(const uint8_t* ram) const {
    const int div = L.tick_zp ? ram[L.tick_zp] : 0;
    return ram[0xFA] && div ? 8000.0 / ram[0xFA] / div : 0;
}

bool AsciiDriver::tempo_writes(const uint8_t* ram, double tps, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    if (tps <= 0 || !ram[0xFA] || !L.tick_zp) return false;
    const uint8_t div = uint8_t(std::clamp(int(8000.0 / ram[0xFA] / tps + 0.5), 1, 255));
    out.push_back({L.tick_zp, div});
    if (L.tempo_zp) out.push_back({L.tempo_zp, div});
    return true;
}

// State: len = last length, x[0] = song loop point, x[1] = last pitch,
// flags bit 0 = the last note was slurred.
void AsciiDriver::decode(const uint8_t* p, int pc, State& s, Event& e, Flow& f) const {
    const uint8_t b = p[0];
    e.b[0] = b;
    e.size = 1;
    e.pitch = -1;
    e.type = EventType::Command;
    const bool rest = b == (L.version == 1 ? 0x9B : 0x9F);
    if (b >= L.note_base || rest) {
        if (p[1] < 0x80) { s.len = p[1]; e.b[1] = p[1]; e.size = 2; }
        if (b == 0xFF) { s.flags |= 1; return; }   // slur on its own: the next note continues
        e.duration = s.len;
        if (rest) { e.type = EventType::Rest; s.flags &= ~1; for (int k = e.size; k < 16; ++k) e.b[k] = 0x80; return; }
        e.pitch = b - L.note_base + 12 + s.trans;
        e.type = (s.flags & 1) && e.pitch == s.x[1] ? EventType::Tie : EventType::Note;
        s.x[1] = e.pitch;
        s.flags &= ~1;
        if (L.version == 1) {
            if (p[e.size] == 0xFF) { e.b[e.size] = 0xFF; ++e.size; s.flags |= 1; }
            if (p[e.size] == 0x9A) { for (int i = 0; i < 4; ++i) e.b[e.size + i] = p[e.size + i]; e.size = uint8_t(e.size + 4); }
        }
        for (int k = e.size; k < 16; ++k) e.b[k] = 0x80;   // re-decoding from the event must not see a length
        return;
    }
    const CmdSpec& sp = spec(b);
    e.size = sp.size;
    for (int i = 1; i < e.size; ++i) e.b[i] = p[i];
    const bool v1 = L.version == 1;
    switch (b) {
        case 0x80: f.kind = Flow::End; e.type = EventType::End; break;
        case 0x81: s.x[0] = pc + 1; break;
        case 0x82: f.kind = Flow::Jump; f.target = s.x[0] ? s.x[0] : s.start; break;
        case 0x83: f.kind = Flow::RepStart; break;
        case 0x84: f.kind = Flow::RepEnd; f.count = p[1] ? p[1] : 256; break;
        case 0x85: if (!v1) { f.kind = Flow::RepBreak; f.target = -1; } break;
        case 0x86: f.kind = Flow::Call; f.count = 1; f.target = p[1] | (p[2] << 8); break;
        case 0x87: f.kind = Flow::Return; break;
        default: break;
    }
    if (b == (v1 ? 0x8B : 0x8C)) s.trans = int8_t(p[1]);
    if (!v1 && b == 0x8D) s.trans += int8_t(p[1]);
    if (!v1 && b == 0xA4) s.trans = int8_t(p[4]);
}

uint16_t AsciiDriver::live_ptr(const uint8_t* ram, const seq::Position& pos, int v) const {
    (void)pos;
    return uint16_t(ram[(L.ptr_lo + v) & 0xFFFF] | (ram[(L.ptr_hi_zp + v) & 0xFF] << 8));
}

void AsciiDriver::live_state_writes(const uint8_t* ram, const seq::Position& pos, int voice, uint16_t ptr, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    int mapped = remap.find(live_ptr(ram, pos, voice));
    if (mapped >= 0) ptr = uint16_t(mapped);
    out.push_back({uint16_t(L.ptr_lo + voice), uint8_t(ptr & 0xFF)});
    out.push_back({uint16_t(L.ptr_hi_zp + voice), uint8_t(ptr >> 8)});
    live_extra_writes(ram, voice, remap, out);
}

void AsciiDriver::live_extra_writes(const uint8_t* ram, int voice, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    auto split = [&](uint16_t lo, uint16_t hi, int idx) {
        const int addr = ram[(lo + idx) & 0xFFFF] | (ram[(hi + idx) & 0xFFFF] << 8);
        const int mapped = remap.find(uint16_t(addr));
        if (mapped < 0) return;
        out.push_back({uint16_t(lo + idx), uint8_t(mapped & 0xFF)});
        out.push_back({uint16_t(hi + idx), uint8_t(mapped >> 8)});
    };
    if (L.call_sp) for (int i = voice * 4; i < ram[(L.call_sp + voice) & 0xFFFF] && i < voice * 4 + 4; ++i) split(L.call_lo, L.call_hi, i);
    if (L.rep_sp) for (int i = voice * 4; i < ram[(L.rep_sp + voice) & 0xFFFF] && i < voice * 4 + 4; ++i) split(L.rep_lo, L.rep_hi, i);
    if (L.end_sp) for (int i = voice * 4; i < ram[(L.end_sp + voice) & 0xFFFF] && i < voice * 4 + 4; ++i) split(L.end_lo, L.end_hi, i);
    if (L.loop_lo) {
        const int addr = ram[(L.loop_lo + voice) & 0xFFFF] | (ram[(L.loop_hi_zp + voice) & 0xFF] << 8);
        const int mapped = remap.find(uint16_t(addr));
        if (mapped >= 0) { out.push_back({uint16_t(L.loop_lo + voice), uint8_t(mapped & 0xFF)}); out.push_back({uint16_t(L.loop_hi_zp + voice), uint8_t(mapped >> 8)}); }
    }
}

void AsciiDriver::track_pointer_writes(const seq::Song& song, int pattern_idx, int voice, uint16_t dest, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    (void)pattern_idx;
    out.push_back({uint16_t(song.order_addr + voice), uint8_t(dest & 0xFF)});
    out.push_back({uint16_t(song.order_addr + 8 + voice), uint8_t(dest >> 8)});
}

bool AsciiDriver::transpose_event(Event& e, int semis) const {
    if (e.type != EventType::Note) return false;
    const int n = e.b[0] + semis;
    if (n < L.note_base || n > 0xFE) return false;
    e.b[0] = uint8_t(n);
    if (e.pitch >= 0) e.pitch += semis;
    return true;
}

void AsciiDriver::apply_note_byte(Event& e, uint8_t byte) const {
    const uint8_t old = e.b[0];
    const bool was_rest = e.type == EventType::Rest;
    if (byte == rest_byte()) {
        e.type = EventType::Rest; e.pitch = -1;
        if (!was_rest) {   // drop the slur / slide suffix
            e.b[0] = byte;
            e.size = uint8_t(e.size >= 2 && e.b[1] < 0x80 ? 2 : 1);
        }
        return;
    }
    e.b[0] = byte;
    e.type = EventType::Note;
    if (was_rest) e.pitch = note_semitone(byte);
    else if (e.pitch >= 0 && old >= L.note_base) e.pitch += byte - old;
    else e.pitch = note_semitone(byte);
}

bool AsciiDriver::set_duration(std::vector<Event>& ev, int i, int dur) const {
    if (i < 0 || i >= int(ev.size()) || dur < 1 || dur > 4096) return false;
    Event& e = ev[size_t(i)];
    if (e.duration <= 0) return false;
    // Notes without a length byte use the last one: give the next such note its own.
    for (size_t k = size_t(i) + 1; k < ev.size(); ++k) {
        Event& n = ev[k];
        if (n.duration <= 0) continue;
        if (n.size >= 2 && n.b[1] < 0x80) break;
        if (n.in_sub) return false;
        uint8_t rest[16]; std::memcpy(rest, n.b + 1, 15);
        n.b[1] = uint8_t(n.duration); std::memcpy(n.b + 2, rest, size_t(n.size - 1)); n.size = uint8_t(n.size + 1); n.addr = 0;
        break;
    }
    const int first = std::min(dur, 127);
    if (!(e.size >= 2 && e.b[1] < 0x80)) {
        uint8_t rest[16]; std::memcpy(rest, e.b + 1, 15);
        std::memcpy(e.b + 2, rest, size_t(e.size - 1)); e.size = uint8_t(e.size + 1);
    }
    e.b[1] = uint8_t(first);
    e.duration = first;
    e.addr = 0;
    std::vector<Event> tail;
    Event* last = &e;
    for (int left = dur - first; left > 0;) {
        const int n = std::min(left, 127);
        Event t{};
        t.type = e.type == EventType::Rest ? EventType::Rest : EventType::Tie;
        t.b[0] = e.type == EventType::Rest ? rest_byte() : e.b[0]; t.b[1] = uint8_t(n); t.size = 2; t.duration = n; t.pitch = -1;
        if (e.type != EventType::Rest && !(last->size >= 3 && last->b[2] == 0xFF) && last->size < 3) { last->b[last->size] = 0xFF; ++last->size; }   // slur into the continuation
        tail.push_back(t);
        last = &tail.back();
        left -= n;
    }
    ev.insert(ev.begin() + i + 1, tail.begin(), tail.end());
    return true;
}

bool AsciiDriver::note_retriggers(const std::vector<Event>& ev, int i) const {
    (void)ev; (void)i;
    return true;
}

std::string AsciiDriver::event_text(const Event& e) const {
    char b[96];
    if (e.type == EventType::Note) { std::snprintf(b, sizeof b, "%s  %d ticks%s%s", note_name(e).c_str(), e.duration, e.size >= 2 && e.b[1] < 0x80 ? "" : "  (last length)", e.size >= 2 && e.b[e.size - 1] == 0xFF ? "  slur" : ""); return b; }
    if (e.type == EventType::Rest) { std::snprintf(b, sizeof b, "rest  %d ticks", e.duration); return b; }
    if (e.type == EventType::Tie) { std::snprintf(b, sizeof b, "%s  %d ticks (slurred)", note_name(e).c_str(), e.duration); return b; }
    if (e.type == EventType::Command) {
        const bool v1 = L.version == 1;
        if (e.b[0] == 0x84) { std::snprintf(b, sizeof b, "Repeat end x%d", e.b[1] ? e.b[1] : 256); return b; }
        if (e.b[0] == 0x86) { std::snprintf(b, sizeof b, "Call $%02X%02X", e.b[2], e.b[1]); return b; }
        if (e.b[0] == (v1 ? 0x8B : 0x8C)) { std::snprintf(b, sizeof b, "Transpose %+d", int8_t(e.b[1])); return b; }
        if (!v1 && e.b[0] == 0x8D) { std::snprintf(b, sizeof b, "Transpose (relative) %+d", int8_t(e.b[1])); return b; }
        if (e.b[0] == (v1 ? 0x8A : 0x8B)) { std::snprintf(b, sizeof b, "Tempo %d", e.b[1]); return b; }
    }
    return seq::Driver::event_text(e);
}

}
