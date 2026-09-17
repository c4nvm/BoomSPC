#include "compile.hpp"

#include <cstdio>
#include <cstdlib>

#include <cstdio>
#include <cstring>

#include "spc700.hpp"

using seq::Event;
using seq::EventType;
using seq::FxClass;
using stream::CmdSpec;
using stream::Flow;
using stream::State;
using stream::find_pattern;
using stream::rd16;

namespace compile {
namespace {
const int W = 0x100;
const uint8_t kDurs[16] = {1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 9, 18, 30, 36, 42};

const CmdSpec kCmds[0x30] = {
    {3, "Jmp", "Jump", FxClass::Song, 1},                                 // 80
    {4, "Lp]", "Repeat: count down, jump while not zero (nest, address)", FxClass::Song, 2, true},   // 81
    {1, "End", "End of track", FxClass::Song},                            // 82
    {2, "Vib", "Vibrato envelope", FxClass::Pitch},                       // 83
    {2, "Por", "Portamento time", FxClass::Pitch},                        // 84
    {1, "???", "Unknown", FxClass::Misc},                                 // 85
    {1, "???", "Unknown", FxClass::Misc},                                 // 86
    {2, "Vol", "Volume", FxClass::Volume},                                // 87
    {2, "VEn", "Volume envelope", FxClass::Volume},                       // 88
    {2, "Tr+", "Transpose (relative)", FxClass::Pitch},                   // 89
    {2, "Vl+", "Volume (relative)", FxClass::Volume},                     // 8A
    {3, "???", "Unknown", FxClass::Misc},                                 // 8B
    {2, "Nop", "(no effect)", FxClass::Misc},                             // 8C
    {3, "Lp[", "Repeat counter (nest, count)", FxClass::Song, 0, true},   // 8D
    {2, "???", "Unknown", FxClass::Misc},                                 // 8E
    {2, "???", "Unknown", FxClass::Misc},                                 // 8F
    {2, "Flg", "Flags", FxClass::Sys2},                                   // 90
    {2, "???", "Unknown", FxClass::Misc},                                 // 91
    {2, "???", "Unknown", FxClass::Misc},                                 // 92
    {3, "???", "Unknown", FxClass::Misc},                                 // 93
    {2, "???", "Unknown", FxClass::Misc},                                 // 94
    {1, "???", "Unknown", FxClass::Misc},                                 // 95
    {2, "Tmp", "Tempo", FxClass::Speed},                                  // 96
    {2, "Tun", "Tuning", FxClass::Pitch},                                 // 97
    {2, "???", "Unknown", FxClass::Misc},                                 // 98
    {1, "???", "Unknown", FxClass::Misc},                                 // 99
    {3, "Cal", "Call subroutine", FxClass::Song, 1, true},                // 9A
    {1, "Ret", "Return", FxClass::Song},                                  // 9B
    {1, "???", "Unknown", FxClass::Misc},                                 // 9C
    {2, "???", "Unknown", FxClass::Misc},                                 // 9D
    {1, "???", "Unknown", FxClass::Misc},                                 // 9E
    {2, "ADS", "ADSR envelope", FxClass::Sys2},                           // 9F
    {2, "Ins", "Instrument", FxClass::Instrument},                        // A0
    {1, "PoO", "Portamento on", FxClass::Pitch},                          // A1
    {1, "PoF", "Portamento off", FxClass::Pitch},                         // A2
    {2, "PnE", "Pan envelope", FxClass::Panning},                         // A3
    {2, "If=", "Only on voice n: the next two bytes", FxClass::Song},   // A4
    {4, "Jp=", "Jump on voice n", FxClass::Song, 2},                      // A5
    {2, "???", "Unknown", FxClass::Misc},                                 // A6
    {2, "???", "Unknown", FxClass::Misc},                                 // A7
    {1, "???", "Unknown", FxClass::Misc},                                 // A8
    {1, "???", "Unknown", FxClass::Misc},                                 // A9
    {1, "???", "Unknown", FxClass::Misc},                                 // AA
    {2, "Pan", "Panning", FxClass::Panning},                              // AB
    {2, "???", "Unknown", FxClass::Misc},                                 // AC
    {4, "Brk", "Repeat: count down, jump when zero (nest, address)", FxClass::Song, 2, true},   // AD
    {1, "???", "Unknown", FxClass::Misc},                                 // AE
    {1, "???", "Unknown", FxClass::Misc},                                 // AF
};
const CmdSpec kDur = {1, "Dur", "Note length", FxClass::Time};
const CmdSpec kDurDirect = {2, "Dur", "Note length (ticks)", FxClass::Time};
const CmdSpec kTune2 = {3, "Tun", "Tuning", FxClass::Pitch};
const CmdSpec kNop = {1, "Nop", "(no effect)", FxClass::Misc};

int dur_bytes(const uint8_t* p, int& dur) {
    int n = 0;
    for (;;) {
        if (p[n] == 0xDE) { dur = p[n + 1]; n += 2; }
        else if (p[n] >= 0xDF && p[n] <= 0xEE) { dur = kDurs[p[n] - 0xDF]; ++n; }
        else return n;
        if (n > 8) return n;
    }
}
}

Layout detect_layout(const uint8_t* ram) {
    Layout L;
    const int init[] = {0xE5, W, W, 0xC4, W, 0xE5, W, W, 0xC4, W, 0xE5, W, W, 0xC4, W, 0x60, 0x88, 0x11, 0xC4, W, 0xE5, W, W, 0xC4, W, 0x88, 0x00, 0xC4, W};
    int p = find_pattern(ram, 0x200, 0x4000, init, 29);
    if (p < 0) return Layout{};
    const uint16_t engine = rd16(ram, p + 1);
    if (engine < 0x100 || engine + 18 > 0x10000) return Layout{};
    const bool and0c = ram[p - 4] == 0x28 && ram[p - 3] == 0x0C;
    L.version = ram[p + 4] == 0x4E ? (and0c ? 2 : 1) : (and0c ? 3 : 4);
    L.song_list = rd16(ram, engine);
    // The loader copies the 14 header fields of a track into per-voice
    // arrays: MOV A,[zp]+Y ... MOV !array+X,A ... INC Y for each field.
    const int copy_zp[] = {0xF7, W, 0xD4, W, 0xFC, 0xF7, W, 0xD5, W, W, 0xFC};
    const int copy[] = {0xF7, W, 0xD5, W, W, 0xFC, 0xF7, W, 0xD5, W, W, 0xFC};
    for (int a = 0x200; a < 0x4000; ++a) {
        const bool zp_first = find_pattern(ram, a, a + 11, copy_zp, 11) == a && ram[a + 1] == ram[a + 6];
        if (!zp_first && (find_pattern(ram, a, a + 12, copy, 12) != a || ram[a + 1] != ram[a + 7])) continue;
        const uint8_t zp = ram[a + 1];
        uint16_t field[14] = {};
        int k = 0, at = a;
        for (int guard = 0; guard < 80 && k < 14; ++guard) {
            spc700::Insn in = spc700::disassemble(ram, uint16_t(at));
            const uint8_t o = in.bytes[0];
            if (o == 0xD5 && !field[k]) field[k] = uint16_t(in.bytes[1] | (in.bytes[2] << 8));
            if (o == 0xFC) ++k;
            if (o == 0x6F || o == 0x5F || o == 0x2F || o == 0xD0 || o == 0xF0) break;
            if (o == 0xF7 && in.bytes[1] != zp) break;
            at += in.len;
        }
        if (k < 10) continue;
        for (int i = 0; i < 14; ++i) L.field[i] = field[i];
        break;
    }
    return L;
}

std::unique_ptr<seq::Driver> detect(const uint8_t* ram) {
    Layout L = detect_layout(ram);
    if (!L.valid()) return nullptr;
    return std::make_unique<CompileDriver>(L);
}

std::string CompileDriver::name() const {
    static const char* const names[] = {"", "Super Aleste", "Jaki Crush", "Super Puyo Puyo / Kirby's Avalanche", "Super Nazo Puyo and later"};
    return std::string("Compile (") + names[L.version] + ")";
}

const CmdSpec& CompileDriver::spec(uint8_t op) const {
    if (op == 0xDE) return kDurDirect;
    if (op >= 0xDF && op <= 0xEE) return kDur;
    if (op < 0x80 || op >= 0xB0) return kNop;
    if (op == 0x97 && L.version >= 3) return kTune2;
    return kCmds[op - 0x80];
}

std::vector<uint16_t> CompileDriver::song_headers(const uint8_t* ram) const {
    std::vector<uint16_t> out;
    for (int i = 0, misses = 0; i < 0x40 && misses < 4; ++i) {
        const uint16_t h = rd16(ram, L.song_list + i * 2);
        const int n = h >= 0x200 && h < 0xFF00 ? ram[h] : 0;
        bool ok = n >= 1 && ram[(h + 1) & 0xFFFF] < 8 && rd16(ram, h + 1 + 8) >= 0x200 && rd16(ram, h + 1 + 8) < 0xFF00;
        if (!ok) { ++misses; continue; }
        misses = 0;
        if (std::find(out.begin(), out.end(), h) == out.end()) out.push_back(h);
    }
    // A song started outside the list (hacked rips): a header whose tracks
    // sit just below the live pointers.
    uint16_t live[8];
    for (int v = 0; v < 8; ++v) live[v] = uint16_t(ram[L.field[7] + v] | (ram[L.field[8] + v] << 8));
    int best = -1, best_near = 1;
    for (int h = 0x200; h < 0xFF00; ++h) {
        if (ram[h] < 1 || ram[h] > 8 || std::find(out.begin(), out.end(), uint16_t(h)) != out.end()) continue;
        int near = 0, n = 0;
        for (int i = 0; i < 8; ++i) {
            const int e = h + 1 + i * 14;
            if (e + 10 >= 0x10000 || ram[e] >= 8 || ram[e + 7] >= 8) break;
            const uint16_t a = rd16(ram, e + 8);
            if (a < 0x200 || a >= 0xFF00) break;
            ++n;
            if (live[ram[e]] >= a && live[ram[e]] < a + 0x400) ++near;
        }
        if (near > best_near && near * 2 >= n) { best_near = near; best = h; }
    }
    if (best >= 0) out.push_back(uint16_t(best));
    return out;
}

// The first byte of a track entry is the voice it plays on; the count
// byte is not reliable, so entries run until one is not a valid track.
int CompileDriver::entry_of(const uint8_t* ram, uint16_t header, int v) const {
    if (ram[header] < 1) return -1;
    for (int i = 0; i < 8; ++i) {
        const int e = (header + 1 + i * 14) & 0xFFFF;
        if (ram[e] >= 8 || ram[(e + 7) & 0xFFFF] >= 8 || rd16(ram, e + 8) < 0x200 || rd16(ram, e + 8) >= 0xFF00) return -1;
        if (ram[e] == v) return i;
    }
    return -1;
}

uint16_t CompileDriver::track_start(const uint8_t* ram, uint16_t header, int v) const {
    const int i = entry_of(ram, header, v);
    if (i < 0) return 0;
    const uint16_t a = rd16(ram, header + 1 + i * 14 + 8);
    slots_[uint32_t(header) << 8 | uint32_t(v)] = uint16_t(header + 1 + i * 14 + 8);
    return a;
}

State CompileDriver::initial_state(const uint8_t* ram, uint16_t header, int voice) const {
    State s;
    s.len = 12;
    if (int i = entry_of(ram, header, voice); i >= 0) s.trans = int8_t(ram[(header + 1 + i * 14 + 5) & 0xFFFF]);
    data_lo_ = std::min(data_lo_ == 0x200 ? int(header) : data_lo_, int(header));
    for (int v = 0; v < 8; ++v) if (uint16_t a = track_start(ram, header, v)) data_lo_ = std::min(data_lo_, int(a));
    return s;
}

void CompileDriver::decode(const uint8_t* p, int pc, State& s, Event& e, Flow& f) const {
    (void)pc;
    const uint8_t b = p[0];
    e.b[0] = b;
    e.size = 1;
    e.pitch = -1;
    e.type = EventType::Command;
    if (b < 0x80 || (b >= 0xC0 && b <= 0xDD)) {
        int dur = s.len;
        const int n = dur_bytes(p + 1, dur);
        s.len = dur;
        e.size = uint8_t(1 + n);
        for (int i = 1; i < e.size; ++i) e.b[i] = p[i];
        e.duration = dur;
        e.type = b == 0 ? EventType::Rest : b >= 0xC0 ? EventType::Percussion : EventType::Note;
        if (e.type == EventType::Note) e.pitch = b - 1 - 12 + s.trans;
        return;
    }
    if (b == 0xDE || (b >= 0xDF && b <= 0xEE)) {
        int dur = s.len;
        const int n = dur_bytes(p, dur);
        s.len = dur;
        e.size = uint8_t(n);
        for (int i = 1; i < e.size; ++i) e.b[i] = p[i];
        return;
    }
    const CmdSpec& sp = spec(b);
    e.size = sp.size;
    if (b == 0xA4 && s.voice >= 0 && s.voice != p[1]) e.size = 4;   // other voices skip the two bytes that follow
    for (int i = 1; i < e.size; ++i) e.b[i] = p[i];
    switch (b) {
        case 0x80: f.kind = Flow::Jump; f.target = p[1] | (p[2] << 8); break;
        case 0xA5: if (s.voice == p[1]) { f.kind = Flow::Jump; f.target = p[2] | (p[3] << 8); f.count = 1; } break;
        case 0x81: { int& c = s.x[p[1] & 3]; if (--c != 0) { f.kind = Flow::Jump; f.target = p[2] | (p[3] << 8); f.count = 1; } break; }
        case 0xAD: { int& c = s.x[p[1] & 3]; if (--c == 0) { f.kind = Flow::Jump; f.target = p[2] | (p[3] << 8); f.count = 1; } break; }
        case 0x8D: s.x[p[1] & 3] = p[2]; break;
        case 0x82: f.kind = Flow::End; e.type = EventType::End; break;
        case 0x89: s.trans += int8_t(p[1]); break;
        case 0x9A: f.kind = Flow::Call; f.target = p[1] | (p[2] << 8); f.count = 1; break;
        case 0x9B: f.kind = Flow::Return; f.count = 1; break;
        default: break;
    }
}

uint16_t CompileDriver::live_ptr(const uint8_t* ram, const seq::Position& pos, int v) const {
    (void)pos;
    return uint16_t(ram[(L.field[7] + v) & 0xFFFF] | (ram[(L.field[8] + v) & 0xFFFF] << 8));
}

void CompileDriver::live_state_writes(const uint8_t* ram, const seq::Position& pos, int voice, uint16_t ptr, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    (void)pos;
    int mapped = remap.find(live_ptr(ram, pos, voice));
    if (mapped >= 0) ptr = uint16_t(mapped);
    out.push_back({uint16_t(L.field[7] + voice), uint8_t(ptr & 0xFF)});
    out.push_back({uint16_t(L.field[8] + voice), uint8_t(ptr >> 8)});
}

void CompileDriver::track_pointer_writes(const seq::Song& song, int pattern_idx, int voice, uint16_t dest, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    (void)pattern_idx;
    auto it = slots_.find(uint32_t(song.order_addr) << 8 | uint32_t(voice));
    if (it == slots_.end()) return;
    const uint16_t at = it->second;
    out.push_back({at, uint8_t(dest & 0xFF)});
    out.push_back({uint16_t(at + 1), uint8_t(dest >> 8)});
}

bool CompileDriver::transpose_event(Event& e, int semis) const {
    if (e.type != EventType::Note) return false;
    const int k = e.b[0] + semis;
    if (k < 1 || k > 0x7F) return false;
    e.b[0] = uint8_t(k);
    if (e.pitch >= 0) e.pitch += semis;
    return true;
}

void CompileDriver::apply_note_byte(Event& e, uint8_t byte) const {
    const int old = e.b[0];
    e.b[0] = byte;
    e.type = byte == 0 ? EventType::Rest : byte >= 0xC0 ? EventType::Percussion : EventType::Note;
    if (e.type == EventType::Note) {
        if (e.pitch >= 0 && old >= 1 && old < 0x80) e.pitch += byte - old;
        else if (e.pitch < 0) e.pitch = note_semitone(byte);
    } else e.pitch = -1;
}

bool CompileDriver::set_duration(std::vector<Event>& ev, int i, int dur) const {
    if (i < 0 || i >= int(ev.size()) || dur < 1 || dur > 4096) return false;
    Event& e = ev[size_t(i)];
    if (e.duration <= 0) return false;
    // A later note without a length of its own keeps the old one.
    for (size_t k = size_t(i) + 1; k < ev.size(); ++k) {
        Event& n = ev[k];
        if (n.type == EventType::Command && (n.b[0] == 0xDE || (n.b[0] >= 0xDF && n.b[0] <= 0xEE))) break;
        if (n.duration <= 0) continue;
        if (n.size > 1) break;
        if (n.in_sub) return false;
        n.b[1] = 0xDE; n.b[2] = uint8_t(n.duration); n.size = 3; n.addr = 0;
        break;
    }
    const int first = std::min(dur, 255);
    int k = -1;
    for (int t = 0; t < 16; ++t) if (kDurs[t] == first) k = t;
    if (k >= 0) { e.b[1] = uint8_t(0xDF + k); e.size = 2; }
    else { e.b[1] = 0xDE; e.b[2] = uint8_t(first); e.size = 3; }
    e.duration = first;
    e.addr = 0;
    std::vector<Event> tail;
    for (int left = dur - first; left > 0;) {
        const int n = std::min(left, 255);
        Event t{};
        t.type = EventType::Rest; t.b[0] = 0x00; t.b[1] = 0xDE; t.b[2] = uint8_t(n); t.size = 3; t.duration = n;
        tail.push_back(t);
        left -= n;
    }
    ev.insert(ev.begin() + i + 1, tail.begin(), tail.end());
    return true;
}

std::string CompileDriver::event_text(const Event& e) const {
    char b[96];
    if (e.type == EventType::Note) { std::snprintf(b, sizeof b, "%s  %d ticks", note_name(e).c_str(), e.duration); return b; }
    if (e.type == EventType::Percussion) { std::snprintf(b, sizeof b, "percussion %d  %d ticks", e.b[0] - 0xC0, e.duration); return b; }
    if (e.type == EventType::Rest) { std::snprintf(b, sizeof b, "rest  %d ticks", e.duration); return b; }
    if (e.type == EventType::Command) {
        switch (e.b[0]) {
            case 0x80: std::snprintf(b, sizeof b, "Jump $%02X%02X", e.b[2], e.b[1]); return b;
            case 0x81: std::snprintf(b, sizeof b, "Repeat %d: jump $%02X%02X while counting", e.b[1], e.b[3], e.b[2]); return b;
            case 0xAD: std::snprintf(b, sizeof b, "Repeat %d: jump $%02X%02X when done", e.b[1], e.b[3], e.b[2]); return b;
            case 0x8D: std::snprintf(b, sizeof b, "Repeat %d x%d", e.b[1], e.b[2]); return b;
            case 0x9A: std::snprintf(b, sizeof b, "Call $%02X%02X", e.b[2], e.b[1]); return b;
            case 0x96: std::snprintf(b, sizeof b, "Tempo %d", e.b[1]); return b;
            case 0x89: std::snprintf(b, sizeof b, "Transpose %+d", int8_t(e.b[1])); return b;
            case 0xDE: std::snprintf(b, sizeof b, "Note length %d", e.b[1]); return b;
            default: if (e.b[0] >= 0xDF && e.b[0] <= 0xEE) { std::snprintf(b, sizeof b, "Note length %d", kDurs[e.b[0] - 0xDF]); return b; } break;
        }
    }
    return seq::Driver::event_text(e);
}

bool CompileDriver::pitch_fx(const Event& e, seq::PitchFx& out) const {
    if (e.type != EventType::Command) return false;
    out = seq::PitchFx{};
    if (e.b[0] == 0x83) { out.kind = e.b[1] ? seq::PitchFx::Vibrato : seq::PitchFx::VibratoOff; out.depth = e.b[1]; return true; }
    if (e.b[0] == 0xA1) { out.kind = seq::PitchFx::Portamento; return true; }
    if (e.b[0] == 0xA2) { out.kind = seq::PitchFx::SlideOff; return true; }
    return false;
}

double CompileDriver::ticks_per_second(const uint8_t* ram) const {
    if (!L.field[5]) return 0;
    int tempo = 0;
    for (int v = 0; v < 8 && !tempo; ++v) tempo = ram[(L.field[5] + v) & 0xFFFF];
    return tempo * 60.0988 / 256.0;
}

bool CompileDriver::tempo_writes(const uint8_t* ram, double tps, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    (void)ram;
    if (!L.field[5] || tps <= 0) return false;
    const uint8_t t = uint8_t(std::clamp(int(tps * 256.0 / 60.0988 + 0.5), 1, 255));
    for (int v = 0; v < 8; ++v) out.push_back({uint16_t(L.field[5] + v), t});
    return true;
}

}
