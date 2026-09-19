#include "graphres.hpp"

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

namespace graphres {
namespace {
const int W = 0x100;

const CmdSpec kCmds[0x20] = {
    {1, "???", "Unknown", FxClass::Misc},                                // E0
    {1, "???", "Unknown", FxClass::Misc},                                // E1
    {1, "???", "Unknown", FxClass::Misc},                                // E2
    {1, "???", "Unknown", FxClass::Misc},                                // E3
    {2, "Trn", "Transpose", FxClass::Pitch},                             // E4
    {2, "MVl", "Master volume", FxClass::Volume},                        // E5
    {2, "EcV", "Echo volume", FxClass::Sys1},                            // E6
    {1, "Oc-", "Octave down", FxClass::Pitch},                           // E7
    {1, "Oc+", "Octave up", FxClass::Pitch},                             // E8
    {1, "Brk", "Leave the repeat on its last pass", FxClass::Song, 0, true},    // E9
    {1, "Lp[", "Repeat start", FxClass::Song, 0, true},                  // EA
    {4, "Lp]", "Repeat end (count, offset)", FxClass::Song, 2, true},    // EB
    {2, "Gat", "Gate (n/8)", FxClass::Time},                             // EC
    {3, "DSP", "DSP register write", FxClass::Sys1},                     // ED
    {4, "Lp1", "One-level repeat (count, offset)", FxClass::Song, 2},    // EE
    {3, "???", "Unknown", FxClass::Misc},                                // EF
    {1, "Noi", "Noise toggle", FxClass::Sys1},                           // F0
    {2, "Vol", "Volume", FxClass::Volume},                               // F1
    {1, "???", "Unknown", FxClass::Misc},                                // F2
    {2, "MVF", "Master volume fade", FxClass::Volume},                   // F3
    {2, "Pan", "Panning", FxClass::Panning},                             // F4
    {1, "???", "Unknown", FxClass::Misc},                                // F5
    {1, "???", "Unknown", FxClass::Misc},                                // F6
    {3, "ADS", "ADSR", FxClass::Sys2},                                   // F7
    {1, "Ret", "Return", FxClass::Song, 0, true},                        // F8
    {3, "Cal", "Call (offset)", FxClass::Song, 1, true},                 // F9
    {3, "Jmp", "Jump (offset)", FxClass::Song, 1},                       // FA
    {2, "???", "Unknown", FxClass::Misc},                                // FB
    {2, "Ins", "Instrument", FxClass::Instrument},                       // FC
    {2, "Len", "Default note length", FxClass::Time},                    // FD
    {1, "Slr", "Slur into the next note", FxClass::Time},                // FE
    {1, "End", "End of track", FxClass::Song},                           // FF
};
const CmdSpec kVol = {1, "Vol", "Volume (0-15)", FxClass::Volume};
const CmdSpec kOct = {1, "Oct", "Octave", FxClass::Pitch};
const CmdSpec kNop = {1, "Nop", "(no effect)", FxClass::Misc};
const CmdSpec kUnknown = {1, "???", "Unknown opcode (not in the driver's table)", FxClass::Misc};

const int kKeySemi[16] = {0, 2, 4, 5, 7, 9, 11, -1, 1, 3, 4, 6, 8, 10, 11, -1};
const uint8_t kSemiKey[12] = {0, 8, 1, 9, 2, 3, 11, 4, 12, 5, 13, 6};
}

Layout detect_layout(const uint8_t* ram) {
    Layout L;
    // Loader: header address, first track's ROM address - 24, then the
    // per-voice enable byte and pointer word.
    const int load[] = {0xE8, W, 0xC4, W, 0xE8, W, 0xC4, W, 0xE5, W, W, 0x80, 0xA8, 0x18, 0xC4, W, 0xE5, W, W, 0xA8, 0x00, 0xC4, W,
                        0xCD, 0x00, 0x8D, 0x00, 0xE8, 0x00, 0xD4, W, 0xF7, W, 0xD4, W, 0xF0, W};
    int p = find_pattern(ram, 0x200, 0x4000, load, 37);
    if (p < 0) return Layout{};
    L.header = uint16_t(ram[p + 1] | (ram[p + 5] << 8));
    if (rd16(ram, p + 9) != L.header + 1) return Layout{};
    L.enable_zp = ram[p + 34];
    const int ptr[] = {0xF7, W, 0x80, 0xA4, W, 0xD4, W, 0xFC, 0xF7, W, 0xA4, W, 0xD4, W};
    int q = find_pattern(ram, p, p + 80, ptr, 14);
    if (q < 0 || ram[q + 13] != ram[q + 6] + 1) return Layout{};
    L.ptr_zp = ram[q + 6];
    // Call: return address to [$0D]+Y with $0D = 4 * voice + base, Y = stack index.
    const int call[] = {0xE4, W, 0x1C, 0x1C, 0x60, 0x88, W, 0xC4, W, 0xE8, W, 0x88, 0x00, 0xC4, W, 0xF8, W, 0xFB, W, 0x6F};
    if (int c = find_pattern(ram, 0x200, 0x4000, call, 20); c >= 0) { L.call_base = uint16_t(ram[c + 6] | (ram[c + 10] << 8)); L.call_sp_zp = ram[c + 18]; }
    // Repeat end: counter, then the EB's address into two byte arrays.
    const int rep[] = {0xF5, W, W, 0x80, 0xA8, 0x01, 0xD5, W, W, 0xF0, W, 0xE4, W, 0xD5, W, W, 0xE4, W, 0xD5, W, W};
    if (int r = find_pattern(ram, 0x200, 0x4000, rep, 21); r >= 0) {
        L.loop_count = rd16(ram, r + 1); L.loop_lo = rd16(ram, r + 14); L.loop_hi = rd16(ram, r + 19);
        const int sp[] = {0xF8, W, 0x7D, 0x60, 0x95, W, W, 0x5D, 0x6F};
        if (int s = find_pattern(ram, 0x200, 0x4000, sp, 9); s >= 0) L.loop_sp = rd16(ram, s + 5);
    }
    const int len[] = {0x8D, 0x01, 0xF7, W, 0xF8, W, 0xD4, W, 0x5F};
    if (int l = find_pattern(ram, 0x200, 0x4000, len, 9); l >= 0) L.len_zp = ram[l + 7];
    return L;
}

std::unique_ptr<seq::Driver> detect(const uint8_t* ram) {
    Layout L = detect_layout(ram);
    if (!L.valid()) return nullptr;
    return std::make_unique<GraphResDriver>(L);
}

const CmdSpec& GraphResDriver::spec(uint8_t op) const {
    if (op < 0x80) return kUnknown;
    if (op < 0x90) return kVol;
    if (op < 0xA0) return kOct;
    if (op < 0xE0) return kUnknown;
    return kCmds[op - 0xE0];
}

uint16_t GraphResDriver::track_start(const uint8_t* ram, uint16_t header, int v) const {
    const int e = (header + v * 3) & 0xFFFF;
    if (!ram[e]) return 0;
    base_[header] = rd16(ram, header + 1);
    int a = (header + 24 + int(rd16(ram, e + 1)) - int(base_[header])) & 0xFFFF;
    // Voice 0 always starts right after the header; a relocated stream
    // leaves a jump there.
    if (v == 0 && ram[(header + 24) & 0xFFFF] == 0xFA) a = (header + 24 + int16_t(rd16(ram, header + 25))) & 0xFFFF;
    return a >= 0x200 && a < 0xFF00 ? uint16_t(a) : 0;
}

State GraphResDriver::initial_state(const uint8_t* ram, uint16_t header, int voice) const {
    (void)voice;
    State s;
    s.oct = 4;
    s.len = 1;
    s.ratio = 8;
    data_lo_ = std::min(data_lo_ == 0x200 ? int(header) : data_lo_, int(header));
    for (int v = 0; v < 8; ++v) if (uint16_t a = track_start(ram, header, v)) data_lo_ = std::min(data_lo_, int(a));
    return s;
}

int GraphResDriver::note_semitone(uint8_t b) const {
    const int semi = kKeySemi[b & 15];
    return semi < 0 ? -1 : 4 * 12 + semi - 12;
}

uint8_t GraphResDriver::note_byte(int semitone_from_c0) const {
    return kSemiKey[((semitone_from_c0 % 12) + 12) % 12];
}

// State: len = default length, ratio = gate, x[0] = last pitch, x[1] = EE
// counter, flags bit 0 = the last note was followed by FE.
void GraphResDriver::decode(const uint8_t* p, int pc, State& s, Event& e, Flow& f) const {
    (void)pc;
    const uint8_t b = p[0];
    e.b[0] = b;
    e.size = 1;
    e.pitch = -1;
    e.type = EventType::Command;
    if (b < 0x80) {
        int len = s.len;
        if (b & 0x10) { len = p[1]; e.b[1] = p[1]; e.size = 2; }
        const int key = b & 15;
        e.duration = len;
        if (key == 7) e.type = EventType::Rest;
        else if (key == 15) e.type = EventType::Tie;
        else {
            e.pitch = s.oct * 12 + kKeySemi[key] - 12 + s.trans;
            e.type = (s.flags & 1) && e.pitch == s.x[0] ? EventType::Tie : EventType::Note;
            s.x[0] = e.pitch;
        }
        s.flags = (s.flags & ~1) | (p[e.size] == 0xFE ? 1 : 0);
        return;
    }
    const CmdSpec& sp = spec(b);
    e.size = sp.size;
    for (int i = 1; i < e.size; ++i) e.b[i] = p[i];
    if (b < 0xA0) { if (b >= 0x90) s.oct = b & 15; return; }
    if (b < 0xE0) return;
    const int rel = (pc + int16_t(p[1] | (p[2] << 8))) & 0xFFFF;
    switch (b) {
        case 0xE4: s.trans = int8_t(p[1]); break;
        case 0xE7: --s.oct; break;
        case 0xE8: ++s.oct; break;
        case 0xE9: f.kind = Flow::RepBreak; f.target = -1; break;
        case 0xEA: f.kind = Flow::RepStart; break;
        case 0xEB: f.kind = Flow::RepEnd; f.count = p[1]; break;
        case 0xEC: s.ratio = p[1]; break;
        case 0xEE:
            if (!s.x[1]) s.x[1] = p[1];
            if (--s.x[1] > 0) { f.kind = Flow::Jump; f.count = 1; f.target = rel; }
            break;
        case 0xF8: f.kind = Flow::Return; break;
        case 0xF9: f.kind = Flow::Call; f.count = 1; f.target = rel; break;
        case 0xFA: f.kind = Flow::Jump; f.target = rel; break;
        case 0xFD: s.len = p[1]; break;
        case 0xFF: f.kind = Flow::End; e.type = EventType::End; break;
        default: break;
    }
}

int GraphResDriver::jump_target(const Event& e) const {
    if (e.type != EventType::Command || !e.addr) return -1;
    const CmdSpec& sp = spec(e.b[0]);
    if (!sp.addr_at) return -1;
    return (e.addr + int16_t(e.b[sp.addr_at] | (e.b[sp.addr_at + 1] << 8))) & 0xFFFF;
}

void GraphResDriver::set_jump_target(Event& e, uint16_t addr) const {
    const CmdSpec& sp = spec(e.b[0]);
    if (!sp.addr_at || !e.addr) return;
    const int rel = int(addr) - int(e.addr);
    e.b[sp.addr_at] = uint8_t(rel & 0xFF);
    e.b[sp.addr_at + 1] = uint8_t((rel >> 8) & 0xFF);
}

std::vector<uint8_t> GraphResDriver::serialize_relocated(const std::vector<Event>& ev, uint16_t dest, std::vector<int>* offsets) const {
    std::vector<int> off;
    std::vector<uint8_t> out = stream::Driver::serialize_relocated(ev, dest, &off);
    for (size_t i = 0; i < ev.size(); ++i) {   // offsets are taken from the command's new address
        if (ev[i].in_sub || off[i] < 0 || ev[i].type != EventType::Command) continue;
        const CmdSpec& sp = spec(ev[i].b[0]);
        if (!sp.addr_at) continue;
        const int t = jump_target(ev[i]);
        const int j = relocated_target(ev, int(i), off);
        if (t < 0 && j < 0) continue;
        const int target = j >= 0 ? dest + off[size_t(j)] : t;
        const int rel = target - int(dest + off[i]);
        out[size_t(off[i] + sp.addr_at)] = uint8_t(rel & 0xFF);
        out[size_t(off[i] + sp.addr_at + 1)] = uint8_t((rel >> 8) & 0xFF);
    }
    if (offsets) *offsets = off;
    return out;
}

void GraphResDriver::live_extra_writes(const uint8_t* ram, int voice, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    if (L.call_base && L.call_sp_zp)
        for (int k = 0; k < ram[L.call_sp_zp + voice] && k < 4; k += 2) seq::remap_word(ram, uint16_t(L.call_base + voice * 4 + k), remap, out);
    if (!L.loop_sp || !L.loop_count) return;
    for (int sp = ram[(L.loop_sp + voice) & 0xFFFF]; sp <= 0xC0; sp += 0x30) {
        const int idx = (voice + sp) & 0xFF;
        if (ram[(L.loop_count + idx) & 0xFFFF] & 0x80) continue;
        const int addr = ram[(L.loop_lo + idx) & 0xFFFF] | (ram[(L.loop_hi + idx) & 0xFFFF] << 8);
        const int mapped = remap.find(uint16_t(addr));
        if (mapped < 0) continue;
        out.push_back({uint16_t(L.loop_lo + idx), uint8_t(mapped & 0xFF)});
        out.push_back({uint16_t(L.loop_hi + idx), uint8_t(mapped >> 8)});
    }
}

void GraphResDriver::track_pointer_writes(const seq::Song& song, int pattern_idx, int voice, uint16_t dest, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    (void)pattern_idx;
    const uint16_t h = song.order_addr;
    if (voice == 0) {
        const int rel = int(dest) - int(h + 24);
        out.push_back({uint16_t(h + 24), 0xFA});
        out.push_back({uint16_t(h + 25), uint8_t(rel & 0xFF)});
        out.push_back({uint16_t(h + 26), uint8_t((rel >> 8) & 0xFF)});
        return;
    }
    auto it = base_.find(h);
    const uint16_t virt = uint16_t(dest - h - 24 + (it == base_.end() ? 0 : it->second));
    out.push_back({uint16_t(h + voice * 3 + 1), uint8_t(virt & 0xFF)});
    out.push_back({uint16_t(h + voice * 3 + 2), uint8_t(virt >> 8)});
}

bool GraphResDriver::transpose_event(Event& e, int semis) const {
    if (e.type != EventType::Note) return false;
    const int semi = kKeySemi[e.b[0] & 15] + semis;
    if (semi < 0 || semi > 11) return false;
    e.b[0] = uint8_t((e.b[0] & 0x70) | kSemiKey[semi]);
    if (e.pitch >= 0) e.pitch += semis;
    return true;
}

void GraphResDriver::apply_note_byte(Event& e, uint8_t byte) const {
    const int key = byte & 15, old = e.b[0] & 15;
    e.b[0] = uint8_t((e.b[0] & 0x70) | key);
    e.type = key == 7 ? EventType::Rest : key == 15 ? EventType::Tie : EventType::Note;
    if (e.type == EventType::Note && e.pitch >= 0 && kKeySemi[old] >= 0) e.pitch += kKeySemi[key] - kKeySemi[old];
    else if (e.type == EventType::Note && e.pitch < 0) e.pitch = note_semitone(byte);
    else if (e.type != EventType::Note) e.pitch = -1;
}

bool GraphResDriver::enter_note(std::vector<Event>& ev, int tick, int semitone, int pattern_len) const {
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
    const int rel = semitone + 12 - s.trans;
    int oct = s.oct, semi = rel - oct * 12;
    if (semi < 0 || semi > 11) {
        oct = rel >= 0 ? rel / 12 : -1;
        semi = rel - oct * 12;
        if (oct < 0 || oct > 15) return false;
    }
    if (!set_note_at(ev, tick, kSemiKey[semi], pattern_len)) return false;
    if (oct == s.oct) return true;
    idx = -1;
    for (size_t i = 0; i < ev.size(); ++i) if (ev[i].duration > 0 && !ev[i].in_sub && ev[i].tick == tick) { idx = int(i); break; }
    if (idx < 0) return false;
    bool later = false;
    for (size_t i = size_t(idx) + 1; i < ev.size(); ++i) if (ev[i].type == EventType::Note && !ev[i].in_sub) { later = true; break; }
    if (later) ev.insert(ev.begin() + idx + 1, stream::cmd_event(uint8_t(0x90 + s.oct)));
    ev.insert(ev.begin() + idx, stream::cmd_event(uint8_t(0x90 + oct)));
    retime(ev);
    return true;
}

bool GraphResDriver::set_duration(std::vector<Event>& ev, int i, int dur) const {
    if (i < 0 || i >= int(ev.size()) || dur < 1 || dur > 4096) return false;
    Event& e = ev[size_t(i)];
    if (e.duration <= 0) return false;
    const int first = std::min(dur, 255);
    e.b[0] |= 0x10; e.b[1] = uint8_t(first); e.size = 2;
    e.duration = first;
    e.addr = 0;
    std::vector<Event> tail;
    for (int left = dur - first; left > 0;) {
        const int n = std::min(left, 255);
        Event t{};
        t.type = e.type == EventType::Rest ? EventType::Rest : EventType::Tie;
        t.b[0] = uint8_t(e.type == EventType::Rest ? 0x17 : 0x1F); t.b[1] = uint8_t(n); t.size = 2; t.duration = n; t.pitch = -1;
        tail.push_back(t);
        left -= n;
    }
    ev.insert(ev.begin() + i + 1, tail.begin(), tail.end());
    return true;
}

bool GraphResDriver::note_retriggers(const std::vector<Event>& ev, int i) const {
    (void)ev; (void)i;
    return true;
}

std::string GraphResDriver::event_text(const Event& e) const {
    char b[96];
    if (e.type == EventType::Note) { std::snprintf(b, sizeof b, "%s  %d ticks%s", note_name(e).c_str(), e.duration, (e.b[0] & 0x10) ? "" : "  (default length)"); return b; }
    if (e.type == EventType::Rest) { std::snprintf(b, sizeof b, "rest  %d ticks", e.duration); return b; }
    if (e.type == EventType::Tie) { std::snprintf(b, sizeof b, "tie  %d ticks", e.duration); return b; }
    if (e.type == EventType::Command) {
        if (e.b[0] >= 0x80 && e.b[0] < 0x90) { std::snprintf(b, sizeof b, "Volume %d", e.b[0] & 15); return b; }
        if (e.b[0] >= 0x90 && e.b[0] < 0xA0) { std::snprintf(b, sizeof b, "Octave %d", e.b[0] & 15); return b; }
        switch (e.b[0]) {
            case 0xE4: std::snprintf(b, sizeof b, "Transpose %+d", int8_t(e.b[1])); return b;
            case 0xEB: std::snprintf(b, sizeof b, "Repeat end x%d", e.b[1]); return b;
            case 0xEE: std::snprintf(b, sizeof b, "One-level repeat x%d", e.b[1]); return b;
            case 0xF9: std::snprintf(b, sizeof b, "Call %+d", int16_t(e.b[1] | (e.b[2] << 8))); return b;
            case 0xFA: std::snprintf(b, sizeof b, "Jump %+d", int16_t(e.b[1] | (e.b[2] << 8))); return b;
            case 0xFD: std::snprintf(b, sizeof b, "Default length %d", e.b[1]); return b;
            default: break;
        }
    }
    return seq::Driver::event_text(e);
}

int GraphResDriver::echo_length(const uint8_t* ram, int dsp_edl) const {
    int edl = dsp_edl;
    for (int a = L.header; a < 0xFF00 - 3; ++a)   // ED value $7D in the song data
        if (ram[a] == 0xED && ram[a + 2] == 0x7D && (ram[a + 1] & 0x0F) > edl && ram[a + 1] < 0x10) edl = ram[a + 1];
    return edl;
}

bool GraphResDriver::tempo_writes(const uint8_t* ram, double tps, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    (void)ram;
    if (tps <= 0) return false;
    out.push_back({0x00FA, uint8_t(std::clamp(int(8000.0 / tps + 0.5), 1, 255))});
    return true;
}

}
