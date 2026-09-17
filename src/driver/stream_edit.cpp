// Generic editing for drivers whose songs are one byte program per voice.
// The drivers supply the format-specific pieces (duration encoding, note
// bytes, jump targets); the operations here only touch events outside
// subroutine bodies and leave the address of every event they modify at 0
// so Tracker::write_track knows the stream has to be rewritten.
#include <algorithm>
#include <cstring>

#include "seq.hpp"

namespace seq {
namespace {
bool timed(const Event& e) { return e.duration > 0 && (e.type == EventType::Note || e.type == EventType::Rest || e.type == EventType::Tie || e.type == EventType::Percussion); }

int end_tick(const std::vector<Event>& ev) {
    int t = 0;
    for (const Event& e : ev) t = std::max(t, e.tick + e.duration);
    return t;
}

int terminal_index(const Driver& d, const std::vector<Event>& ev) {
    for (int i = int(ev.size()) - 1; i >= 0; --i) {
        const Event& e = ev[size_t(i)];
        if (e.in_sub) continue;
        if (e.type == EventType::End) return i;
        if (e.type == EventType::Command && d.ends_stream(e)) return i;
        if (e.type == EventType::Command && d.jump_target(e) >= 0) {
            int t = d.jump_target(e);
            for (int j = 0; j < i; ++j) if (!ev[size_t(j)].in_sub && ev[size_t(j)].addr == t && ev[size_t(j)].addr) return i;
        }
        break;
    }
    return int(ev.size());
}

}

void stream_prepare(const Driver& d, std::vector<Event>& ev, int tick, int min_end) {
    d.retime(ev);
    for (int guard = 0; guard < 4 && d.unroll_at(ev, tick); ++guard) {}
    int end = end_tick(ev);
    if (tick >= end) d.extend(ev, std::max(min_end, tick + 1));
}

int stream_timed_covering(const std::vector<Event>& ev, int tick, bool allow_sub) {
    for (size_t i = 0; i < ev.size(); ++i) {
        const Event& e = ev[i];
        if (!timed(e) || (e.in_sub && !allow_sub)) continue;
        if (e.tick <= tick && tick < e.tick + e.duration) return int(i);
    }
    return -1;
}

bool stream_split_at(const Driver& d, std::vector<Event>& ev, int tick) {
    d.retime(ev);
    int i = stream_timed_covering(ev, tick, false);
    if (i < 0 || ev[size_t(i)].tick == tick) return false;
    const int start = ev[size_t(i)].tick, total = ev[size_t(i)].duration;
    Event second = ev[size_t(i)];
    second.addr = 0;
    if (!d.set_duration(ev, i, tick - start)) return false;
    d.retime(ev);
    int j = -1;
    for (size_t k = 0; k < ev.size(); ++k) if (timed(ev[k]) && !ev[k].in_sub && ev[k].tick == start) { j = int(k); break; }
    if (j < 0) return false;
    for (size_t k = size_t(j); k < ev.size(); ++k) if (timed(ev[k]) && !ev[k].in_sub && ev[k].tick + ev[k].duration == tick) { j = int(k); break; }
    ev.insert(ev.begin() + j + 1, second);
    d.retime(ev);
    if (!d.set_duration(ev, j + 1, total - (tick - start))) return false;
    d.retime(ev);
    return true;
}

bool stream_set_note_at(const Driver& d, std::vector<Event>& ev, int tick, uint8_t byte, int min_end) {
    stream_prepare(d, ev, tick, min_end);
    int i = stream_timed_covering(ev, tick, true);
    if (i < 0) return false;
    if (ev[size_t(i)].tick != tick) {
        if (ev[size_t(i)].in_sub) return false;
        if (!stream_split_at(d, ev, tick)) return false;
        i = -1;
        for (size_t k = 0; k < ev.size(); ++k) if (timed(ev[k]) && !ev[k].in_sub && ev[k].tick == tick) { i = int(k); break; }
        if (i < 0) return false;
    }
    d.apply_note_byte(ev[size_t(i)], byte);
    d.retime(ev);
    return true;
}

bool stream_insert_command_at(const Driver& d, std::vector<Event>& ev, int tick, const uint8_t* bytes, int size, int min_end) {
    stream_prepare(d, ev, tick, min_end);
    if (size <= 0 || size > 16) return false;
    int cover = stream_timed_covering(ev, tick, true);
    if (cover >= 0 && ev[size_t(cover)].in_sub) return false;
    if (cover >= 0 && ev[size_t(cover)].tick != tick && !stream_split_at(d, ev, tick)) return false;
    d.retime(ev);
    int at = terminal_index(d, ev);
    for (size_t k = 0; k < ev.size(); ++k)
        if (!ev[k].in_sub && ev[k].tick >= tick && int(k) < at) { at = int(k); break; }
    if (tick > end_tick(ev)) return false;
    Event c{};
    c.type = EventType::Command;
    c.addr = 0;
    c.size = uint8_t(size);
    std::memcpy(c.b, bytes, size_t(size));
    c.tick = tick;
    ev.insert(ev.begin() + at, c);
    d.retime(ev);
    return true;
}

bool stream_remove_span(const Driver& d, std::vector<Event>& ev, int tick, int ticks) {
    d.retime(ev);
    if (ticks <= 0 || tick >= end_tick(ev)) return false;
    const int t1 = tick + ticks;
    for (int guard = 0; guard < 64; ++guard) {
        int hit = -1;
        for (const Event& e : ev) if (e.in_sub && timed(e) && e.tick < t1 && e.tick + e.duration > tick) { hit = e.tick; break; }
        if (hit < 0 || !d.unroll_at(ev, std::max(hit, tick))) break;
    }
    std::vector<std::pair<int, int>> todo;   // index, new duration (0 = erase)
    for (int i = 0; i < int(ev.size()); ++i) {
        const Event& e = ev[size_t(i)];
        if (!timed(e) || e.in_sub) continue;
        const int s = e.tick, n = e.tick + e.duration;
        const int overlap = std::min(n, t1) - std::max(s, tick);
        if (overlap <= 0) continue;
        todo.push_back({i, overlap >= e.duration ? 0 : e.duration - overlap});
    }
    if (todo.empty()) return false;
    for (auto it = todo.rbegin(); it != todo.rend(); ++it) {
        if (it->second == 0) ev.erase(ev.begin() + it->first);
        else if (!d.set_duration(ev, it->first, it->second)) return false;
    }
    d.retime(ev);
    return true;
}

bool stream_insert_span(const Driver& d, std::vector<Event>& ev, int tick, int ticks, uint8_t byte) {
    d.retime(ev);
    if (ticks <= 0 || tick > end_tick(ev)) return false;
    int i = stream_timed_covering(ev, tick, true);
    if (i >= 0 && ev[size_t(i)].in_sub) { d.unroll_at(ev, tick); i = stream_timed_covering(ev, tick, true); }
    if (i >= 0 && ev[size_t(i)].tick != tick) {
        if (!d.set_duration(ev, i, ev[size_t(i)].duration + ticks)) return false;
        d.retime(ev);
        return true;
    }
    int model = i >= 0 ? i : -1;
    if (model < 0) for (int k = int(ev.size()) - 1; k >= 0; --k) if (timed(ev[size_t(k)]) && !ev[size_t(k)].in_sub) { model = k; break; }
    if (model < 0) return false;
    Event r = ev[size_t(model)];
    r.addr = 0;
    d.apply_note_byte(r, byte);
    int at = i >= 0 ? i : terminal_index(d, ev);
    ev.insert(ev.begin() + at, r);
    d.retime(ev);
    if (!d.set_duration(ev, at, ticks)) return false;
    d.retime(ev);
    return true;
}

bool stream_set_instrument(const Driver& d, std::vector<Event>& ev, int tick0, int tick1, uint8_t ins) {
    stream_prepare(d, ev, tick0, tick1);
    for (Event& e : ev)
        if (e.type == EventType::Command && d.is_instrument_cmd(e.b[0]) && e.tick >= tick0 && e.tick < tick1) { e.b[1] = ins; return true; }
    uint8_t bytes[16] = {d.instrument_opcode(), ins};
    int size = d.cmd_size(d.instrument_opcode());
    if (size < 2) return false;
    return stream_insert_command_at(d, ev, tick0, bytes, size);
}

void resolve_stream_position(const Pattern& p, const uint16_t ptr[8], const Position* prev, bool after_only, Position& pos) {
    std::vector<int> cands[8];
    for (int v = 0; v < 8; ++v) {
        pos.voice_event[v] = -1;
        pos.voice_tick[v] = -1;
        pos.voice_ptr[v] = ptr[v];
        const std::vector<Event>& ev = p.tracks[v].events;
        for (int i = 0; i < int(ev.size()); ++i) {
            const Event& e = ev[size_t(i)];
            if (uint16_t(e.addr + e.size) == ptr[v] && e.duration > 0) { cands[v].push_back(i); continue; }
            if (!after_only && e.addr == ptr[v] && i > 0) {
                int k = i - 1;
                while (k > 0 && ev[size_t(k)].duration <= 0) --k;
                if (ev[size_t(k)].duration > 0 && (cands[v].empty() || cands[v].back() != k)) cands[v].push_back(k);
            }
        }
        if (cands[v].empty() && !ev.empty() && ptr[v] == ev[0].addr) cands[v].push_back(0);
    }
    int prev_tick = -1;
    if (prev && prev->valid) for (int v = 0; v < 8; ++v) prev_tick = std::max(prev_tick, prev->voice_tick[v]);
    auto span = [&](int v, int i, int& t0, int& t1) { const Event& e = p.tracks[v].events[size_t(i)]; t0 = e.tick; t1 = e.tick + std::max(e.duration, 1); };
    auto covers = [&](int v, int t) { for (int i : cands[v]) { int a, b; span(v, i, a, b); if (a <= t && t < b) return true; } return false; };
    int best_t = -1, best_n = 0;
    bool best_forward = false;
    for (int v = 0; v < 8; ++v)
        for (int i : cands[v]) {
            int a, b; span(v, i, a, b);
            int n = 0;
            for (int w = 0; w < 8; ++w) if (covers(w, a)) ++n;
            const bool forward = prev_tick < 0 || a + 2 >= prev_tick;   // continuity: not behind where we were
            if (n > best_n || (n == best_n && ((forward && !best_forward) || (forward == best_forward && a < best_t)))) { best_t = a; best_n = n; best_forward = forward; }
        }
    if (best_n == 0) return;
    for (int v = 0; v < 8; ++v) {
        if (cands[v].empty()) continue;
        int pick = -1, dist = 1 << 30;
        for (int i : cands[v]) {
            int a, b; span(v, i, a, b);
            int d = best_t < a ? a - best_t : best_t >= b ? best_t - b + 1 : 0;
            if (d < dist) { dist = d; pick = i; }
        }
        pos.voice_event[v] = pick;
        pos.voice_tick[v] = p.tracks[v].events[size_t(pick)].tick;
        pos.valid = true;
    }
}

bool stream_structure_changed(const std::vector<Event>& ev, const std::vector<Event>& original) {
    std::vector<std::pair<uint16_t, int>> a, b;
    for (const Event& e : ev) if (!e.in_sub) { if (e.addr == 0) return true; a.push_back({e.addr, e.size}); }
    for (const Event& e : original) if (!e.in_sub) b.push_back({e.addr, e.size});
    return a != b;
}

std::vector<uint8_t> Driver::serialize_relocated(const std::vector<Event>& ev, uint16_t dest, std::vector<int>* offsets) const {
    if (offsets) offsets->assign(ev.size(), -1);
    if (!has_stream_edit()) return serialize_track(ev);
    std::vector<int> main;
    for (size_t i = 0; i < ev.size(); ++i) if (!ev[i].in_sub) main.push_back(int(i));
    std::vector<int> off(ev.size(), -1);
    std::vector<bool> drop(ev.size(), false);
    int pos = 0;
    for (size_t k = 0; k < main.size(); ++k) {
        const Event& e = ev[size_t(main[k])];
        int t = e.type == EventType::Command ? jump_target(e) : -1;
        if (t >= 0 && k + 1 < main.size() && ev[size_t(main[k + 1])].addr == t && t != 0) { drop[size_t(main[k])] = true; continue; }
        off[size_t(main[k])] = pos;
        pos += e.size;
    }
    if (offsets) *offsets = off;
    std::vector<uint8_t> out;
    out.reserve(size_t(pos));
    for (int i : main) {
        if (drop[size_t(i)]) continue;
        Event e = ev[size_t(i)];
        int t = e.type == EventType::Command ? jump_target(e) : -1;
        if (t > 0)
            for (int j : main)
                if (ev[size_t(j)].addr == t && off[size_t(j)] >= 0) { set_jump_target(e, uint16_t(dest + off[size_t(j)])); break; }
        for (int k = 0; k < e.size; ++k) out.push_back(e.b[k]);
    }
    return out;
}

void Driver::live_state_writes(const uint8_t* ram, const Position& pos, int voice, uint16_t ptr, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    if (!pos.track_ptr_base) return;
    uint16_t at = uint16_t(pos.track_ptr_base + voice * 2);
    int mapped = remap.find(uint16_t(ram[at] | (ram[(at + 1) & 0xFFFF] << 8)));
    if (mapped >= 0) ptr = uint16_t(mapped);
    out.push_back({at, uint8_t(ptr & 0xFF)});
    out.push_back({uint16_t(at + 1), uint8_t(ptr >> 8)});
}

void remap_word(const uint8_t* ram, uint16_t at, const Driver::Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) {
    int mapped = remap.find(uint16_t(ram[at] | (ram[(at + 1) & 0xFFFF] << 8)));
    if (mapped < 0) return;
    out.push_back({at, uint8_t(mapped & 0xFF)});
    out.push_back({uint16_t(at + 1), uint8_t(mapped >> 8)});
}

void Driver::track_pointer_writes(const Song& song, int pattern_idx, int voice, uint16_t dest, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    if (pattern_idx < 0 || pattern_idx >= int(song.patterns.size())) return;
    uint16_t at = uint16_t(song.patterns[size_t(pattern_idx)].addr + voice * 2);
    out.push_back({at, uint8_t(dest & 0xFF)});
    out.push_back({uint16_t(at + 1), uint8_t(dest >> 8)});
}

}
