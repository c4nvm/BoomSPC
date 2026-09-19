#include "heartbeat.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using seq::Event;
using seq::EventType;
using seq::FxClass;
using stream::CmdSpec;
using stream::Flow;
using stream::State;
using stream::find_pattern;
using stream::rd16;

namespace heartbeat {
namespace {
const int W = 0x100;

// Sizes are read from the driver's own table; these give the names.
const CmdSpec kCmds[0x28] = {
    {1, "SlO", "Slur on", FxClass::Time},                                // D2
    {1, "SlF", "Slur off", FxClass::Time},                               // D3
    {2, "Ins", "Instrument", FxClass::Instrument},                       // D4
    {8, "???", "Unknown", FxClass::Misc},                                // D5
    {2, "Pan", "Panning", FxClass::Panning},                             // D6
    {3, "PnF", "Pan fade", FxClass::Panning},                            // D7
    {4, "Vib", "Vibrato (delay, rate, depth)", FxClass::Pitch},          // D8
    {2, "VbF", "Vibrato fade", FxClass::Pitch},                          // D9
    {1, "VbO", "Vibrato off", FxClass::Pitch},                           // DA
    {2, "MVl", "Master volume", FxClass::Volume},                        // DB
    {3, "MVF", "Master volume fade", FxClass::Volume},                   // DC
    {2, "Tmp", "Tempo", FxClass::Speed},                                 // DD
    {3, "???", "Unknown", FxClass::Misc},                                // DE
    {2, "GTr", "Global transpose", FxClass::Pitch},                      // DF
    {2, "Trn", "Transpose", FxClass::Pitch},                             // E0
    {4, "Trm", "Tremolo", FxClass::Volume},                              // E1
    {1, "TrF", "Tremolo off", FxClass::Volume},                          // E2
    {2, "Vol", "Volume", FxClass::Volume},                               // E3
    {3, "VlF", "Volume fade", FxClass::Volume},                          // E4
    {4, "???", "Unknown", FxClass::Misc},                                // E5
    {4, "PEr", "Pitch envelope (release)", FxClass::Pitch},              // E6
    {4, "PEa", "Pitch envelope (attack)", FxClass::Pitch},               // E7
    {1, "PEF", "Pitch envelope off", FxClass::Pitch},                    // E8
    {2, "Tun", "Tuning", FxClass::Pitch},                                // E9
    {3, "EcV", "Echo volume", FxClass::Sys1},                            // EA
    {4, "EcP", "Echo parameters", FxClass::Sys1},                        // EB
    {4, "???", "Unknown", FxClass::Misc},                                // EC
    {1, "EcF", "Echo off", FxClass::Sys1},                               // ED
    {1, "EcO", "Echo on", FxClass::Sys1},                                // EE
    {9, "FIR", "Echo filter", FxClass::Sys1},                            // EF
    {3, "ADS", "ADSR", FxClass::Sys2},                                   // F0
    {2, "NtP", "Note parameter (gate, velocity)", FxClass::Time},        // F1
    {3, "Jmp", "Jump (offset from the header)", FxClass::Song, 1},       // F2
    {3, "Cal", "Call (offset from the header)", FxClass::Song, 1, true}, // F3
    {1, "Ret", "Return", FxClass::Song, 0, true},                        // F4
    {1, "NoO", "Noise on", FxClass::Sys1},                               // F5
    {1, "NoF", "Noise off", FxClass::Sys1},                              // F6
    {2, "Noi", "Noise frequency", FxClass::Sys1},                        // F7
    {1, "???", "Unknown", FxClass::Misc},                                // F8
    {2, "Sub", "Sub-command (loop count / loop again / ADSR parts)", FxClass::Song}, // F9
};
const CmdSpec kNop = {1, "Nop", "(no effect)", FxClass::Misc};
const CmdSpec kUnknown = {1, "???", "Unknown opcode (not in the driver's table)", FxClass::Misc};
}

Layout detect_layout(const uint8_t* ram) {
    Layout L;
    const int list[] = {0xEE, 0xF6, W, W, 0xC4, W, 0xF6, W, W, 0xC4, W, 0xF8, W, 0xDD, 0xD5, W, W, 0x8D, 0x00};
    int p = find_pattern(ram, 0x200, 0x4000, list, 19);
    if (p < 0) return Layout{};
    L.song_lo = rd16(ram, p + 2); L.song_hi = rd16(ram, p + 7); L.slot_song = rd16(ram, p + 15);
    if (L.song_hi <= L.song_lo || L.song_hi - L.song_lo > 16) return Layout{};
    const int len[] = {0x80, 0xA8, 0xD2, 0xFD, 0xF6, W, W, 0x60, 0x84};
    if (int a = find_pattern(ram, 0x200, 0x4000, len, 9); a >= 0) L.len_table = rd16(ram, a + 5);
    const int off[] = {0xF5, W, W, 0xFD, 0xF5, W, W, 0x7A, W, 0xDA, W};
    if (int a = find_pattern(ram, 0x200, 0x4000, off, 11); a >= 0) { L.off_hi = rd16(ram, a + 1); L.off_lo = rd16(ram, a + 5); }
    const int tempo[] = {0xF5, W, W, 0x04, W, 0xEB, W, 0xCF, 0x60, 0x95, W, W, 0xD5, W, W};
    if (int a = find_pattern(ram, 0x200, 0x4000, tempo, 15); a >= 0) L.tempo_base = rd16(ram, a + 1);
    const int vt[] = {0xF5, W, W, 0x3F, W, W, 0xAB, W, 0xF5, W, W, 0x3F, W, W, 0xAB, W};
    if (int a = find_pattern(ram, 0x200, 0x4000, vt, 16); a >= 0 && rd16(ram, a + 9) == rd16(ram, a + 1) + 7) L.voice_table = rd16(ram, a + 1);
    return L;
}

std::unique_ptr<seq::Driver> detect(const uint8_t* ram) {
    Layout L = detect_layout(ram);
    if (!L.valid()) return nullptr;
    auto d = std::make_unique<HeartbeatDriver>(L);
    return d;
}

const CmdSpec& HeartbeatDriver::spec(uint8_t op) const {
    if (op < 0xD2 || op > 0xF9) return kUnknown;
    return kCmds[op - 0xD2];
}

std::vector<uint16_t> HeartbeatDriver::song_headers(const uint8_t* ram) const {
    std::vector<uint16_t> out;
    for (int i = 0; i < 0x28; ++i) lens_[i] = uint8_t(ram[(L.len_table + i) & 0xFFFF] + 1);
    for (int i = 0; i < L.song_hi - L.song_lo; ++i) {
        const uint16_t h = uint16_t(ram[(L.song_lo + i) & 0xFFFF] | (ram[(L.song_hi + i) & 0xFFFF] << 8));
        if (h < 0x200 || h >= 0xFF00 || !rd16(ram, h + 2) || std::find(out.begin(), out.end(), h) != out.end()) continue;
        out.push_back(h);
    }
    return out;
}

int HeartbeatDriver::slot_of(const uint8_t* ram, uint16_t header) const {
    if (!L.slot_song) return -1;
    for (int s = 0; s < 7; ++s) {
        const int idx = ram[(L.slot_song + s) & 0xFFFF];
        if (idx & 0x80) continue;
        if (idx >= L.song_hi - L.song_lo) continue;
        const uint16_t h = uint16_t(ram[(L.song_lo + idx) & 0xFFFF] | (ram[(L.song_hi + idx) & 0xFFFF] << 8));
        if (h == header) return s;
    }
    return -1;
}

int HeartbeatDriver::track_of(const uint8_t* ram, uint16_t header, int v) const {
    const int s = slot_of(ram, header);
    if (s < 0 || !L.voice_table) return v;
    bool direct = false;   // jingle slots name channels past the DSP voices: take the tracks in order
    for (int t = 0; t < 8; ++t) if (ram[(L.voice_table + t * 7 + s) & 0xFFFF] < 8) direct = true;
    if (!direct) return v;
    for (int t = 0; t < 8; ++t) if (ram[(L.voice_table + t * 7 + s) & 0xFFFF] == v) return t;
    return -1;
}

uint16_t HeartbeatDriver::track_start(const uint8_t* ram, uint16_t header, int v) const {
    const int t = track_of(ram, header, v);
    if (t < 0) return 0;
    for (int k = 0; k <= t; ++k) if (!rd16(ram, header + 2 + k * 2)) return 0;   // the list ends before this track
    const uint16_t off = rd16(ram, header + 2 + t * 2);
    slots_[uint32_t(header) << 8 | uint32_t(v)] = uint16_t(header + 2 + t * 2);
    const int a = (header + off) & 0xFFFF;
    return a >= 0x200 && a < 0xFF00 ? uint16_t(a) : 0;
}

State HeartbeatDriver::initial_state(const uint8_t* ram, uint16_t header, int voice) const {
    (void)voice;
    State s;
    cur_ = header;
    data_lo_ = std::min(data_lo_ == 0x200 ? int(header) : data_lo_, int(header));
    for (int v = 0; v < 8; ++v) if (uint16_t a = track_start(ram, header, v)) data_lo_ = std::min(data_lo_, int(a));
    return s;
}

std::string HeartbeatDriver::song_label(const uint8_t* ram, uint16_t header, const seq::Pattern& p, int index) const {
    std::string s = stream::Driver::song_label(ram, header, p, index);
    const int slot = slot_of(ram, header);
    if (slot >= 0) s += " slot " + std::to_string(slot);
    return s;
}

double HeartbeatDriver::ticks_per_second(const uint8_t* ram) const {
    const int slot = std::max(0, slot_of(ram, cur_));
    const int tempo = L.tempo_base ? ram[(L.tempo_base + slot) & 0xFFFF] : 0;
    return ram[0xFA] && tempo ? 8000.0 / ram[0xFA] * tempo / 256.0 : 0;
}

bool HeartbeatDriver::tempo_writes(const uint8_t* ram, double tps, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    if (tps <= 0 || !ram[0xFA] || !L.tempo_base) return false;
    const int slot = std::max(0, slot_of(ram, cur_));
    out.push_back({uint16_t(L.tempo_base + slot), uint8_t(std::clamp(int(tps * 256.0 * ram[0xFA] / 8000.0 + 0.5), 1, 255))});
    return true;
}

// State: len = note length, x[0] = loop count (F9 00), flags bit 0 = slur.
// A length byte is glued to the note / tie / rest it precedes: [len] [param] note.
void HeartbeatDriver::decode(const uint8_t* p, int pc, State& s, Event& e, Flow& f) const {
    (void)pc;
    const uint8_t b = p[0];
    e.b[0] = b;
    e.size = 1;
    e.pitch = -1;
    e.type = EventType::Command;
    if (b == 0x00) { e.type = EventType::End; f.kind = Flow::End; return; }
    int k = 0;
    if (b < 0x80) {
        s.len = b;
        k = 1;
        if (p[1] < 0x80 && p[1] != 0) { e.b[1] = p[1]; k = 2; }
        e.size = uint8_t(k);
        if (p[k] < 0x80 || p[k] > 0xD1) return;   // a bare length: a command in its own right
        e.b[k] = p[k];
        e.size = uint8_t(k + 1);
    }
    const uint8_t n = p[k];
    if (n < 0x80) return;
    if (n < 0xD0) { e.type = EventType::Note; e.duration = s.len; e.pitch = n - 0x80 + 12 + s.trans; return; }
    if (n == 0xD0) { e.type = EventType::Tie; e.duration = s.len; return; }
    if (n == 0xD1) { e.type = EventType::Rest; e.duration = s.len; return; }
    const int i = n - 0xD2;
    int size = i < 0x28 && lens_[i] ? lens_[i] : spec(n).size;
    if (n == 0xF9) {
        const uint8_t t = p[1];
        size = (t == 0x01 || t == 0x09) ? 4 : (t == 0x00 || (t >= 0x03 && t <= 0x07)) ? 3 : 2;
    }
    e.size = uint8_t(size);
    for (int j = 1; j < size; ++j) e.b[j] = p[j];
    const uint16_t target = uint16_t(cur_ + (p[1] | (p[2] << 8)));
    switch (n) {
        case 0xD2: s.flags |= 1; break;
        case 0xD3: s.flags &= ~1; break;
        case 0xE0: s.trans = int8_t(p[1]); break;
        case 0xF2: f.kind = Flow::Jump; f.target = target; break;
        case 0xF3: f.kind = Flow::Call; f.count = 1; f.target = target; break;
        case 0xF4: f.kind = Flow::Return; break;
        case 0xF9:
            if (p[1] == 0x00) s.x[0] = p[2];
            else if (p[1] == 0x01 && s.x[0]) { if (--s.x[0]) { f.kind = Flow::Jump; f.count = 1; f.target = uint16_t(cur_ + (p[2] | (p[3] << 8))); } }
            break;
        default: break;
    }
}

int HeartbeatDriver::jump_target(const Event& e) const {
    if (e.type != EventType::Command) return -1;
    if (e.b[0] == 0xF2 || e.b[0] == 0xF3) return uint16_t(cur_ + (e.b[1] | (e.b[2] << 8)));
    if (e.b[0] == 0xF9 && e.b[1] == 0x01) return uint16_t(cur_ + (e.b[2] | (e.b[3] << 8)));
    return -1;
}

void HeartbeatDriver::set_jump_target(Event& e, uint16_t addr) const {
    const int at = e.b[0] == 0xF9 ? 2 : (e.b[0] == 0xF2 || e.b[0] == 0xF3) ? 1 : 0;
    if (!at) return;
    const uint16_t off = uint16_t(addr - cur_);
    e.b[at] = uint8_t(off & 0xFF);
    e.b[at + 1] = uint8_t(off >> 8);
}

std::vector<uint8_t> HeartbeatDriver::serialize_relocated(const std::vector<Event>& ev, uint16_t dest, std::vector<int>* offsets) const {
    return stream::Driver::serialize_relocated(ev, dest, offsets);   // header-relative offsets: the base's absolute retarget works as is
}

uint16_t HeartbeatDriver::live_ptr(const uint8_t* ram, const seq::Position& pos, int v) const {
    (void)pos;
    const int off = ram[(L.off_lo + v) & 0xFFFF] | (ram[(L.off_hi + v) & 0xFFFF] << 8);
    if (off == 0xFFFF || !cur_) return 0;
    return uint16_t(cur_ + off);
}

void HeartbeatDriver::live_state_writes(const uint8_t* ram, const seq::Position& pos, int voice, uint16_t ptr, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    int mapped = remap.find(live_ptr(ram, pos, voice));
    if (mapped >= 0) ptr = uint16_t(mapped);
    const uint16_t off = uint16_t(ptr - cur_);
    out.push_back({uint16_t(L.off_lo + voice), uint8_t(off & 0xFF)});
    out.push_back({uint16_t(L.off_hi + voice), uint8_t(off >> 8)});
}

void HeartbeatDriver::track_pointer_writes(const seq::Song& song, int pattern_idx, int voice, uint16_t dest, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    (void)pattern_idx;
    auto it = slots_.find(uint32_t(song.order_addr) << 8 | uint32_t(voice));
    if (it == slots_.end()) return;
    const uint16_t off = uint16_t(dest - song.order_addr);
    out.push_back({it->second, uint8_t(off & 0xFF)});
    out.push_back({uint16_t(it->second + 1), uint8_t(off >> 8)});
}

namespace {
int note_at(const Event& e) { return e.b[0] < 0x80 ? (e.size >= 3 && e.b[1] < 0x80 ? 2 : 1) : 0; }   // index of the note byte
}

bool HeartbeatDriver::transpose_event(Event& e, int semis) const {
    if (e.type != EventType::Note) return false;
    const int at = note_at(e), n = e.b[at] + semis;
    if (n < 0x80 || n > 0xCF) return false;
    e.b[at] = uint8_t(n);
    if (e.pitch >= 0) e.pitch += semis;
    return true;
}

void HeartbeatDriver::apply_note_byte(Event& e, uint8_t byte) const {
    const int at = note_at(e);
    const uint8_t old = e.b[at];
    e.b[at] = byte;
    if (byte == 0xD1) { e.type = EventType::Rest; e.pitch = -1; return; }
    if (byte == 0xD0) { e.type = EventType::Tie; e.pitch = -1; return; }
    e.type = EventType::Note;
    if (e.pitch >= 0 && old >= 0x80 && old < 0xD0) e.pitch += byte - old;
    else e.pitch = note_semitone(byte);
}

// Notes without a length byte use the last one: give the next such note its own.
bool HeartbeatDriver::set_duration(std::vector<Event>& ev, int i, int dur) const {
    if (i < 0 || i >= int(ev.size()) || dur < 1 || dur > 4096) return false;
    Event& e = ev[size_t(i)];
    if (e.duration <= 0) return false;
    for (size_t k = size_t(i) + 1; k < ev.size(); ++k) {
        Event& n = ev[k];
        if (n.type == EventType::Command && n.b[0] < 0x80) break;   // a bare length resets it anyway
        if (n.duration <= 0) continue;
        if (n.b[0] < 0x80) break;
        if (n.in_sub) return false;
        n.b[1] = n.b[0]; n.b[0] = uint8_t(e.duration); n.size = 2; n.addr = 0;
        break;
    }
    const int first = std::min(dur, 127);
    if (e.b[0] >= 0x80) { e.b[1] = e.b[0]; e.size = 2; }
    e.b[0] = uint8_t(first);
    e.duration = first;
    e.addr = 0;
    std::vector<Event> tail;
    for (int left = dur - first; left > 0;) {
        const int n = std::min(left, 127);
        Event t{};
        t.type = e.type == EventType::Rest ? EventType::Rest : EventType::Tie;
        t.b[0] = uint8_t(n); t.b[1] = e.type == EventType::Rest ? 0xD1 : 0xD0; t.size = 2; t.duration = n; t.pitch = -1;
        tail.push_back(t);
        left -= n;
    }
    ev.insert(ev.begin() + i + 1, tail.begin(), tail.end());
    return true;
}

std::string HeartbeatDriver::event_text(const Event& e) const {
    char b[96];
    const char* qv = e.b[0] < 0x80 && e.size >= 3 && e.b[1] < 0x80 ? "  (gate, velocity set)" : "";
    if (e.type == EventType::Note) { std::snprintf(b, sizeof b, "%s  %d ticks%s", note_name(e).c_str(), e.duration, qv); return b; }
    if (e.type == EventType::Rest) { std::snprintf(b, sizeof b, "rest  %d ticks%s", e.duration, qv); return b; }
    if (e.type == EventType::Tie) { std::snprintf(b, sizeof b, "tie  %d ticks%s", e.duration, qv); return b; }
    if (e.type == EventType::Command) {
        if (e.b[0] < 0x80) { if (e.size >= 2) std::snprintf(b, sizeof b, "Length %d  gate %d  velocity %d", e.b[0], e.b[1] >> 4, e.b[1] & 15); else std::snprintf(b, sizeof b, "Length %d", e.b[0]); return b; }
        switch (e.b[0]) {
            case 0xE0: std::snprintf(b, sizeof b, "Transpose %+d", int8_t(e.b[1])); return b;
            case 0xF2: std::snprintf(b, sizeof b, "Jump to header+%04X", e.b[1] | (e.b[2] << 8)); return b;
            case 0xF3: std::snprintf(b, sizeof b, "Call header+%04X", e.b[1] | (e.b[2] << 8)); return b;
            case 0xF9:
                if (e.b[1] == 0x00) { std::snprintf(b, sizeof b, "Loop count %d", e.b[2]); return b; }
                if (e.b[1] == 0x01) { std::snprintf(b, sizeof b, "Loop again to header+%04X", e.b[2] | (e.b[3] << 8)); return b; }
                std::snprintf(b, sizeof b, "Sub-command %02X", e.b[1]); return b;
            default: break;
        }
    }
    return seq::Driver::event_text(e);
}

bool HeartbeatDriver::pitch_fx(const Event& e, seq::PitchFx& out) const {
    if (e.type != EventType::Command) return false;
    out = seq::PitchFx{};
    if (e.b[0] == 0xD8) { out.kind = seq::PitchFx::Vibrato; out.delay = e.b[1]; out.rate = e.b[2]; out.depth = e.b[3]; return true; }
    if (e.b[0] == 0xDA) { out.kind = seq::PitchFx::VibratoOff; return true; }
    return false;
}

}
