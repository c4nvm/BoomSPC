#include "wolfteam.hpp"

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

namespace wolfteam {
namespace {
const int W = 0x100;

// State: x[0] = next order entry (parse only), x[1] = last note, x[2] =
// ticks the last note keeps sounding past its event, x[4] = repeat slots
// in use, x[5..6] = slot words (order entry | counter << 16, counter FF =
// fresh), x[7] = parsing from RAM.

const CmdSpec kCmds[0x23] = {
    {2, "Rst", "Rest", FxClass::Time},                                   // 90
    {1, "Pat", "Pattern end", FxClass::Song},                            // 91
    {1, "Lp[", "Repeat start", FxClass::Song, 0, true},                  // 92
    {2, "Lp]", "Repeat n times (0 = forever)", FxClass::Song, 0, true},  // 93
    {3, "Bnd", "Pitch bend (wait, amount - 40)", FxClass::Pitch},        // 94
    {3, "Tmp", "Tempo (wait, tempo)", FxClass::Speed},                   // 95
    {2, "Ins", "Instrument", FxClass::Instrument},                       // 96
    {3, "Vol", "Volume (wait, volume)", FxClass::Volume},                // 97
    {3, "Pan", "Pan (wait, pan)", FxClass::Panning},                     // 98
    {3, "Prm", "Param 1A (wait, value)", FxClass::Misc},                 // 99
    {3, "Nop", "(no effect)", FxClass::Misc},                            // 9A
    {2, "Flg", "Flag 01 on/off", FxClass::Misc},                         // 9B
    {4, "Env", "Envelope (a, b, c)", FxClass::Sys2},                     // 9C
    {1, "???", "(invalid)", FxClass::Misc},                              // 9D
    {1, "???", "(invalid)", FxClass::Misc},                              // 9E
    {1, "???", "(invalid)", FxClass::Misc},                              // 9F
    {1, "???", "(invalid)", FxClass::Misc},                              // A0
    {1, "???", "(invalid)", FxClass::Misc},                              // A1
    {2, "Trn", "Transpose (t - 40)", FxClass::Pitch},                    // A2
    {2, "Flg", "Flag 02 on/off", FxClass::Misc},                         // A3
    {1, "???", "(invalid)", FxClass::Misc},                              // A4
    {1, "???", "(invalid)", FxClass::Misc},                              // A5
    {1, "???", "(invalid)", FxClass::Misc},                              // A6
    {1, "???", "(invalid)", FxClass::Misc},                              // A7
    {1, "???", "(invalid)", FxClass::Misc},                              // A8
    {1, "???", "(invalid)", FxClass::Misc},                              // A9
    {3, "Ech", "Echo (feedback, volume)", FxClass::Sys1},                // AA
    {1, "???", "(invalid)", FxClass::Misc},                              // AB
    {1, "???", "(invalid)", FxClass::Misc},                              // AC
    {2, "Nse", "Noise / echo bits", FxClass::Sys1},                      // AD
    {2, "Flg", "Flag 04 on/off", FxClass::Misc},                         // AE
    {3, "ADS", "ADSR (a, b)", FxClass::Sys2},                            // AF
    {2, "EVl", "Echo volume", FxClass::Sys1},                            // B0
    {1, "???", "(invalid)", FxClass::Misc},                              // B1
    {2, "Hum", "Humanize on/off", FxClass::Misc},                        // B2
};
const CmdSpec kPatEnd = {1, "Pat", "Pattern end", FxClass::Song};
const CmdSpec kNop = {1, "Nop", "(no effect)", FxClass::Misc};

inline bool is_op(const Event& e, uint8_t op) { return e.type == EventType::Command && e.b[0] == op; }
}

Layout detect_layout(const uint8_t* ram) {
    Layout L;
    const int loader[] = {0x68, 0xFF, 0xF0, 0x06, 0x60, 0x84, W, 0x60, 0x88, W, 0x8D, 0x04, 0xD7, 0x86};
    int p = find_pattern(ram, 0x200, 0x3000, loader, 14);
    if (p < 0) return Layout{};
    L.page_zp = ram[p + 6];
    L.page_add = ram[p + 9];
    const int tick[] = {0x8F, W, 0x86, 0x8F, W, 0x87, 0xCD, 0x00, 0x8D, 0x00, 0xF7, 0x86, 0x10};
    int t = find_pattern(ram, 0x200, 0x3000, tick, 13);
    if (t < 0) return Layout{};
    L.tracks = uint16_t(ram[t + 1] | (ram[t + 4] << 8));
    const int save[] = {0x8F, W, 0xE2, 0x8F, W, 0xE3, 0x8D, 0x0A, 0xF7, 0x86, 0x68, 0xFF};
    if (int s = find_pattern(ram, 0x200, 0x3000, save, 12); s >= 0) L.saves = uint16_t(ram[s + 1] | (ram[s + 4] << 8));
    return L;
}

std::unique_ptr<seq::Driver> detect(const uint8_t* ram) {
    Layout L = detect_layout(ram);
    if (!L.valid()) return nullptr;
    return std::make_unique<WolfteamDriver>(L);
}

const CmdSpec& WolfteamDriver::spec(uint8_t op) const {
    if (op == 0xFD) return kPatEnd;
    if (op < 0x90 || op > 0xB2) return kNop;
    return kCmds[op - 0x90];
}

uint16_t WolfteamDriver::order_start(const uint8_t* ram, uint16_t header, int v) const {
    const int e = header + 0x23 + track_of(v) * 3;
    if (!(ram[e] & 0x80)) return 0;
    return uint16_t((header + rd16(ram, e + 1)) & 0xFFFF);
}

std::vector<uint16_t> WolfteamDriver::song_headers(const uint8_t* ram) const {
    ram_ = ram;
    const int h = ((ram[L.page_zp] + L.page_add) & 0xFF) << 8;
    if (h < 0x200) return {};
    int lo = h;
    while (lo > 0x200 && (ram[lo - 1] == 0 || ram[lo - 1] == 0xFF)) --lo;
    free_lo_ = uint16_t(lo);
    int n = 0;
    for (int t = 0; t < 14 && n < 8; ++t) {
        const int e = h + 0x23 + t * 3;
        if (!(ram[e] & 0x80) || rd16(ram, (h + rd16(ram, e + 1)) & 0xFFFF) == 0xFFFF) continue;
        col_[n++] = int8_t(t);
    }
    for (int v = n; v < 8; ++v) col_[v] = int8_t(v < 14 ? v : 0);
    if (!n) return {};
    for (int v = 0; v < 8; ++v) if (track_start(ram, uint16_t(h), v)) return {uint16_t(h)};
    return {};
}

uint16_t WolfteamDriver::track_start(const uint8_t* ram, uint16_t header, int v) const {
    const uint16_t o = order_start(ram, header, v);
    if (!o) return 0;
    const int w = rd16(ram, o);
    if (w == 0xFFFF) return 0;
    const int a = (header + w) & 0xFFFF;
    return a >= 0x200 ? uint16_t(a) : 0;
}

State WolfteamDriver::initial_state(const uint8_t* ram, uint16_t header, int voice) const {
    ram_ = ram;
    header_ = header;
    State s;
    s.x[7] = 1;
    s.x[1] = -1;
    s.x[0] = (order_start(ram, header, voice) + 2) & 0xFFFF;
    return s;
}

State WolfteamDriver::edit_state(const std::vector<Event>& ev) const {
    (void)ev;
    State s;
    s.x[1] = -1;
    return s;
}

void WolfteamDriver::decode(const uint8_t* p, int pc, State& s, Event& e, Flow& f) const {
    (void)pc;
    const uint8_t b = p[0];
    e.b[0] = b;
    e.size = 1;
    e.pitch = -1;
    e.type = EventType::Command;
    if (b < 0x80) {
        e.size = 4;
        for (int i = 1; i < 4; ++i) e.b[i] = p[i];
        const int dur = p[1], gate = p[2] + 1;
        e.duration = dur;
        e.pitch = b + 12;
        e.type = s.x[2] > 0 && s.x[1] == b ? EventType::Tie : EventType::Note;
        s.x[1] = b;
        s.x[2] = gate > dur ? gate - dur : 0;
        return;
    }
    const CmdSpec& sp = spec(b);
    e.size = sp.size;
    for (int i = 1; i < e.size; ++i) e.b[i] = p[i];
    switch (b) {
        case 0x90: e.type = EventType::Rest; e.duration = p[1]; s.x[2] = std::max(0, s.x[2] - e.duration); return;
        case 0x94: case 0x95: case 0x97: case 0x98: case 0x99: e.duration = p[1]; return;
        case 0x91: case 0xFD: {
            int target = 0, entry = 0;
            if (s.x[7] && ram_) {
                entry = s.x[0] & 0xFFFF;
                const int w = rd16(ram_, entry);
                if (w != 0xFFFF) { target = (header_ + w) & 0xFFFF; s.x[0] = (entry + 2) & 0xFFFF; }
            } else { target = p[1] | (p[2] << 8); entry = p[3] | (p[4] << 8); }
            e.b[1] = uint8_t(target & 0xFF); e.b[2] = uint8_t(target >> 8);
            e.b[3] = uint8_t(entry & 0xFF); e.b[4] = uint8_t(entry >> 8);
            if (!target) { e.type = EventType::End; f.kind = Flow::End; return; }
            f.kind = Flow::Jump; f.target = target; f.count = 1;
            return;
        }
        case 0x92:
            e.b[1] = 0xFF;
            if (s.x[4] < 2) {
                const int k = s.x[4]++;
                s.x[5 + k] = (s.x[0] & 0xFFFF) | (0xFF << 16);
                e.b[1] = uint8_t(k);
                f.kind = Flow::RepStart; f.slot = k; f.count = 0;
            }
            return;
        case 0x93: {
            const int n = p[1];
            e.b[2] = 0;
            if (s.x[4] == 0) return;
            const int k = s.x[4] - 1;
            int& w = s.x[5 + k];
            int cnt = (w >> 16) & 0xFF;
            if (cnt == 0xFF) { cnt = (n - 1) & 0xFF; if (cnt == 0) { --s.x[4]; return; } }
            else if (--cnt == 0) { --s.x[4]; return; }
            w = (w & 0xFFFF) | (cnt << 16);
            s.x[0] = w & 0xFFFF;
            e.b[2] = 1;
            f.kind = Flow::RepEnd; f.slot = k; f.count = n;
            return;
        }
        default: return;
    }
}

// Patterns another track also plays are shared bytes.
void WolfteamDriver::prune_idle_voices(const uint8_t* ram, seq::Pattern& p) const {
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

std::string WolfteamDriver::song_label(const uint8_t* ram, uint16_t header, const seq::Pattern& p, int index) const {
    (void)ram; (void)index;
    bool loops = false;
    for (int v = 0; v < 8; ++v) if (p.tracks[v].loops) loops = true;
    char b[64];
    std::snprintf(b, sizeof b, "song @%04X (%d ticks%s)", header, p.length_ticks, loops ? ", loops" : "");
    return b;
}

bool WolfteamDriver::tempo_writes(const uint8_t* ram, double tps, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    (void)ram;
    if (tps <= 0) return false;
    out.push_back({0xFA, uint8_t(std::clamp(int(8000.0 / tps + 0.5), 1, 255))});
    return true;
}

void WolfteamDriver::live_state_writes(const uint8_t* ram, const seq::Position& pos, int voice, uint16_t ptr, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    auto map = [&](uint16_t a) {
        int m = remap.find(a);
        if (m < 0) for (const auto& p : reloc_) if (p.first == a) { m = p.second; break; }
        return m;
    };
    auto rewritten = [&](uint16_t a) { for (const Piece& p : pieces_) if (a >= p.lo && a <= p.hi) return true; return false; };
    auto word = [&](uint16_t at, uint16_t val) { out.push_back({at, uint8_t(val & 0xFF)}); out.push_back({uint16_t(at + 1), uint8_t(val >> 8)}); };
    const uint16_t live = live_ptr(ram, pos, voice);
    if (int m = map(live); m >= 0) ptr = uint16_t(m);
    else if (!rewritten(live)) ptr = live;
    const int t = track_of(voice);
    word(uint16_t(L.tracks + 32 * t + 3), ptr);
    if (!L.saves) return;
    for (int k = 0; k < 2; ++k) {   // open repeats point at their 92
        const uint16_t at = uint16_t(L.saves + 4 * t + 2 * k);
        if (ram[(L.tracks + 32 * t + 10 + k) & 0xFFFF] == 0xFF) continue;
        if (int m = map(uint16_t(rd16(ram, at))); m >= 0) word(at, uint16_t(m));
    }
}

void WolfteamDriver::track_pointer_writes(const seq::Song& song, int pattern_idx, int voice, uint16_t dest, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    (void)pattern_idx;
    if (!ram_) return;
    for (const Piece& p : pieces_) {
        const uint16_t entry = p.entry ? p.entry : order_start(ram_, song.order_addr, voice);
        if (!entry) { out.clear(); return; }
        const int off = (int(dest) + p.offset - int(song.order_addr)) & 0xFFFF;
        out.push_back({entry, uint8_t(off & 0xFF)});
        out.push_back({uint16_t(entry + 1), uint8_t(off >> 8)});
    }
}

// The block holds every pattern visit with new events, each once: repeat
// passes the parse expanded are skipped.
std::vector<uint8_t> WolfteamDriver::serialize_relocated(const std::vector<Event>& ev, uint16_t dest, std::vector<int>* offsets) const {
    std::vector<int> off(ev.size(), -1);
    std::vector<uint8_t> out;
    reloc_.clear();
    pieces_.clear();
    std::vector<std::pair<int, int>> visits;
    for (size_t i = 0, first = 0; i < ev.size(); ++i)
        if (is_end(ev[i]) || ev[i].type == EventType::End || i + 1 == ev.size()) { visits.push_back({int(first), int(i)}); first = i + 1; }
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
            piece.entry = uint16_t(pe.b[3] | (pe.b[4] << 8));
            if (pe.b[1] | pe.b[2]) piece.lo = uint16_t(pe.b[1] | (pe.b[2] << 8));
        }
        std::vector<int> frames;
        bool skipping = false, ended = false;
        int skip_nest = 0;
        for (int i = v.first; i <= v.second && !ended; ++i) {
            const Event& e = ev[size_t(i)];
            if (skipping) {
                if (is_op(e, 0x93) && e.nest == skip_nest && e.b[2] == 0) { skipping = false; frames.pop_back(); }
                continue;
            }
            if (is_op(e, 0x92)) {
                if (e.b[1] != 0xFF) frames.push_back(e.nest + 1);
            } else if (is_op(e, 0x93) && e.b[2]) {
                if (!frames.empty() && e.nest == frames.back()) { skipping = true; skip_nest = e.nest; }
                else ended = true;   // jumps back to an earlier pattern: what follows replays other bytes
            } else if (is_op(e, 0x93) && !frames.empty() && e.nest == frames.back()) {
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
        if (ended) out.push_back(0xFD);
        pieces_.push_back(piece);
    }
    if (offsets) *offsets = off;
    return out;
}

// Unrolled events keep their address and are flagged in_call, which this
// format never sets.
bool WolfteamDriver::unroll_at(std::vector<Event>& ev, int tick) const {
    retime(ev);
    int i = -1;
    for (size_t k = 0; k < ev.size(); ++k) {
        const Event& e = ev[k];
        if (!e.in_sub) continue;
        if ((e.duration > 0 && e.tick <= tick && tick < e.tick + e.duration) || (e.duration == 0 && e.tick == tick)) { i = int(k); break; }
    }
    if (i < 0) return false;
    const int nest = ev[size_t(i)].nest;
    if (nest > 0) {   // a later pass of a repeat inside one pattern
        int fb = -1;
        bool replay = false;
        for (int j = i - 1; j >= 0; --j) {
            const Event& e = ev[size_t(j)];
            if (is_op(e, 0x93) && e.nest == nest) { if (e.b[2] == 0) break; replay = true; }
            if (is_end(e)) break;
            if (is_op(e, 0x92) && e.b[1] != 0xFF && e.nest == nest - 1) { fb = j; break; }
        }
        if (fb >= 0 && replay) {
            int last = i;
            for (size_t j = size_t(i); j < ev.size(); ++j) {
                if (is_end(ev[j])) return false;
                if (is_op(ev[j], 0x93) && ev[j].nest == nest) { last = int(j); if (ev[j].b[2] == 0) break; }
            }
            std::vector<Event> out;
            out.reserve(ev.size());
            for (size_t j = 0; j < ev.size(); ++j) {
                if (int(j) == fb || (int(j) > fb && int(j) <= last && is_op(ev[j], 0x93) && ev[j].nest == nest)) continue;
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
    int a = i, b = i;
    while (a > 0 && ev[size_t(a - 1)].in_sub && !is_end(ev[size_t(a - 1)])) --a;
    while (b + 1 < int(ev.size()) && ev[size_t(b + 1)].in_sub && !is_end(ev[size_t(b)]) && !(is_op(ev[size_t(b)], 0x93) && ev[size_t(b)].b[2])) ++b;
    for (int j = a; j <= b; ++j) { Event& c = ev[size_t(j)]; c.in_sub = false; c.in_call = true; c.sub_iter = 0; }
    retime(ev);
    return true;
}

bool WolfteamDriver::transpose_event(Event& e, int semis) const {
    if (e.type != EventType::Note || e.b[0] >= 0x80) return false;
    const int n = e.b[0] + semis;
    if (n < 0 || n > 0x7F) return false;
    e.b[0] = uint8_t(n);
    if (e.pitch >= 0) e.pitch += semis;
    return true;
}

void WolfteamDriver::apply_note_byte(Event& e, uint8_t byte) const {
    const int dur = std::clamp(e.duration, 0, 255);
    if (byte == kRest) {
        e.b[0] = 0x90; e.b[1] = uint8_t(dur); e.size = 2;
        e.type = EventType::Rest; e.pitch = -1; e.addr = 0;
        return;
    }
    const int key = byte & 0x7F;
    if (e.b[0] >= 0x80) { e.b[1] = uint8_t(dur); e.b[2] = uint8_t(std::max(dur - 1, 0)); e.b[3] = 0x7F; }   // a rest becomes a full-length note
    e.b[0] = uint8_t(key);
    e.size = 4;
    e.pitch = key + 12;
    e.type = EventType::Note;
    e.addr = 0;
}

bool WolfteamDriver::set_note_at(std::vector<Event>& ev, int tick, uint8_t byte, int pattern_len) const {
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
    apply_note_byte(ev[size_t(i)], byte);
    retime(ev);
    return true;
}

bool WolfteamDriver::set_duration(std::vector<Event>& ev, int i, int dur) const {
    if (i < 0 || i >= int(ev.size()) || dur < 1 || dur > 4096) return false;
    Event& e = ev[size_t(i)];
    std::vector<Event> tail;
    if (e.type == EventType::Rest) {
        int left = dur;
        auto chunk = [&](Event& r) { const int n = std::min(left, 255); r.b[0] = 0x90; r.b[1] = uint8_t(n); r.size = 2; r.duration = n; r.addr = 0; r.type = EventType::Rest; r.pitch = -1; left -= n; };
        chunk(e);
        while (left > 0) { Event r = e; chunk(r); tail.push_back(r); }
    } else if (e.type == EventType::Note || e.type == EventType::Tie) {
        const int old_dur = e.b[1], old_gate = e.b[2] + 1;
        const int first = std::min(dur, 255);
        int gate = old_gate > old_dur ? first + 1 : old_gate == old_dur ? first : std::min(old_gate, first);   // held notes stay held
        if (dur > 255) gate = first + 1;
        e.b[1] = uint8_t(first); e.b[2] = uint8_t(std::clamp(gate - 1, 0, 255)); e.duration = first; e.addr = 0;
        for (int left = dur - first; left > 0; left -= std::min(left, 255)) {   // the same note again, still sounding: a tie
            Event t = e;
            const int n = std::min(left, 255);
            t.b[1] = uint8_t(n); t.b[2] = uint8_t(std::min(n, 254)); t.duration = n; t.type = EventType::Tie;
            tail.push_back(t);
        }
    } else return false;
    ev.insert(ev.begin() + i + 1, tail.begin(), tail.end());
    return true;
}

std::string WolfteamDriver::event_text(const Event& e) const {
    char b[96];
    if (e.type == EventType::Note || e.type == EventType::Tie) {
        std::snprintf(b, sizeof b, "%s%s  %d ticks, sounds %d, vel %d", e.type == EventType::Tie ? "tie " : "", note_name(e).c_str(), e.duration, e.b[2] + 1, e.b[3]);
        return b;
    }
    if (e.type == EventType::Rest) { std::snprintf(b, sizeof b, "rest  %d ticks", e.duration); return b; }
    if (is_end(e)) {
        if (e.type == EventType::End) return "Pattern end (last)";
        std::snprintf(b, sizeof b, "Pattern end, next at $%02X%02X", e.b[2], e.b[1]);
        return b;
    }
    switch (e.b[0]) {
        case 0x92: return e.b[1] == 0xFF ? "Repeat start (no slot free)" : "Repeat start";
        case 0x93: if (e.b[1] == 0) return "Repeat forever"; std::snprintf(b, sizeof b, "Repeat x%d", e.b[1]); return b;
        case 0x94: std::snprintf(b, sizeof b, "Pitch bend %+d over %d ticks", int(e.b[2]) - 0x40, e.b[1]); return b;
        case 0x95: std::snprintf(b, sizeof b, "Tempo %d, wait %d", e.b[2], e.b[1]); return b;
        case 0x97: std::snprintf(b, sizeof b, "Volume %d, wait %d", e.b[2], e.b[1]); return b;
        case 0x98: std::snprintf(b, sizeof b, "Pan %d, wait %d", e.b[2], e.b[1]); return b;
        case 0xA2: std::snprintf(b, sizeof b, "Transpose %+d", int(e.b[1]) - 0x40); return b;
        default: break;
    }
    return seq::Driver::event_text(e);
}

}
