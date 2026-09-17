#include "berlioz.hpp"

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

namespace berlioz {
namespace {
const int W = 0x100;

// State: x[0] = last note, x[1] = inside an EB/EC bend list, flags bit 0 =
// F4 seen (the next note is a tie), trans = pitch offset in 1/20 semitones,
// oct = F6 depth; frame k: x[2+3k] = body | end << 16, x[3+3k] = exit |
// count << 16 | passes << 24, x[4+3k] = its pitch offset.

const CmdSpec kCmds[0x20] = {
    {3, "Env", "Voice envelope (a, b)", FxClass::Sys2},                 // E0
    {2, "Vp1", "Voice param 1", FxClass::Sys2},                         // E1
    {2, "Vp2", "Voice param 2", FxClass::Sys2},                         // E2
    {2, "Vp3", "Voice param 3", FxClass::Sys2},                         // E3
    {2, "Vp4", "Voice param 4", FxClass::Sys2},                         // E4
    {4, "Alc", "DSP voice choice (flags, voice, mask)", FxClass::Sys1}, // E5
    {1, "Alc", "Default DSP voice", FxClass::Sys1},                     // E6
    {1, "Nop", "(no effect)", FxClass::Misc},                           // E7
    {1, "???", "(invalid)", FxClass::Misc},                             // E8
    {2, "Glb", "Global param", FxClass::Misc},                          // E9
    {1, "Off", "Key off", FxClass::Time},                               // EA
    {1, "Bnd", "Pitch bend list", FxClass::Pitch},                      // EB
    {1, "Bnd", "Key off, pitch bend list", FxClass::Pitch},             // EC
    {2, "Vel", "Velocity scale", FxClass::Volume},                      // ED
    {2, "Ext", "Extended", FxClass::Misc},                              // EE
    {3, "Nte", "Note (note, ticks)", FxClass::Misc},                    // EF
    {1, "End", "End of track", FxClass::Song},                          // F0
    {3, "Vol", "Volume (a * b / 256)", FxClass::Volume},                // F1
    {2, "Vol", "Volume", FxClass::Volume},                              // F2
    {2, "Pri", "Priority", FxClass::Sys1},                              // F3
    {1, "Tie", "The next note does not retrigger", FxClass::Time},      // F4
    {2, "Ins", "Instrument", FxClass::Instrument},                      // F5
    {11, "Blk", "Play a block (body, end, times, offset, table)", FxClass::Song, 0, true},   // F6
    {3, "Det", "Detune (1/20 semitones)", FxClass::Pitch},              // F7
    {1, "Rst", "Rest 32", FxClass::Time},                               // F8
    {1, "Lop", "Restart the track", FxClass::Song},                     // F9
    {2, "Tmp", "Tempo (ticks per 20 ms, /256)", FxClass::Speed},        // FA
    {2, "Prm", "Track param", FxClass::Misc},                           // FB
    {1, "Flg", "Flag", FxClass::Misc},                                  // FC
    {1, "Rst", "Rest 64", FxClass::Time},                               // FD
    {1, "Rst", "Rest 128", FxClass::Time},                              // FE
    {1, "Rst", "Rest 256", FxClass::Time},                              // FF
};
const CmdSpec kBend = {1, "Bnd", "Bend item", FxClass::Pitch};

inline bool is_op(const Event& e, uint8_t op) { return e.type == EventType::Command && e.b[0] == op; }
inline int le16(const uint8_t* p) { return p[0] | (p[1] << 8); }
}

Layout detect_layout(const uint8_t* ram) {
    Layout L;
    const int fetch[] = {0xF4, W, 0xC5, W, W, 0xF4, W, 0xC5, W, W, 0xE5, W, W, 0xBB, W, 0xD0, 0x02, 0xBB, W, 0x6F};
    int p = find_pattern(ram, 0x200, 0x4000, fetch, 20);
    if (p < 0 || ram[p + 14] != ram[p + 1] || ram[p + 18] != ram[p + 6]) return Layout{};
    L.ptr_lo = ram[p + 1];
    L.ptr_hi = ram[p + 6];
    const int song[] = {0xDA, W, 0x8D, 0x00, 0xF7, W, 0xC4, W, 0xC4, W, 0xF0};
    int q = find_pattern(ram, 0x200, 0x4000, song, 11);
    if (q < 0 || ram[q + 5] != ram[q + 1]) return Layout{};
    L.header_zp = ram[q + 1];
    L.ntracks_zp = ram[q + 7];
    const int tempo[] = {0xE4, W, 0xF0, 0x07, 0x60, 0x84, W, 0xC4, W, 0x90, 0x03, 0x3F};
    if (int t = find_pattern(ram, 0x200, 0x4000, tempo, 12); t >= 0) L.tempo_zp = ram[t + 1];
    const int call[] = {0x8D, 0x1A, 0x3F, W, W};
    if (int c = find_pattern(ram, 0x200, 0x4000, call, 5); c >= 0) {
        const int sub = rd16(ram, c + 3);
        const int look[] = {0x60, 0x96, W, W, 0xC4};
        if (int l = find_pattern(ram, sub, sub + 24, look, 5); l >= 0) L.song_table = uint16_t(rd16(ram, rd16(ram, l + 2) + 0x1A));   // entry 1A of the pointer table
    }
    const int depth[] = {0xF5, W, W, 0x68, 0x05, 0xF0, W, 0x3F, W, W, 0xF6, W, W, 0xC4, 0x00, 0xF6, W, W, 0xC4, 0x01};
    if (int d = find_pattern(ram, 0x200, 0x4000, depth, 20); d >= 0) {
        L.depth = uint16_t(rd16(ram, d + 1));
        L.frame_lo = uint16_t(rd16(ram, d + 11));
        L.frame_hi = uint16_t(rd16(ram, d + 16));
    }
    const int esa[] = {0xE5, W, W, 0x8F, 0x6D, 0xF2, 0xC4, 0xF3};
    if (int e = find_pattern(ram, 0x200, 0x4000, esa, 8); e >= 0) L.esa_addr = uint16_t(rd16(ram, e + 1));
    const int count[] = {0xF6, W, W, 0xBC, 0xD6, W, W, 0x8D, 0x04};
    if (int c = find_pattern(ram, 0x200, 0x4000, count, 9); c >= 0) L.frame_count = uint16_t(rd16(ram, c + 1));
    return L;
}

std::unique_ptr<seq::Driver> detect(const uint8_t* ram) {
    Layout L = detect_layout(ram);
    if (!L.valid()) return nullptr;
    return std::make_unique<BerliozDriver>(L);
}

const CmdSpec& BerliozDriver::spec(uint8_t op) const {
    if (op < 0xE0) return kBend;
    return kCmds[op - 0xE0];
}

std::vector<uint16_t> BerliozDriver::song_headers(const uint8_t* ram) const {
    std::vector<uint16_t> out;
    auto valid = [&](int h) {
        if (h < 0x200 || h >= 0xFFC0) return false;
        const int n = ram[h];
        if (n < 1 || n > 20) return false;
        for (int t = 0; t < n; ++t) { const int a = rd16(ram, h + 1 + t * 2); if (a < 0x200 || a >= 0xFFF0) return false; }
        return true;
    };
    auto add = [&](int h) { if (valid(h) && std::find(out.begin(), out.end(), uint16_t(h)) == out.end()) out.push_back(uint16_t(h)); };
    add(rd16(ram, L.header_zp));
    for (int i = 0; i < 64; ++i) {
        const int h = rd16(ram, L.song_table + i * 2);
        if (h == 0xFFFF || h == 0) continue;
        add(h);
    }
    return out;
}

// The driver names the song it plays.
int BerliozDriver::pick_current_song(const uint8_t* ram, const std::vector<seq::Song>& songs) const {
    const uint16_t h = uint16_t(rd16(ram, L.header_zp));
    for (size_t i = 0; i < songs.size(); ++i) if (songs[i].order_addr == h) return int(i);
    return stream::Driver::pick_current_song(ram, songs);
}

uint16_t BerliozDriver::track_start(const uint8_t* ram, uint16_t header, int v) const {
    if (v >= ram[header]) return 0;
    const int a = rd16(ram, header + 1 + v * 2);
    return a >= 0x200 ? uint16_t(a) : 0;
}

State BerliozDriver::initial_state(const uint8_t* ram, uint16_t header, int voice) const {
    (void)ram; (void)header; (void)voice;
    return State{};
}

std::string BerliozDriver::song_label(const uint8_t* ram, uint16_t header, const seq::Pattern& p, int index) const {
    bool loops = false;
    for (int v = 0; v < 8; ++v) if (p.tracks[v].loops) loops = true;
    char b[80];
    std::snprintf(b, sizeof b, "song %d @%04X, %d tracks (%d ticks%s)", index, header, ram[header], p.length_ticks, loops ? ", loops" : "");
    return b;
}

// Tracks that play another track's bytes through F6 share them.
void BerliozDriver::prune_idle_voices(const uint8_t* ram, seq::Pattern& p) const {
    (void)ram;
    std::vector<std::vector<bool>> used(8, std::vector<bool>(0x10000, false));
    for (int v = 0; v < 8; ++v)
        for (const Event& e : p.tracks[v].events)
            if (e.addr && e.size) for (int k = 0; k < e.size; ++k) used[size_t(v)][size_t((e.addr + k) & 0xFFFF)] = true;
    for (int v = 0; v < 8; ++v)
        for (Event& e : p.tracks[v].events) {
            if (!e.addr || e.in_sub || !e.size) continue;
            for (int w = 0; w < 8 && !e.in_sub; ++w) if (w != v && used[size_t(w)][e.addr]) e.in_sub = true;
        }
}

double BerliozDriver::ticks_per_second(const uint8_t* ram) const {
    const double rate = 8000.0 / (ram[0xFB] ? ram[0xFB] : 256);
    const int tempo = L.tempo_zp ? ram[L.tempo_zp] : 0;
    return tempo ? rate * tempo / 256.0 : rate;
}

bool BerliozDriver::tempo_writes(const uint8_t* ram, double tps, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    if (tps <= 0 || !L.tempo_zp) return false;
    const double rate = 8000.0 / (ram[0xFB] ? ram[0xFB] : 256);
    const int t = int(tps / rate * 256.0 + 0.5);
    out.push_back({L.tempo_zp, uint8_t(t >= 256 ? 0 : std::max(t, 1))});
    return true;
}

void BerliozDriver::decode(const uint8_t* p, int pc, State& s, Event& e, Flow& f) const {
    const uint8_t b = p[0];
    e.b[0] = b;
    e.size = 1;
    e.pitch = -1;
    e.type = EventType::Command;
    auto note_event = [&](int note, int dur) {
        s.x[0] = note & 0xFF;
        e.duration = dur;
        e.pitch = note + 15 + (s.trans + (s.trans >= 0 ? 10 : -10)) / 20;
        e.type = (s.flags & 1) ? EventType::Tie : EventType::Note;
        s.flags &= uint8_t(~1);
    };
    // The end of the innermost F6 block: back to its body or out.
    if (s.oct > 0 && pc) {
        const int k = s.oct - 1;
        const int body = s.x[2 + 3 * k] & 0xFFFF, end = (s.x[2 + 3 * k] >> 16) & 0xFFFF;
        if (pc == end) {
            const int exit = s.x[3 + 3 * k] & 0xFFFF, count = (s.x[3 + 3 * k] >> 16) & 0xFF;
            int passes = ((s.x[3 + 3 * k] >> 24) & 0xFF) + 1;
            e.size = 0;
            e.b[0] = 0xF6;
            e.b[1] = uint8_t(body & 0xFF); e.b[2] = uint8_t(body >> 8);
            e.b[3] = uint8_t(exit & 0xFF); e.b[4] = uint8_t(exit >> 8);
            if (count == 0) { f.kind = Flow::Jump; f.target = body; f.count = 0; return; }   // forever: the track's loop
            f.kind = Flow::Return;   // the parser plays the body again or goes on after the F6
            if (passes < count) s.x[3 + 3 * k] = (s.x[3 + 3 * k] & 0xFFFFFF) | ((passes & 0xFF) << 24);
            else { s.trans -= int16_t(s.x[4 + 3 * k]); --s.oct; }
            return;
        }
    }
    if (s.x[1]) {   // bend items
        e.b[15] = 0xBE;
        const int hi = b >> 4, lo = b & 15;
        if (hi) { e.duration = lo; return; }
        if (lo == 0) { s.x[1] = 0; return; }
        if (lo == 1) { e.size = 2; e.b[1] = p[1]; e.duration = p[1]; return; }
        if (lo == 2) return;
        e.size = 2; e.b[1] = p[1];
        return;
    }
    if (b < 0x80) {
        const int note = (s.x[0] + (b >> 3) - 8) & 0xFF;
        e.b[1] = uint8_t(note);
        note_event(note, (b & 7) + 1);
        return;
    }
    if (b < 0xC0) { e.size = 2; e.b[1] = p[1]; note_event(b - 0x60, p[1]); return; }
    if (b < 0xE0) { e.type = EventType::Rest; e.duration = b - 0xBF; return; }
    const CmdSpec& sp = spec(b);
    e.size = sp.size;
    for (int i = 1; i < e.size; ++i) e.b[i] = p[i];
    switch (b) {
        case 0xEF: note_event(p[1], p[2]); return;
        case 0xF8: e.type = EventType::Rest; e.duration = 32; return;
        case 0xFD: e.type = EventType::Rest; e.duration = 64; return;
        case 0xFE: e.type = EventType::Rest; e.duration = 128; return;
        case 0xFF: e.type = EventType::Rest; e.duration = 256; return;
        case 0xF0: e.type = EventType::End; f.kind = Flow::End; return;
        case 0xF9:
            if (s.start) { e.b[1] = uint8_t(s.start & 0xFF); e.b[2] = uint8_t(s.start >> 8); }
            f.kind = Flow::Jump; f.target = s.start ? s.start : (p[1] | (p[2] << 8)); f.count = 0;
            return;
        case 0xF4:   // a tie is the F4 and the note after it, one event
            if (p[1] < 0xC0 || p[1] == 0xEF) {
                s.flags |= 1;
                Event n{};
                Flow g{};
                decode(p + 1, pc ? pc + 1 : 0, s, n, g);
                e.size = uint8_t(1 + n.size);
                for (int i = 1; i < e.size; ++i) e.b[i] = p[i];
                e.duration = n.duration; e.pitch = n.pitch; e.type = n.type;
                return;
            }
            s.flags |= 1;
            return;
        case 0xEB: case 0xEC: s.x[1] = 1; return;
        case 0xF6: {
            const int body = le16(p + 1), end = le16(p + 3), count = p[5], offset = int16_t(le16(p + 6));
            const int skip = p[10];
            e.size = uint8_t(std::min(11 + skip, 16));
            for (int i = 11; i < e.size; ++i) e.b[i] = p[i];
            if (e.size < 16) e.b[15] = body < pc && pc ? 1 : 0;   // a block of this track's own earlier bytes
            if (count == 0 && end == pc && pc) { f.kind = Flow::Jump; f.target = body; f.count = 0; return; }   // the track's loop
            if (s.oct < 2) {
                const int k = s.oct++;
                s.x[2 + 3 * k] = body | (end << 16);
                s.x[3 + 3 * k] = ((pc + 11 + skip) & 0xFFFF) | (count << 16);
                s.x[4 + 3 * k] = offset;
                s.trans += offset;
            }
            if (count == 0) { f.kind = Flow::Jump; f.target = body; f.count = 0; }
            else { f.kind = Flow::Call; f.target = body; f.count = count; }
            return;
        }
        default: return;
    }
}

int BerliozDriver::jump_target(const Event& e) const {
    if (e.type != EventType::Command) return -1;
    if (!e.size || e.b[0] == 0xF9) return (e.b[1] | (e.b[2] << 8)) ? e.b[1] | (e.b[2] << 8) : -1;   // a block end or restart
    if (e.b[0] == 0xF6 && e.size >= 11) return le16(e.b + 1);   // the body
    return -1;
}

uint16_t BerliozDriver::live_ptr(const uint8_t* ram, const seq::Position& pos, int v) const {
    (void)pos;
    return uint16_t(ram[(L.ptr_lo + v) & 0xFF] | (ram[(L.ptr_hi + v) & 0xFF] << 8));
}

void BerliozDriver::live_state_writes(const uint8_t* ram, const seq::Position& pos, int voice, uint16_t ptr, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    auto map = [&](uint16_t a) {
        int m = remap.find(a);
        if (m < 0) for (const auto& p : reloc_) if (p.first == a) { m = p.second; break; }
        return m;
    };
    if (int m = map(live_ptr(ram, pos, voice)); m >= 0) ptr = uint16_t(m);
    out.push_back({uint16_t(L.ptr_lo + voice), uint8_t(ptr & 0xFF)});
    out.push_back({uint16_t(L.ptr_hi + voice), uint8_t(ptr >> 8)});
    if (!L.depth) return;
    for (int k = ram[(L.depth + voice) & 0xFFFF]; k < 5; ++k) {   // open blocks: the saved parameter pointer is the F6 + 1
        const int y = voice * 5 + k;
        const uint16_t blk = uint16_t(ram[(L.frame_lo + y) & 0xFFFF] | (ram[(L.frame_hi + y) & 0xFFFF] << 8));
        if (int m = map(uint16_t(blk - 1)); m >= 0) {
            out.push_back({uint16_t(L.frame_lo + y), uint8_t((m + 1) & 0xFF)});
            out.push_back({uint16_t(L.frame_hi + y), uint8_t((m + 1) >> 8)});
        }
    }
}

void BerliozDriver::track_pointer_writes(const seq::Song& song, int pattern_idx, int voice, uint16_t dest, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    (void)pattern_idx;
    out.push_back({uint16_t(song.order_addr + 1 + voice * 2), uint8_t(dest & 0xFF)});
    out.push_back({uint16_t(song.order_addr + 2 + voice * 2), uint8_t(dest >> 8)});
}

// F6 addresses follow their bytes: the body to the event that starts it,
// the end to the block-end event (or the F6 itself when it loops back).
// Bytes another track also plays (shared, but this track's own first pass
// of them) are written out too; only replays of F6 passes are left out.
std::vector<uint8_t> BerliozDriver::serialize_relocated(const std::vector<Event>& ev, uint16_t dest, std::vector<int>* offsets) const {
    std::vector<int> off(ev.size(), -1);
    std::vector<uint8_t> out;
    reloc_.clear();
    auto own = [&](const Event& e) { return !e.in_sub || (e.sub_iter == 0 && !e.in_call); };
    for (size_t i = 0; i < ev.size(); ++i) {
        if (!own(ev[i])) continue;
        off[i] = int(out.size());
        if (ev[i].addr) for (int k = 0; k <= ev[i].size; ++k) reloc_.push_back({uint16_t(ev[i].addr + k), uint16_t(dest + out.size() + k)});
        for (int k = 0; k < ev[i].size; ++k) out.push_back(ev[i].b[k]);
    }
    auto at = [&](int addr) {   // where the byte at `addr` went: an event's start, else the end of the one before it; -1 for bytes that stay
        for (size_t j = 0; j < ev.size(); ++j) if (off[j] >= 0 && ev[j].addr == addr && ev[j].addr) return int(dest + off[j]);
        for (size_t j = 0; j < ev.size(); ++j) if (off[j] >= 0 && ev[j].addr && ev[j].size && ev[j].addr + ev[j].size == addr) return int(dest + off[j] + ev[j].size);
        return -1;
    };
    for (size_t i = 0; i < ev.size(); ++i) {
        const Event& e = ev[i];
        if (off[i] < 0 || !is_op(e, 0xF6) || e.size < 11) continue;
        const int end = le16(e.b + 3);
        int j = -1;
        if (e.size < 16 && e.b[15] == 1) {   // own bytes: by address, else by the tick the body starts at
            for (size_t k = 0; k < ev.size() && j < 0; ++k) if (off[k] >= 0 && ev[k].addr && ev[k].addr == le16(e.b + 1)) j = int(k);
            for (size_t k = 0; k < ev.size() && j < 0 && e.target_tick >= 0; ++k) if (off[k] >= 0 && k != i && ev[k].tick == e.target_tick && (e.target_timed ? ev[k].duration > 0 : ev[k].type == EventType::Command)) j = int(k);
        }
        int nb = j >= 0 ? int(dest + off[size_t(j)]) : -1, ne = end == e.addr && e.addr ? int(dest + off[i]) : at(end);
        if (nb >= 0) { out[size_t(off[i] + 1)] = uint8_t(nb & 0xFF); out[size_t(off[i] + 2)] = uint8_t(nb >> 8); }
        if (ne >= 0) { out[size_t(off[i] + 3)] = uint8_t(ne & 0xFF); out[size_t(off[i] + 4)] = uint8_t(ne >> 8); }
    }
    if (offsets) *offsets = off;
    return out;
}

void BerliozDriver::absolutize(Event& e) {
    int note, dur, n;
    if (e.b[0] == 0xF4 && e.size == 2 && e.b[1] < 0x80) { note = std::clamp(e.pitch - 15, 0, 0x7F); dur = (e.b[1] & 7) + 1; n = 1; }   // a tie to a relative note: the pitch says which
    else if (is_rel(e)) { note = e.b[1]; dur = (e.b[0] & 7) + 1; n = 0; }
    else return;
    if (note >= 0x20 && note <= 0x5F) e.b[n] = uint8_t(note + 0x60);
    else { e.b[n] = 0xEF; e.b[n + 1] = uint8_t(note); ++n; }
    e.b[n + 1] = uint8_t(dur);
    e.size = uint8_t(n + 2);
    e.addr = 0;
}

void BerliozDriver::fix_next(std::vector<Event>& ev, int i) const {
    for (size_t k = size_t(i) + 1; k < ev.size(); ++k) {
        const Event& e = ev[k];
        if (e.type != EventType::Note && e.type != EventType::Tie) continue;
        if (is_rel(e) && !e.in_sub) absolutize(ev[k]);
        return;
    }
}

bool BerliozDriver::transpose_event(Event& e, int semis) const {
    if (e.type != EventType::Note || is_rel(e) || e.b[0] == 0xF4) return false;
    const int idx = e.b[0] == 0xEF ? 1 : 0;
    const int note = (idx ? e.b[1] : e.b[0] - 0x60) + semis;
    if (note < 0 || note > 0x7F || (!idx && (note < 0x20 || note > 0x5F))) return false;
    if (idx) e.b[1] = uint8_t(note); else e.b[0] = uint8_t(note + 0x60);
    if (e.pitch >= 0) e.pitch += semis;
    return true;
}

void BerliozDriver::apply_note_byte(Event& e, uint8_t byte) const {
    const bool note_like = e.type == EventType::Note || e.type == EventType::Tie;
    const int dur = std::max(e.duration, 1);
    if (byte == kRest) {
        e.type = EventType::Rest; e.pitch = -1; e.size = 1; e.addr = 0;
        e.b[0] = dur == 256 ? 0xFF : dur == 128 ? 0xFE : dur == 64 ? 0xFD : uint8_t(0xC0 + std::min(dur, 32) - 1);
        e.duration = dur == 256 || dur == 128 || dur == 64 ? dur : std::min(dur, 32);
        return;
    }
    int note = note_like ? (e.b[0] == 0xF4 ? (e.b[1] == 0xEF ? e.b[2] : e.b[1] < 0x80 ? -1 : e.b[1] - 0x60) : e.b[0] == 0xEF ? e.b[1] : is_rel(e) ? e.b[1] : e.b[0] - 0x60) : -1;
    if (byte == kTie) {
        if (note < 0) note = e.pitch >= 15 ? e.pitch - 15 : 45;
        e.b[0] = 0xF4; e.size = 3;
        if (note >= 0x20 && note <= 0x5F) e.b[1] = uint8_t(note + 0x60);
        else { e.b[1] = 0xEF; e.b[2] = uint8_t(note & 0x7F); e.size = 4; }
        e.b[e.size - 1] = uint8_t(std::min(dur, 255)); e.addr = 0;
        e.type = EventType::Tie; e.duration = e.b[e.size - 1]; e.pitch = note + 15;
        return;
    }
    const int key = std::clamp(int(byte) - 0x60, 0, 0x7F);
    const bool tie = e.b[0] == 0xF4 && note_like;
    int n = tie ? 1 : 0;
    e.b[0] = 0xF4;
    if (key >= 0x20 && key <= 0x5F) e.b[n] = uint8_t(key + 0x60);
    else { e.b[n] = 0xEF; e.b[n + 1] = uint8_t(key); ++n; }
    e.b[n + 1] = uint8_t(std::min(dur, 255));
    e.size = uint8_t(n + 2);
    e.addr = 0;
    e.duration = e.b[e.size - 1];
    e.pitch = e.pitch >= 0 && note >= 0 ? e.pitch + key - note : key + 15;
    e.type = e.b[0] == 0xF4 ? EventType::Tie : EventType::Note;
}

bool BerliozDriver::set_note_at(std::vector<Event>& ev, int tick, uint8_t byte, int pattern_len) const {
    seq::stream_prepare(*this, ev, tick, pattern_len);
    int i = seq::stream_timed_covering(ev, tick, true);
    if (i < 0) return false;
    if (ev[size_t(i)].tick != tick) {
        if (ev[size_t(i)].in_sub || !seq::stream_split_at(*this, ev, tick)) return false;
        i = -1;
        for (size_t k = 0; k < ev.size(); ++k) if (ev[k].duration > 0 && !ev[k].in_sub && ev[k].tick == tick) { i = int(k); break; }
        if (i < 0) return false;
    }
    if (ev[size_t(i)].in_sub) return false;
    fix_next(ev, i);
    const int dur = ev[size_t(i)].duration;
    apply_note_byte(ev[size_t(i)], byte);
    retime(ev);
    if (ev[size_t(i)].duration != dur && !set_duration(ev, i, dur)) return false;
    retime(ev);
    return true;
}

bool BerliozDriver::set_duration(std::vector<Event>& ev, int i, int dur) const {
    if (i < 0 || i >= int(ev.size()) || dur < 1 || dur > 4096) return false;
    Event& e = ev[size_t(i)];
    std::vector<Event> tail;
    if (e.type == EventType::Rest) {
        int left = dur;
        auto chunk = [&](Event& r) {
            const int n = left >= 256 ? 256 : left >= 128 ? 128 : left >= 64 ? 64 : std::min(left, 32);
            r.b[0] = n == 256 ? 0xFF : n == 128 ? 0xFE : n == 64 ? 0xFD : uint8_t(0xC0 + n - 1);
            r.size = 1; r.duration = n; r.addr = 0; r.type = EventType::Rest; r.pitch = -1;
            left -= n;
        };
        chunk(e);
        while (left > 0) { Event r = e; chunk(r); tail.push_back(r); }
    } else if (e.type == EventType::Note || e.type == EventType::Tie) {
        fix_next(ev, i);
        absolutize(e);
        const int first = std::min(dur, 255);
        e.b[e.size - 1] = uint8_t(first); e.duration = first; e.addr = 0;   // the duration is always the last byte
        const int note = e.b[0] == 0xF4 ? (e.b[1] == 0xEF ? e.b[2] : e.b[1] - 0x60) : e.b[0] == 0xEF ? e.b[1] : e.b[0] - 0x60;
        for (int left = dur - first; left > 0; left -= std::min(left, 255)) {
            Event t{};
            t.type = EventType::Tie; t.b[0] = 0xF4; t.size = 3;
            if (note >= 0x20 && note <= 0x5F) t.b[1] = uint8_t(note + 0x60);
            else { t.b[1] = 0xEF; t.b[2] = uint8_t(note & 0x7F); t.size = 4; }
            t.b[t.size - 1] = uint8_t(std::min(left, 255));
            t.duration = t.b[t.size - 1]; t.pitch = e.pitch;
            tail.push_back(t);
        }
    } else return false;
    ev.insert(ev.begin() + i + 1, tail.begin(), tail.end());
    return true;
}

std::string BerliozDriver::event_text(const Event& e) const {
    char b[96];
    if (e.type == EventType::Note || e.type == EventType::Tie) {
        std::snprintf(b, sizeof b, "%s%s  %d ticks%s", e.type == EventType::Tie ? "tie " : "", note_name(e).c_str(), e.duration, is_rel(e) ? " (relative)" : "");
        return b;
    }
    if (e.type == EventType::Rest) { std::snprintf(b, sizeof b, "rest  %d ticks", e.duration); return b; }
    if (e.type == EventType::Command && e.size == 0) return "Block end";
    if (e.type == EventType::Command && e.b[15] == 0xBE) {
        const int hi = e.b[0] >> 4, lo = e.b[0] & 15;
        if (hi) std::snprintf(b, sizeof b, "Bend %+d for %d ticks", hi >= 8 ? hi - 16 : hi, lo);
        else if (lo == 0) return "Bend list end";
        else if (lo == 1) std::snprintf(b, sizeof b, "Bend wait %d ticks", e.b[1]);
        else if (lo == 2) return "Bend key off";
        else std::snprintf(b, sizeof b, "Bend %+d", int8_t(e.b[1]));
        return b;
    }
    if (is_op(e, 0xF6)) {
        const int count = e.b[5], off = int16_t(le16(e.b + 6));
        if (count) std::snprintf(b, sizeof b, "Block $%04X-$%04X x%d, pitch %+d/20", le16(e.b + 1), le16(e.b + 3), count, off);
        else std::snprintf(b, sizeof b, "Block $%04X-$%04X forever, pitch %+d/20", le16(e.b + 1), le16(e.b + 3), off);
        return b;
    }
    if (is_op(e, 0xF9)) return "Restart the track";
    return seq::Driver::event_text(e);
}

}
