#include "konami.hpp"

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

namespace konami {
namespace {
const int W = 0x100;

const CmdSpec kCmds[0x20] = {
    {2, "Rst", "Rest", FxClass::Time},                                   // E0
    {3, "Tie", "Tie (length, gate ratio)", FxClass::Time},               // E1
    {2, "Ins", "Instrument", FxClass::Instrument},                       // E2
    {2, "Pan", "Panning", FxClass::Panning},                             // E3
    {4, "Vib", "Vibrato (delay, rate, depth)", FxClass::Pitch},          // E4
    {4, "RPt", "Random pitch (rate, mask)", FxClass::Pitch},             // E5
    {1, "Lp[", "Repeat start", FxClass::Song, 0, true},                  // E6
    {4, "Lp]", "Repeat end (count, vel delta, pitch delta)", FxClass::Song, 0, true},   // E7
    {1, "L2[", "Repeat 2 start", FxClass::Song, 0, true},                // E8
    {4, "L2]", "Repeat 2 end (count, vel delta, pitch delta)", FxClass::Song, 0, true}, // E9
    {2, "Tmp", "Tempo", FxClass::Speed},                                 // EA
    {3, "TmF", "Tempo fade", FxClass::Speed},                            // EB
    {2, "Trn", "Transpose", FxClass::Pitch},                             // EC
    {2, "AD1", "ADSR 1", FxClass::Sys2},                                 // ED
    {2, "Vol", "Volume", FxClass::Volume},                               // EE
    {3, "VlF", "Volume fade", FxClass::Volume},                          // EF
    {2, "Por", "Portamento speed", FxClass::Pitch},                      // F0
    {4, "PEn", "Pitch envelope", FxClass::Pitch},                        // F1
    {2, "Tun", "Tuning", FxClass::Pitch},                                // F2
    {4, "Sld", "Pitch slide (delay, length, target)", FxClass::Pitch},   // F3
    {4, "Ech", "Echo (voices, vol L, vol R)", FxClass::Sys1},            // F4
    {4, "EcP", "Echo parameters (delay, feedback, FIR)", FxClass::Sys1}, // F5
    {1, "Vt[", "Repeat with alternate ending: start", FxClass::Song, 0, true},   // F6
    {1, "Vt]", "Repeat with alternate ending: ending", FxClass::Song, 0, true},  // F7
    {3, "PnF", "Pan fade", FxClass::Panning},                            // F8
    {2, "VbF", "Vibrato fade", FxClass::Pitch},                          // F9
    {4, "ADG", "ADSR + gain", FxClass::Sys2},                            // FA
    {2, "AD2", "ADSR 2", FxClass::Sys2},                                 // FB
    {3, "InV", "Volume + instrument", FxClass::Instrument},              // FC
    {3, "Jmp", "Jump", FxClass::Song, 1},                                // FD
    {3, "Cal", "Call subroutine", FxClass::Song, 1, true},               // FE
    {1, "End", "End / return", FxClass::Song},                           // FF
};
const CmdSpec kPercOn  = {1, "PcO", "Percussion on", FxClass::Instrument};
const CmdSpec kPercOff = {1, "PcX", "Percussion off", FxClass::Instrument};
const CmdSpec kGain    = {2, "Gai", "Gain", FxClass::Volume};
const CmdSpec kTune    = {1, "ITn", "Instant tuning", FxClass::Pitch};
const CmdSpec kMisc    = {1, "???", "Unknown", FxClass::Misc};
const CmdSpec kCondJmp = {5, "CJp", "Conditional jump (address, alternate)", FxClass::Song, 1};
const CmdSpec kLinEnv  = {3, "LPE", "Linear pitch envelope", FxClass::Pitch};
const CmdSpec kNop3    = {4, "Nop", "(no effect)", FxClass::Misc};
const CmdSpec kNop1    = {2, "Nop", "(no effect)", FxClass::Misc};
const CmdSpec kUnk2    = {3, "???", "Unknown", FxClass::Misc};

int table_index(const Layout& L, uint8_t op) {
    const int n6x = 0xE0 - L.first_6x;
    if (op >= 0xE0) return op - 0xE0 + n6x;
    if (op >= 0x60 && op < 0x60 + n6x) return op - 0x60;
    return -1;
}

int handler(const uint8_t* ram, const Layout& L, uint8_t op) {
    int i = table_index(L, op);
    if (i < 0) return -1;
    return ram[(L.cmd_lo + i * 2) & 0xFFFF] | (ram[(L.cmd_hi + i * 2) & 0xFFFF] << 8);
}

// First absolute+X store (D5 lo hi) in the handler's opening bytes.
uint16_t abs_store(const uint8_t* ram, int h, int limit = 14) {
    if (h < 0) return 0;
    for (int i = 0; i < limit; ++i) if (ram[(h + i) & 0xFFFF] == 0xD5) return rd16(ram, h + i + 1);
    return 0;
}
uint8_t zp_inc(const uint8_t* ram, int h, int limit = 14) {
    if (h < 0) return 0;
    for (int i = 0; i < limit; ++i) if (ram[(h + i) & 0xFFFF] == 0xBB) return ram[(h + i + 1) & 0xFFFF];
    return 0;
}
}

Layout detect_layout(const uint8_t* ram) {
    Layout L;
    const int gg4[] = {0x1C, 0xFD, 0xF6, W, W, 0x2D, 0xF6, W, W, 0x2D, 0xF6, W, W, 0xF0, W, 0xE7, W, 0xBB, W};
    const int cn3[] = {0x80, 0xA4, W, 0x1C, 0xFD, 0xF6, W, W, 0x2D, 0xF6, W, W, 0x2D, 0xDD, 0x5C, 0xFD, 0xF6, W, W, 0xF0, W, 0xE7, W, 0xBB, W};
    if (int p = find_pattern(ram, 0x200, 0x4000, gg4, 19); p >= 0) {
        L.cmd_hi = rd16(ram, p + 3); L.cmd_lo = rd16(ram, p + 7); L.len_table = rd16(ram, p + 11);
        L.len_item = 2; L.ptr_zp = ram[p + 16];
    } else if (int q = find_pattern(ram, 0x200, 0x4000, cn3, 25); q >= 0) {
        L.cmd_hi = rd16(ram, q + 6); L.cmd_lo = rd16(ram, q + 10); L.len_table = rd16(ram, q + 17);
        L.len_item = 1; L.ptr_zp = ram[q + 22];
        const int b6[] = {0xE4, W, 0x8F, W, 0x04, 0x68, 0xE0, 0xB0};
        int r = find_pattern(ram, 0x200, 0x4000, b6, 8);
        if (r < 0) return Layout{};
        L.first_6x = ram[r + 3];
    } else return Layout{};
    const int hdr[] = {0x8F, W, 0x0A, 0x8F, W, 0x0B, 0xCD, 0x00, 0xD8};
    const int list1[] = {0xE4, W, 0x8F, W, 0x04, 0x8F, W, 0x05, 0x9C, 0x8D, 0x05, 0xCF, 0x7A, 0x04};
    const int list0[] = {0xC4, W, 0x8F, W, 0x04, 0x8F, W, 0x05, 0x8D, 0x05, 0xCF, 0x7A, 0x04};
    if (int p = find_pattern(ram, 0x200, 0x4000, hdr, 9); p >= 0) L.header = uint16_t(ram[p + 1] | (ram[p + 4] << 8));
    else if (int q = find_pattern(ram, 0x200, 0x4000, list1, 14); q >= 0) { L.song_list = uint16_t(ram[q + 3] | (ram[q + 6] << 8)); L.song_zp = ram[q + 1]; L.song_one_based = true; }
    else if (int r = find_pattern(ram, 0x200, 0x4000, list0, 13); r >= 0) { L.song_list = uint16_t(ram[r + 3] | (ram[r + 6] << 8)); L.song_zp = ram[r + 1]; }
    else return Layout{};
    const int copy[] = {0xF7, 0x0A, 0xD4, W, 0xD5, W, W, 0xD5, W, W, 0xD5, W, W, 0xFC, 0xF7, 0x0A};
    if (int c = find_pattern(ram, 0x200, 0x4000, copy, 16); c >= 0) L.start_copy = rd16(ram, c + 11);
    auto len_of = [&](uint8_t op) { int i = table_index(L, op); return i < 0 ? -1 : int(ram[(L.len_table + i * L.len_item) & 0xFFFF]); };
    const int n6x = 0xE0 - L.first_6x;
    if (L.song_list) L.version = n6x == 5 ? 1 : n6x == 2 ? 2 : 3;
    else L.version = len_of(0xED) == 3 ? 4 : len_of(0xFC) == 2 ? 5 : 6;
    L.tempo_base = abs_store(ram, handler(ram, L, 0xEA));
    L.sub_ret = abs_store(ram, handler(ram, L, 0xFE));
    L.rep1_ret = abs_store(ram, handler(ram, L, 0xE6));
    L.rep2_ret = abs_store(ram, handler(ram, L, 0xE8));
    L.volta_start = abs_store(ram, handler(ram, L, 0xF6));
    L.volta_end = abs_store(ram, handler(ram, L, 0xF7), 40);
    if (int h = handler(ram, L, 0xFC); L.version == 1 && h >= 0 && ram[h & 0xFFFF] == 0x13) L.cond_zp = ram[(h + 1) & 0xFFFF];
    L.rep1_count = zp_inc(ram, handler(ram, L, 0xE7));
    L.rep2_count = zp_inc(ram, handler(ram, L, 0xE9));
    for (int size = 7; size <= 8 && !L.ins_table; ++size) {
        const int mul[] = {0x8D, size, 0xCF, 0x7A, 0x04, 0xDA, 0x04};
        int r = find_pattern(ram, 0x200, 0x4000, mul, 7);
        if (r < 0) continue;
        const int call[] = {0x68, W, 0xB0, W, 0x8F, W, 0x04, 0x8F, W, 0x05, 0x3F, r & 0xFF, r >> 8};
        if (int c = find_pattern(ram, 0x200, 0x4000, call, 13); c >= 0) {
            L.ins_table = uint16_t(ram[c + 5] | (ram[c + 8] << 8));
            L.ins_count = ram[c + 1];
            L.ins_size = uint8_t(size);
        }
    }
    {
        KonamiDriver probe(L);
        int lo = 0x10000;
        for (uint16_t h : probe.song_headers(ram)) {
            lo = std::min(lo, int(h));
            for (int v = 0; v < 8; ++v) if (uint16_t a = probe.track_start(ram, h, v)) lo = std::min(lo, int(a));
        }
        L.data_lo = uint16_t(lo < 0x10000 ? lo : 0x200);
    }
    return L;
}

std::unique_ptr<seq::Driver> detect(const uint8_t* ram) {
    Layout L = detect_layout(ram);
    if (!L.valid()) return nullptr;
    return std::make_unique<KonamiDriver>(L);
}

std::string KonamiDriver::name() const {
    static const char* const names[] = {"", "Contra III / Axelay", "Madara 2", "Pop'n Twinbee", "Goemon 2 / Sparkster", "Goemon 3 / Parodius", "Animaniacs"};
    return std::string("Konami v") + char('0' + L.version) + " (" + names[L.version] + ")";
}

const CmdSpec& KonamiDriver::spec(uint8_t op) const {
    static CmdSpec out;
    if (op == 0x60) return kPercOn;
    if (op == 0x61) return kPercOff;
    if (op < 0x60) return kMisc;
    const CmdSpec* base = &kMisc;
    if (op >= 0xE0) {
        base = &kCmds[op - 0xE0];
        if (L.version <= 3 && op == 0xED) base = &kNop3;
        if (L.version <= 3 && op == 0xFA) base = &kNop3;
        if (L.version <= 3 && op == 0xFB) base = &kNop1;
        if (L.version == 1 && op == 0xFC) base = &kCondJmp;
        if (L.version == 2 && op == 0xFC) base = &kLinEnv;
        if (L.version == 3 && op == 0xFC) base = &kUnk2;
    } else if (op < 0x80) {
        if (L.version >= 5 && op >= 0x70) base = &kTune;
        else if (L.version >= 2 && op == 0x62) base = &kGain;
    }
    out = *base;
    if (L.version >= 5 && op == 0xF1) out.size = 6;
    return out;
}

bool KonamiDriver::transpose_event(Event& e, int semis) const {
    if (e.type != EventType::Note) return false;
    int key = (e.b[0] & 0x7F) + semis;
    if (key < 0 || key > 0x5F) return false;
    e.b[0] = uint8_t((e.b[0] & 0x80) | key);
    if (e.pitch >= 0) e.pitch += semis;
    return true;
}

void KonamiDriver::apply_note_byte(Event& e, uint8_t byte) const {
    if (byte == 0xE0 || byte == 0xE1) {
        const int len = e.type == EventType::Note ? (e.b[0] & 0x80 ? 0 : e.b[1]) : e.b[1];
        e.type = byte == 0xE0 ? EventType::Rest : EventType::Tie;
        e.b[0] = byte; e.b[1] = uint8_t(len ? len : e.duration); e.b[2] = 0x7F;
        e.size = byte == 0xE0 ? 2 : 3;
        e.pitch = -1;
        return;
    }
    if (e.type == EventType::Note) {
        int old = e.b[0] & 0x7F;
        e.b[0] = uint8_t((e.b[0] & 0x80) | (byte & 0x7F));
        if (e.pitch >= 0) e.pitch += (byte & 0x7F) - old;
        return;
    }
    const int len = e.b[1];
    e.type = EventType::Note;
    e.b[0] = uint8_t(byte & 0x7F); e.b[1] = uint8_t(len); e.b[2] = 0x7F; e.b[3] = 0x70;
    e.size = 4;
    e.pitch = note_semitone(byte);
}

void KonamiDriver::decode(const uint8_t* p, int pc, State& s, Event& e, Flow& f) const {
    const uint8_t b = p[0];
    e.type = EventType::Command;
    e.b[0] = b;
    e.size = 1;
    e.pitch = -1;
    if ((b & 0x7F) < 0x60) {
        int n = 1;
        if (!(b & 0x80)) s.len = p[n++];
        const uint8_t d = p[n++];
        if (!(d & 0x80)) { s.ratio = d; s.vel = p[n++]; }
        else s.vel = d & 0x7F;
        if (p[n] == 0xF3) n += slide_size(p + n);
        e.type = EventType::Note;
        e.size = uint8_t(n);
        e.duration = s.len;
        e.pitch = (b & 0x7F) + 12 + s.trans;
        for (int i = 1; i < n; ++i) e.b[i] = p[i];
        return;
    }
    const CmdSpec& sp = spec(b);
    e.size = sp.size ? sp.size : 1;
    for (int i = 1; i < e.size; ++i) e.b[i] = p[i];
    switch (b) {
        case 0x60: s.flags |= 1; break;
        case 0x61: s.flags &= uint8_t(~1); break;
        case 0xE0:
            e.type = EventType::Rest; s.len = p[1]; e.duration = p[1];
            if (p[2] == 0xF3) { e.size = uint8_t(2 + slide_size(p + 2)); for (int i = 2; i < e.size; ++i) e.b[i] = p[i]; }
            break;
        case 0xE1:
            e.type = EventType::Tie; s.len = p[1]; s.ratio = p[2]; e.duration = p[1];
            if (p[3] == 0xF3) { e.size = uint8_t(3 + slide_size(p + 3)); for (int i = 3; i < e.size; ++i) e.b[i] = p[i]; }
            break;
        case 0xE6: f.kind = Flow::RepStart; f.slot = 0; break;
        case 0xE7: f.kind = Flow::RepEnd; f.slot = 0; f.count = p[1]; break;
        case 0xE8: f.kind = Flow::RepStart; f.slot = 1; break;
        case 0xE9: f.kind = Flow::RepEnd; f.slot = 1; f.count = p[1]; break;
        case 0xEC: s.trans = int8_t(p[1]); break;
        case 0xF6: s.x[0] = pc + 1; s.x[2] = s.x[3] = 0; break;
        case 0xF7:
            if (s.x[2]) { s.x[2] = 0; s.x[3] = 1; s.x[1] = pc + 1; f.kind = Flow::Jump; f.target = s.x[0] ? s.x[0] : s.start; f.count = 1; }
            else if (s.x[3]) { s.x[3] = 0; s.x[2] = 1; f.kind = Flow::Jump; f.target = s.x[1]; f.count = 1; }
            else s.x[2] = 1;
            break;
        case 0xFC: if (L.version == 1) { f.kind = Flow::Jump; f.target = (s.flags & 4) ? p[3] | (p[4] << 8) : p[1] | (p[2] << 8); s.flags &= ~4; } break;   // the request flag is one-shot
        case 0xFD: f.kind = Flow::Jump; f.target = p[1] | (p[2] << 8); break;
        case 0xFE: f.kind = Flow::Call; f.target = p[1] | (p[2] << 8); f.count = 1; break;
        case 0xFF: f.kind = Flow::Return; break;
        default: break;
    }
}

State KonamiDriver::initial_state(const uint8_t* ram, uint16_t header, int voice) const {
    (void)header; (void)voice;
    State s;
    if (L.cond_zp && (ram[L.cond_zp] & 1)) s.flags |= 4;
    return s;
}

uint16_t KonamiDriver::track_start(const uint8_t* ram, uint16_t header, int v) const {
    int count = 8;
    for (int w = 0; w < 8; ++w) {
        uint16_t p = rd16(ram, header + w * 2);
        if (p >= header && p < header + 16) count = std::min(count, (p - header) / 2);
    }
    if (v >= count) return 0;
    uint16_t a = rd16(ram, header + v * 2);
    if (a < 0x200) return 0;
    for (int w = 0; w < v; ++w) if (rd16(ram, header + w * 2) < 0x200) return 0;
    return a;
}

std::vector<uint16_t> KonamiDriver::song_headers(const uint8_t* ram) const {
    std::vector<uint16_t> out;
    if (L.header) { out.push_back(L.header); return out; }
    if (L.start_copy && rd16(ram, L.start_copy) >= 0x200) out.push_back(L.start_copy);
    int misses = 0;
    for (int i = 0; i < 96 && misses < 12; ++i) {
        int entry = L.song_list + i * 5;
        if (entry + 5 > 0x10000) break;
        uint16_t h = rd16(ram, entry + 3);
        if (h < 0x200 || h > 0xFFF0) { ++misses; continue; }
        bool ok = true;
        for (int v = 0; v < 2 && ok; ++v) ok = rd16(ram, h + v * 2) >= 0x200;
        if (!ok) { ++misses; continue; }
        misses = 0;
        out.push_back(h);
    }
    return out;
}

std::string KonamiDriver::event_text(const Event& e) const {
    char b[96];
    if (e.type == EventType::Note) {
        const bool has_len = !(e.b[0] & 0x80);
        const uint8_t d = e.b[has_len ? 2 : 1];
        std::snprintf(b, sizeof b, "%s  %d ticks%s  gate %s  vel %d%s", note_name(e).c_str(), e.duration, has_len ? "" : " (reused)",
                      (d & 0x80) ? "kept" : std::to_string(d).c_str(), (d & 0x80) ? d & 0x7F : e.b[has_len ? 3 : 2] & 0x7F, note_slide(e) ? "  + pitch slide" : "");
        return b;
    }
    if (e.type == EventType::Rest) { std::snprintf(b, sizeof b, "rest  %d ticks", e.duration); return b; }
    if (e.type == EventType::Tie) { std::snprintf(b, sizeof b, "tie  %d ticks  gate %d", e.duration, e.b[2]); return b; }
    if (e.type == EventType::Command) {
        switch (e.b[0]) {
            case 0xE7: case 0xE9:
                if (e.b[1]) std::snprintf(b, sizeof b, "Repeat end  x%d  vel %+d  pitch %+d", e.b[1], int8_t(e.b[2]), int8_t(e.b[3]));
                else std::snprintf(b, sizeof b, "Repeat end  forever");
                return b;
            case 0xEA: std::snprintf(b, sizeof b, "Tempo %d", e.b[1]); return b;
            case 0xEC: std::snprintf(b, sizeof b, "Transpose %+d", int8_t(e.b[1])); return b;
            case 0xFD: std::snprintf(b, sizeof b, "Jump $%02X%02X", e.b[2], e.b[1]); return b;
            case 0xFE: std::snprintf(b, sizeof b, "Call $%02X%02X", e.b[2], e.b[1]); return b;
            default: break;
        }
    }
    return seq::Driver::event_text(e);
}

int KonamiDriver::slide_size(const uint8_t* p) const {
    if (L.version == 1) return 4;
    if (L.version >= 5) return 6;
    return p[2] ? 7 : 4;
}

int KonamiDriver::note_slide(const Event& e) const {
    if (e.type == EventType::Tie) return e.size > 3 && e.b[3] == 0xF3 ? 3 : 0;
    if (e.type == EventType::Rest) return e.size > 2 && e.b[2] == 0xF3 ? 2 : 0;
    if (e.type != EventType::Note) return 0;
    int n = (e.b[0] & 0x80) ? 1 : 2;
    n += (e.b[n] & 0x80) ? 1 : 2;
    return n < e.size && e.b[n] == 0xF3 ? n : 0;
}

bool KonamiDriver::pitch_fx(const Event& e, seq::PitchFx& out) const {
    if (int at = note_slide(e)) {
        out = seq::PitchFx{};
        out.kind = seq::PitchFx::Slide; out.delay = e.b[at + 1]; out.length = e.b[at + 2];
        out.target = e.pitch >= 0 ? e.pitch + ((e.b[at + 3] & 0x7F) - (e.b[0] & 0x7F)) : (e.b[at + 3] & 0x7F) + 12;
        return true;
    }
    if (e.type != EventType::Command) return false;
    out = seq::PitchFx{};
    if (e.b[0] == 0xE4) { out.kind = e.b[3] ? seq::PitchFx::Vibrato : seq::PitchFx::VibratoOff; out.delay = e.b[1]; out.rate = e.b[2]; out.depth = e.b[3]; return true; }
    if (e.b[0] == 0xF0) { out.kind = e.b[1] ? seq::PitchFx::Portamento : seq::PitchFx::SlideOff; out.length = e.b[1]; return true; }
    return false;
}

bool KonamiDriver::note_retriggers(const std::vector<Event>& ev, int i) const {
    if (i <= 0 || i >= int(ev.size()) || ev[size_t(i)].type != EventType::Note) return true;
    for (int k = i - 1; k >= 0; --k) {
        const Event& p = ev[size_t(k)];
        if (p.duration <= 0) continue;
        if (p.type == EventType::Tie) return false;
        if (p.type != EventType::Note) return true;
        State s = state_before(ev, k + 1);
        return !(s.ratio >= (L.version == 1 ? 100 : 127) && (p.b[0] & 0x7F) == (ev[size_t(i)].b[0] & 0x7F));
    }
    return true;
}

seq::Instrument KonamiDriver::read_instrument(const uint8_t* ram, int index) const {
    seq::Instrument in{};
    const uint8_t* e = ram + ((L.ins_table + index * L.ins_size) & 0xFFFF);
    in.srcn = e[0]; in.transpose = int8_t(e[1]); in.adsr0 = e[3]; in.adsr1 = e[4];
    in.gain = L.ins_size == 8 ? e[5] : e[4];
    in.pitch_hi = 0x10; in.pitch_lo = 0;
    return in;
}

bool KonamiDriver::preview_regs(const uint8_t* ram, uint8_t note_byte, int instrument, uint8_t regs[8]) const {
    if (!L.ins_table || instrument < 0 || instrument >= L.ins_count) return false;
    const uint8_t* e = ram + ((L.ins_table + instrument * L.ins_size) & 0xFFFF);
    const int semi = note_semitone(note_byte) + int8_t(e[1]);
    double pitch = 4096.0 * std::pow(2.0, (semi - 60) / 12.0) * std::pow(2.0, int8_t(e[2]) / (12.0 * 256.0));
    int pv = std::min(int(pitch + 0.5), 0x3FFF);
    regs[0] = 0x40; regs[1] = 0x40;
    regs[2] = uint8_t(pv & 0xFF); regs[3] = uint8_t(pv >> 8);
    regs[4] = e[0]; regs[5] = e[3]; regs[6] = e[4]; regs[7] = L.ins_size == 8 ? e[5] : 0;
    return true;
}

double KonamiDriver::ticks_per_second(const uint8_t* ram) const {
    const int latch = ram[0xFA];
    if (!latch || !L.tempo_base) return 0;
    int tempo = 0;
    for (int v = 0; v < 8 && !tempo; ++v) tempo = ram[(L.tempo_base + v * 2) & 0xFFFF];
    return 8000.0 / latch * tempo / 256.0;
}

bool KonamiDriver::tempo_writes(const uint8_t* ram, double tps, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    const int latch = ram[0xFA];
    if (!latch || !L.tempo_base || tps <= 0) return false;
    const int tempo = std::clamp(int(tps * latch / 8000.0 * 256.0 + 0.5), 1, 255);
    for (int v = 0; v < 8; ++v) out.push_back({uint16_t(L.tempo_base + v * 2), uint8_t(tempo)});
    return true;
}

void KonamiDriver::live_extra_writes(const uint8_t* ram, int voice, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    for (uint16_t base : {L.sub_ret, L.rep1_ret, L.rep2_ret, L.volta_start, L.volta_end})
        if (base) seq::remap_word(ram, uint16_t(base + voice * 2), remap, out);
}

bool KonamiDriver::set_duration(std::vector<Event>& ev, int i, int dur) const {
    if (i < 0 || i >= int(ev.size()) || dur < 1 || dur > 4096) return false;
    Event& e = ev[size_t(i)];
    if (e.type == EventType::Note && (e.b[0] & 0x80)) {
        uint8_t rest[16]; std::memcpy(rest, e.b + 1, 15);
        e.b[0] &= 0x7F; e.b[1] = uint8_t(e.duration);
        std::memcpy(e.b + 2, rest, size_t(e.size - 1));
        e.size = uint8_t(e.size + 1);
    }
    // A later note reusing this length must keep the old one.
    for (size_t k = size_t(i) + 1; k < ev.size(); ++k) {
        Event& n = ev[k];
        if (n.type == EventType::Rest || n.type == EventType::Tie) break;
        if (n.type != EventType::Note) continue;
        if (!(n.b[0] & 0x80)) break;
        if (n.in_sub) return false;
        uint8_t rest[16]; std::memcpy(rest, n.b + 1, 15);
        n.b[0] &= 0x7F; n.b[1] = uint8_t(n.duration);
        std::memcpy(n.b + 2, rest, size_t(n.size - 1));
        n.size = uint8_t(n.size + 1);
        n.addr = 0;
        break;
    }
    e.addr = 0;
    const int first = std::min(dur, 255);
    e.b[1] = uint8_t(first);
    e.duration = first;
    int left = dur - first;
    std::vector<Event> tail;
    while (left > 0) {
        const int n = std::min(left, 255);
        Event t{};
        if (e.type == EventType::Rest) { t.type = EventType::Rest; t.b[0] = 0xE0; t.b[1] = uint8_t(n); t.size = 2; }
        else { t.type = EventType::Tie; t.b[0] = 0xE1; t.b[1] = uint8_t(n); t.b[2] = 0x7F; t.size = 3; }
        t.duration = n;
        tail.push_back(t);
        left -= n;
    }
    if (!tail.empty() && e.type == EventType::Note) {
        if (!(e.b[2] & 0x80)) e.b[2] = 0x7F;
        else { uint8_t vel = e.b[2]; e.b[2] = 0x7F; e.b[3] = uint8_t(vel & 0x7F); e.size = 4; }
    }
    ev.insert(ev.begin() + i + 1, tail.begin(), tail.end());
    return true;
}

}
