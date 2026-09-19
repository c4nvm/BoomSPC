#include "neverland.hpp"

#include <cstdio>
#include <cstring>

using seq::Event;
using seq::EventType;
using seq::FxClass;
using stream::CmdSpec;
using stream::Flow;
using stream::State;
using stream::find_pattern;

namespace neverland {
namespace {
const int W = 0x100;
const int kRing = 0x7FFF;   // a note left ringing by a gate of 0

// State: len / ratio (gate) / vel = the last explicit note's values, trans =
// section transpose, x[0] = list position (parse only), x[1] = ticks the
// last note keeps sounding past its event (kRing = until the next key on),
// x[2] = last key, x[4] = repeat slots in use, x[5..6] = slot words (list
// position | transpose << 16 | counter << 23, counter FE = fresh, FF =
// forever), x[7] = parsing from RAM.

const CmdSpec kCmds[0x10] = {
    {3, "LFO", "LFO depth (delay, depth)", FxClass::Pitch},          // F0
    {3, "Vol", "Volume (delay, volume)", FxClass::Volume},           // F1
    {3, "Pan", "Pan (delay, pan; 40 = centre)", FxClass::Panning},   // F2
    {2, "Wai", "Wait", FxClass::Time},                               // F3
    {3, "Tmp", "Tempo (delay, timer latch)", FxClass::Speed},        // F4
    {1, "Nop", "(no effect)", FxClass::Misc},                        // F5
    {3, "PSc", "Pitch scale (delay, x/128; 80 = x1)", FxClass::Pitch},   // F6
    {3, "Ins", "Instrument (delay, instrument)", FxClass::Instrument},   // F7
    {1, "Nop", "(no effect)", FxClass::Misc},                        // F8
    {1, "Nop", "(no effect)", FxClass::Misc},                        // F9
    {1, "Nop", "(no effect)", FxClass::Misc},                        // FA
    {1, "Lp[", "Repeat start", FxClass::Song, 0, true},              // FB
    {2, "Lp]", "Repeat n times (0 = forever)", FxClass::Song, 0, true},   // FC
    {1, "Sec", "Section end", FxClass::Song},                        // FD
    {1, "End", "Voice end", FxClass::Song},                          // FE
    {3, "Ext", "Extended (sub, value)", FxClass::Sys1},              // FF
};
const CmdSpec kNop = {1, "Nop", "(no effect)", FxClass::Misc};
const CmdSpec kUnknown = {1, "???", "Unknown opcode (not in the driver's table)", FxClass::Misc};

const char* kExtNames[0x1F] = {
    "Echo delay", "Echo feedback", "Echo volume", "Echo on", "Echo off", "(no effect)", "(no effect)", "(no effect)",
    "Attack rate", "Decay rate", "Sustain level", "Sustain rate", "GAIN", "Master volume", "Master volume L", "Master volume R",
    "Noise clock", "Noise on", "Noise off", "Pitch mod on", "Pitch mod off", "LFO rate", "LFO delay", "LFO shape",
    "LFO fade", "Surround L", "Surround R", "Surround off", "Flag 4 on", "Flag 3 on", "Flags off",
};

inline int be16(const uint8_t* ram, int a) { return (ram[a & 0xFFFF] << 8) | ram[(a + 1) & 0xFFFF]; }
inline int le16(const uint8_t* ram, int a) { return ram[a & 0xFFFF] | (ram[(a + 1) & 0xFFFF] << 8); }
inline bool is_op(const Event& e, uint8_t op) { return e.type == EventType::Command && e.b[0] == op; }
}

Layout detect_layout(const uint8_t* ram) {
    Layout L;
    const int parse[] = {0xF5, W, W, 0xC4, W, 0xF5, W, W, 0xC4, W, 0x8D, 0x00, 0xF7, W, 0x30, W, 0x3F, W, W, 0xFC, 0x60, 0x95, W, W, 0xD5};
    int p = find_pattern(ram, 0x200, 0x8000, parse, 25);
    if (p < 0 || ram[p + 9] != ram[p + 4] + 1 || ram[p + 13] != ram[p + 4]) return Layout{};
    L.ptr_lo = uint16_t(le16(ram, p + 1));
    L.ptr_hi = uint16_t(le16(ram, p + 6));
    L.trans = uint16_t(le16(ram, p + 22));
    const int list[] = {0x28, 0x7F, 0xD5, W, W, 0xF5, W, W, 0x60, 0x88, 0x01, 0xD5, W, W, 0xC4, W, 0xF5, W, W, 0x88, 0x00, 0xD5, W, W, 0xC4, W, 0x2F, W};
    int q = find_pattern(ram, 0x200, 0x8000, list, 28);
    if (q < 0) return Layout{};
    L.list_lo = uint16_t(le16(ram, q + 6));
    L.list_hi = uint16_t(le16(ram, q + 17));
    if (ram[q + 28] == 0x60 && ram[q + 29] == 0x88) L.base = uint16_t(ram[q + 30] << 8);
    else if (ram[q + 28] == 0xD5) L.v1 = true;
    else return Layout{};
    const int start[] = {0x8F, 0x10, W, 0x8F, W, W, 0xCD};
    int h = find_pattern(ram, 0x200, 0x8000, start, 7);
    if (h < 0 || ram[h + 5] != ram[h + 2] + 1) return Layout{};
    L.header = uint16_t(ram[h + 4] << 8);
    if (L.v1) {
        const int slot[] = {0xF5, W, W, 0xD0, W, 0xF5, W, W, 0xD5, W, W, 0xF5, W, W, 0xD5, W, W, 0xF5, W, W, 0x60, 0x88, 0x01, 0xD5, W, W, 0xF5, W, W, 0x88, 0x00, 0xD5, W, W, 0xE8, 0xFE, 0xD5, W, W, 0xF5, W, W, 0xD5, W, W};
        int r = find_pattern(ram, 0x200, 0x8000, slot, 45);
        if (r < 0) return Layout{};
        L.slot_lp_lo = uint16_t(le16(ram, r + 9));
        L.slot_lp_hi = uint16_t(le16(ram, r + 15));
        L.slot_sp_lo = uint16_t(le16(ram, r + 24));
        L.slot_sp_hi = uint16_t(le16(ram, r + 32));
        L.slot_count = uint16_t(le16(ram, r + 37));
        L.slot_trans = uint16_t(le16(ram, r + 43));
        int r2 = find_pattern(ram, r + 45, r + 120, slot + 5, 40);   // the second slot's copy
        if (r2 < 0) return Layout{};
        L.slot_stride = uint16_t(le16(ram, r2 + 4) - L.slot_lp_lo);
    } else {
        const int slot[] = {0x88, 0x10, 0x5D, 0xF6, W, W, 0xD5, W, W, 0xF6, W, W, 0xD5, W, W, 0xF6, W, W, 0x60, 0x88, 0x01, 0xD5, W, W, 0xF6, W, W, 0x88, 0x00, 0xD5, W, W, 0xE8, 0xFE, 0xD5, W, W, 0xF6, W, W, 0xD5, W, W};
        int r = find_pattern(ram, 0x200, 0x8000, slot, 43);
        if (r < 0) return Layout{};
        L.slot_lp_lo = uint16_t(le16(ram, r + 7));
        L.slot_lp_hi = uint16_t(le16(ram, r + 13));
        L.slot_sp_lo = uint16_t(le16(ram, r + 22));
        L.slot_sp_hi = uint16_t(le16(ram, r + 30));
        L.slot_count = uint16_t(le16(ram, r + 35));
        L.slot_trans = uint16_t(le16(ram, r + 41));
        const int fast[] = {0xC4, 0x1C, 0xE9, W, W, 0xF0, 0x02, 0x0B, 0x1C};
        if (int t = find_pattern(ram, 0x200, 0x8000, fast, 9); t >= 0) L.fast_flag = uint16_t(le16(ram, t + 3));
    }
    const int pitch[] = {0xCD, 0x0C, 0x9E};
    if (int t = find_pattern(ram, 0x200, 0x8000, pitch, 3); t >= 0) {
        const int tab[] = {0xF6, W, W, 0xC4, W, 0xF6, W, W};
        if (int u = find_pattern(ram, t, t + 16, tab, 8); u >= 0) { L.pitch_lo = uint16_t(le16(ram, u + 1)); L.pitch_hi = uint16_t(le16(ram, u + 6)); }
    }
    const int ins2[] = {0x1C, 0x1C, 0xFD, 0xF6, W, W, 0xAB, 0xF2, 0xC4, 0xF3, 0xF6};
    const int ins1[] = {0x1C, 0x1C, 0x60, 0x88, 0x00, 0xC4, W, 0xE8, 0x00, 0x88, W};
    if (int t = find_pattern(ram, 0x200, 0x8000, ins2, 11); t >= 0) L.ins_table = uint16_t(le16(ram, t + 4));
    else if (int u = find_pattern(ram, 0x200, 0x8000, ins1, 11); u >= 0) L.ins_table = uint16_t(ram[u + 10] << 8);
    return L;
}

std::unique_ptr<seq::Driver> detect(const uint8_t* ram) {
    Layout L = detect_layout(ram);
    if (!L.valid()) return nullptr;
    return std::make_unique<NeverlandDriver>(L);
}

NeverlandDriver::NeverlandDriver(Layout layout) : L(layout) {
    for (int i = 0; i < 0x10; ++i) cmds_[i] = kCmds[i];
    if (L.v1) {
        for (uint8_t op : {0xF3, 0xF4, 0xF5, 0xF8, 0xF9, 0xFA}) cmds_[op - 0xF0] = {2, "Nop", "(no effect)", FxClass::Misc};
        cmds_[0] = {3, "Nop", "(no effect; delay)", FxClass::Misc};
        cmds_[6] = {4, "PSc", "Pitch scale (delay, x/128; 80 = x1, extra)", FxClass::Pitch};
    }
}

const CmdSpec& NeverlandDriver::spec(uint8_t op) const {
    if (op < 0xF0) return kUnknown;
    return cmds_[op - 0xF0];
}

uint16_t NeverlandDriver::list_start(const uint8_t* ram, int v) const {
    const int off = le16(ram, L.header + 0x20 + v * 2);
    if (off == 0xFFFF || (!L.v1 && off >= 0x8000)) return 0;
    return uint16_t(L.base + off);
}

std::vector<uint16_t> NeverlandDriver::song_headers(const uint8_t* ram) const {
    ram_ = ram;
    const int h = L.header;
    if (h < 0x200) return {};
    bool flags = true;
    for (int v = 0; v < 8; ++v) if (ram[h + 0x10 + v] != 0x00 && ram[h + 0x10 + v] != 0x80 && ram[h + 0x10 + v] != 0xFF) flags = false;
    const bool sig = ram[h] == 'S' && (ram[h + 1] == '2' || ram[h + 1] == 'F') && ram[h + 2] == 'C';
    if (!flags && !sig) return {};
    for (int v = 0; v < 8; ++v) if (track_start(ram, uint16_t(h), v)) return {uint16_t(h)};
    return {};
}

uint16_t NeverlandDriver::track_start(const uint8_t* ram, uint16_t header, int v) const {
    if (ram[(header + 0x10 + v) & 0xFFFF] == 0xFF) return 0;   // voice unused
    int lp = list_start(ram, v);
    if (!lp || ram[lp] == 0xFF) return 0;
    while (ram[lp & 0xFFFF] & 0x80) ++lp;
    const int a = (L.base + be16(ram, lp)) & 0xFFFF;
    return a >= 0x200 ? uint16_t(a) : 0;
}

State NeverlandDriver::initial_state(const uint8_t* ram, uint16_t header, int voice) const {
    (void)header;
    ram_ = ram;
    State s;
    s.x[7] = 1;
    s.x[2] = -1;
    int lp = list_start(ram, voice);
    int t = 0;
    while (ram[lp & 0xFFFF] & 0x80) { t = ram[lp & 0xFFFF] & 0x7F; ++lp; }
    s.x[0] = lp & 0xFFFF;
    entry_[voice] = uint16_t(lp);
    s.trans = t;
    return s;
}

// The transpose of the first section is not in the events: recover it
// from the first note's parsed pitch.
State NeverlandDriver::edit_state(const std::vector<Event>& ev) const {
    State s;
    s.x[2] = -1;
    for (const Event& e : ev) {
        if (is_op(e, 0xFD)) break;
        if (e.type == EventType::Note && e.pitch >= 0 && e.b[0] < 0xF0) { s.trans = e.pitch - 12 - (e.b[0] & 0x7F); break; }
    }
    return s;
}

void NeverlandDriver::decode(const uint8_t* p, int pc, State& s, Event& e, Flow& f) const {
    (void)pc;
    const uint8_t b = p[0];
    e.b[0] = b;
    e.size = 1;
    e.pitch = -1;
    e.type = EventType::Command;
    auto ringing = [&](Event& ev) {
        if (s.x[1] > 0) { ev.type = EventType::Tie; ev.pitch = s.x[2] >= 0 ? s.x[2] + 12 : -1; return true; }
        ev.type = EventType::Rest;
        return false;
    };
    if (b < 0xF0) {
        if (b < 0x80) { s.len = p[1]; s.ratio = p[2]; s.vel = p[3]; e.size = 4; }
        e.b[1] = uint8_t(s.len); e.b[2] = uint8_t(s.ratio); e.b[3] = uint8_t(s.vel);
        const int key = ((b & 0x7F) + s.trans) & 0xFF, len = s.len, gate = s.ratio;
        e.duration = len;
        if (gate == 0) { if (ringing(e)) s.x[1] = kRing; return; }
        e.pitch = key + 12;
        e.type = s.x[1] > 0 && key == s.x[2] ? EventType::Tie : EventType::Note;
        s.x[2] = key;
        s.x[1] = gate > len ? gate - len : 0;
        return;
    }
    switch (b) {
        case 0xF3:
            if (L.v1) { e.size = 2; e.b[1] = p[1]; return; }
            e.size = 2; e.b[1] = p[1]; e.b[2] = uint8_t(s.ratio); e.b[3] = uint8_t(s.vel);
            e.duration = p[1];
            if (ringing(e) && s.x[1] != kRing) s.x[1] = std::max(0, s.x[1] - e.duration);
            return;
        case 0xF0: case 0xF1: case 0xF2: case 0xF4: case 0xF5: case 0xF6: case 0xF7: case 0xF8: case 0xF9: case 0xFA:
            e.size = spec(b).size;
            for (int i = 1; i < e.size; ++i) e.b[i] = p[i];
            if (e.size >= 3) e.duration = p[1];
            return;
        case 0xFF:
            e.size = 3; e.b[1] = p[1]; e.b[2] = p[2];
            return;
        case 0xFB:
            e.b[1] = 0xFF;
            if (s.x[4] < 2) {
                const int k = s.x[4]++;
                s.x[5 + k] = (s.x[0] & 0xFFFF) | ((s.trans & 0x7F) << 16) | (0xFE << 23);
                e.b[1] = uint8_t(k);
                f.kind = Flow::RepStart; f.slot = k; f.count = 0;
            }
            return;
        case 0xFC: {
            const int n = p[1];
            e.size = 2; e.b[1] = uint8_t(n); e.b[2] = 0;
            const int c = (n - 1) & 0xFF;
            if (c == 0 || s.x[4] == 0) return;
            const int k = s.x[4] - 1;
            int& w = s.x[5 + k];
            int cnt = (w >> 23) & 0xFF;
            if (cnt == 0xFE) cnt = c;
            else if (cnt != 0xFF && --cnt == 0) { --s.x[4]; return; }
            w = (w & 0x7FFFFF) | (cnt << 23);
            s.x[0] = w & 0xFFFF;
            s.trans = (w >> 16) & 0x7F;
            e.b[2] = 1;
            f.kind = Flow::RepEnd; f.slot = k; f.count = n;
            return;
        }
        case 0xFD: {
            int target = 0, t = 0, entry = 0;
            if (s.x[7] && ram_) {
                int lp = (s.x[0] + 2) & 0xFFFF;
                if (ram_[lp] != 0xFF) {
                    while (ram_[lp] & 0x80) { t = ram_[lp] & 0x7F; lp = (lp + 1) & 0xFFFF; }
                    target = (L.base + be16(ram_, lp)) & 0xFFFF;
                    s.x[0] = entry = lp;
                }
            } else {
                target = p[1] | (p[2] << 8);
                t = p[3];
                entry = p[4] | (p[5] << 8);
            }
            s.trans = t;
            e.b[1] = uint8_t(target & 0xFF); e.b[2] = uint8_t(target >> 8); e.b[3] = uint8_t(t);
            e.b[4] = uint8_t(entry & 0xFF); e.b[5] = uint8_t(entry >> 8);
            if (!target) { e.type = EventType::End; f.kind = Flow::End; return; }
            f.kind = Flow::Jump; f.target = target; f.count = 1;
            return;
        }
        case 0xFE:
            e.type = EventType::End; f.kind = Flow::End;
            return;
        default:
            return;
    }
}

// Sections another voice also plays are shared bytes.
void NeverlandDriver::prune_idle_voices(const uint8_t* ram, seq::Pattern& p) const {
    (void)ram;
    std::vector<std::vector<bool>> used(8, std::vector<bool>(0x10000, false));
    for (int v = 0; v < 8; ++v)
        for (const Event& e : p.tracks[v].events)
            if (e.addr) for (int k = 0; k < e.size; ++k) used[size_t(v)][size_t((e.addr + k) & 0xFFFF)] = true;
    for (int v = 0; v < 8; ++v)
        for (Event& e : p.tracks[v].events) {
            if (!e.addr || e.in_sub) continue;
            for (int w = 0; w < 8 && !e.in_sub; ++w) if (w != v && used[size_t(w)][e.addr]) e.in_sub = true;
        }
}

std::string NeverlandDriver::song_label(const uint8_t* ram, uint16_t header, const seq::Pattern& p, int index) const {
    (void)index;
    std::string title;
    for (int i = 0; i < 12; ++i) { const char c = char(ram[(header + 4 + i) & 0xFFFF]); title += c >= 0x20 && c < 0x7F ? c : ' '; }
    while (!title.empty() && title.back() == ' ') title.pop_back();
    bool loops = false;
    for (int v = 0; v < 8; ++v) if (p.tracks[v].loops) loops = true;
    char b[96];
    std::snprintf(b, sizeof b, "%s (%d ticks%s)", title.empty() ? "song" : title.c_str(), p.length_ticks, loops ? ", loops" : "");
    return b;
}

double NeverlandDriver::ticks_per_second(const uint8_t* ram) const {
    const int latch = ram[0xFA] ? ram[0xFA] : 256;
    return 8000.0 / latch * (L.fast_flag && ram[L.fast_flag] ? 2 : 1);
}

bool NeverlandDriver::tempo_writes(const uint8_t* ram, double tps, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    if (tps <= 0) return false;
    const int mult = L.fast_flag && ram[L.fast_flag] ? 2 : 1;
    out.push_back({0xFA, uint8_t(std::clamp(int(8000.0 * mult / tps + 0.5), 1, 255))});
    return true;
}

uint16_t NeverlandDriver::live_ptr(const uint8_t* ram, const seq::Position& pos, int v) const {
    (void)pos;
    return uint16_t(ram[(L.ptr_lo + v) & 0xFFFF] | (ram[(L.ptr_hi + v) & 0xFFFF] << 8));
}

void NeverlandDriver::live_state_writes(const uint8_t* ram, const seq::Position& pos, int voice, uint16_t ptr, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    auto map = [&](uint16_t a) {
        int m = remap.find(a);
        if (m < 0) for (const auto& p : reloc_) if (p.first == a) { m = p.second; break; }
        return m;
    };
    auto rewritten = [&](uint16_t a) { for (const Piece& p : pieces_) if (a >= p.lo && a <= p.hi) return true; return false; };
    auto word = [&](uint16_t lo, uint16_t hi, uint16_t val) { out.push_back({lo, uint8_t(val & 0xFF)}); out.push_back({hi, uint8_t(val >> 8)}); };
    // A pointer in a section that stayed keeps its value; one in a
    // rewritten section follows its byte, or the caller's guess.
    const uint16_t live = live_ptr(ram, pos, voice);
    if (int m = map(live); m >= 0) ptr = uint16_t(m);
    else if (!rewritten(live)) ptr = live;
    word(uint16_t(L.ptr_lo + voice), uint16_t(L.ptr_hi + voice), ptr);
    for (int k = 0; k < 2; ++k) {
        const int o = k * L.slot_stride + voice;
        if (!ram[(L.slot_lp_hi + o) & 0xFFFF]) continue;
        const uint16_t sp = uint16_t(ram[(L.slot_sp_lo + o) & 0xFFFF] | (ram[(L.slot_sp_hi + o) & 0xFFFF] << 8));
        if (int m = map(sp); m >= 0) word(uint16_t(L.slot_sp_lo + o), uint16_t(L.slot_sp_hi + o), uint16_t(m));
    }
}

void NeverlandDriver::track_pointer_writes(const seq::Song& song, int pattern_idx, int voice, uint16_t dest, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    (void)song; (void)pattern_idx;
    for (const Piece& p : pieces_) {
        const int off = int(dest) + p.offset - int(L.base);
        const uint16_t entry = p.entry ? p.entry : entry_[voice];
        if (off < 0 || off >= 0x8000 || !entry) { out.clear(); return; }
        out.push_back({entry, uint8_t(off >> 8)});
        out.push_back({uint16_t(entry + 1), uint8_t(off & 0xFF)});
    }
}

// The block holds every section visit with new events, each once: repeat
// passes the parse expanded are skipped.
std::vector<uint8_t> NeverlandDriver::serialize_relocated(const std::vector<Event>& ev, uint16_t dest, std::vector<int>* offsets) const {
    std::vector<int> off(ev.size(), -1);
    std::vector<uint8_t> out;
    reloc_.clear();
    pieces_.clear();
    std::vector<std::pair<int, int>> visits;   // [first, last] event of each section visit
    for (size_t i = 0, first = 0; i < ev.size(); ++i)
        if (is_op(ev[i], 0xFD) || ev[i].type == EventType::End || i + 1 == ev.size()) { visits.push_back({int(first), int(i)}); first = i + 1; }
    auto modified = [&](const std::pair<int, int>& v) {   // new or unrolled events, or a gap where one was removed
        int next = -1;
        for (int i = v.first; i <= v.second; ++i) {
            const Event& e = ev[size_t(i)];
            if (e.in_sub) continue;
            if (!e.addr || e.in_call || (next >= 0 && e.addr != next)) return true;
            next = e.addr + e.size;
        }
        return false;
    };
    for (size_t k = 0; k < visits.size(); ++k) {
        const auto& v = visits[k];
        if (!modified(v)) continue;
        Piece piece{0, uint16_t(out.size()), 0xFFFF, 0};
        if (k) {   // the previous section end names this visit's entry and where its old bytes start
            const Event& fd = ev[size_t(visits[k - 1].second)];
            piece.entry = uint16_t(fd.b[4] | (fd.b[5] << 8));
            if (fd.b[1] | fd.b[2]) piece.lo = uint16_t(fd.b[1] | (fd.b[2] << 8));
        }
        std::vector<int> frames;
        bool skipping = false, ended = false;
        int skip_nest = 0;
        for (int i = v.first; i <= v.second && !ended; ++i) {
            const Event& e = ev[size_t(i)];
            if (skipping) {   // later passes, up to the FC that fell through (the same bytes as the one written)
                if (is_op(e, 0xFC) && e.nest == skip_nest && e.b[2] == 0) { skipping = false; frames.pop_back(); }
                continue;
            }
            if (is_op(e, 0xFB)) {
                if (e.b[1] != 0xFF) frames.push_back(e.nest + 1);
            } else if (is_op(e, 0xFC) && e.b[2]) {
                if (!frames.empty() && e.nest == frames.back()) { skipping = true; skip_nest = e.nest; }
                else ended = true;   // jumps back to an earlier section: what follows replays other bytes
            } else if (is_op(e, 0xFC) && !frames.empty() && e.nest == frames.back()) {
                frames.pop_back();
            }
            off[size_t(i)] = int(out.size());
            if (e.addr) {
                piece.lo = std::min(piece.lo, e.addr);
                piece.hi = std::max(piece.hi, uint16_t(e.addr + e.size));
                for (int b = 0; b <= e.size; ++b) reloc_.push_back({uint16_t(e.addr + b), uint16_t(dest + out.size() + b)});
            }
            for (int b = 0; b < e.size; ++b) out.push_back(e.b[b]);
        }
        if (ended) out.push_back(0xFD);   // the section's own end, reached only on the last pass
        pieces_.push_back(piece);
    }
    if (offsets) *offsets = off;
    return out;
}

void NeverlandDriver::longify(Event& e) {
    if (!is_short(e)) return;
    e.b[0] &= 0x7F;
    e.size = 4;
    e.addr = 0;
}

void NeverlandDriver::longify_next(std::vector<Event>& ev, int i) const {
    for (size_t k = size_t(i) + 1; k < ev.size(); ++k) {
        if (ev[k].type == EventType::Command || ev[k].b[0] >= 0xF0) continue;
        if (!is_short(ev[k])) return;
        if (ev[k].in_sub) {
            const int t = ev[k].tick;
            unroll_at(ev, t);
            retime(ev);
            for (Event& e : ev) if (e.tick == t && is_short(e) && !e.in_sub) { longify(e); return; }
            return;
        }
        longify(ev[k]);
        return;
    }
}

// Unrolled events keep their address (the live state may point into
// those bytes) and are flagged in_call, which this format never sets.
bool NeverlandDriver::unroll_at(std::vector<Event>& ev, int tick) const {
    retime(ev);
    int i = -1;
    for (size_t k = 0; k < ev.size(); ++k) {
        const Event& e = ev[k];
        if (!e.in_sub) continue;
        if ((e.duration > 0 && e.tick <= tick && tick < e.tick + e.duration) || (e.duration == 0 && e.tick == tick)) { i = int(k); break; }
    }
    if (i < 0) return false;
    const int nest = ev[size_t(i)].nest;
    // A later pass of a repeat inside one section: write every pass out
    // and drop the frame. One that spans sections cannot be rewritten
    // without changing the list, so it stays shared.
    if (nest > 0) {
        int fb = -1;
        bool replay = false;
        for (int j = i - 1; j >= 0; --j) {
            const Event& e = ev[size_t(j)];
            if (is_op(e, 0xFC) && e.nest == nest) { if (e.b[2] == 0) break; replay = true; }
            if (is_op(e, 0xFD)) break;
            if (is_op(e, 0xFB) && e.b[1] != 0xFF && e.nest == nest - 1) { fb = j; break; }
        }
        if (fb >= 0 && replay) {
            int last = i;
            for (size_t j = size_t(i); j < ev.size(); ++j) {
                if (is_op(ev[j], 0xFD)) return false;
                if (is_op(ev[j], 0xFC) && ev[j].nest == nest) { last = int(j); if (ev[j].b[2] == 0) break; }
            }
            std::vector<Event> out;
            out.reserve(ev.size());
            for (size_t j = 0; j < ev.size(); ++j) {
                if (int(j) == fb || (int(j) > fb && int(j) <= last && is_op(ev[j], 0xFC) && ev[j].nest == nest)) continue;
                Event c = ev[j];
                if (int(j) > fb && int(j) < last && c.in_sub) { c.in_sub = false; c.in_call = true; c.sub_iter = 0; }
                out.push_back(c);
            }
            ev.swap(out);
            retime(ev);
            return true;
        }
        if (replay) return false;
    }
    // This visit of a section shared with another voice or played again
    // earlier: give the voice its own copy.
    int a = i, b = i;
    while (a > 0 && ev[size_t(a - 1)].in_sub && !is_op(ev[size_t(a - 1)], 0xFD)) --a;
    while (b + 1 < int(ev.size()) && ev[size_t(b + 1)].in_sub && !is_op(ev[size_t(b)], 0xFD) && !(is_op(ev[size_t(b)], 0xFC) && ev[size_t(b)].b[2])) ++b;   // up to the section end or a jump back
    for (int j = a; j <= b; ++j) { Event& c = ev[size_t(j)]; c.in_sub = false; c.in_call = true; c.sub_iter = 0; }   // in_call: a copy of bytes that stay in use elsewhere
    retime(ev);
    return true;
}

bool NeverlandDriver::transpose_event(Event& e, int semis) const {
    if (e.type != EventType::Note || e.b[0] >= 0xF0) return false;
    const int key = (e.b[0] & 0x7F) + semis;
    if (key < 0 || key > (e.size == 1 ? 0x6F : 0x7F)) return false;
    e.b[0] = uint8_t((e.b[0] & 0x80) | key);
    if (e.pitch >= 0) e.pitch += semis;
    return true;
}

void NeverlandDriver::apply_note_byte(Event& e, uint8_t byte) const {
    if (byte == kRest || byte == kTie) {
        if (e.b[0] < 0xF0) { longify(e); e.b[2] = 0; }
        e.type = byte == kRest ? EventType::Rest : EventType::Tie;
        if (e.type == EventType::Rest) e.pitch = -1;
        return;
    }
    const int key = byte & 0x7F;
    if (e.b[0] == 0xF3 && !L.v1) {
        e.b[0] = uint8_t(key); e.b[2] = e.b[1]; e.size = 4; e.addr = 0;
        e.pitch = key + 12;
    } else if (e.b[0] >= 0xF0) {
        return;
    } else {
        const int old = e.b[0] & 0x7F;
        if (e.size == 1 && key <= 0x6F) e.b[0] = uint8_t(0x80 | key);
        else { longify(e); e.b[0] = uint8_t(key); }
        if (e.b[2] == 0) e.b[2] = e.b[1];
        if (e.pitch >= 0 && (e.type == EventType::Note || e.type == EventType::Tie)) e.pitch += key - old;
        else e.pitch = key + 12;
    }
    e.type = EventType::Note;
}

bool NeverlandDriver::set_note_at(std::vector<Event>& ev, int tick, uint8_t byte, int pattern_len) const {
    seq::stream_prepare(*this, ev, tick, pattern_len);
    int i = seq::stream_timed_covering(ev, tick, true);
    if (i < 0) return false;
    if (ev[size_t(i)].tick != tick) {
        if (ev[size_t(i)].in_sub || !seq::stream_split_at(*this, ev, tick)) return false;
        i = -1;
        for (size_t k = 0; k < ev.size(); ++k) if (timed(ev[k]) && !ev[k].in_sub && ev[k].tick == tick) { i = int(k); break; }
        if (i < 0) return false;
    }
    if (ev[size_t(i)].in_sub) return false;
    longify_next(ev, i);
    apply_note_byte(ev[size_t(i)], byte);
    if (byte == kRest || byte == kTie) {
        // The note before decides whether it keeps ringing into this event.
        int j = -1;
        for (int k = i - 1; k >= 0; --k) if (timed(ev[size_t(k)])) { j = k; break; }
        if (j >= 0 && !ev[size_t(j)].in_sub && ev[size_t(j)].b[0] < 0xF0 && ev[size_t(j)].b[2] > 0) {
            Event& n = ev[size_t(j)];
            const int len = n.b[1];
            if (byte == kRest && n.b[2] > len) { longify_next(ev, j); longify(n); n.b[2] = uint8_t(len); n.addr = 0; }
            if (byte == kTie && n.b[2] <= len && len < 255) { longify_next(ev, j); longify(n); n.b[2] = uint8_t(len + 1); n.addr = 0; }
        }
    }
    retime(ev);
    return true;
}

bool NeverlandDriver::set_duration(std::vector<Event>& ev, int i, int dur) const {
    if (i < 0 || i >= int(ev.size()) || dur < 1 || dur > 4096) return false;
    Event& e = ev[size_t(i)];
    if (e.type == EventType::Command || e.type == EventType::End) return false;
    std::vector<Event> tail;
    if (L.v1 && (e.b[0] == kRest || e.b[0] == kTie)) { e.b[0] = 0; e.b[1] = 0; e.b[2] = 0; e.b[3] = 0x40; e.size = 4; }   // no wait command: a note with gate 0
    if (e.b[0] >= 0xF0 || e.b[0] == kRest || e.b[0] == kTie) {   // a wait
        if (e.b[0] != 0xF3) { e.b[0] = 0xF3; e.b[2] = e.b[3] = 0; }
        const int first = std::min(dur, 255);
        e.b[1] = uint8_t(first); e.size = 2; e.duration = first; e.addr = 0;
        for (int left = dur - first; left > 0; left -= std::min(left, 255)) {
            Event t = e;
            t.b[1] = uint8_t(std::min(left, 255)); t.duration = t.b[1];
            tail.push_back(t);
        }
    } else {
        longify_next(ev, i);
        longify(e);
        const int old_len = e.b[1], old_gate = e.b[2];
        const int first = dur > 255 ? 254 : dur;
        int gate;
        if (old_gate == 0) gate = 0;
        else if (dur > 255) gate = first + 1;
        else if (old_gate > old_len) gate = std::min(255, first + 1);
        else if (old_gate == old_len) gate = first;
        else gate = std::min(old_gate, first);
        e.b[1] = uint8_t(first); e.b[2] = uint8_t(gate); e.duration = first; e.addr = 0;
        for (int left = dur - first; left > 0; left -= std::min(left, 255)) {
            Event t = e;
            t.b[1] = uint8_t(std::min(left, 255)); t.b[2] = 0; t.duration = t.b[1];
            t.type = e.type == EventType::Rest ? EventType::Rest : EventType::Tie;
            tail.push_back(t);
        }
    }
    ev.insert(ev.begin() + i + 1, tail.begin(), tail.end());
    return true;
}

bool NeverlandDriver::set_instrument(std::vector<Event>& ev, int tick0, int tick1, uint8_t ins) const {
    seq::stream_prepare(*this, ev, tick0, tick1);
    for (Event& e : ev)
        if (is_op(e, 0xF7) && !e.in_sub && e.tick >= tick0 && e.tick < tick1) { e.b[2] = ins; return true; }
    const uint8_t bytes[3] = {0xF7, 0, ins};
    return seq::stream_insert_command_at(*this, ev, tick0, bytes, 3);
}

bool NeverlandDriver::remove_span(std::vector<Event>& ev, int tick, int ticks, bool keep_length) const {
    (void)keep_length;
    retime(ev);
    const int t1 = tick + ticks;
    for (int guard = 0; guard < 64; ++guard) {
        int hit = -1;
        for (const Event& e : ev) if (e.in_sub && timed(e) && e.tick < t1 && e.tick + e.duration > tick) { hit = e.tick; break; }
        if (hit < 0 || !unroll_at(ev, std::max(hit, tick))) break;
    }
    retime(ev);
    // Notes that inherit their length from one about to change get it spelled out.
    for (size_t k = 0; k < ev.size(); ++k) {
        Event& e = ev[k];
        if (e.type == EventType::Command || e.b[0] >= 0xF0 || e.tick + e.duration <= tick) continue;
        if (is_short(e) && !e.in_sub) longify(e);
        if (e.tick >= t1) break;
    }
    return seq::stream_remove_span(*this, ev, tick, ticks);
}

bool NeverlandDriver::note_retriggers(const std::vector<Event>& ev, int i) const {
    return ev[size_t(i)].type != EventType::Tie;
}

int NeverlandDriver::instrument_count(const uint8_t* ram) const {
    (void)ram;
    return L.ins_table ? 32 : 0;
}

bool NeverlandDriver::instrument_used(const uint8_t* ram, int index) const {
    const uint8_t* t = ram + ((L.ins_table + index * 4) & 0xFFFF);
    return (t[0] | t[1]) != 0;
}

seq::Instrument NeverlandDriver::read_instrument(const uint8_t* ram, int index) const {
    seq::Instrument ins;
    const uint8_t* t = ram + ((L.ins_table + index * 4) & 0xFFFF);
    ins.srcn = uint8_t(index);
    ins.adsr0 = t[0]; ins.adsr1 = t[1];
    ins.pitch_hi = t[2]; ins.pitch_lo = t[3];
    return ins;
}

bool NeverlandDriver::preview_regs(const uint8_t* ram, uint8_t note_byte, int instrument, uint8_t regs[8]) const {
    if (!L.ins_table || !L.pitch_lo || instrument < 0 || instrument >= 32) return false;
    const int key = note_byte & 0x7F, oct = key / 12, n = key % 12;
    if (oct > 8) return false;
    int p = ram[(L.pitch_lo + n) & 0xFFFF] | (ram[(L.pitch_hi + n) & 0xFFFF] << 8);
    if (oct) p >>= 8 - oct;
    const uint8_t* t = ram + ((L.ins_table + instrument * 4) & 0xFFFF);
    const int pitch = std::min(((p * ((t[2] << 8) | t[3])) >> 8) & 0xFFFF, 0x3FFF);
    regs[0] = 0x40; regs[1] = 0x40;
    regs[2] = uint8_t(pitch & 0xFF); regs[3] = uint8_t(pitch >> 8);
    regs[4] = uint8_t(instrument); regs[5] = t[0]; regs[6] = t[1]; regs[7] = 0;
    return true;
}

std::string NeverlandDriver::event_text(const Event& e) const {
    char b[96];
    if (e.b[0] < 0xF0 && e.type != EventType::Command) {
        const char* kind = e.type == EventType::Rest ? "rest" : e.type == EventType::Tie ? "tie" : "note";
        if (e.type == EventType::Note || (e.type == EventType::Tie && e.pitch >= 0)) std::snprintf(b, sizeof b, "%s %s  %d ticks, gate %d, vel %d%s", kind, note_name(e).c_str(), e.b[1], e.b[2], e.b[3], e.size == 1 ? " (inherited)" : "");
        else std::snprintf(b, sizeof b, "%s  %d ticks%s", kind, e.b[1], e.size == 1 ? " (inherited)" : "");
        return b;
    }
    switch (e.b[0]) {
        case 0xF3: std::snprintf(b, sizeof b, "%s  %d ticks", e.type == EventType::Tie ? "hold" : "wait", e.b[1]); return b;
        case 0xF4: std::snprintf(b, sizeof b, "Tempo latch %d (%.1f ticks/s)", e.b[2], e.b[2] ? 8000.0 / e.b[2] : 0.0); return b;
        case 0xFB: return e.b[1] == 0xFF ? "Repeat start (no slot free)" : "Repeat start";
        case 0xFC: if (e.b[1] == 0) return "Repeat forever"; std::snprintf(b, sizeof b, "Repeat x%d", e.b[1]); return b;
        case 0xFD:
            if (e.type == EventType::End) return "Section end (last)";
            std::snprintf(b, sizeof b, "Section end, next at $%02X%02X transposed +%d", e.b[2], e.b[1], e.b[3]);
            return b;
        case 0xFF: std::snprintf(b, sizeof b, "%s %d", e.b[1] < 0x1F ? kExtNames[e.b[1]] : "(unknown extended)", e.b[2]); return b;
        default: break;
    }
    if (e.type == EventType::Command && e.size == 3) {
        const char* name = e.b[0] == 0xF1 ? "Volume" : e.b[0] == 0xF7 ? "Instrument" : cmd_name(e.b[0]);
        if (e.b[1]) std::snprintf(b, sizeof b, "%s %d, then wait %d", name, e.b[2], e.b[1]);
        else std::snprintf(b, sizeof b, "%s %d", name, e.b[2]);
        return b;
    }
    return seq::Driver::event_text(e);
}

bool NeverlandDriver::pitch_fx(const Event& e, seq::PitchFx& out) const {
    if (!is_op(e, 0xF0)) return false;
    out = seq::PitchFx{};
    out.kind = e.b[2] ? seq::PitchFx::Vibrato : seq::PitchFx::VibratoOff;
    out.depth = e.b[2];
    out.delay = e.b[1];
    out.sticky = true;
    return true;
}

}
