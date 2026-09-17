#include "stream.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using seq::Event;
using seq::EventType;

namespace stream {

int find_pattern(const uint8_t* ram, int lo, int hi, const int* pat, int n) {
    for (int a = lo; a + n <= hi; ++a) {
        bool ok = true;
        for (int i = 0; i < n && ok; ++i) ok = pat[i] == 0x100 || ram[a + i] == pat[i];
        if (ok) return a;
    }
    return -1;
}

Event make_event(EventType t, uint16_t addr, const uint8_t* ram, int size, int tick) {
    Event e{};
    e.type = t;
    e.addr = addr;
    e.size = uint8_t(std::min(size, 16));
    for (int i = 0; i < e.size; ++i) e.b[i] = ram[(addr + i) & 0xFFFF];
    e.tick = tick;
    return e;
}

Event cmd_event(uint8_t op, int a, int b) {
    Event c{};
    c.type = EventType::Command;
    c.b[0] = op;
    c.size = 1;
    if (a >= 0) { c.b[1] = uint8_t(a); c.size = 2; }
    if (b >= 0) { c.b[2] = uint8_t(b); c.size = 3; }
    return c;
}

namespace {
struct Frame {
    bool call;
    int slot, start, ret, target, count, pass;
    int end = -1;   // repeats: the byte after the RepEnd, once seen
};
}

seq::Track Driver::parse_track(const uint8_t* ram, uint16_t start, const State& init, int budget) const {
    seq::Track t;
    t.addr = start;
    if (!start) return t;
    std::vector<uint8_t> iter(0x10000, 0);
    State s = init;
    s.start = start;
    std::vector<Frame> frames;
    int pc = start, tick = 0;
    auto event_at = [&](int addr) {
        for (size_t i = 0; i < t.events.size(); ++i) if (t.events[i].addr == addr) return int(i);
        return 0;
    };
    auto pop_to_call = [&]() {
        while (!frames.empty() && !frames.back().call) frames.pop_back();
    };
    for (;;) {
        if (int(t.events.size()) >= budget || pc < 0 || pc > 0xFFFF) break;
        uint8_t buf[16];
        for (int i = 0; i < 16; ++i) buf[i] = ram[(pc + i) & 0xFFFF];
        Event e{};
        Flow f{};
        s.tick = tick;
        decode(buf, pc, s, e, f);
        if (e.size == 0 && f.kind == Flow::Next) break;   // a size-0 event marks a point the program only jumps from
        e.addr = uint16_t(pc);
        e.tick = tick;
        bool in_call = false;
        for (const Frame& fr : frames) if (fr.call) in_call = true;
        e.in_sub = iter[size_t(pc)] > 0 || in_call;
        e.in_call = in_call;
        e.nest = uint8_t(frames.size());
        e.sub_iter = iter[size_t(pc)];
        if (iter[size_t(pc)] < 255) ++iter[size_t(pc)];
        t.events.push_back(e);
        tick += e.duration;
        const int next = pc + e.size;
        switch (f.kind) {
            case Flow::Next:
                if (frames.empty() && !e.in_sub && next <= 0xFFFF && iter[size_t(next)] > 0) {
                    Event j{};
                    if (loop_jump(uint16_t(next), j)) {
                        j.type = EventType::Command; j.tick = tick; j.addr = 0;
                        t.events.push_back(j);
                        t.loops = true; t.loop_event = event_at(next); t.terminated = true;
                        break;
                    }
                }
                pc = next;
                break;
            case Flow::End: t.terminated = true; break;
            case Flow::Jump:
                if (f.count == 0 && iter[size_t(f.target & 0xFFFF)] > 0) { t.loops = true; t.loop_event = event_at(f.target); t.terminated = true; break; }
                pc = f.target;
                break;
            case Flow::Call:
                frames.push_back({true, 0, 0, next, f.target, std::max(1, f.count), 1});
                pc = f.target;
                break;
            case Flow::Return: {
                pop_to_call();
                if (frames.empty() && f.count) { pc = next; break; }
                if (frames.empty()) { t.events.back().type = EventType::End; t.terminated = true; break; }
                Frame& fr = frames.back();
                if (fr.pass < fr.count) { ++fr.pass; pc = fr.target; }
                else { pc = fr.ret; frames.pop_back(); }
                break;
            }
            case Flow::RepStart:
                frames.push_back({false, f.slot, next, 0, 0, f.count, 1});
                pc = next;
                break;
            case Flow::RepEnd: {
                int k = int(frames.size()) - 1;
                while (k >= 0 && (frames[size_t(k)].call || frames[size_t(k)].slot != f.slot)) --k;
                if (k < 0) { frames.insert(frames.begin(), {false, f.slot, start, 0, 0, 0, 1}); k = 0; }
                Frame& fr = frames[size_t(k)];
                const int count = fr.count ? fr.count : f.count;
                fr.end = next;
                if (count == 0) { t.loops = true; t.loop_event = event_at(fr.start); t.terminated = true; break; }
                fr.count = count;
                if (fr.pass < count) { ++fr.pass; frames.resize(size_t(k) + 1); pc = fr.start; }
                else { frames.resize(size_t(k)); pc = next; }
                break;
            }
            case Flow::RepBreak: {
                int k = int(frames.size()) - 1;
                while (k >= 0 && (frames[size_t(k)].call || frames[size_t(k)].slot != f.slot)) --k;
                if (k < 0) { pc = next; break; }
                Frame& fr = frames[size_t(k)];
                const int on = f.count ? f.count : fr.count;
                const int target = f.target >= 0 ? f.target : fr.end;
                if (on && fr.pass == on && target >= 0) { frames.resize(size_t(k)); pc = target; }
                else pc = next;
                break;
            }
        }
        if (t.terminated) break;
    }
    for (Event& e : t.events) {
        const int target = e.type == EventType::Command ? jump_target(e) : -1;
        if (target < 0) continue;
        const Event* hit = nullptr;
        for (const Event& o : t.events) if (o.addr == target && (!hit || (hit->in_sub && !o.in_sub))) hit = &o;
        if (hit) { e.target_tick = hit->tick; e.target_timed = hit->duration > 0; }
    }
    if (!t.terminated) t.truncated = true;
    t.total_ticks = tick;
    t.used_events = int(t.events.size());
    t.end_addr = t.addr;
    return t;
}

bool Driver::parse_header(const uint8_t* ram, uint16_t header, seq::Pattern& out, int budget) const {
    out = seq::Pattern{};
    out.addr = header;
    bool any = false;
    for (int v = 0; v < 8; ++v) {
        uint16_t start = track_start(ram, header, v);
        if (!start) continue;
        State s = initial_state(ram, header, v);
        s.voice = v;
        out.tracks[v] = parse_track(ram, start, s, budget);
        track_parsed(v, out.tracks[v]);
        if (out.tracks[v].truncated) {
            if (std::getenv("BOOMSPC_DEBUG_PARSE")) {
                const seq::Track& t = out.tracks[v];
                std::fprintf(stderr, "header %04X v%d @%04X: truncated after %zu events\n", header, v, start, t.events.size());
                for (size_t i = t.events.size() > 6 ? t.events.size() - 6 : 0; i < t.events.size(); ++i) std::fprintf(stderr, "   %04X t%d [%02X %02X %02X] %s\n", t.events[i].addr, t.events[i].tick, t.events[i].b[0], t.events[i].b[1], t.events[i].b[2], event_text(t.events[i]).c_str());
            }
            out.tracks[v] = seq::Track{};
            continue;
        }
        if (out.tracks[v].total_ticks > 0) any = true;
        out.length_ticks = std::max(out.length_ticks, out.tracks[v].total_ticks);
    }
    return any;
}

std::string Driver::song_label(const uint8_t* ram, uint16_t header, const seq::Pattern& p, int index) const {
    (void)ram;
    bool loops = false;
    for (int v = 0; v < 8; ++v) if (p.tracks[v].loops) loops = true;
    char b[64];
    std::snprintf(b, sizeof b, "song %d @%04X (%d ticks%s)", index, header, p.length_ticks, loops ? ", loops" : "");
    return b;
}

std::vector<seq::Song> Driver::find_songs(const uint8_t* ram, const uint8_t* dsp) const {
    (void)dsp;
    std::vector<seq::Song> songs;
    int index = 0;
    for (uint16_t header : song_headers(ram)) {
        seq::Pattern pat;
        if (!parse_header(ram, header, pat)) { ++index; continue; }
        prune_idle_voices(ram, pat);
        int notes = 0;
        for (int v = 0; v < 8; ++v) for (const Event& e : pat.tracks[v].events) if (e.type == EventType::Note || e.type == EventType::Percussion) ++notes;
        if (notes < min_notes()) { ++index; continue; }
        bool loops = false;
        for (int v = 0; v < 8; ++v) if (pat.tracks[v].loops) loops = true;
        seq::Song sg;
        sg.order_addr = header;
        sg.order_end = uint16_t(header + 16);
        sg.orders.push_back({header, header});
        sg.label = song_label(ram, header, pat, index);
        sg.patterns.push_back(std::move(pat));
        sg.loop_count = loops ? 0xFF : 0;
        sg.loop_to = loops ? 0 : -1;
        songs.push_back(std::move(sg));
        ++index;
    }
    return songs;
}

void Driver::prune_idle_voices(const uint8_t* ram, seq::Pattern& p) const { (void)ram; (void)p; }

// Konami headers keep stale pointers in the slots of voices the song does
// not use; when the song is the one playing, a voice whose live pointer is
// nowhere near its parsed program is such a slot.
void Driver::prune_by_live_pointers(const uint8_t* ram, seq::Pattern& p) const {
    seq::Position none;
    bool inside[8] = {};
    int hits = 0, used = 0;
    for (int v = 0; v < 8; ++v) {
        if (!p.tracks[v].addr) continue;
        ++used;
        const uint16_t ptr = live_ptr(ram, none, v);
        for (const Event& e : p.tracks[v].events)
            if (ptr >= e.addr && ptr <= uint16_t(e.addr + e.size)) { inside[v] = true; break; }
        if (inside[v]) ++hits;
    }
    if (hits < 3 || hits == used) return;
    for (int v = 0; v < 8; ++v) if (p.tracks[v].addr && !inside[v]) p.tracks[v] = seq::Track{};
    p.length_ticks = 0;
    for (int v = 0; v < 8; ++v) p.length_ticks = std::max(p.length_ticks, p.tracks[v].total_ticks);
}

uint16_t Driver::live_ptr(const uint8_t* ram, const seq::Position& pos, int v) const {
    (void)pos;
    return rd16(ram, live_ptr_addr(v));
}

int Driver::pick_current_song(const uint8_t* ram, const std::vector<seq::Song>& songs) const {
    int best = -1, best_hits = 0;
    seq::Position none;
    for (size_t i = 0; i < songs.size(); ++i) {
        const seq::Pattern& p = songs[i].patterns[0];
        select_song(songs[i].order_addr);
        int hits = 0;
        for (int v = 0; v < 8; ++v) {
            uint16_t ptr = live_ptr(ram, none, v);
            if (!ptr) continue;
            for (const Event& e : p.tracks[v].events)
                if (e.addr == ptr || uint16_t(e.addr + e.size) == ptr) { ++hits; break; }
        }
        if (hits > best_hits) { best_hits = hits; best = int(i); }
    }
    if (best < 0 && !songs.empty()) best = 0;
    if (best >= 0) select_song(songs[best].order_addr);
    return best;
}

seq::Position Driver::locate(const uint8_t* ram, const seq::Song& song, const seq::Position* prev) const {
    seq::Position pos;
    select_song(song.order_addr);
    pos.track_ptr_base = live_ptr_addr(0);
    pos.track_ptr_span = uint8_t(ptr_span());
    pos.order_index = 0;
    uint16_t ptr[8];
    for (int v = 0; v < 8; ++v) ptr[v] = live_ptr(ram, pos, v);
    seq::resolve_stream_position(song.patterns[0], ptr, prev, ptr_after_note(), pos);
    return pos;
}

std::vector<uint8_t> Driver::serialize_track(const std::vector<Event>& events) const {
    std::vector<uint8_t> out;
    for (const Event& e : events) {
        if (e.in_sub) continue;
        for (int i = 0; i < e.size; ++i) out.push_back(e.b[i]);
    }
    return out;
}

State Driver::state_before(const std::vector<Event>& ev, int i) const {
    State s = edit_state(ev);
    for (int k = 0; k < i && k < int(ev.size()); ++k) {
        Event tmp{};
        Flow f{};
        s.tick = ev[size_t(k)].tick;
        decode(ev[size_t(k)].b, ev[size_t(k)].addr, s, tmp, f);
    }
    return s;
}

void Driver::retime(std::vector<Event>& ev) const {
    State s = edit_state(ev);
    int tick = 0, base = 0;
    bool have_base = false;
    for (Event& e : ev) {
        Event tmp{};
        Flow f{};
        s.tick = tick;
        decode(e.b, e.addr, s, tmp, f);
        e.tick = tick;
        e.duration = tmp.duration;
        if (tmp.pitch >= 0) {
            if (!have_base && e.pitch >= 0 && e.type == EventType::Note) { base = e.pitch - tmp.pitch; have_base = true; }
            e.pitch = tmp.pitch + base;
        } else if (e.type == EventType::Note || e.type == EventType::Rest || e.type == EventType::Tie) e.pitch = -1;
        tick += e.duration;
    }
}

int Driver::pitch_base(const std::vector<Event>& ev) const {
    State s = edit_state(ev);
    for (const Event& e : ev) {
        Event tmp{};
        Flow f{};
        s.tick = e.tick;
        decode(e.b, e.addr, s, tmp, f);
        if (e.type == EventType::Note && e.pitch >= 0 && tmp.pitch >= 0) return e.pitch - tmp.pitch;
    }
    return 0;
}

bool Driver::enter_note(std::vector<Event>& ev, int tick, int semitone, int pattern_len) const {
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
    return set_note_at(ev, tick, note_byte_in(semitone - pitch_base(ev), state_before(ev, idx)), pattern_len);
}

bool Driver::is_return_command(const Event& e) const {
    if (e.type != EventType::Command) return false;
    State s;
    Event tmp{};
    Flow f{};
    decode(e.b, e.addr, s, tmp, f);
    return f.kind == Flow::Return;
}

int Driver::jump_target(const Event& e) const {
    if (e.type != EventType::Command) return -1;
    const CmdSpec& sp = spec(e.b[0]);
    if (!sp.addr_at || sp.addr_at + 1 >= e.size) return -1;
    return e.b[sp.addr_at] | (e.b[sp.addr_at + 1] << 8);
}

void Driver::set_jump_target(Event& e, uint16_t addr) const {
    const CmdSpec& sp = spec(e.b[0]);
    if (!sp.addr_at) return;
    e.b[sp.addr_at] = uint8_t(addr & 0xFF);
    e.b[sp.addr_at + 1] = uint8_t(addr >> 8);
}

void Driver::live_state_writes(const uint8_t* ram, const seq::Position& pos, int voice, uint16_t ptr, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    (void)pos;
    const uint16_t at = live_ptr_addr(voice);
    int mapped = remap.find(rd16(ram, at));
    if (mapped >= 0) ptr = uint16_t(mapped);
    out.push_back({at, uint8_t(ptr & 0xFF)});
    out.push_back({uint16_t(at + 1), uint8_t(ptr >> 8)});
    live_extra_writes(ram, voice, remap, out);
}

}
