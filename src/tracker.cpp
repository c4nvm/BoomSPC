#include "tracker.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "brr.hpp"

using seq::Event;
using seq::EventType;

void Tracker::reset() {
    drv.reset();
    songs.clear();
    song_index = -1;
    pos = seq::Position{};
    driver_name = "unknown";
    analyzed = false;
    rescans_left = 0;
    song_pinned = false;
    next_repick = 0;
}

const nspc::Layout* Tracker::nspc_layout() const {
    const auto* n = dynamic_cast<const nspc::NspcDriver*>(drv.get());
    return n ? &n->L : nullptr;
}

void Tracker::analyze(const EngineSnapshot& s) {
    drv = s.snes_rom ? seq::detect_snes_driver(s.snes_rom) : seq::detect_driver(s.ram, s.dsp);
    driver_name = drv ? drv->name() : "unknown";
    songs = drv ? drv->find_songs(s.ram, s.dsp) : std::vector<seq::Song>{};
    song_index = drv ? drv->pick_current_song(s.ram, songs) : -1;
    pos = song() ? drv->locate(s.ram, *song(), nullptr) : seq::Position{};
    analyzed = true;
    song_pinned = false;
    rescans_left = pos.valid ? 0 : 6;
}

void Tracker::update(const EngineSnapshot& s, double now) {
    if (!analyzed || !s.loaded) return;
    if (drv && !song_pinned && now >= next_repick) {
        next_repick = now + 0.5;
        int pick = drv->pick_current_song(s.ram, songs);
        if (pick >= 0 && pick != song_index) {
            song_index = pick;
            pos = seq::Position{};
        }
    }
    const bool restarted = s.sample_pairs < last_samples;
    last_samples = s.sample_pairs;
    if (song()) pos = drv->locate(s.ram, *song(), restarted ? nullptr : &pos);
    if (!pos.valid && rescans_left > 0 && now >= next_rescan) {
        --rescans_left;
        next_rescan = now + 0.75;
        int keep = rescans_left;
        analyze(s);
        if (!pos.valid) rescans_left = keep;
    }
}

void Tracker::reparse(const EngineSnapshot& s) {
    seq::Song* sg = song();
    if (!sg || !drv) return;
    uint16_t addr = sg->order_addr;
    std::vector<seq::Song> fresh = drv->find_songs(s.ram, s.dsp);
    for (size_t i = 0; i < fresh.size(); ++i)
        if (fresh[i].order_addr == addr) { songs = std::move(fresh); song_index = int(i); return; }
    songs = std::move(fresh);
    song_index = drv->pick_current_song(s.ram, songs);
}

void Tracker::remove_event(const seq::Driver& d, std::vector<Event>& ev, int index) {
    if (index < 0 || index >= int(ev.size())) return;
    ev.erase(ev.begin() + index);
    d.retime(ev);
}

int Tracker::find_free_space(const EngineSnapshot& s, int need) const {
    std::vector<bool> reserved(0x10000, false);
    int driver_end = 0x200;
    for (const seq::Song& sg : songs) driver_end = std::max(driver_end, int(sg.order_addr));
    for (const seq::Song& sg : songs) driver_end = std::min(driver_end, int(sg.order_addr));
    for (int a = 0; a < driver_end; ++a) reserved[a] = true;
    int lo = 0x200, hi = 0x10000;
    if (drv) drv->free_space_bounds(lo, hi);
    const bool spc_space = !drv || drv->spc_ram_space();
    if (spc_space) for (int a = 0xFFC0; a < 0x10000; ++a) reserved[a] = true;
    int esa = s.dsp[0x6D] << 8, edl = s.dsp[0x7D] & 0x0F;
    int echo_len = edl ? edl * 2048 : 4;
    if (spc_space) for (int a = esa; a < std::min(0x10000, esa + echo_len); ++a) reserved[a] = true;
    int dir = spc_space ? s.dsp[0x5D] << 8 : 0;
    if (spc_space) for (int a = dir; a < std::min(0x10000, dir + 0x400); ++a) reserved[a] = true;
    std::vector<int> starts;
    for (int i = 0; i < (spc_space ? 256 : 0); ++i) {
        int e = (dir + i * 4) & 0xFFFF;
        int start = s.ram[e] | (s.ram[(e + 1) & 0xFFFF] << 8);
        int loop = s.ram[(e + 2) & 0xFFFF] | (s.ram[(e + 3) & 0xFFFF] << 8);
        if (start == 0xFFFF || (start == 0 && loop == 0 && i > 0)) break;
        if (start >= 0x200 && start < 0xFFC0 && loop >= start) starts.push_back(start);
    }
    for (int start : starts) {
        int limit = 0x10000;
        for (int o : starts) if (o > start) limit = std::min(limit, o);
        brr::Info info = brr::scan(s.ram, uint16_t(start));
        int end = info.truncated || info.blocks > 0x1000 ? limit : std::min(limit, start + info.blocks * 9);
        for (int a = start; a < end; ++a) reserved[a] = true;
    }
    std::vector<bool> reclaimable(0x10000, false);
    for (size_t si = 0; si < songs.size(); ++si) {
        const seq::Song& sg = songs[si];
        const bool other = int(si) != song_index;
        auto mark = [&](int a) { a &= 0xFFFF; reserved[a] = true; if (other) reclaimable[a] = true; };
        for (int a = sg.order_addr; a < sg.order_end; ++a) mark(a);
        for (const seq::Pattern& p : sg.patterns) {
            for (int a = p.addr; a < p.addr + 16; ++a) mark(a);
            for (const seq::Track& t : p.tracks) {
                if (!t.addr) continue;
                for (const Event& e : t.events)
                    for (int k = 0; k < e.size; ++k) mark(e.addr + k);
                mark(t.end_addr);
            }
        }
    }
    if (song_index >= 0) {
        const seq::Song& sg = songs[size_t(song_index)];
        for (int a = sg.order_addr; a < sg.order_end; ++a) reclaimable[a] = false;
        for (const seq::Pattern& p : sg.patterns) {
            for (int a = p.addr; a < p.addr + 16; ++a) reclaimable[a & 0xFFFF] = false;
            for (const seq::Track& t : p.tracks)
                for (const Event& e : t.events)
                    for (int k = 0; k < e.size; ++k) reclaimable[(e.addr + k) & 0xFFFF] = false;
        }
    }

    std::vector<bool> sacrificial(0x10000, false);
    if (drv) {
        std::vector<std::pair<uint16_t, uint16_t>> extra;
        drv->reclaimable_ranges(s.ram, extra);
        for (auto& r : extra) for (int a = r.first; a < r.second && a < 0x10000; ++a) if (!reserved[a] || reclaimable[a]) sacrificial[a] = true;
    }
    for (int pass = 0; pass < 2; ++pass) {
        const uint8_t fill = pass == 0 ? 0x00 : 0xFF;
        int best = -1, best_len = 0;
        int run = 0;
        for (int a = lo; a < hi; ++a) {
            bool free = (!reserved[a] && (s.ram[a] == 0 || s.ram[a] == fill)) || (reclaim_other_songs && reclaimable[a]) || sacrificial[a];
            if (free) ++run; else run = 0;
            if (run > best_len) { best_len = run; best = a - run + 1; }
        }
        if (std::getenv("BOOMSPC_DEBUG_FREE")) std::fprintf(stderr, "find_free_space(%d): best run %d at $%04X (fill %02X)\n", need, best_len, best, fill);
        if (best_len >= need + 2) return best + 1;
    }
    return -1;
}

bool Tracker::default_args(const seq::Driver& d, const seq::Pattern& pat, int voice, int tick, uint8_t* bytes, int size) {
    (void)d;
    const uint8_t op = bytes[0];
    auto scan = [&](int v, bool before_only) {
        const seq::Track& t = pat.tracks[v];
        const seq::Event* best = nullptr;
        for (int i = 0; i < t.used_events; ++i) {
            const seq::Event& e = t.events[size_t(i)];
            if (e.type != seq::EventType::Command || e.b[0] != op || e.size != size) continue;
            if (before_only && e.tick > tick) break;
            best = &e;
            if (!before_only) break;
        }
        return best;
    };
    const seq::Event* src = voice >= 0 && voice < 8 ? scan(voice, true) : nullptr;
    if (!src && voice >= 0 && voice < 8) src = scan(voice, false);
    for (int v = 0; v < 8 && !src; ++v) if (v != voice && pat.tracks[v].addr) src = scan(v, false);
    if (!src) return false;
    for (int i = 1; i < size; ++i) bytes[i] = src->b[i];
    return true;
}

int Tracker::find_space_or_reclaim(const EngineSnapshot& s, int need, bool& reclaimed) {
    reclaimed = false;
    int at = find_free_space(s, need);
    if (at >= 0 || reclaim_other_songs) return at;
    reclaim_other_songs = true;
    at = find_free_space(s, need);
    reclaim_other_songs = false;
    reclaimed = at >= 0;
    if (reclaimed) reclaim_other_songs = true;
    return at;
}

Tracker::Result Tracker::write_track(Engine& eng, int pattern_idx, int voice, std::vector<Event> events) {
    Result r;
    seq::Song* sg = song();
    if (!sg || !drv || pattern_idx < 0 || pattern_idx >= int(sg->patterns.size())) { r.msg = "no pattern"; return r; }
    seq::Pattern& pat = sg->patterns[size_t(pattern_idx)];
    seq::Track& t = pat.tracks[voice];

    EngineSnapshot snap;
    eng.snapshot(snap);
    std::string note;
    {
        bool had_shared = false, has_shared = false;
        for (const Event& e : t.events) if (e.in_sub) { had_shared = true; break; }
        for (const Event& e : events) if (e.in_sub) { has_shared = true; break; }
        if (had_shared && !has_shared) note += " (a called / repeated section was written out as plain data)";
    }

    if (drv->in_place_only()) {
        const bool restructure = drv->has_stream_edit() && seq::stream_structure_changed(events, t.events);
        int written = 0;
        std::vector<uint8_t> bytes;
        std::vector<int> offsets;
        uint16_t dest = t.addr;
        bool relocated = false;
        if (restructure) {
            int extent = 0;
            uint16_t next = t.addr;
            for (const Event& e : t.events) {
                if (e.in_sub) continue;
                if (e.addr != next) break;
                extent += e.size; next = uint16_t(next + e.size);
            }
            bytes = drv->serialize_relocated(events, t.addr, &offsets);
            if (int(bytes.size()) > extent) {
                bool reclaimed = false;
                int at = find_space_or_reclaim(snap, int(bytes.size()), reclaimed);
                if (at < 0) { r.msg = "stream grew and no free RAM was found, even in the other songs' data"; return r; }
                if (reclaimed) note += " (RAM of the other songs reclaimed)";
                dest = uint16_t(at);
                relocated = true;
                bytes = drv->serialize_relocated(events, dest, &offsets);
            }
        }
        seq::Position live = restructure ? drv->locate(snap.ram, *sg, &pos) : seq::Position{};
        seq::Position image = restructure ? drv->locate(eng.image_ram(), *sg, nullptr) : seq::Position{};
        const char* moved_note = " (playing it)";
        eng.begin_edit();
        for (const Event& e : events) {
            if (e.size == 0 || e.addr == 0) continue;
            if (restructure && !e.in_sub) continue;
            if (std::memcmp(snap.ram + e.addr, e.b, e.size) == 0) continue;
            eng.write_ram(e.addr, e.b, e.size);
            ++written;
        }
        if (restructure) {
            eng.write_ram(dest, bytes.data(), bytes.size());
            if (relocated) {
                std::vector<std::pair<uint16_t, uint8_t>> ptr;
                drv->track_pointer_writes(*sg, pattern_idx, voice, dest, ptr);
                if (ptr.empty()) { eng.end_edit(); r.msg = "this driver's song pointers cannot be moved yet"; return r; }
                for (auto& w : ptr) eng.write_ram(w.first, &w.second, 1);
            }
            seq::Driver::Remap remap;
            for (size_t i = 0; i < events.size(); ++i) {
                if (events[i].in_sub || events[i].addr == 0 || offsets[i] < 0) continue;
                remap.pairs.push_back({events[i].addr, uint16_t(dest + offsets[i])});
                remap.pairs.push_back({uint16_t(events[i].addr + events[i].size), uint16_t(dest + offsets[i] + events[i].size)});
            }
            auto move_pointer = [&](const uint8_t* ram, const seq::Position& at_pos, Engine::WriteTarget target) {
                if (!at_pos.valid || at_pos.voice_event[voice] < 0 || at_pos.voice_event[voice] >= int(t.events.size())) return true;
                const Event& at = t.events[size_t(at_pos.voice_event[voice])];
                int best_i = -1;
                for (size_t i = 0; i < events.size(); ++i)
                    if (!events[i].in_sub && events[i].duration > 0 && offsets[i] >= 0 && events[i].tick >= at.tick) { best_i = int(i); break; }
                const bool can_move = at.in_sub ? drv->remaps_stack() : (best_i >= 0 && (at.nest == 0 || drv->remaps_stack()));
                if (!can_move) return false;
                std::vector<std::pair<uint16_t, uint8_t>> lp;
                uint16_t fallback = at.in_sub ? at_pos.voice_ptr[voice] : uint16_t(dest + offsets[size_t(best_i)] + events[size_t(best_i)].size);
                drv->live_state_writes(ram, at_pos, voice, fallback, remap, lp);
                if (std::getenv("BOOMSPC_DEBUG_EDIT")) for (auto& w : lp) std::fprintf(stderr, "%s write $%04X = %02X\n", target == Engine::kLiveOnly ? "live" : "image", w.first, w.second);
                for (auto& w : lp) eng.write_ram(w.first, &w.second, 1, target);
                return true;
            };
            if (!move_pointer(snap.ram, live, Engine::kLiveOnly)) moved_note = "";
            move_pointer(eng.image_ram(), image, Engine::kImageOnly);
            ++written;
        }
        eng.end_edit();
        eng.snapshot(snap);
        reparse(snap);
        r.ok = true;
        if (relocated) { char b[64]; std::snprintf(b, sizeof b, "stream rewritten at $%04X%s", dest, moved_note); r.msg = b; }
        else r.msg = restructure ? "stream rewritten in place" : written ? "patched in place" : "nothing changed";
        if (restructure && !*moved_note) r.msg += " (playback stays on the old bytes until the song restarts)";
        r.msg += note;
        return r;
    }

    std::vector<uint8_t> bytes = drv->serialize_track(events);
    uint16_t dest = t.addr;
    bool relocated = false;

    drv->retime(events);
    int ticks_total = 0;
    for (const Event& e : events) ticks_total = std::max(ticks_total, e.tick + e.duration);
    if (t.addr && !t.terminated && ticks_total >= pat.length_ticks && bytes.size() > 1 &&
        int(bytes.size()) - 1 <= int(t.end_addr) - int(t.addr))
        bytes.pop_back();

    if (!t.addr) {
        bool reclaimed = false;
        int at = find_space_or_reclaim(snap, int(bytes.size()), reclaimed);
        if (at < 0) { r.msg = "no free RAM for a new track, even in the other songs' data"; return r; }
        if (reclaimed) note += " (RAM of the other songs reclaimed)";
        dest = uint16_t(at);
        relocated = true;
    } else {
        int avail = int(t.end_addr) - int(t.addr);
        if (int(bytes.size()) > avail) {
            bool reclaimed = false;
            int at = find_space_or_reclaim(snap, int(bytes.size()), reclaimed);
            if (at < 0) { r.msg = "track grew and no free RAM was found, even in the other songs' data"; return r; }
            if (reclaimed) note += " (RAM of the other songs reclaimed)";
            dest = uint16_t(at);
            relocated = true;
        }
    }

    eng.begin_edit();
    eng.write_ram(dest, bytes.data(), bytes.size());
    if (relocated) {
        uint8_t ptr[2] = {uint8_t(dest & 0xFF), uint8_t(dest >> 8)};
        eng.write_ram(uint16_t(pat.addr + voice * 2), ptr, 2);
    }
    eng.end_edit();

    eng.snapshot(snap);
    reparse(snap);
    r.ok = true;
    r.msg = "written in place";
    if (relocated) { char b[32]; std::snprintf(b, sizeof b, "track relocated to $%04X", dest); r.msg = b; }
    r.msg += note;
    return r;
}
