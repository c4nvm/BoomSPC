#include "follin.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <vector>

using seq::Event;
using seq::EventType;

namespace follin {
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

struct CmdSpec { uint8_t argc; const char* code; const char* name; seq::FxClass cls; };

using seq::FxClass;
const CmdSpec kCmds[0x3A] = {
    {0, "End", "End of voice", FxClass::Song},                     // 80
    {2, "Jmp", "Jump", FxClass::Song},                             // 81
    {2, "Cal", "Call subroutine", FxClass::Song},                  // 82
    {0, "Ret", "Return", FxClass::Song},                           // 83
    {1, "Rep", "Repeat start (count)", FxClass::Song},             // 84
    {0, "RpE", "Repeat end", FxClass::Song},                       // 85
    {1, "Dur", "Default duration", FxClass::Time},                 // 86
    {0, "Dr1", "Explicit duration for next note", FxClass::Time},  // 87
    {1, "Trn", "Transpose", FxClass::Pitch},                       // 88
    {1, "Ins", "Instrument (sample)", FxClass::Instrument},        // 89
    {1, "VoL", "Volume left", FxClass::Volume},                    // 8A
    {1, "VoR", "Volume right", FxClass::Volume},                   // 8B
    {3, "Env", "Envelope (level, attack, decay)", FxClass::Volume},// 8C
    {1, "P8D", "Voice param $02C0", FxClass::Misc},                // 8D
    {3, "SwU", "Pitch sweep up (delay, units/tick, ticks per leg; 0 = to the note's end)", FxClass::Pitch},   // 8E
    {3, "SwD", "Pitch sweep down (delay, units/tick, ticks per leg; 0 = to the note's end)", FxClass::Pitch}, // 8F
    {1, "Gld", "Glide speed: notes slide to their pitch at n units/tick (0 = off)", FxClass::Pitch},      // 90
    {0, "SwO", "Pitch sweep off", FxClass::Pitch},                 // 91
    {1, "CtB", "Note cut n ticks before end", FxClass::Volume},    // 92
    {1, "CtA", "Note cut after n ticks", FxClass::Volume},         // 93
    {2, "Por", "Portamento (rate, speed)", FxClass::Pitch},        // 94
    {0, "PrO", "Portamento off", FxClass::Pitch},                  // 95
    {3, "Vib", "Vibrato (depth, speed, delay)", FxClass::Pitch},   // 96
    {1, "EnP", "Envelope preset", FxClass::Volume},                // 97
    {0, "EcO", "Echo on", FxClass::Sys1},                          // 98
    {0, "EcF", "Echo off", FxClass::Sys1},                         // 99
    {1, "Noi", "Noise clock", FxClass::Sys1},                      // 9A
    {0, "VbR", "Vibrato restarts per note", FxClass::Pitch},       // 9B
    {0, "RtO", "No restarts per note", FxClass::Misc},             // 9C
    {0, "EnR", "Envelope restarts per note", FxClass::Volume},     // 9D
    {0, "KOn", "Key-on per note", FxClass::Misc},                  // 9E
    {0, "Leg", "No key-on (legato)", FxClass::Misc},               // 9F
    {0, "TrS", "Skip transpose for next note", FxClass::Pitch},    // A0
    {0, "Fad", "Reset master volume fade", FxClass::Volume},       // A1
    {7, "EnI", "Envelope inline (7 bytes)", FxClass::Volume},      // A2
    {1, "RJm", "Random jump (table of n)", FxClass::Song},         // A3
    {1, "RCl", "Random call (table of n)", FxClass::Song},         // A4
    {1, "FSt", "Set flag", FxClass::Song},                         // A5
    {1, "FCl", "Clear flag", FxClass::Song},                       // A6
    {3, "JFS", "Jump if flag set", FxClass::Song},                 // A7
    {3, "JFC", "Jump if flag clear", FxClass::Song},               // A8
    {1, "WFl", "Wait for flag", FxClass::Song},                    // A9
    {0, "NzO", "Noise on", FxClass::Sys1},                         // AA
    {0, "NzF", "Noise off", FxClass::Sys1},                        // AB
    {1, "EVL", "Echo volume left", FxClass::Sys1},                 // AC
    {1, "EVR", "Echo volume right", FxClass::Sys1},                // AD
    {1, "EFB", "Echo feedback", FxClass::Sys1},                    // AE
    {8, "FIR", "Echo FIR filter", FxClass::Sys1},                  // AF
    {1, "PnS", "Pan sweep rate", FxClass::Panning},                // B0
    {0, "PMO", "Pitch modulation on", FxClass::Sys1},              // B1
    {0, "PMF", "Pitch modulation off", FxClass::Sys1},             // B2
    {2, "Pan", "Volume and pan", FxClass::Panning},                // B3
    {1, "PB4", "Voice param $0355", FxClass::Misc},                // B4
    {1, "SFX", "Trigger sound effect", FxClass::Sys2},             // B5
    {1, "Tmp", "Tempo (timer period)", FxClass::Speed},            // B6
    {0, "Mut", "Silence voice", FxClass::Volume},                  // B7
    {1, "Ext", "Extended command", FxClass::Misc},                 // B8
    {0, "Stk", "Stack check", FxClass::Misc},                      // B9
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
    const int fetch[] = {0xE7, 0x100, 0xBB, 0x100, 0xD0, 0x02, 0xBB, 0x100, 0x08, 0x00, 0x6F};
    int f = find_pattern(ram, 0x200, 0x2000, fetch, 11);
    if (f < 0 || ram[f + 1] != ram[f + 3] || ram[f + 7] != ram[f + 1] + 1) return Layout{};
    const int jt[] = {0x1C, 0xFD, 0xF6, 0x100, 0x100, 0x2D, 0xF6, 0x100, 0x100, 0x2D, 0x6F};
    int j = find_pattern(ram, 0x200, 0x2000, jt, 11);
    if (j < 0) return Layout{};
    int disp = -1;
    for (int a = j - 12; a < j; ++a) if (ram[a] == 0x68 && ram[a + 2] == 0xB0) disp = a;
    if (disp < 0 || ram[disp + 1] < 0xA0) return Layout{};
    L.end_first = ram[disp + 1];
    L.voice_ptr_base = ram[f + 1];

    int a = 0x200;
    for (int v = 0; v < 8; ++v) {
        const int blk[] = {0xF6, 0x100, 0x100, 0xF0, 0x0A, 0x100, 0x100, 0xF6, 0x100, 0x100, 0x100, 0x100};
        int p = find_pattern(ram, a, 0x2000, blk, 12);
        if (p < 0) return Layout{};
        L.song_hi[v] = rd16(ram, p + 1);
        L.song_lo[v] = rd16(ram, p + 8);
        a = p + 12;
    }
    for (int p = a - 0x100; p < a; ++p)
        if (ram[p] == 0x68 && ram[p + 2] == 0xB0 && ram[p + 1] > 0 && ram[p + 1] <= 16) { L.slots = ram[p + 1]; break; }

    const int pt[] = {0xF6, 0x100, 0x100, 0xC4, 0xD9, 0xF6, 0x100, 0x100, 0xC4, 0xDA, 0xFB};
    if (int p = find_pattern(ram, 0x200, 0x2000, pt, 11); p >= 0) { L.pitch_lo = rd16(ram, p + 1); L.pitch_hi = rd16(ram, p + 6); }
    const int mt[] = {0xF6, 0x100, 0x100, 0xFD, 0x6D, 0xE4, 0xD9, 0xCF};
    if (int p = find_pattern(ram, 0x200, 0x2000, mt, 8); p >= 0) L.mult_table = rd16(ram, p + 1);
    const int tt[] = {0xFB, 0x20, 0x60, 0x96, 0x100, 0x100};
    if (int p = find_pattern(ram, 0x200, 0x2000, tt, 6); p >= 0) L.transpose_table = rd16(ram, p + 4);
    if (L.transpose_table && L.mult_table > L.transpose_table) L.instrument_count = std::min(64, L.mult_table - L.transpose_table);
    const int rp[] = {0xFB, 0x100, 0xF6, 0x100, 0x100, 0x9C, 0xF0, 0x100, 0xD6, 0x100, 0x100, 0xF6, 0x100, 0x100, 0xD4, 0x100, 0xF6, 0x100, 0x100, 0xD4, 0x100};
    if (int p = find_pattern(ram, 0x200, 0x2000, rp, 21); p >= 0) { L.stack_ptr_zp = ram[p + 1]; L.stack_base = rd16(ram, p + 17); }
    return L;
}

std::unique_ptr<seq::Driver> detect(const uint8_t* ram) {
    Layout L = detect_layout(ram);
    if (!L.valid()) return nullptr;
    return std::make_unique<FollinDriver>(L);
}

uint8_t FollinDriver::note_byte(int semitone_from_c0) const { return uint8_t(std::clamp(semitone_from_c0 + 2, 1, 0x7F)); }

int FollinDriver::cmd_size(uint8_t op) const {
    if (op < 0x80) return 0;
    if (op >= L.end_first) return 1;
    return 1 + kCmds[op - 0x80].argc;
}
const char* FollinDriver::cmd_name(uint8_t op) const { return op < 0x80 ? "?" : op >= L.end_first ? "End of voice" : kCmds[op - 0x80].name; }
const char* FollinDriver::cmd_code(uint8_t op) const { return op < 0x80 ? "???" : op >= L.end_first ? "End" : kCmds[op - 0x80].code; }
seq::FxClass FollinDriver::cmd_class(uint8_t op) const { return op < 0x80 ? seq::FxClass::Invalid : op >= L.end_first ? seq::FxClass::Song : kCmds[op - 0x80].cls; }

bool FollinDriver::pitch_fx(const Event& e, seq::PitchFx& out) const {
    if (e.type != EventType::Command) return false;
    out = seq::PitchFx{};
    switch (e.b[0]) {
        case 0x8E: case 0x8F:
            out.kind = seq::PitchFx::Slide; out.sticky = true;
            out.delay = e.b[1]; out.units = e.b[0] == 0x8E ? e.b[2] : -int(e.b[2]); out.length = e.b[3];
            return true;
        case 0x91: out.kind = seq::PitchFx::SlideOff; out.sticky = true; return true;
        case 0x90: out.kind = seq::PitchFx::Glide; out.sticky = true; out.units = e.b[1]; return true;
        case 0x95: out.kind = seq::PitchFx::SlideOff; return true;
        case 0x94: out.kind = seq::PitchFx::Portamento; out.rate = e.b[1]; return true;
        case 0x96: out.kind = e.b[1] ? seq::PitchFx::Vibrato : seq::PitchFx::VibratoOff; out.depth = e.b[1]; out.rate = e.b[2]; out.delay = e.b[3]; return true;
        default: return false;
    }
}

bool FollinDriver::note_retriggers(const std::vector<Event>& ev, int i) const {
    for (int k = i - 1; k >= 0; --k) {
        const Event& e = ev[size_t(k)];
        if (e.type != EventType::Command) continue;
        if (e.b[0] == 0x9E) return true;
        if (e.b[0] == 0x9F) return false;
    }
    return true;
}

bool FollinDriver::set_slide(std::vector<Event>& ev, int tick, int dur, int from, int to, int existing) const {
    auto timed = [](const Event& e) { return (e.type == EventType::Note || e.type == EventType::Rest) && e.duration > 0; };
    auto rate_for = [&](int a, int b, int ticks) {
        double d = std::fabs(pitch_units(b) - pitch_units(a));
        return std::clamp(int(std::ceil(d / std::max(ticks, 1))), 1, 255);
    };
    retime(ev);
    if (existing >= 0 && existing < int(ev.size()) && !ev[size_t(existing)].in_sub) {
        Event& x = ev[size_t(existing)];
        if (x.type == EventType::Command && (x.b[0] == 0x8E || x.b[0] == 0x8F)) {
            if (to == from) {
                x.b[0] = 0x91; x.size = 1; x.addr = 0;
                retime(ev);
                return true;
            }
            x.b[0] = to > from ? 0x8E : 0x8F;
            x.b[2] = uint8_t(rate_for(from, to, dur - x.b[1]));
            return true;
        }
        if (x.type == EventType::Note) {
            apply_note_byte(x, note_byte(to));
            const int rate = rate_for(from, to, x.duration);
            for (int k = existing - 1; k >= 0; --k) {
                const Event& e = ev[size_t(k)];
                if (timed(e)) break;
                if (e.type == EventType::Command && e.b[0] == 0x90 && !e.in_sub) { ev[size_t(k)].b[1] = uint8_t(rate); retime(ev); return true; }
            }
            uint8_t g[2] = {0x90, uint8_t(rate)};
            return insert_command_at(ev, x.tick, g, 2);
        }
    }
    if (dur < 2 || to == from) return false;
    seq::stream_prepare(*this, ev, tick);
    int n = -1;
    for (size_t k = 0; k < ev.size(); ++k) if (ev[k].type == EventType::Note && !ev[k].in_sub && ev[k].tick == tick) { n = int(k); break; }
    if (n < 0 || ev[size_t(n)].duration < 2) return false;
    int glide = 0; bool keyon = true;
    for (int k = n - 1; k >= 0; --k) {
        const Event& e = ev[size_t(k)];
        if (e.type != EventType::Command) continue;
        if (e.b[0] == 0x90 && glide == 0) glide = e.b[1] | 0x100;
        if (e.b[0] == 0x9E || e.b[0] == 0x9F) { keyon = e.b[0] == 0x9E; break; }
    }
    glide &= 0xFF;
    const int end = tick + ev[size_t(n)].duration;
    if (!seq::stream_split_at(*this, ev, tick + 1)) return false;
    int j = -1;
    for (size_t k = 0; k < ev.size(); ++k) if (ev[k].type == EventType::Note && !ev[k].in_sub && ev[k].tick == tick + 1) { j = int(k); break; }
    if (j < 0) return false;
    apply_note_byte(ev[size_t(j)], note_byte(to));
    const uint8_t rate = uint8_t(rate_for(from, to, end - (tick + 1)));
    uint8_t restore_g[2] = {0x90, uint8_t(glide)}, keyon_b = 0x9E, legato_b = 0x9F, set_g[2] = {0x90, rate};
    int stream_end = 0;
    for (const Event& e : ev) if (timed(e) && !e.in_sub) stream_end = std::max(stream_end, e.tick + e.duration);
    if (end < stream_end) {
        if (!insert_command_at(ev, end, restore_g, 2)) return false;
        if (keyon && !insert_command_at(ev, end, &keyon_b, 1)) return false;
    }
    if (!insert_command_at(ev, tick + 1, set_g, 2)) return false;
    if (keyon && !insert_command_at(ev, tick + 1, &legato_b, 1)) return false;
    retime(ev);
    return true;
}

std::string FollinDriver::event_text(const Event& e) const {
    char b[96];
    if (e.type == EventType::Note || e.type == EventType::Rest) {
        std::snprintf(b, sizeof b, "%s  %d ticks%s", e.type == EventType::Rest ? "rest" : note_name(e.b[0]).c_str(), e.duration,
                      e.size == 2 ? "" : " (default)");
        return b;
    }
    if (e.type == EventType::Command && e.b[0] == 0xA9 && e.duration > 0) {
        std::snprintf(b, sizeof b, "Wait for flag %d  (%d ticks)", e.b[1], e.duration);
        return b;
    }
    if (e.type == EventType::Command && (e.b[0] == 0xA3 || e.b[0] == 0xA4)) {
        std::snprintf(b, sizeof b, "%s of %d: $%02X%02X ...", e.b[0] == 0xA3 ? "Random jump, one" : "Random call, one", e.b[1], e.b[3], e.b[2]);
        return b;
    }
    if (e.type == EventType::Command && (e.b[0] == 0x81 || e.b[0] == 0x82)) {
        std::snprintf(b, sizeof b, "%s $%02X%02X", kCmds[e.b[0] - 0x80].name, e.b[2], e.b[1]);
        return b;
    }
    return seq::Driver::event_text(e);
}

seq::Instrument FollinDriver::read_instrument(const uint8_t* ram, int index) const {
    seq::Instrument in{};
    in.srcn = uint8_t(index);
    if (L.transpose_table) in.transpose = int8_t(ram[(L.transpose_table + index) & 0xFFFF]);
    if (L.mult_table) in.pitch_hi = ram[(L.mult_table + index) & 0xFFFF];
    in.gain = 0x7F;
    return in;
}

int FollinDriver::note_pitch(const uint8_t* ram, int note, int instrument) const {
    if (!L.pitch_lo || !L.pitch_hi) return 0x1000;
    seq::Instrument in = read_instrument(ram, instrument);
    int n = std::clamp(note + in.transpose, 1, 0x7F);
    int lo = ram[(L.pitch_lo + n) & 0xFFFF], hi = ram[(L.pitch_hi + n) & 0xFFFF];
    int pitch = ((lo * in.pitch_hi) >> 8) + hi * in.pitch_hi + (lo | (hi << 8));
    return std::min(0x3FFF, pitch);
}

bool FollinDriver::preview_regs(const uint8_t* ram, uint8_t note_byte, int instrument, uint8_t regs[8]) const {
    if (!L.pitch_lo) return false;
    int pitch = note_pitch(ram, note_byte, instrument);
    regs[0] = 0x30; regs[1] = 0x30;
    regs[2] = uint8_t(pitch & 0xFF); regs[3] = uint8_t(pitch >> 8);
    regs[4] = uint8_t(instrument); regs[5] = 0x00; regs[6] = 0x00; regs[7] = 0x7F;
    return true;
}

void FollinDriver::parse_slot(const uint8_t* ram, const uint16_t starts[8], seq::Track out[8]) const {
    struct Frame { int ret; bool loop; int count; };
    struct Voice {
        int pc = 0, wait = 1, default_dur = 0, stall = 0;
        bool one_shot = false, done = false;
        std::vector<Frame> stack;
        std::vector<uint8_t> iter;
        seq::Track* t = nullptr;
    };
    Voice vs[8];
    bool flags[256] = {};
    int active = 0;
    for (int v = 0; v < 8; ++v) {
        out[v] = seq::Track{};
        out[v].addr = starts[v];
        vs[v].t = &out[v];
        vs[v].pc = starts[v];
        vs[v].done = starts[v] == 0;
        if (!vs[v].done) { vs[v].iter.assign(0x10000, 0); ++active; }
    }
    auto loop_target = [&](Voice& V, int addr) {
        for (size_t i = 0; i < V.t->events.size(); ++i) if (V.t->events[i].addr == addr) return int(i);
        return 0;
    };
    auto revisits = [&](Voice& V, int addr) { return V.iter[addr & 0xFFFF] > 0; };
    auto emit = [&](Voice& V, Event e) {
        bool called = false;
        for (const Frame& f : V.stack) if (!f.loop) called = true;
        e.in_sub = called || V.iter[e.addr] > 0;
        e.nest = uint8_t(std::min<size_t>(V.stack.size(), 255));
        e.sub_iter = V.iter[e.addr];
        if (V.iter[e.addr] < 255) ++V.iter[e.addr];
        V.t->events.push_back(e);
    };
    auto finish = [&](Voice& V, bool loops, int loop_event) {
        V.done = true; V.t->loops = loops; V.t->terminated = true; V.t->loop_event = loop_event; --active;
    };
    for (int tick = 0; active > 0 && tick < 200000; ++tick) {
        for (int v = 0; v < 8; ++v) {
            Voice& V = vs[v];
            if (V.done) continue;
            if (--V.wait > 0) continue;
            int idle = 0;
            for (;;) {
                if (V.t->events.size() >= 60000 || ++idle > 2000) { V.t->truncated = true; finish(V, false, -1); break; }
                uint8_t b = ram[V.pc & 0xFFFF];
                if (b < 0x80) {
                    int dur = V.default_dur, size = 1;
                    if (V.one_shot || V.default_dur == 0) { dur = ram[(V.pc + 1) & 0xFFFF]; size = 2; V.one_shot = false; }
                    if (dur == 0) dur = 256;
                    Event e = make_event(b == 0 ? EventType::Rest : EventType::Note, uint16_t(V.pc), ram, size, tick);
                    e.duration = dur;
                    emit(V, e);
                    V.pc += size;
                    V.wait = dur;
                    V.stall = 0;
                    break;
                }
                const uint8_t op = b;
                int size = cmd_size(op);
                if (op == 0x80 || op >= L.end_first) { emit(V, make_event(EventType::End, uint16_t(V.pc), ram, 1, tick)); finish(V, false, -1); break; }
                const uint8_t* args = ram + ((V.pc + 1) & 0xFFFF);
                if (op == 0xA3 || op == 0xA4) size = 2 + args[0] * 2;
                emit(V, make_event(EventType::Command, uint16_t(V.pc), ram, size, tick));
                int next = V.pc + size;
                bool stop = false;
                switch (op) {
                    case 0x81: { int target = rd16(ram, V.pc + 1); if (revisits(V, target)) { finish(V, true, loop_target(V, target)); stop = true; } else next = target; break; }
                    case 0x82: if (V.stack.size() > 32) { finish(V, false, -1); stop = true; } else { V.stack.push_back({V.pc + 3, false, 0}); next = rd16(ram, V.pc + 1); } break;
                    case 0x83:
                        while (!V.stack.empty() && V.stack.back().loop) V.stack.pop_back();
                        if (V.stack.empty()) { finish(V, false, -1); stop = true; } else { next = V.stack.back().ret; V.stack.pop_back(); }
                        break;
                    case 0x84: V.stack.push_back({V.pc + 2, true, args[0]}); break;
                    case 0x85: {
                        if (V.stack.empty() || !V.stack.back().loop) { finish(V, false, -1); stop = true; break; }
                        Frame& f = V.stack.back();
                        if (--f.count <= 0) V.stack.pop_back(); else next = f.ret;
                        break;
                    }
                    case 0x86: V.default_dur = args[0]; break;
                    case 0x87: V.one_shot = true; break;
                    case 0xA3: { int target = rd16(ram, V.pc + 2); if (revisits(V, target)) { finish(V, true, loop_target(V, target)); stop = true; } else next = target; break; }
                    case 0xA4: if (V.stack.size() > 32) { finish(V, false, -1); stop = true; } else { V.stack.push_back({V.pc + size, false, 0}); next = rd16(ram, V.pc + 2); } break;
                    case 0xA5: flags[args[0]] = true; break;
                    case 0xA6: flags[args[0]] = false; break;
                    case 0xA7: case 0xA8: {
                        bool taken = op == 0xA7 ? flags[args[0]] : !flags[args[0]];
                        if (taken) { int target = rd16(ram, V.pc + 2); if (revisits(V, target)) { finish(V, true, loop_target(V, target)); stop = true; } else next = target; }
                        break;
                    }
                    case 0xA9:
                        if (!flags[args[0]]) {
                            if (V.stall > 0) V.t->events.pop_back();
                            Event& w = V.t->events.back();
                            w.duration = V.stall + 1;
                            if (++V.stall > 4000) { V.t->truncated = true; finish(V, false, -1); }
                            else V.wait = 1;
                            stop = true;
                        } else V.stall = 0;
                        break;
                    default: break;
                }
                if (stop) break;
                V.pc = next;
            }
        }
    }
    for (int v = 0; v < 8; ++v) {
        seq::Track& t = out[v];
        if (!t.addr) continue;
        int end = 0;
        for (const Event& e : t.events) end = std::max(end, e.tick + e.duration);
        t.total_ticks = end;
        t.used_events = int(t.events.size());
        t.end_addr = t.addr;
    }
}

seq::Track FollinDriver::parse_stream(const uint8_t* ram, uint16_t start) const {
    uint16_t starts[8] = {start, 0, 0, 0, 0, 0, 0, 0};
    seq::Track out[8];
    parse_slot(ram, starts, out);
    return out[0];
}

std::vector<seq::Song> FollinDriver::find_songs(const uint8_t* ram, const uint8_t* dsp) const {
    (void)dsp;
    std::vector<seq::Song> songs;
    for (int slot = 0; slot < L.slots; ++slot) {
        seq::Pattern pat;
        bool any = false, loops = false;
        uint16_t starts[8];
        for (int v = 0; v < 8; ++v) starts[v] = uint16_t(ram[(L.song_lo[v] + slot) & 0xFFFF] | (ram[(L.song_hi[v] + slot) & 0xFFFF] << 8));
        parse_slot(ram, starts, pat.tracks);
        for (int v = 0; v < 8; ++v) {
            if (pat.tracks[v].total_ticks > 0) any = true;
            if (pat.tracks[v].loops) loops = true;
            pat.length_ticks = std::max(pat.length_ticks, pat.tracks[v].total_ticks);
        }
        if (!any) continue;
        seq::Song sg;
        sg.order_addr = uint16_t(L.song_lo[0] + slot);
        sg.order_end = sg.order_addr + 1;
        pat.addr = sg.order_addr;
        sg.orders.push_back({sg.order_addr, pat.addr});
        sg.patterns.push_back(std::move(pat));
        sg.loop_count = loops ? 0xFF : 0;
        sg.loop_to = loops ? 0 : -1;
        char b[64];
        std::snprintf(b, sizeof b, "slot %d (%d ticks%s)", slot, sg.patterns[0].length_ticks, loops ? ", loops" : "");
        sg.label = b;
        songs.push_back(std::move(sg));
    }
    return songs;
}

int FollinDriver::pick_current_song(const uint8_t* ram, const std::vector<seq::Song>& songs) const {
    int slot = ram[L.slot_addr];
    for (size_t i = 0; i < songs.size(); ++i)
        if (songs[i].order_addr == uint16_t(L.song_lo[0] + slot)) return int(i);
    for (size_t i = 0; i < songs.size(); ++i) {
        const seq::Pattern& p = songs[i].patterns[0];
        for (int v = 0; v < 8; ++v) {
            uint16_t ptr = rd16(ram, L.voice_ptr_base + v * 2);
            for (const Event& e : p.tracks[v].events)
                if (e.addr == ptr) return int(i);
        }
    }
    return songs.empty() ? -1 : 0;
}

seq::Position FollinDriver::locate(const uint8_t* ram, const seq::Song& song, const seq::Position* prev) const {
    seq::Position pos;
    pos.track_ptr_base = L.voice_ptr_base;
    pos.order_index = 0;
    uint16_t ptr[8];
    for (int v = 0; v < 8; ++v) ptr[v] = rd16(ram, L.voice_ptr_base + v * 2);
    seq::resolve_stream_position(song.patterns[0], ptr, prev, false, pos);
    return pos;
}

double FollinDriver::ticks_per_second(const uint8_t* ram) const {
    int t = ram[L.tempo_addr];
    return t ? 16000.0 / t : 0;
}

bool FollinDriver::tempo_writes(const uint8_t* ram, double tps, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    (void)ram;
    if (tps <= 0) return false;
    int latch = int(16000.0 / tps + 0.5);
    out.push_back({L.tempo_addr, uint8_t(std::clamp(latch, 1, 255))});
    return true;
}

std::vector<uint8_t> FollinDriver::serialize_track(const std::vector<Event>& events) const {
    std::vector<uint8_t> out;
    for (const Event& e : events) {
        if (e.in_sub) continue;
        for (int i = 0; i < e.size; ++i) out.push_back(e.b[i]);
    }
    return out;
}

void FollinDriver::retime(std::vector<Event>& ev) const {
    int tick = 0, default_dur = 0;
    for (Event& e : ev) {
        e.tick = tick;
        if (e.type == EventType::Note || e.type == EventType::Rest) {
            e.duration = e.size == 2 ? e.b[1] : default_dur;
            if (e.duration == 0) e.duration = 256;
            tick += e.duration;
        } else if (e.type == EventType::Command && e.b[0] == 0xA9) {
            tick += e.duration;
        } else {
            e.duration = 0;
            if (e.type == EventType::Command && e.b[0] == 0x86) default_dur = e.b[1];
        }
    }
}

bool FollinDriver::set_duration(std::vector<Event>& ev, int i, int dur) const {
    if (i < 0 || i >= int(ev.size()) || dur < 1 || dur > 256) return false;
    Event& e = ev[size_t(i)];
    e.addr = 0;
    if (e.size == 1) {
        Event one{};
        one.type = EventType::Command; one.addr = 0; one.b[0] = 0x87; one.size = 1;
        ev.insert(ev.begin() + i, one);
        ++i;
    }
    Event& n = ev[size_t(i)];
    n.size = 2;
    n.b[1] = uint8_t(dur == 256 ? 0 : dur);
    n.duration = dur;
    return true;
}

void FollinDriver::track_pointer_writes(const seq::Song& song, int pattern_idx, int voice, uint16_t dest, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    (void)pattern_idx;
    int slot = int(song.order_addr) - int(L.song_lo[0]);
    out.push_back({uint16_t(L.song_lo[voice] + slot), uint8_t(dest & 0xFF)});
    out.push_back({uint16_t(L.song_hi[voice] + slot), uint8_t(dest >> 8)});
}

void FollinDriver::reclaimable_ranges(const uint8_t* ram, std::vector<std::pair<uint16_t, uint16_t>>& out) const { (void)ram; (void)out; }

void FollinDriver::live_state_writes(const uint8_t* ram, const seq::Position& pos, int voice, uint16_t ptr, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    seq::Driver::live_state_writes(ram, pos, voice, ptr, remap, out);
    if (!L.stack_base) return;
    int y = ram[(L.stack_ptr_zp + voice * 2) & 0xFF];
    for (int k = 0; k < 8 && y >= 3; ++k, y -= 3) seq::remap_word(ram, uint16_t(L.stack_base + y), remap, out);
}

bool FollinDriver::set_note_at(std::vector<Event>& ev, int tick, uint8_t note_byte, int pattern_len) const {
    (void)pattern_len;
    return seq::stream_set_note_at(*this, ev, tick, note_byte, pattern_len);
}

bool FollinDriver::insert_command_at(std::vector<Event>& ev, int tick, const uint8_t* bytes, int size) const {
    return seq::stream_insert_command_at(*this, ev, tick, bytes, size);
}

bool FollinDriver::set_instrument(std::vector<Event>& ev, int tick0, int tick1, uint8_t ins) const {
    return seq::stream_set_instrument(*this, ev, tick0, tick1, ins);
}

bool FollinDriver::remove_span(std::vector<Event>& ev, int tick, int ticks, bool keep_length) const {
    (void)keep_length;
    return seq::stream_remove_span(*this, ev, tick, ticks);
}

bool FollinDriver::insert_span(std::vector<Event>& ev, int tick, int ticks, uint8_t byte) const {
    return seq::stream_insert_span(*this, ev, tick, ticks, byte);
}

}
