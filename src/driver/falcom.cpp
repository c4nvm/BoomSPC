#include "falcom.hpp"

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

namespace falcom {
namespace {
const int W = 0x100;

const CmdSpec kCmds[0x2D] = {
    {1, "Oct", "Octave 0", FxClass::Pitch},                              // D0
    {1, "Oct", "Octave 1", FxClass::Pitch},                              // D1
    {1, "Oct", "Octave 2", FxClass::Pitch},                              // D2
    {1, "Oct", "Octave 3", FxClass::Pitch},                              // D3
    {1, "Oct", "Octave 4", FxClass::Pitch},                              // D4
    {1, "Oct", "Octave 5", FxClass::Pitch},                              // D5
    {1, "Oct", "Octave 6", FxClass::Pitch},                              // D6
    {2, "Tmp", "Tempo", FxClass::Speed},                                 // D7
    {2, "Ins", "Instrument", FxClass::Instrument},                       // D8
    {4, "Vib", "Vibrato (delay, depth, rate)", FxClass::Pitch},          // D9
    {2, "VbO", "Vibrato on/off", FxClass::Pitch},                        // DA
    {4, "Nop", "(no effect)", FxClass::Misc},                            // DB
    {2, "Nop", "(no effect)", FxClass::Misc},                            // DC
    {2, "Gat", "Gate (cut n/256 of the length)", FxClass::Time},         // DD
    {2, "Vol", "Volume", FxClass::Volume},                               // DE
    {1, "Vl-", "Volume -8", FxClass::Volume},                            // DF
    {1, "Vl-", "Volume -1", FxClass::Volume},                            // E0
    {1, "Vl-", "Volume -2", FxClass::Volume},                            // E1
    {1, "Vl-", "Volume -4", FxClass::Volume},                            // E2
    {1, "Vl+", "Volume +8", FxClass::Volume},                            // E3
    {1, "Vl+", "Volume +1", FxClass::Volume},                            // E4
    {1, "Vl+", "Volume +2", FxClass::Volume},                            // E5
    {1, "Vl+", "Volume +4", FxClass::Volume},                            // E6
    {2, "Pan", "Panning", FxClass::Panning},                             // E7
    {1, "Pn-", "Pan -8", FxClass::Panning},                              // E8
    {1, "Pn+", "Pan +8", FxClass::Panning},                              // E9
    {4, "PnL", "Pan LFO (delay, depth, rate)", FxClass::Panning},        // EA
    {2, "PLO", "Pan LFO on/off", FxClass::Panning},                      // EB
    {2, "Tun", "Tuning", FxClass::Pitch},                                // EC
    {3, "Lp[", "Repeat start (count, counter)", FxClass::Song, 0, true}, // ED
    {3, "Brk", "Leave the repeat on its last pass", FxClass::Song, 1, true},   // EE
    {3, "Lp]", "Repeat end", FxClass::Song, 1, true},                    // EF
    {4, "PEn", "Pitch envelope (delay, depth, rate)", FxClass::Pitch},   // F0
    {2, "PEO", "Pitch envelope on/off", FxClass::Pitch},                 // F1
    {3, "ADS", "ADSR", FxClass::Sys2},                                   // F2
    {2, "Gai", "GAIN", FxClass::Sys2},                                   // F3
    {2, "Noi", "Noise frequency", FxClass::Sys1},                        // F4
    {2, "PMd", "Pitch modulation on/off", FxClass::Sys1},                // F5
    {2, "Ech", "Echo on/off", FxClass::Sys1},                            // F6
    {4, "EcP", "Echo parameters (delay, feedback, FIR)", FxClass::Sys1}, // F7
    {2, "EVO", "Echo volume on/off", FxClass::Sys1},                     // F8
    {3, "EcV", "Echo volume (L, R)", FxClass::Sys1},                     // F9
    {10, "FIR", "Echo filter preset overwrite", FxClass::Sys1},          // FA
    {2, "Nop", "(no effect)", FxClass::Misc},                            // FB
    {3, "Jmp", "Jump (0 = end of track)", FxClass::Song, 1},             // FC
};
const CmdSpec kNop = {1, "Nop", "(no effect)", FxClass::Misc};
const CmdSpec kUnknown = {1, "???", "Unknown opcode (not in the driver's table)", FxClass::Misc};
}

Layout detect_layout(const uint8_t* ram) {
    Layout L;
    const int load[] = {0x4B, W, 0xF7, W, 0xD4, W, 0xC4, 0x00, 0xFC, 0xF7, W, 0xC4, 0x01, 0x60, 0x84, W, 0xD4, W, 0x09, 0x01, 0x00, 0xF0, 0x03, 0x18, 0x80, W, 0xFC, 0x3D, 0xC8, 0x08, 0xD0, 0xE0};
    int p = find_pattern(ram, 0x200, 0x4000, load, 32);
    if (p < 0 || ram[p + 3] != ram[p + 10] || ram[p + 15] != ram[p + 3] + 1) return Layout{};
    L.header_zp = ram[p + 3];
    L.ptr_lo_zp = ram[p + 5];
    L.ptr_hi_zp = ram[p + 17];
    const int tempo[] = {0x3F, W, W, 0x78, 0x08, W, 0xF0, W, 0xB0, W, 0x78, 0x00, W, 0xD0, 0x02, 0xC4, W};
    if (int t = find_pattern(ram, 0x200, 0x4000, tempo, 17); t >= 0) L.tempo_zp = ram[t + 16];
    return L;
}

std::unique_ptr<seq::Driver> detect(const uint8_t* ram) {
    Layout L = detect_layout(ram);
    if (!L.valid()) return nullptr;
    return std::make_unique<FalcomDriver>(L);
}

const CmdSpec& FalcomDriver::spec(uint8_t op) const {
    if (op < 0xD0 || op > 0xFC) return kUnknown;
    return kCmds[op - 0xD0];
}

std::vector<uint16_t> FalcomDriver::song_headers(const uint8_t* ram) const {
    const uint16_t h = rd16(ram, L.header_zp);
    if (h < 0x200 || h >= 0xFF00) return {};
    for (int v = 0; v < 8; ++v) if (track_start(ram, h, v)) return {h};
    return {};
}

uint16_t FalcomDriver::track_start(const uint8_t* ram, uint16_t header, int v) const {
    const uint16_t off = rd16(ram, header + v * 2);
    if (!off) return 0;
    const int a = (header + off) & 0xFFFF;
    return a >= 0x200 && a < 0xFF00 ? uint16_t(a) : 0;
}

State FalcomDriver::initial_state(const uint8_t* ram, uint16_t header, int voice) const {
    (void)voice;
    State s;
    for (int i = 0; i < 7; ++i) lens_[i] = ram[(header + 0x18 + i) & 0xFFFF];
    data_lo_ = std::min(data_lo_ == 0x200 ? int(header) : data_lo_, int(header));
    for (int v = 0; v < 8; ++v) if (uint16_t a = track_start(ram, header, v)) data_lo_ = std::min(data_lo_, int(a));
    return s;
}

double FalcomDriver::ticks_per_second(const uint8_t* ram) const {
    const int tempo = L.tempo_zp ? ram[L.tempo_zp] : 0;
    return ram[0xFA] && tempo ? 8000.0 / ram[0xFA] * tempo / 256.0 : 0;
}

bool FalcomDriver::tempo_writes(const uint8_t* ram, double tps, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    if (tps <= 0 || !ram[0xFA] || !L.tempo_zp) return false;
    out.push_back({L.tempo_zp, uint8_t(std::clamp(int(tps * 256.0 * ram[0xFA] / 8000.0 + 0.5), 1, 255))});
    return true;
}

// State: oct = octave, x[1] = last pitch, flags bit 0 = the last note was slurred.
void FalcomDriver::decode(const uint8_t* p, int pc, State& s, Event& e, Flow& f) const {
    const uint8_t b = p[0];
    e.b[0] = b;
    e.size = 1;
    e.pitch = -1;
    e.type = EventType::Command;
    if (b < 0xD0) {
        const int key = b >> 4, idx = b & 7;
        int len = idx ? lens_[idx - 1] : p[1];
        if (!idx) { e.b[1] = p[1]; e.size = 2; }
        e.duration = len;
        if (key == 12) { e.type = EventType::Rest; s.flags &= ~1; return; }
        e.pitch = s.oct * 12 + key + 12;
        e.type = (s.flags & 1) && e.pitch == s.x[1] ? EventType::Tie : EventType::Note;
        s.x[1] = e.pitch;
        s.flags = (s.flags & ~1) | ((b & 8) ? 1 : 0);
        return;
    }
    const CmdSpec& sp = spec(b);
    e.size = sp.size;
    for (int i = 1; i < e.size; ++i) e.b[i] = p[i];
    const int rel = (pc + 3 + int16_t(p[1] | (p[2] << 8))) & 0xFFFF;
    if (b <= 0xD6) { s.oct = b - 0xD0; return; }
    switch (b) {
        case 0xED: f.kind = Flow::RepStart; f.count = p[1] ? p[1] : 256; break;
        case 0xEE: f.kind = Flow::RepBreak; f.target = rel; break;
        case 0xEF: f.kind = Flow::RepEnd; break;
        case 0xFC:
            if (!(p[1] | p[2])) { f.kind = Flow::End; e.type = EventType::End; }
            else { f.kind = Flow::Jump; f.target = rel; }
            break;
        default: break;
    }
}

int FalcomDriver::jump_target(const Event& e) const {
    if (e.type != EventType::Command || !e.addr) return -1;
    const uint8_t op = e.b[0];
    if (op != 0xEE && op != 0xEF && op != 0xFC) return -1;
    const int rel = int16_t(e.b[1] | (e.b[2] << 8));
    if (op == 0xFC && !rel) return -1;
    const int t = (e.addr + 3 + rel) & 0xFFFF;
    return op == 0xEF ? (t - 2) & 0xFFFF : t;   // the repeat end points at the ED's counter byte
}

void FalcomDriver::set_jump_target(Event& e, uint16_t addr) const {
    if (!e.addr || jump_target(e) < 0) return;
    const int rel = int(addr) + (e.b[0] == 0xEF ? 2 : 0) - int(e.addr + 3);
    e.b[1] = uint8_t(rel & 0xFF);
    e.b[2] = uint8_t((rel >> 8) & 0xFF);
}

std::vector<uint8_t> FalcomDriver::serialize_relocated(const std::vector<Event>& ev, uint16_t dest, std::vector<int>* offsets) const {
    std::vector<int> off;
    std::vector<uint8_t> out = stream::Driver::serialize_relocated(ev, dest, &off);
    for (size_t i = 0; i < ev.size(); ++i) {   // offsets are taken from the command's new address
        if (ev[i].in_sub || off[i] < 0 || ev[i].type != EventType::Command) continue;
        const uint8_t op = ev[i].b[0];
        if (op != 0xEE && op != 0xEF && op != 0xFC) continue;
        const int t = jump_target(ev[i]);
        const int j = relocated_target(ev, int(i), off);
        if (t < 0 && j < 0) continue;
        const int target = (j >= 0 ? dest + off[size_t(j)] : t) + (op == 0xEF ? 2 : 0);
        const int rel = target - int(dest + off[i] + 3);
        out[size_t(off[i] + 1)] = uint8_t(rel & 0xFF);
        out[size_t(off[i] + 2)] = uint8_t((rel >> 8) & 0xFF);
    }
    if (offsets) *offsets = off;
    return out;
}

uint16_t FalcomDriver::live_ptr(const uint8_t* ram, const seq::Position& pos, int v) const {
    (void)pos;
    return uint16_t(ram[(L.ptr_lo_zp + v) & 0xFF] | (ram[(L.ptr_hi_zp + v) & 0xFF] << 8));
}

void FalcomDriver::live_state_writes(const uint8_t* ram, const seq::Position& pos, int voice, uint16_t ptr, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    int mapped = remap.find(live_ptr(ram, pos, voice));
    if (mapped >= 0) ptr = uint16_t(mapped);
    out.push_back({uint16_t(L.ptr_lo_zp + voice), uint8_t(ptr & 0xFF)});
    out.push_back({uint16_t(L.ptr_hi_zp + voice), uint8_t(ptr >> 8)});
}

void FalcomDriver::track_pointer_writes(const seq::Song& song, int pattern_idx, int voice, uint16_t dest, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    (void)pattern_idx;
    const uint16_t off = uint16_t(dest - song.order_addr);
    out.push_back({uint16_t(song.order_addr + voice * 2), uint8_t(off & 0xFF)});
    out.push_back({uint16_t(song.order_addr + voice * 2 + 1), uint8_t(off >> 8)});
}

bool FalcomDriver::transpose_event(Event& e, int semis) const {
    if (e.type != EventType::Note) return false;
    const int key = (e.b[0] >> 4) + semis;
    if (key < 0 || key > 11) return false;
    e.b[0] = uint8_t((e.b[0] & 0x0F) | (key << 4));
    if (e.pitch >= 0) e.pitch += semis;
    return true;
}

void FalcomDriver::apply_note_byte(Event& e, uint8_t byte) const {
    const int key = byte >> 4, old = e.b[0] >> 4;
    e.b[0] = uint8_t((e.b[0] & 0x0F) | (key << 4));
    if (key == 12) { e.type = EventType::Rest; e.pitch = -1; e.b[0] &= uint8_t(~8); return; }
    e.type = EventType::Note;
    if (e.pitch >= 0 && old < 12) e.pitch += key - old;
    else e.pitch = note_semitone(byte);
}

bool FalcomDriver::enter_note(std::vector<Event>& ev, int tick, int semitone, int pattern_len) const {
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
    const int rel = semitone - 12;
    int oct = s.oct, key = rel - oct * 12;
    if (key < 0 || key > 11) {
        oct = rel >= 0 ? rel / 12 : -1;
        key = rel - oct * 12;
        if (oct < 0 || oct > 6) return false;
    }
    if (!set_note_at(ev, tick, uint8_t(key << 4), pattern_len)) return false;
    if (oct == s.oct) return true;
    idx = -1;
    for (size_t i = 0; i < ev.size(); ++i) if (ev[i].duration > 0 && !ev[i].in_sub && ev[i].tick == tick) { idx = int(i); break; }
    if (idx < 0) return false;
    bool later = false;
    for (size_t i = size_t(idx) + 1; i < ev.size(); ++i) if (ev[i].type == EventType::Note && !ev[i].in_sub) { later = true; break; }
    if (later) ev.insert(ev.begin() + idx + 1, stream::cmd_event(uint8_t(0xD0 + s.oct)));
    ev.insert(ev.begin() + idx, stream::cmd_event(uint8_t(0xD0 + oct)));
    retime(ev);
    return true;
}

bool FalcomDriver::set_duration(std::vector<Event>& ev, int i, int dur) const {
    if (i < 0 || i >= int(ev.size()) || dur < 1 || dur > 4096) return false;
    Event& e = ev[size_t(i)];
    if (e.duration <= 0) return false;
    const int first = std::min(dur, 255);
    e.b[0] &= 0xF8; e.b[1] = uint8_t(first); e.size = 2;
    e.duration = first;
    e.addr = 0;
    std::vector<Event> tail;
    Event* last = &e;
    for (int left = dur - first; left > 0;) {
        const int n = std::min(left, 255);
        Event t{};
        t.type = e.type == EventType::Rest ? EventType::Rest : EventType::Tie;
        t.b[0] = uint8_t(e.b[0] & 0xF0); t.b[1] = uint8_t(n); t.size = 2; t.duration = n; t.pitch = -1;
        if (e.type != EventType::Rest) last->b[0] |= 8;   // slur into the continuation
        tail.push_back(t);
        last = &tail.back();
        left -= n;
    }
    ev.insert(ev.begin() + i + 1, tail.begin(), tail.end());
    return true;
}

bool FalcomDriver::note_retriggers(const std::vector<Event>& ev, int i) const {
    (void)ev; (void)i;
    return true;
}

std::string FalcomDriver::event_text(const Event& e) const {
    char b[96];
    if (e.type == EventType::Note) { std::snprintf(b, sizeof b, "%s  %d ticks%s", note_name(e).c_str(), e.duration, (e.b[0] & 8) ? "  slur" : ""); return b; }
    if (e.type == EventType::Rest) { std::snprintf(b, sizeof b, "rest  %d ticks", e.duration); return b; }
    if (e.type == EventType::Tie) { std::snprintf(b, sizeof b, "%s  %d ticks (slurred)", note_name(e).c_str(), e.duration); return b; }
    if (e.type == EventType::Command) {
        if (e.b[0] <= 0xD6) { std::snprintf(b, sizeof b, "Octave %d", e.b[0] - 0xD0); return b; }
        switch (e.b[0]) {
            case 0xED: std::snprintf(b, sizeof b, "Repeat x%d (counter %d)", e.b[1] ? e.b[1] : 256, e.b[2]); return b;
            case 0xEE: std::snprintf(b, sizeof b, "Break to %+d", int16_t(e.b[1] | (e.b[2] << 8))); return b;
            case 0xEF: std::snprintf(b, sizeof b, "Repeat end %+d", int16_t(e.b[1] | (e.b[2] << 8))); return b;
            case 0xFC: if (e.b[1] | e.b[2]) { std::snprintf(b, sizeof b, "Jump %+d", int16_t(e.b[1] | (e.b[2] << 8))); return b; } return "End of track";
            default: break;
        }
    }
    return seq::Driver::event_text(e);
}

bool FalcomDriver::pitch_fx(const Event& e, seq::PitchFx& out) const {
    if (e.type != EventType::Command || e.b[0] != 0xDA) return false;
    out = seq::PitchFx{};
    out.kind = e.b[1] ? seq::PitchFx::Vibrato : seq::PitchFx::VibratoOff;
    return true;
}

}
