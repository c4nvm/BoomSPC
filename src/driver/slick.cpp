#include "slick.hpp"

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

namespace slick {
namespace {
const int W = 0x100;

// State: flags bit 0 = notes carry a velocity byte, x[0] = next order
// entry (parse only), x[1] = a pattern's header byte and delay come next,
// x[2] = loop point, x[7] = parsing from RAM. Every event keeps the
// velocity flag in b[14]; a pattern start has b[15] = A1.

const CmdSpec kCmds[0x10] = {
    {1, "Nop", "(no effect)", FxClass::Misc},                    // E0
    {1, "Nop", "(no effect)", FxClass::Misc},                    // E1
    {2, "Lop", "Loop point (0) / loop (other)", FxClass::Song},  // E2
    {1, "End", "End of track", FxClass::Song},                   // E3
    {1, "Nop", "(no effect)", FxClass::Misc},                    // E4
    {1, "Nop", "(no effect)", FxClass::Misc},                    // E5
    {2, "Prg", "Program", FxClass::Instrument},                  // E6
    {2, "Tmp", "Tempo ((t + 40) / 60 ticks per timer tick)", FxClass::Speed},   // E7
    {1, "Pat", "Next pattern", FxClass::Song},                   // E8
    {3, "DSP", "DSP setting (which, value)", FxClass::Sys1},     // E9
    {1, "Rst", "Rest", FxClass::Time},                           // EA
    {1, "Nop", "(no effect; ends a sound effect)", FxClass::Misc},   // EB
    {2, "Mut", "Voice unmute", FxClass::Sys1},                   // EC
    {2, "Mut", "Voice mute", FxClass::Sys1},                     // ED
    {2, "Mut", "Voice mute count", FxClass::Sys1},               // EE
    {1, "Nop", "(no effect)", FxClass::Misc},                    // EF
};
const CmdSpec kByte = {1, "Cmd", "(one byte)", FxClass::Misc};
const CmdSpec kCtl = {2, "Ctl", "Controller (n & 1F, value)", FxClass::Sys2};
const CmdSpec kNop = {1, "Nop", "(no effect)", FxClass::Misc};

inline bool is_op(const Event& e, uint8_t op) { return e.type == EventType::Command && e.b[0] == op; }
}

int SlickDriver::vlq(const uint8_t* p, int& n) {
    int v = 0;
    n = 0;
    for (;;) {
        const uint8_t b = p[n++];
        v = (v << 7) | (b & 0x7F);
        if (!(b & 0x80) || n == 3) return v;
    }
}

int SlickDriver::put_vlq(uint8_t* p, int v) {
    v = std::clamp(v, 0, 0x1FFFFF);
    if (v < 0x80) { p[0] = uint8_t(v); return 1; }
    if (v < 0x4000) { p[0] = uint8_t(0x80 | (v >> 7)); p[1] = uint8_t(v & 0x7F); return 2; }
    p[0] = uint8_t(0x80 | (v >> 14)); p[1] = uint8_t(0x80 | ((v >> 7) & 0x7F)); p[2] = uint8_t(v & 0x7F);
    return 3;
}

Layout detect_layout(const uint8_t* ram) {
    Layout L;
    const int fetch[] = {0xF5, W, W, 0xC4, 0x1F, 0xF5, W, W, 0xC4, 0x20, 0xF5, W, W, 0xC4, 0x33, 0xF5, W, W, 0xC4, 0x34, 0x8D, 0x00, 0xF7, 0x1F};
    int p = find_pattern(ram, 0x200, 0x2000, fetch, 24);
    if (p < 0) return Layout{};
    L.ptr_lo = uint16_t(rd16(ram, p + 1));
    L.ptr_hi = uint16_t(rd16(ram, p + 6));
    L.flags = uint16_t(rd16(ram, p + 11));
    L.state = uint16_t(rd16(ram, p + 16));
    const int dir[] = {0x8F, W, 0x0E, 0x8F, W, 0x0F, 0x8D, 0x00, 0xFC, 0xAD, 0x11};
    int d = find_pattern(ram, 0x200, 0x2000, dir, 11);
    if (d < 0) return Layout{};
    L.dir = uint16_t(ram[d + 1] | (ram[d + 4] << 8));
    const int next[] = {0xF5, W, W, 0xC4, 0x08, 0xF5, W, W, 0xC4, 0x09, 0x3A, 0x08, 0x3A, 0x08};
    int n = find_pattern(ram, 0x200, 0x2000, next, 14);
    if (n < 0) return Layout{};
    L.order_lo = uint16_t(rd16(ram, n + 1));
    L.order_hi = uint16_t(rd16(ram, n + 6));
    const int base[] = {0xF5, W, W, 0x60, 0x84, 0x1F, 0xC4, 0x1F, 0xF5, W, W, 0x84, 0x20};
    if (int b = find_pattern(ram, n, n + 60, base, 13); b >= 0) { L.base_lo = uint16_t(rd16(ram, b + 1)); L.base_hi = uint16_t(rd16(ram, b + 9)); }
    const int loop[] = {0xE4, 0x1F, 0xD5, W, W, 0xE4, 0x20, 0xD5, W, W, 0xF5, W, W, 0xD5, W, W, 0xF5, W, W, 0xD5, W, W, 0x5F};
    if (int l = find_pattern(ram, 0x200, 0x2000, loop, 23); l >= 0) {
        L.loop_lo = uint16_t(rd16(ram, l + 3)); L.loop_hi = uint16_t(rd16(ram, l + 8));
        L.loop_order_lo = uint16_t(rd16(ram, l + 14)); L.loop_order_hi = uint16_t(rd16(ram, l + 20));
    }
    const int tempo[] = {0xF5, W, W, 0xEB, 0x2E, 0xCF, 0xC4, 0x09, 0x8F, 0x00, 0x08, 0xEB, 0x2E, 0xF5, W, W, 0xCF};
    if (int t = find_pattern(ram, 0x200, 0x2000, tempo, 17); t >= 0) { L.tempo_int = uint16_t(rd16(ram, t + 1)); L.tempo_frac = uint16_t(rd16(ram, t + 14)); }
    return L;
}

std::unique_ptr<seq::Driver> detect(const uint8_t* ram) {
    Layout L = detect_layout(ram);
    if (!L.valid()) return nullptr;
    return std::make_unique<SlickDriver>(L);
}

const CmdSpec& SlickDriver::spec(uint8_t op) const {
    if (op < 0x80) return kNop;
    if (op < 0xC0) return kByte;
    if (op < 0xE0) return kCtl;
    if (op < 0xF0) return kCmds[op - 0xE0];
    return kNop;
}

int SlickDriver::data_base(const uint8_t* ram, uint16_t header) const {
    return (header + 7 + (ram[(header + 6) & 0xFFFF] ? 12 : 0) + 8 * ram[(header + 3) & 0xFFFF]) & 0xFFFF;
}

uint16_t SlickDriver::order_start(const uint8_t* ram, uint16_t header, int v) const {
    if (v >= ram[(header + 3) & 0xFFFF]) return 0;
    const int e = (header + 7 + (ram[(header + 6) & 0xFFFF] ? 12 : 0) + v * 8) & 0xFFFF;
    return uint16_t((header + rd16(ram, e + 6)) & 0xFFFF);
}

std::vector<uint16_t> SlickDriver::song_headers(const uint8_t* ram) const {
    ram_ = ram;
    std::vector<uint16_t> out;
    for (int i = 0; i < 8; ++i) {
        const int h = rd16(ram, L.dir + i * 2);
        if (h < 0x200 || h >= 0xFF00) continue;
        const int n = ram[h + 3];
        if (n < 1 || n > 22) continue;
        bool ok = true;
        for (int v = 0; v < n && ok; ++v) {
            const int o = order_start(ram, uint16_t(h), v);
            ok = o >= 0x200 && o < 0xFFF0 && ((data_base(ram, uint16_t(h)) + rd16(ram, o)) & 0xFFFF) >= 0x200;
        }
        if (ok && std::find(out.begin(), out.end(), uint16_t(h)) == out.end()) out.push_back(uint16_t(h));
    }
    return out;
}

// Only slots that are playing count: idle ones keep stale pointers.
int SlickDriver::pick_current_song(const uint8_t* ram, const std::vector<seq::Song>& songs) const {
    int best = -1, best_hits = 0;
    seq::Position none;
    for (size_t i = 0; i < songs.size(); ++i) {
        const seq::Pattern& p = songs[i].patterns[0];
        int hits = 0;
        for (int v = 0; v < 8; ++v) {
            if (L.state && !(ram[(L.state + v) & 0xFFFF] & 0x80)) continue;
            const uint16_t ptr = live_ptr(ram, none, v);
            for (const Event& e : p.tracks[v].events)
                if (e.addr == ptr || uint16_t(e.addr + e.size) == ptr) { ++hits; break; }
        }
        if (hits > best_hits) { best_hits = hits; best = int(i); }
    }
    return best >= 0 ? best : (songs.empty() ? -1 : 0);
}

uint16_t SlickDriver::track_start(const uint8_t* ram, uint16_t header, int v) const {
    const uint16_t o = order_start(ram, header, v);
    if (!o) return 0;
    const int a = (data_base(ram, header) + rd16(ram, o)) & 0xFFFF;
    return a >= 0x200 && a < 0xFFF0 ? uint16_t(a) : 0;
}

State SlickDriver::initial_state(const uint8_t* ram, uint16_t header, int voice) const {
    ram_ = ram;
    base_ = data_base(ram, header);
    State s;
    s.x[7] = 1;
    s.x[1] = 1;
    const uint16_t o = order_start(ram, header, voice);
    s.x[0] = (o + 2) & 0xFFFF;
    const int e = (header + 7 + (ram[(header + 6) & 0xFFFF] ? 12 : 0) + voice * 8) & 0xFFFF;
    s.flags = (ram[e] & 1) ? 0 : 1;
    return s;
}

State SlickDriver::edit_state(const std::vector<Event>& ev) const {
    State s;
    s.x[1] = ev.empty() || is_start(ev[0]) ? 1 : 0;
    for (const Event& e : ev) if (e.b[14] & 0x80) { s.flags = e.b[14] & 1; break; }
    return s;
}

void SlickDriver::decode(const uint8_t* p, int pc, State& s, Event& e, Flow& f) const {
    const uint8_t b = p[0];
    e.b[0] = b;
    e.size = 1;
    e.pitch = -1;
    e.type = EventType::Command;
    e.b[14] = uint8_t(0x80 | (s.flags & 1));
    int n = 0;
    auto tail = [&](int at) {   // the delay after the event's bytes
        e.duration = vlq(p + at, n);
        e.size = uint8_t(at + n);
        for (int i = 1; i < e.size; ++i) e.b[i] = p[i];
    };
    if (s.x[1]) {   // a pattern's header byte and first delay
        s.x[1] = 0;
        tail(1);
        e.type = EventType::Rest;
        e.b[15] = 0xA1;
        return;
    }
    if (b < 0x80) {
        int at = 1;
        if (s.flags & 1) at = 2;
        vlq(p + at, n);   // the gate
        tail(at + n);
        e.type = EventType::Note;
        e.pitch = b;
        return;
    }
    if (b < 0xC0) { tail(1); return; }
    if (b < 0xE0) { tail(2); return; }
    switch (b) {
        case 0xE3: e.type = EventType::End; f.kind = Flow::End; return;
        case 0xE8: {
            int target = 0, entry = 0;
            if (s.x[7] && ram_) {
                entry = s.x[0] & 0xFFFF;
                target = (base_ + rd16(ram_, entry)) & 0xFFFF;
                s.x[0] = (entry + 2) & 0xFFFF;
            } else { target = p[1] | (p[2] << 8); entry = p[3] | (p[4] << 8); }
            e.b[1] = uint8_t(target & 0xFF); e.b[2] = uint8_t(target >> 8);
            e.b[3] = uint8_t(entry & 0xFF); e.b[4] = uint8_t(entry >> 8);
            s.x[1] = 1;
            f.kind = Flow::Jump; f.target = target; f.count = 1;
            return;
        }
        case 0xE2:
            if (p[1] == 0) { tail(2); if (pc) s.x[2] = pc; return; }   // the loop point
            e.size = 2; e.b[1] = p[1];
            {
                const int target = s.x[7] ? s.x[2] : (p[2] | (p[3] << 8));
                e.b[2] = uint8_t(target & 0xFF); e.b[3] = uint8_t(target >> 8);
                if (!target) { e.type = EventType::End; f.kind = Flow::End; return; }
                f.kind = Flow::Jump; f.target = target; f.count = 0;
            }
            return;
        case 0xE6: case 0xE7: case 0xEC: case 0xED: case 0xEE: tail(2); return;
        case 0xE9: tail(3); return;
        case 0xEA: tail(1); e.type = EventType::Rest; return;
        default: tail(1); return;   // E0 E1 E4 E5 EB EF and anything else
    }
}

// Patterns another track also plays are shared bytes.
void SlickDriver::prune_idle_voices(const uint8_t* ram, seq::Pattern& p) const {
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

std::string SlickDriver::song_label(const uint8_t* ram, uint16_t header, const seq::Pattern& p, int index) const {
    bool loops = false;
    for (int v = 0; v < 8; ++v) if (p.tracks[v].loops) loops = true;
    char b[80];
    std::snprintf(b, sizeof b, "song %d (id %d) @%04X, %d tracks (%d ticks%s)", index, ram[header + 2], header, ram[header + 3], p.length_ticks, loops ? ", loops" : "");
    return b;
}

double SlickDriver::ticks_per_second(const uint8_t* ram) const {
    const double timer = 8000.0 / (ram[0xFA] ? ram[0xFA] : 256);
    if (!L.tempo_int) return timer;
    const double f = ram[L.tempo_int] + ram[L.tempo_frac] / 256.0;
    return f > 0 ? timer * f : timer;
}

bool SlickDriver::tempo_writes(const uint8_t* ram, double tps, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    if (tps <= 0 || !L.tempo_int) return false;
    const double timer = 8000.0 / (ram[0xFA] ? ram[0xFA] : 256);
    const int f = std::clamp(int(tps / timer * 256.0 + 0.5), 1, 0xFFFF);
    for (int v = 0; v < 8; ++v) {
        if (L.state && !(ram[(L.state + v) & 0xFFFF] & 0x80)) continue;
        out.push_back({uint16_t(L.tempo_int + v), uint8_t(f >> 8)});
        out.push_back({uint16_t(L.tempo_frac + v), uint8_t(f & 0xFF)});
    }
    return true;
}

uint16_t SlickDriver::live_ptr(const uint8_t* ram, const seq::Position& pos, int v) const {
    (void)pos;
    return uint16_t(ram[(L.ptr_lo + v) & 0xFFFF] | (ram[(L.ptr_hi + v) & 0xFFFF] << 8));
}

void SlickDriver::live_state_writes(const uint8_t* ram, const seq::Position& pos, int voice, uint16_t ptr, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    auto map = [&](uint16_t a) {
        int m = remap.find(a);
        if (m < 0) for (const auto& p : reloc_) if (p.first == a) { m = p.second; break; }
        return m;
    };
    auto rewritten = [&](uint16_t a) { for (const Piece& p : pieces_) if (a >= p.lo && a <= p.hi) return true; return false; };
    auto word = [&](uint16_t lo, uint16_t hi, uint16_t val) { out.push_back({lo, uint8_t(val & 0xFF)}); out.push_back({hi, uint8_t(val >> 8)}); };
    const uint16_t live = live_ptr(ram, pos, voice);
    if (int m = map(live); m >= 0) ptr = uint16_t(m);
    else if (!rewritten(live)) ptr = live;   // a pattern that stayed
    word(uint16_t(L.ptr_lo + voice), uint16_t(L.ptr_hi + voice), ptr);
    if (L.loop_lo) {
        const uint16_t lp = uint16_t(ram[(L.loop_lo + voice) & 0xFFFF] | (ram[(L.loop_hi + voice) & 0xFFFF] << 8));
        if (int m = map(lp); m >= 0) word(uint16_t(L.loop_lo + voice), uint16_t(L.loop_hi + voice), uint16_t(m));
    }
}

void SlickDriver::track_pointer_writes(const seq::Song& song, int pattern_idx, int voice, uint16_t dest, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    (void)pattern_idx;
    if (!ram_) return;
    const int base = data_base(ram_, song.order_addr);
    for (const Piece& p : pieces_) {
        const uint16_t entry = p.entry ? p.entry : order_start(ram_, song.order_addr, voice);
        if (!entry) { out.clear(); return; }
        const int off = (int(dest) + p.offset - base) & 0xFFFF;
        out.push_back({entry, uint8_t(off & 0xFF)});
        out.push_back({uint16_t(entry + 1), uint8_t(off >> 8)});
    }
}

// The block holds every pattern visit with new events, each once.
std::vector<uint8_t> SlickDriver::serialize_relocated(const std::vector<Event>& ev, uint16_t dest, std::vector<int>* offsets) const {
    std::vector<int> off(ev.size(), -1);
    std::vector<uint8_t> out;
    reloc_.clear();
    pieces_.clear();
    std::vector<std::pair<int, int>> visits;
    for (size_t i = 0, first = 0; i < ev.size(); ++i)
        if (is_op(ev[i], 0xE8) || ev[i].type == EventType::End || i + 1 == ev.size()) { visits.push_back({int(first), int(i)}); first = i + 1; }
    auto modified = [&](const std::pair<int, int>& v) {
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
        if (k) {
            const Event& pe = ev[size_t(visits[k - 1].second)];
            if (is_op(pe, 0xE8)) { piece.entry = uint16_t(pe.b[3] | (pe.b[4] << 8)); piece.lo = uint16_t(pe.b[1] | (pe.b[2] << 8)); }
        }
        for (int i = v.first; i <= v.second; ++i) {
            const Event& e = ev[size_t(i)];
            off[size_t(i)] = int(out.size());
            if (e.addr) {
                piece.lo = std::min(piece.lo, e.addr);
                piece.hi = std::max(piece.hi, uint16_t(e.addr + e.size));
                for (int b = 0; b <= e.size; ++b) reloc_.push_back({uint16_t(e.addr + b), uint16_t(dest + out.size() + b)});
            }
            for (int b = 0; b < e.size; ++b) out.push_back(e.b[b]);
        }
        pieces_.push_back(piece);
    }
    if (offsets) *offsets = off;
    return out;
}

// Unrolled events keep their address (the live state may point into
// those bytes) and are flagged in_call, which this format never sets.
bool SlickDriver::unroll_at(std::vector<Event>& ev, int tick) const {
    retime(ev);
    int i = -1;
    for (size_t k = 0; k < ev.size(); ++k) {
        const Event& e = ev[k];
        if (!e.in_sub) continue;
        if ((e.duration > 0 && e.tick <= tick && tick < e.tick + e.duration) || (e.duration == 0 && e.tick == tick)) { i = int(k); break; }
    }
    if (i < 0) return false;
    int a = i, b = i;
    while (a > 0 && ev[size_t(a - 1)].in_sub && !is_op(ev[size_t(a - 1)], 0xE8)) --a;
    while (b + 1 < int(ev.size()) && ev[size_t(b + 1)].in_sub && !is_op(ev[size_t(b)], 0xE8)) ++b;
    for (int j = a; j <= b; ++j) { Event& c = ev[size_t(j)]; c.in_sub = false; c.in_call = true; c.sub_iter = 0; }
    retime(ev);
    return true;
}

bool SlickDriver::transpose_event(Event& e, int semis) const {
    if (e.type != EventType::Note) return false;
    const int n = e.b[0] + semis;
    if (n < 0 || n > 0x7F) return false;
    e.b[0] = uint8_t(n);
    if (e.pitch >= 0) e.pitch += semis;
    return true;
}

void SlickDriver::apply_note_byte(Event& e, uint8_t byte) const {
    const bool vel = (e.b[14] & 0x80) ? (e.b[14] & 1) : false;
    if (e.type != EventType::Note && e.type != EventType::Rest) return;
    if (is_start(e)) return;
    const int dur = std::max(e.duration, 0);
    if (byte == kRest) {
        e.b[0] = kRest;
        e.size = uint8_t(1 + put_vlq(e.b + 1, dur));
        e.type = EventType::Rest; e.pitch = -1; e.addr = 0;
        return;
    }
    const int key = byte & 0x7F;
    uint8_t gate[3];
    int gl = 0;
    if (e.type == EventType::Note) {   // keep the gate
        int at = vel ? 2 : 1, n = 0;
        vlq(e.b + at, n);
        std::memcpy(gate, e.b + at, size_t(n));
        gl = n;
    } else gl = put_vlq(gate, dur);
    int at = 0;
    e.b[at++] = uint8_t(key);
    if (vel) e.b[at++] = e.type == EventType::Note ? e.b[1] : 0x7F;
    std::memcpy(e.b + at, gate, size_t(gl));
    at += gl;
    at += put_vlq(e.b + at, dur);
    e.size = uint8_t(at);
    e.type = EventType::Note; e.pitch = key; e.addr = 0;
}

// Where an event's delay starts in its bytes.
int SlickDriver::delay_at(const Event& e) {
    if (is_start(e)) return 1;
    const uint8_t b = e.b[0];
    if (b < 0x80) { int n = 0; const int at = (e.b[14] & 1) ? 2 : 1; vlq(e.b + at, n); return at + n; }
    if (b < 0xC0) return 1;
    if (b < 0xE0) return 2;
    if (b == 0xE9) return 3;
    if (b == 0xE2 || b == 0xE6 || b == 0xE7 || b == 0xEC || b == 0xED || b == 0xEE) return 2;
    return 1;
}

// Moves the delay of the event at i into a rest after it; returns the
// rest's index.
int SlickDriver::peel_delay(std::vector<Event>& ev, int i) const {
    Event& e = ev[size_t(i)];
    Event r{};
    r.type = EventType::Rest; r.b[0] = kRest; r.size = uint8_t(1 + put_vlq(r.b + 1, e.duration)); r.duration = e.duration; r.pitch = -1;
    r.b[14] = e.b[14];
    const int at = delay_at(e);
    e.b[at] = 0; e.size = uint8_t(at + 1); e.duration = 0; e.addr = 0;
    ev.insert(ev.begin() + i + 1, r);
    return i + 1;
}

bool SlickDriver::set_note_at(std::vector<Event>& ev, int tick, uint8_t byte, int pattern_len) const {
    seq::stream_prepare(*this, ev, tick, pattern_len);
    int i = seq::stream_timed_covering(ev, tick, true);
    if (i >= 0 && !ev[size_t(i)].in_sub && is_start(ev[size_t(i)])) { i = peel_delay(ev, i); retime(ev); }
    if (i < 0) {   // a command's delay covers the tick
        for (size_t k = 0; k < ev.size(); ++k)
            if (!ev[k].in_sub && ev[k].duration > 0 && ev[k].tick <= tick && tick < ev[k].tick + ev[k].duration) { i = peel_delay(ev, int(k)); break; }
        if (i < 0) return false;
        retime(ev);
    }
    if (ev[size_t(i)].tick != tick) {
        if (ev[size_t(i)].in_sub || !seq::stream_split_at(*this, ev, tick)) return false;
        i = -1;
        for (size_t k = 0; k < ev.size(); ++k) if (timed(ev[k]) && !ev[k].in_sub && ev[k].tick == tick) { i = int(k); break; }
        if (i < 0) return false;
    }
    if (ev[size_t(i)].in_sub || is_start(ev[size_t(i)])) return false;
    apply_note_byte(ev[size_t(i)], byte);
    retime(ev);
    return true;
}

bool SlickDriver::set_duration(std::vector<Event>& ev, int i, int dur) const {
    if (i < 0 || i >= int(ev.size()) || dur < 0 || dur > 0x1FFFFF) return false;
    Event& e = ev[size_t(i)];
    if (e.type != EventType::Note && e.type != EventType::Rest) return false;
    const bool vel = e.b[14] & 1;
    int at = 1;
    if (e.type == EventType::Note) {   // past the gate; a gate that reached the next event follows it
        int n = 0;
        at = vel ? 2 : 1;
        const int gate = vlq(e.b + at, n);
        if (gate >= e.duration && e.duration > 0) at += put_vlq(e.b + at, dur);
        else at += n;
    }
    e.size = uint8_t(at + put_vlq(e.b + at, dur));
    e.duration = dur;
    e.addr = 0;
    return true;
}

bool SlickDriver::set_instrument(std::vector<Event>& ev, int tick0, int tick1, uint8_t ins) const {
    seq::stream_prepare(*this, ev, tick0, tick1);
    for (Event& e : ev)
        if (is_op(e, 0xE6) && !e.in_sub && e.tick >= tick0 && e.tick < tick1) { e.b[1] = ins; return true; }
    const uint8_t bytes[3] = {0xE6, ins, 0};
    return insert_command_at(ev, tick0, bytes, 2);
}

// Commands carry their own delay: the inserted bytes get a zero one.
bool SlickDriver::insert_command_at(std::vector<Event>& ev, int tick, const uint8_t* bytes, int size) const {
    if (size <= 0 || size > 14) return false;
    uint8_t b[16];
    std::memcpy(b, bytes, size_t(size));
    b[size] = 0;
    return seq::stream_insert_command_at(*this, ev, tick, b, size + 1);
}

std::string SlickDriver::event_text(const Event& e) const {
    char b[96];
    if (is_start(e)) { std::snprintf(b, sizeof b, "pattern start (%02X), wait %d ticks", e.b[0], e.duration); return b; }
    if (e.type == EventType::Note) {
        const bool vel = e.b[14] & 1;
        int n = 0;
        const int gate = vlq(e.b + (vel ? 2 : 1), n);
        if (vel) std::snprintf(b, sizeof b, "%s  %d ticks, sounds %d, vel %d", note_name(e).c_str(), e.duration, gate, e.b[1]);
        else std::snprintf(b, sizeof b, "%s  %d ticks, sounds %d", note_name(e).c_str(), e.duration, gate);
        return b;
    }
    if (e.type == EventType::Rest) { std::snprintf(b, sizeof b, "rest  %d ticks", e.duration); return b; }
    if (is_op(e, 0xE8)) { std::snprintf(b, sizeof b, "Next pattern at $%02X%02X", e.b[2], e.b[1]); return b; }
    std::string s;
    if (is_op(e, 0xE2)) { if (e.b[1]) { std::snprintf(b, sizeof b, "Loop to $%02X%02X", e.b[3], e.b[2]); return b; } s = "Loop point"; }
    else if (e.type == EventType::Command && e.b[0] >= 0xC0 && e.b[0] < 0xE0) { std::snprintf(b, sizeof b, "Controller %d = %d", e.b[0] & 0x1F, e.b[1]); s = b; }
    else s = seq::Driver::event_text(e);
    if (e.type == EventType::Command && e.duration) { std::snprintf(b, sizeof b, ", wait %d", e.duration); s += b; }
    return s;
}

}
