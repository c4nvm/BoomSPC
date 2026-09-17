#include "pandora.hpp"

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

namespace pandora {
namespace {
const int W = 0x100;

const CmdSpec kCmds[0x17] = {
    {2, "Tmp", "Tempo (BPM)", FxClass::Speed},                           // E0
    {2, "Tun", "Tuning", FxClass::Pitch},                                // E1
    {2, "Trn", "Transpose", FxClass::Pitch},                             // E2
    {2, "Pan", "Panning", FxClass::Panning},                             // E3
    {1, "Oc+", "Octave up", FxClass::Pitch},                             // E4
    {1, "Oc-", "Octave down", FxClass::Pitch},                           // E5
    {1, "Vl+", "Volume up", FxClass::Volume},                            // E6
    {1, "Vl-", "Volume down", FxClass::Volume},                          // E7
    {6, "VbP", "Vibrato parameters", FxClass::Pitch},                    // E8
    {2, "Vib", "Vibrato on/off", FxClass::Pitch},                        // E9
    {1, "EcF", "Echo off", FxClass::Sys1},                               // EA
    {1, "EcO", "Echo on", FxClass::Sys1},                                // EB
    {2, "Lp[", "Repeat start (count, FF = forever)", FxClass::Song, 0, true},   // EC
    {1, "Lp]", "Repeat end", FxClass::Song, 0, true},                    // ED
    {1, "Brk", "Leave the repeat on its last pass", FxClass::Song, 0, true},    // EE
    {1, "Nop", "(no effect)", FxClass::Misc},                            // EF
    {1, "Nop", "(no effect)", FxClass::Misc},                            // F0
    {3, "DSP", "DSP register write", FxClass::Sys1},                     // F1
    {2, "Noi", "Noise / channel type", FxClass::Sys1},                   // F2
    {6, "ADS", "ADSR (AR DR SR SL x)", FxClass::Sys2},                   // F3
    {2, "???", "Unknown", FxClass::Misc},                                // F4
    {1, "End", "End of track", FxClass::Song},                           // F5
    {2, "Vol", "Volume", FxClass::Volume},                               // F6
};
const CmdSpec kOct = {1, "Oct", "Octave", FxClass::Pitch};
const CmdSpec kGate = {1, "Gat", "Gate (n/8)", FxClass::Time};
const CmdSpec kVolT = {1, "VlT", "Volume from table", FxClass::Volume};
const CmdSpec kIns = {1, "Ins", "Instrument", FxClass::Instrument};
const CmdSpec kNop = {1, "Nop", "(no effect)", FxClass::Misc};
}

Layout detect_layout(const uint8_t* ram) {
    Layout L;
    const int load[] = {0x8D, 0x10, 0xFC, 0xF7, W, 0xDC, 0x37, W, 0x68, 0xFF, 0xF0, W};
    const int load2[] = {0x8D, 0x10, 0x7D, 0xF0, W, 0x6D, 0xF7, W, 0xFC, 0xC4, W, 0xF7, W, 0xFC, 0xC4, W, 0xBC, 0xF0, W};
    int n = 0;
    for (int a = 0x200; a < 0x10000 - 20 && n < 2; ++a) {
        if (find_pattern(ram, a, a + 12, load, 12) == a && ram[a + 4] == ram[a + 7]) { L.header_zp[n++] = ram[a + 4]; a += 11; }
        else if (find_pattern(ram, a, a + 19, load2, 19) == a && ram[a + 7] == ram[a + 12]) { L.header_zp[n++] = ram[a + 7]; a += 18; }
    }
    if (!n) return Layout{};
    const int slots[] = {0x5D, 0xF5, W, W, 0x5D, 0x6F};
    int s = find_pattern(ram, 0x200, 0x10000 - 6, slots, 6);
    if (s < 0) return Layout{};
    const uint16_t table = rd16(ram, s + 2);
    for (int v = 0; v < 8; ++v) L.slot[v] = ram[(table + v) & 0xFFFF];
    const int stk[] = {0x8D, W, 0xCF, 0x60, 0x88, W, 0xD4, W, 0xDD, 0x88, W, 0xD4, W};
    if (int k = find_pattern(ram, 0x200, 0x10000 - 13, stk, 13); k >= 0) { L.stack_stride = ram[k + 1]; L.stack_base = uint16_t(ram[k + 5] | (ram[k + 10] << 8)); }
    return L;
}

std::unique_ptr<seq::Driver> detect(const uint8_t* ram) {
    Layout L = detect_layout(ram);
    if (!L.valid()) return nullptr;
    return std::make_unique<PandoraDriver>(L);
}

const CmdSpec& PandoraDriver::spec(uint8_t op) const {
    if (op < 0x40) return kNop;
    if (op < 0x48) return kOct;
    if (op < 0x50) return kGate;
    if (op < 0x60) return kVolT;
    if (op < 0xE0) return kIns;
    if (op < 0xE0 + 0x17) return kCmds[op - 0xE0];
    return kNop;
}

std::vector<uint16_t> PandoraDriver::song_headers(const uint8_t* ram) const {
    std::vector<uint16_t> out;
    for (uint8_t zp : L.header_zp) {
        if (!zp) continue;
        const uint16_t h = rd16(ram, zp);
        if (h < 0x200 || h >= 0xFF00 || (ram[(h + 7) & 0xFFFF] & 3) || !ram[(h + 7) & 0xFFFF]) continue;
        bool any = false;
        for (int v = 0; v < 8; ++v) if (track_start(ram, h, v)) any = true;
        if (any && std::find(out.begin(), out.end(), h) == out.end()) out.push_back(h);
    }
    return out;
}

uint16_t PandoraDriver::track_start(const uint8_t* ram, uint16_t header, int v) const {
    const uint16_t off = rd16(ram, header + 0x10 + v * 2);
    if (off == 0xFFFF) return 0;
    const uint16_t a = uint16_t(header + off);
    return a >= 0x200 && a > header ? a : 0;
}

State PandoraDriver::initial_state(const uint8_t* ram, uint16_t header, int voice) const {
    (void)voice;
    State s;
    s.oct = 3;
    s.len = 1;
    ppqn_ = std::max(1, ram[(header + 7) & 0xFFFF] / 4);
    data_lo_ = std::min(data_lo_ == 0x200 ? int(header) : data_lo_, int(header));
    for (int v = 0; v < 8; ++v) if (uint16_t a = track_start(ram, header, v)) data_lo_ = std::min(data_lo_, int(a));
    return s;
}

void PandoraDriver::decode(const uint8_t* p, int pc, State& s, Event& e, Flow& f) const {
    (void)pc;
    const uint8_t b = p[0];
    e.b[0] = b;
    e.size = 1;
    e.pitch = -1;
    e.type = EventType::Command;
    if (b < 0x40) {
        if (!(b & 0x20)) { s.len = p[1]; e.b[1] = p[1]; e.size = 2; }
        const int key = b & 15;
        e.type = key ? EventType::Note : EventType::Rest;
        e.duration = s.len;
        if (key) e.pitch = s.oct * 12 + key - 1 - 12 + s.trans;
        return;
    }
    const CmdSpec& sp = spec(b);
    e.size = sp.size;
    for (int i = 1; i < e.size; ++i) e.b[i] = p[i];
    if (b < 0x48) { s.oct = b - 0x40; return; }
    if (b < 0x50) { s.ratio = b - 0x48; return; }
    if (b < 0xE0) return;
    switch (b) {
        case 0xE2: s.trans = int8_t(p[1]); break;
        case 0xE4: ++s.oct; break;
        case 0xE5: --s.oct; break;
        case 0xEC: f.kind = Flow::RepStart; f.count = p[1] == 0xFF ? 0 : p[1]; break;
        case 0xED: f.kind = Flow::RepEnd; break;
        case 0xEE: f.kind = Flow::RepBreak; f.target = -1; break;
        case 0xF5: f.kind = Flow::End; e.type = EventType::End; break;
        default: break;
    }
}

void PandoraDriver::live_extra_writes(const uint8_t* ram, int voice, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    if (!L.stack_base) return;
    const uint16_t base = uint16_t(L.stack_base + voice * L.stack_stride), top = rd16(ram, L.slot[voice] + 2);
    for (int at = base; at + 5 <= top && at + 5 <= base + L.stack_stride; at += 5) {
        seq::remap_word(ram, uint16_t(at), remap, out);
        seq::remap_word(ram, uint16_t(at + 2), remap, out);
    }
}

void PandoraDriver::live_state_writes(const uint8_t* ram, const seq::Position& pos, int voice, uint16_t ptr, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    (void)pos;
    int mapped = remap.find(live_ptr(ram, pos, voice));
    if (mapped >= 0) ptr = uint16_t(mapped);
    out.push_back({L.slot[voice], uint8_t(ptr & 0xFF)});
    out.push_back({uint16_t(L.slot[voice] + 1), uint8_t(ptr >> 8)});
    live_extra_writes(ram, voice, remap, out);
}

void PandoraDriver::track_pointer_writes(const seq::Song& song, int pattern_idx, int voice, uint16_t dest, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    (void)pattern_idx;
    const uint16_t at = uint16_t(song.order_addr + 0x10 + voice * 2);
    const uint16_t off = uint16_t(dest - song.order_addr);
    out.push_back({at, uint8_t(off & 0xFF)});
    out.push_back({uint16_t(at + 1), uint8_t(off >> 8)});
}

bool PandoraDriver::transpose_event(Event& e, int semis) const {
    if (e.type != EventType::Note) return false;
    const int key = (e.b[0] & 15) - 1 + semis;
    if (key < 0 || key > 11) return false;
    e.b[0] = uint8_t((e.b[0] & 0x30) | (key + 1));
    if (e.pitch >= 0) e.pitch += semis;
    return true;
}

void PandoraDriver::apply_note_byte(Event& e, uint8_t byte) const {
    const int key = byte & 15, old = e.b[0] & 15;
    e.b[0] = uint8_t((e.b[0] & 0x30) | key);
    e.type = key ? EventType::Note : EventType::Rest;
    if (key && e.pitch >= 0 && old) e.pitch += key - old;
    else if (key && e.pitch < 0) e.pitch = note_semitone(byte);
    else if (!key) e.pitch = -1;
}

bool PandoraDriver::enter_note(std::vector<Event>& ev, int tick, int semitone, int pattern_len) const {
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
    int key = semitone + 12 - s.trans - s.oct * 12, oct = s.oct;
    if (key < 0 || key > 11) {
        oct = (semitone + 12 - s.trans) / 12;
        key = semitone + 12 - s.trans - oct * 12;
        if (oct < 0 || oct > 7 || key < 0) return false;
    }
    if (!set_note_at(ev, tick, uint8_t(key + 1), pattern_len)) return false;
    if (oct == s.oct) return true;
    idx = -1;
    for (size_t i = 0; i < ev.size(); ++i) if (ev[i].duration > 0 && !ev[i].in_sub && ev[i].tick == tick) { idx = int(i); break; }
    if (idx < 0) return false;
    bool later = false;
    for (size_t i = size_t(idx) + 1; i < ev.size(); ++i) if (ev[i].type == EventType::Note && !ev[i].in_sub) { later = true; break; }
    if (later) ev.insert(ev.begin() + idx + 1, stream::cmd_event(uint8_t(0x40 + s.oct)));
    ev.insert(ev.begin() + idx, stream::cmd_event(uint8_t(0x40 + oct)));
    retime(ev);
    return true;
}

bool PandoraDriver::set_instrument(std::vector<Event>& ev, int tick0, int tick1, uint8_t ins) const {
    seq::stream_prepare(*this, ev, tick0, tick1);
    for (Event& e : ev)
        if (e.type == EventType::Command && is_instrument_cmd(e.b[0]) && e.tick >= tick0 && e.tick < tick1) { e.b[0] = uint8_t(0x60 + (ins & 0x7F)); return true; }
    const uint8_t byte = uint8_t(0x60 + (ins & 0x7F));
    return seq::stream_insert_command_at(*this, ev, tick0, &byte, 1);
}

bool PandoraDriver::set_duration(std::vector<Event>& ev, int i, int dur) const {
    if (i < 0 || i >= int(ev.size()) || dur < 1 || dur > 4096) return false;
    Event& e = ev[size_t(i)];
    if (e.duration <= 0) return false;
    for (size_t k = size_t(i) + 1; k < ev.size(); ++k) {   // the next note reusing the length keeps the old one
        Event& n = ev[k];
        if (n.duration <= 0) continue;
        if (!(n.b[0] & 0x20)) break;
        if (n.in_sub) return false;
        n.b[0] &= uint8_t(~0x20); n.b[1] = uint8_t(n.duration); n.size = 2; n.addr = 0;
        break;
    }
    const int first = std::min(dur, 255);
    e.b[0] &= uint8_t(~0x20); e.b[1] = uint8_t(first); e.size = 2;
    e.duration = first;
    e.addr = 0;
    std::vector<Event> tail;
    for (int left = dur - first; left > 0;) {
        const int n = std::min(left, 255);
        Event t{};
        t.type = e.type == EventType::Rest ? EventType::Rest : EventType::Note;
        t.b[0] = uint8_t(e.type == EventType::Rest ? 0x00 : (e.b[0] & 0x0F) | 0x10); t.b[1] = uint8_t(n); t.size = 2; t.duration = n; t.pitch = e.pitch;
        tail.push_back(t);
        left -= n;
    }
    if (!tail.empty() && e.type == EventType::Note) e.b[0] |= 0x10;
    ev.insert(ev.begin() + i + 1, tail.begin(), tail.end());
    return true;
}

bool PandoraDriver::note_retriggers(const std::vector<Event>& ev, int i) const {
    if (i <= 0 || i >= int(ev.size()) || ev[size_t(i)].type != EventType::Note) return true;
    for (int k = i - 1; k >= 0; --k) {
        const Event& p = ev[size_t(k)];
        if (p.duration <= 0) continue;
        return !(p.type == EventType::Note && (p.b[0] & 0x10) && (p.b[0] & 15) == (ev[size_t(i)].b[0] & 15));
    }
    return true;
}

std::string PandoraDriver::event_text(const Event& e) const {
    char b[96];
    if (e.type == EventType::Note) { std::snprintf(b, sizeof b, "%s  %d ticks%s%s", note_name(e).c_str(), e.duration, (e.b[0] & 0x10) ? "  (held)" : "", (e.b[0] & 0x20) ? "  (last length)" : ""); return b; }
    if (e.type == EventType::Rest) { std::snprintf(b, sizeof b, "rest  %d ticks", e.duration); return b; }
    if (e.type == EventType::Command) {
        if (e.b[0] >= 0x40 && e.b[0] < 0x48) { std::snprintf(b, sizeof b, "Octave %d", e.b[0] - 0x40); return b; }
        if (e.b[0] >= 0x48 && e.b[0] < 0x50) { std::snprintf(b, sizeof b, "Gate %d/8", e.b[0] - 0x48); return b; }
        if (e.b[0] >= 0x50 && e.b[0] < 0x60) { std::snprintf(b, sizeof b, "Volume table %d", e.b[0] - 0x50); return b; }
        if (e.b[0] >= 0x60 && e.b[0] < 0xE0) { std::snprintf(b, sizeof b, "Instrument %d", e.b[0] - 0x60); return b; }
        switch (e.b[0]) {
            case 0xE0: std::snprintf(b, sizeof b, "Tempo %d BPM", e.b[1]); return b;
            case 0xE2: std::snprintf(b, sizeof b, "Transpose %+d", int8_t(e.b[1])); return b;
            case 0xEC: if (e.b[1] == 0xFF) return "Repeat forever"; std::snprintf(b, sizeof b, "Repeat x%d", e.b[1]); return b;
            default: break;
        }
    }
    return seq::Driver::event_text(e);
}

bool PandoraDriver::pitch_fx(const Event& e, seq::PitchFx& out) const {
    if (e.type != EventType::Command || e.b[0] != 0xE9) return false;
    out = seq::PitchFx{};
    out.kind = e.b[1] ? seq::PitchFx::Vibrato : seq::PitchFx::VibratoOff;
    return true;
}

bool PandoraDriver::tempo_writes(const uint8_t* ram, double tps, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    (void)ram;
    if (tps <= 0) return false;
    out.push_back({0x00FA, uint8_t(std::clamp(int(8000.0 / tps + 0.5), 1, 255))});
    return true;
}

}
