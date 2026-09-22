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
    fast_repicks = 60;
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

void Tracker::update(const EngineSnapshot& s, double now, bool repick) {
    if (!analyzed || !s.loaded) return;
    if (drv && repick && !song_pinned && now >= next_repick) {
        next_repick = fast_repicks > 0 ? now : now + 0.5;
        if (fast_repicks > 0) --fast_repicks;
        int pick = drv->pick_current_song(s.ram, songs);
        if (pick >= 0 && pick != song_index) {
            song_index = pick;
            pos = seq::Position{};
        }
    }
    const bool restarted = s.sample_pairs < last_samples;
    last_samples = s.sample_pairs;
    if (song()) pos = drv->locate(s.ram, *song(), restarted ? nullptr : &pos);
    // No rescan mid-seek either: the audio thread is walking this song.
    if (!pos.valid && repick && rescans_left > 0 && now >= next_rescan) {
        --rescans_left;
        next_rescan = now + 0.75;
        int keep = rescans_left;
        const bool pinned = song_pinned;
        const uint16_t pinned_addr = song() ? song()->order_addr : 0;
        analyze(s);
        if (pinned) pin_song(s, pinned_addr);
        if (!pos.valid) rescans_left = keep;
    }
}

void Tracker::pin_song(const EngineSnapshot& s, uint16_t order_addr) {
    for (size_t i = 0; i < songs.size(); ++i)
        if (songs[i].order_addr == order_addr) {
            if (int(i) != song_index) { song_index = int(i); pos = drv ? drv->locate(s.ram, songs[i], nullptr) : seq::Position{}; }
            song_pinned = true;
            return;
        }
}

void Tracker::reparse(const EngineSnapshot& s) {
    seq::Song* sg = song();
    if (!sg || !drv) return;
    uint16_t addr = sg->order_addr;
    std::vector<seq::Song> fresh = drv->find_songs(s.ram, s.dsp);
    for (size_t i = 0; i < fresh.size(); ++i)
        if (fresh[i].order_addr == addr) { songs = std::move(fresh); song_index = int(i); break; }
    if (song_index < 0 || song_index >= int(songs.size()) || songs[size_t(song_index)].order_addr != addr) {
        songs = std::move(fresh);
        song_index = drv->pick_current_song(s.ram, songs);
    }
    if (song()) drv->locate(s.ram, *song(), nullptr);   // parsing the other songs may have left the driver on one of them
}

void Tracker::remove_event(const seq::Driver& d, std::vector<Event>& ev, int index) {
    if (index < 0 || index >= int(ev.size())) return;
    ev.erase(ev.begin() + index);
    d.retime(ev);
}

std::vector<bool> Tracker::hard_map(const EngineSnapshot& s) const {
    std::vector<bool> reserved(0x10000, false);
    int driver_end = 0x200;
    for (const seq::Song& sg : songs) driver_end = std::max(driver_end, int(sg.order_addr));
    for (const seq::Song& sg : songs) driver_end = std::min(driver_end, int(sg.order_addr));
    int lo = 0x200, hi = 0x10000;
    if (drv) drv->free_space_bounds(lo, hi);
    if (lo != 0x200) driver_end = std::min(driver_end, lo);
    for (int a = 0; a < driver_end; ++a) reserved[a] = true;
    const bool spc_space = !drv || drv->spc_ram_space();
    if (spc_space) for (int a = 0xFFC0; a < 0x10000; ++a) reserved[a] = true;
    int esa = s.dsp[0x6D] << 8, edl = s.dsp[0x7D] & 0x0F;
    if (drv) edl = std::clamp(drv->echo_length(s.ram, edl), 0, 15);
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
    int samples_end = 0;
    for (int start : starts) {
        int limit = 0x10000;
        for (int o : starts) if (o > start) limit = std::min(limit, o);
        brr::Info info = brr::scan(s.ram, uint16_t(start));
        int end = info.truncated || info.blocks > 0x1000 ? limit : std::min(limit, start + info.blocks * 9);
        for (int a = start; a < end; ++a) reserved[a] = true;
        samples_end = std::max(samples_end, end);
    }
    // Everything below the first song header counts as the driver's, but a
    // long run of $FF between the last sample and the songs is fill the
    // loader never wrote (Smart Ball keeps 10 KB there): hand it out, keeping
    // clear of the echo buffer and the directory. Zero runs stay reserved:
    // they may be the driver's own variables.
    if (spc_space && samples_end > 0 && samples_end < driver_end) {
        for (int a = samples_end; a < driver_end;) {
            if (s.ram[a] != 0xFF) { ++a; continue; }
            int b = a;
            while (b < driver_end && s.ram[b] == 0xFF) ++b;
            if (b - a >= 64)
                for (int k = a; k < b; ++k)
                    if (!(k >= esa && k < esa + echo_len) && !(k >= dir && k < dir + 0x400)) reserved[k] = false;
            a = b;
        }
    }
    return reserved;
}

std::vector<bool> Tracker::song_map(int song, bool parsed_only) const {
    std::vector<bool> used(0x10000, false);
    for (size_t si = 0; si < songs.size(); ++si) {
        if (song >= 0 && int(si) != song) continue;
        const seq::Song& sg = songs[si];
        for (int a = sg.order_addr; a < sg.order_end; ++a) used[a & 0xFFFF] = true;
        for (const seq::Pattern& p : sg.patterns) {
            for (int a = p.addr; a < p.addr + 16; ++a) used[a & 0xFFFF] = true;
            for (const seq::Track& t : p.tracks) {
                if (!t.addr || (parsed_only && t.truncated)) continue;
                for (const Event& e : t.events)
                    for (int k = 0; k < e.size; ++k) used[(e.addr + k) & 0xFFFF] = true;
                if (!t.terminated) used[t.end_addr] = true;
            }
        }
    }
    return used;
}

std::vector<bool> Tracker::free_map(const EngineSnapshot& s, uint8_t fill) const {
    const std::vector<bool> hard = hard_map(s);   // driver, echo, directory, samples: never handed out, even as another song's bytes
    std::vector<bool> reserved = hard;
    int lo = 0x200, hi = 0x10000;
    if (drv) drv->free_space_bounds(lo, hi);
    std::vector<bool> reclaimable(0x10000, false);
    for (size_t si = 0; si < songs.size(); ++si) {
        const seq::Song& sg = songs[si];
        const bool other = int(si) != song_index;
        auto mark = [&](int a) { a &= 0xFFFF; reserved[a] = true; if (other && !hard[a]) reclaimable[a] = true; };
        for (int a = sg.order_addr; a < sg.order_end; ++a) mark(a);
        for (const seq::Pattern& p : sg.patterns) {
            for (int a = p.addr; a < p.addr + 16; ++a) mark(a);
            for (const seq::Track& t : p.tracks) {
                if (!t.addr) continue;
                for (const Event& e : t.events)
                    for (int k = 0; k < e.size; ++k) mark(e.addr + k);
                if (!t.terminated) mark(t.end_addr);
            }
        }
    }
    if (song_index >= 0) {
        std::vector<bool> mine(0x10000, false);   // every byte of the current song
        auto walk = [&](const seq::Song& sg, auto&& f) {
            for (int a = sg.order_addr; a < sg.order_end; ++a) f(a);
            for (const seq::Pattern& p : sg.patterns) {
                for (int a = p.addr; a < p.addr + 16; ++a) f(a);
                for (const seq::Track& t : p.tracks)
                    for (const Event& e : t.events)
                        for (int k = 0; k < e.size; ++k) f(e.addr + k);
            }
        };
        walk(songs[size_t(song_index)], [&](int a) { mine[a & 0xFFFF] = true; reclaimable[a & 0xFFFF] = false; });
        // A song that shares bytes with the current one is the same music seen
        // from another entry point (a pattern of it listed as a song of its
        // own); handing out its bytes would cut holes in what is playing.
        for (size_t si = 0; si < songs.size(); ++si) {
            if (int(si) == song_index) continue;
            bool shared = false;
            walk(songs[si], [&](int a) { if (mine[a & 0xFFFF]) shared = true; });
            if (shared) walk(songs[si], [&](int a) { reclaimable[a & 0xFFFF] = false; });
        }
    }

    std::vector<bool> sacrificial(0x10000, false);
    if (drv) {
        std::vector<std::pair<uint16_t, uint16_t>> extra;
        drv->reclaimable_ranges(s.ram, extra);
        for (auto& r : extra) for (int a = r.first; a < r.second && a < 0x10000; ++a) if (!reserved[a] || reclaimable[a]) sacrificial[a] = true;
    }
    std::vector<bool> free(0x10000, false);
    for (int a = lo; a < hi; ++a)
        free[size_t(a)] = (!reserved[a] && (s.ram[a] == 0 || s.ram[a] == fill)) || (reclaim_other_songs && reclaimable[a]) || sacrificial[a];
    return free;
}

int Tracker::find_free_space(const EngineSnapshot& s, int need, int* run_len) const {
    const bool reclaim = reclaim_other_songs;
    for (int pass = 0; pass < (reclaim ? 2 : 1); ++pass) {
        reclaim_other_songs = pass == 1;
        for (uint8_t fill : {uint8_t(0x00), uint8_t(0xFF)}) {
            const std::vector<bool> free = free_map(s, fill);
            int best = -1, best_len = 0, run = 0;
            for (int a = 0; a < 0x10000; ++a) {
                if (free[size_t(a)]) ++run; else run = 0;
                if (run > best_len) { best_len = run; best = a - run + 1; }
            }
            if (std::getenv("BOOMSPC_DEBUG_FREE")) std::fprintf(stderr, "find_free_space(%d): best run %d at $%04X (fill %02X%s)\n", need, best_len, best, fill, pass ? ", reclaiming" : "");
            if (best_len >= need + 2) { reclaim_other_songs = reclaim; if (run_len) *run_len = best_len - 2; return best + 1; }
        }
    }
    reclaim_other_songs = reclaim;
    return -1;
}

void Tracker::flush_releases(const EngineSnapshot& s, Engine& eng) {
    if (pending_release.empty() || !drv) return;
    for (size_t i = 0; i < pending_release.size();) {
        const auto [lo, hi] = pending_release[i];
        bool busy = false;
        for (int v = 0; v < 8 && !busy; ++v) busy = pos.valid && pos.voice_ptr[v] >= lo && pos.voice_ptr[v] <= hi;
        if (busy) { ++i; continue; }
        eng.begin_edit();
        release_bytes(s, eng, lo, hi - lo);
        eng.end_edit();
        pending_release.erase(pending_release.begin() + long(i));
    }
}

Tracker::FreeStats Tracker::free_stats(const EngineSnapshot& s) const {
    FreeStats st;
    const bool keep = reclaim_other_songs;
    for (int pass = 0; pass < 2; ++pass) {
        reclaim_other_songs = pass == 1;
        int total = 0, largest = 0;
        for (uint8_t fill : {uint8_t(0x00), uint8_t(0xFF)}) {
            const std::vector<bool> free = free_map(s, fill);
            int t = 0, run = 0, best = 0;
            for (int a = 0; a < 0x10000; ++a) {
                if (free[size_t(a)]) { ++t; ++run; best = std::max(best, run); } else run = 0;
            }
            total = std::max(total, t); largest = std::max(largest, best);
        }
        if (pass) { st.total_reclaim = total; st.largest_reclaim = largest; } else { st.total = total; st.largest = largest; }
    }
    reclaim_other_songs = keep;
    return st;
}

bool Tracker::bytes_free(const EngineSnapshot& s, int from, int len) const {
    if (from + len > 0x10000) return false;
    for (uint8_t fill : {uint8_t(0x00), uint8_t(0xFF)}) {
        const std::vector<bool> free = free_map(s, fill);
        bool ok = true;
        for (int a = from; a < from + len && ok; ++a) ok = free[size_t(a)];
        if (ok) return true;
    }
    return false;
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

int Tracker::find_space_or_reclaim(const EngineSnapshot& s, int need, bool& reclaimed, int* run_len) {
    reclaimed = false;
    int at = find_free_space(s, need, run_len);
    if (at >= 0 || reclaim_other_songs) return at;
    reclaim_other_songs = true;
    at = find_free_space(s, need, run_len);
    reclaim_other_songs = false;
    reclaimed = at >= 0;
    if (reclaimed) reclaim_other_songs = true;
    return at;
}

int Tracker::extent_of(const seq::Track& t) {
    int extent = 0;
    uint16_t next = t.addr;
    for (const Event& e : t.events) {
        if (e.in_sub) continue;
        if (e.addr != next) break;
        extent += e.size; next = uint16_t(next + e.size);
    }
    return extent;
}

// Zeroes the bytes a relocated stream left behind, except any that other
// tracks still play, so the space can be handed out again.
void Tracker::release_bytes(const EngineSnapshot& s, Engine& eng, uint16_t at, int len, const seq::Track* skip) {
    (void)s;
    std::vector<bool> used(0x10000, false);
    auto replaced = [&](const seq::Track& t) { return skip ? &t == skip : t.addr == at; };
    // Only the events a track plays count: an N-SPC track without its own
    // terminator parses on through its neighbours' bytes.
    for (const seq::Song& sg : songs)
        for (const seq::Pattern& p : sg.patterns)
            for (const seq::Track& t : p.tracks) {
                if (!t.addr || replaced(t)) continue;
                const size_t n = t.used_events > 0 ? std::min<size_t>(size_t(t.used_events), t.events.size()) : t.events.size();
                for (size_t i = 0; i < n; ++i) for (int k = 0; k < t.events[i].size; ++k) used[(t.events[i].addr + k) & 0xFFFF] = true;
            }
    for (const seq::Song& sg : songs)
        for (const seq::Pattern& p : sg.patterns)
            for (const seq::Track& t : p.tracks)
                if (replaced(t)) for (const Event& e : t.events) if (e.in_sub) for (int k = 0; k < e.size; ++k) used[(e.addr + k) & 0xFFFF] = true;
    const uint8_t zero = 0;
    int zeroed = 0;
    for (int a = at; a < at + len && a < 0x10000; ++a)
        if (!used[size_t(a)]) { eng.write_ram(uint16_t(a), &zero, 1); ++zeroed; }
    if (std::getenv("BOOMSPC_DEBUG_FREE")) std::fprintf(stderr, "release_bytes %04X+%d: zeroed %d\n", at, len, zeroed);
}

std::vector<bool> Tracker::songs_hit(uint16_t at, int len) const {
    std::vector<bool> hit;
    for (size_t si = 0; si < songs.size(); ++si) {
        if (int(si) == song_index) continue;
        std::vector<bool> m = song_map(int(si), true);   // a truncated track walked bytes that are not the song's
        bool overlap = false;
        for (int a = at; a < at + len && a < 0x10000 && !overlap; ++a) overlap = m[size_t(a)];
        if (!overlap) continue;
        if (hit.empty()) hit = std::move(m);
        else for (int a = 0; a < 0x10000; ++a) if (m[size_t(a)]) hit[size_t(a)] = true;
    }
    return hit;
}

// A block written over another song leaves the rest of that song unreadable
// (reserved by nothing, but not zero either, so never handed out); zero it.
void Tracker::sweep_dead_songs(const EngineSnapshot& s, Engine& eng, const std::vector<bool>& victims, uint16_t at, int len) {
    if (victims.empty()) return;
    const std::vector<bool> used = song_map(), hard = hard_map(s);
    const uint8_t zero = 0;
    int zeroed = 0;
    for (int a = 0; a < 0x10000; ++a) {
        if (!victims[size_t(a)] || used[size_t(a)] || hard[size_t(a)] || (a >= at && a < at + len) || s.ram[a] == 0) continue;
        eng.write_ram(uint16_t(a), &zero, 1); ++zeroed;
    }
    if (std::getenv("BOOMSPC_DEBUG_FREE")) std::fprintf(stderr, "sweep_dead_songs: zeroed %d\n", zeroed);
}

Tracker::Result Tracker::write_track(Engine& eng, int pattern_idx, int voice, std::vector<Event> events) {
    Result r;
    seq::Song* sg = song();
    if (!sg || !drv || pattern_idx < 0 || pattern_idx >= int(sg->patterns.size())) { r.msg = "no pattern"; return r; }
    song_pinned = true;
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
        const bool piecewise = drv->piecewise_streams();   // the block holds only the pieces that changed
        std::vector<std::pair<uint16_t, uint16_t>> old;    // [lo, hi) ranges a piecewise block replaces
        if (restructure) {
            const int extent = extent_of(t);
            bytes = drv->serialize_relocated(events, t.addr, &offsets);
            if (piecewise) drv->replaced_ranges(old);
            auto fits_at = [&](int lo, int have) { return int(bytes.size()) <= have || bytes_free(snap, lo + have, int(bytes.size()) - have); };
            const bool fits = !piecewise ? fits_at(t.addr, extent)
                                         : old.size() == 1 && fits_at(old[0].first, int(old[0].second) - int(old[0].first));   // one piece: over its old bytes
            if (piecewise && fits) dest = old[0].first;
            if (!fits) {
                bool reclaimed = false;
                int at = find_space_or_reclaim(snap, int(bytes.size()), reclaimed);
                if (at < 0) { r.msg = "stream grew and no free RAM was found, even in the other songs' data"; return r; }
                if (reclaimed) note += " (RAM of the other songs reclaimed)";
                dest = uint16_t(at);
            }
            if (piecewise || !fits) { relocated = true; bytes = drv->serialize_relocated(events, dest, &offsets); }
        }
        seq::Position live = restructure ? drv->locate(snap.ram, *sg, &pos) : seq::Position{};
        seq::Position image = restructure ? drv->locate(eng.image_ram(), *sg, nullptr) : seq::Position{};
        const char* moved_note = " (playing it)";
        const std::vector<bool> victims = relocated ? songs_hit(dest, int(bytes.size())) : std::vector<bool>{};
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
                if (!at_pos.valid) return true;
                if (at_pos.voice_event[voice] < 0 || at_pos.voice_event[voice] >= int(t.events.size())) {
                    // Caught between commands: keep the pointer, remap the stack.
                    if (!at_pos.voice_ptr[voice] || !drv->remaps_stack()) return true;
                    std::vector<std::pair<uint16_t, uint8_t>> lp;
                    drv->live_state_writes(ram, at_pos, voice, at_pos.voice_ptr[voice], remap, lp);
                    for (auto& w : lp) eng.write_ram(w.first, &w.second, 1, target);
                    return true;
                }
                const Event& at = t.events[size_t(at_pos.voice_event[voice])];
                int best_i = -1;
                bool same_end = false;   // a main event ends where the current one does (an unrolled copy of it)
                auto usable = [&](size_t i) { return !events[i].in_sub && events[i].duration > 0 && offsets[i] >= 0; };
                for (size_t i = 0; i < events.size() && best_i < 0 && at.addr; ++i)
                    if (usable(i) && events[i].addr == at.addr) best_i = int(i);
                for (size_t i = 0; i < events.size() && best_i < 0; ++i)
                    if (usable(i) && events[i].tick + events[i].duration == at.tick + at.duration) { best_i = int(i); same_end = true; }
                for (size_t i = 0; i < events.size() && best_i < 0; ++i)
                    if (usable(i) && events[i].tick >= at.tick) best_i = int(i);
                for (size_t i = events.size(); i-- > 0 && best_i < 0;)
                    if (usable(i)) best_i = int(i);
                const bool shared_stays = at.in_sub && !same_end;   // inside shared bytes that keep playing where they are
                const bool can_move = shared_stays ? drv->remaps_stack() : (best_i >= 0 && (at.nest == 0 || drv->remaps_stack()));
                if (std::getenv("BOOMSPC_DEBUG_EDIT")) std::fprintf(stderr, "%s move: at event %d tick %d+%d best %d can_move %d\n", target == Engine::kLiveOnly ? "live" : "image", at_pos.voice_event[voice], at.tick, at.duration, best_i, can_move);
                if (!can_move) return false;
                std::vector<std::pair<uint16_t, uint8_t>> lp;
                uint16_t fallback = shared_stays ? at_pos.voice_ptr[voice] : uint16_t(dest + offsets[size_t(best_i)] + events[size_t(best_i)].size);
                drv->live_state_writes(ram, at_pos, voice, fallback, remap, lp);
                if (std::getenv("BOOMSPC_DEBUG_EDIT")) for (auto& w : lp) std::fprintf(stderr, "%s write $%04X = %02X\n", target == Engine::kLiveOnly ? "live" : "image", w.first, w.second);
                for (auto& w : lp) eng.write_ram(w.first, &w.second, 1, target);
                return true;
            };
            if (!move_pointer(snap.ram, live, Engine::kLiveOnly)) moved_note = "";
            move_pointer(eng.image_ram(), image, Engine::kImageOnly);
            if (!relocated && !piecewise) {
                // Rewritten in place and shorter: the tail is free now.
                const int extent = extent_of(t), tail = extent - int(bytes.size());
                if (tail > 0) release_bytes(snap, eng, uint16_t(dest + bytes.size()), tail, &t);
            }
            if (relocated && *moved_note) {
                if (!piecewise) old.push_back({t.addr, uint16_t(t.addr + extent_of(t))});
                for (auto& o : old) {   // except what the block itself now occupies
                    const int lo = std::max(int(o.first), int(dest) + int(bytes.size())), hi = int(o.second);
                    if (o.first < dest) release_bytes(snap, eng, o.first, std::min(int(dest), hi) - int(o.first), &t);
                    if (lo < hi) release_bytes(snap, eng, uint16_t(lo), hi - lo, &t);
                }
                std::vector<std::pair<uint16_t, uint8_t>> ptr;   // a pointer may live inside the released bytes
                drv->track_pointer_writes(*sg, pattern_idx, voice, dest, ptr);
                for (auto& w : ptr) eng.write_ram(w.first, &w.second, 1);
            }
            ++written;
        }
        if (!victims.empty()) { eng.snapshot(snap); reparse(snap); sweep_dead_songs(snap, eng, victims, dest, int(bytes.size())); }
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

    std::vector<int> offsets;   // where each event landed in `bytes`
    std::vector<uint8_t> bytes = nspc_layout() ? nspc::serialize_track(events, &offsets) : drv->serialize_track(events);
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
        if (int(bytes.size()) > avail && !bytes_free(snap, t.end_addr, int(bytes.size()) - avail)) {   // grows into the free bytes after it if it can
            bool reclaimed = false;
            int run = 0;
            int at = find_space_or_reclaim(snap, int(bytes.size()), reclaimed, &run);
            if (at < 0) { r.msg = "track grew and no free RAM was found, even in the other songs' data"; return r; }
            if (reclaimed) note += " (RAM of the other songs reclaimed)";
            // A track pointer can double as another voice's bytes (Inindo points
            // voice 0 at its own pattern header, so the pointers play as a
            // stream). Slide along the run until the pattern still parses.
            if (const nspc::Layout* L = nspc_layout()) {
                std::vector<uint8_t> ram(snap.ram, snap.ram + 0x10000);
                auto parses = [&](int cand) {
                    ram[(pat.addr + voice * 2) & 0xFFFF] = uint8_t(cand & 0xFF);
                    ram[(pat.addr + voice * 2 + 1) & 0xFFFF] = uint8_t(cand >> 8);
                    std::memcpy(ram.data() + cand, bytes.data(), bytes.size());
                    const nspc::Pattern p = nspc::parse_pattern(ram.data(), *L, pat.addr);
                    for (int v = 0; v < 8; ++v) {
                        const seq::Track& tr = p.tracks[v];
                        if (!tr.addr || (v != voice && !pat.tracks[v].addr)) continue;
                        if (tr.truncated || tr.events.empty() || tr.events.back().type != EventType::End) return false;
                    }
                    return true;
                };
                int chosen = -1;
                for (int cand = at; cand + int(bytes.size()) <= at + run && chosen < 0; ++cand) {
                    std::memcpy(ram.data() + at, snap.ram + at, size_t(run));
                    if (parses(cand)) chosen = cand;
                }
                if (chosen < 0) { r.msg = "no place in free RAM keeps this pattern readable (its header doubles as a track)"; return r; }
                at = chosen;
            }
            dest = uint16_t(at);
            relocated = true;
        }
    }

    const std::vector<bool> victims = relocated ? songs_hit(dest, int(bytes.size())) : std::vector<bool>{};
    eng.begin_edit();
    eng.write_ram(dest, bytes.data(), bytes.size());
    if (relocated) {
        std::vector<std::pair<uint16_t, uint8_t>> ptr;
        drv->track_pointer_writes(*sg, pattern_idx, voice, dest, ptr);
        if (ptr.empty()) { uint8_t w[2] = {uint8_t(dest & 0xFF), uint8_t(dest >> 8)}; eng.write_ram(uint16_t(pat.addr + voice * 2), w, 2); }
        for (auto& w : ptr) eng.write_ram(w.first, &w.second, 1);
    }
    // A voice reading this track keeps its place in the new bytes: the
    // pointer sits on an event boundary, and the boundaries moved. The
    // running emulator and the image (where the rip was dumped, which is
    // what restart and export play) are separate positions. A pointer
    // inside a shared body the edit unrolled lands on the copy; the stale
    // repeat count only matters at a track end, and an unrolled iteration
    // is followed by the remaining calls or is the last one.
    if (!offsets.empty()) {
        seq::Driver::Remap remap;
        for (size_t i = 0; i < events.size(); ++i) {
            if (events[i].in_sub || events[i].addr == 0 || offsets[i] < 0) continue;
            remap.pairs.push_back({events[i].addr, uint16_t(dest + offsets[i])});
            remap.pairs.push_back({uint16_t(events[i].addr + events[i].size), uint16_t(dest + offsets[i] + events[i].size)});
        }
        auto move_pointer = [&](const uint8_t* ram, const seq::Position& at, Engine::WriteTarget target) {
            if (!at.valid || at.order_index < 0 || at.order_index >= int(sg->orders.size())) return;
            if (sg->orders[size_t(at.order_index)].pattern_addr != pat.addr) return;
            const uint16_t p = at.voice_ptr[voice];
            int mapped = remap.find(p);
            if (mapped < 0 && at.voice_event[voice] >= 0 && at.voice_event[voice] < int(t.events.size())) {
                // Inside a body the edit copied out (the copy has no address
                // of its own): the event at the same tick, then behind it.
                const Event& cur = t.events[size_t(at.voice_event[voice])];
                if (p == uint16_t(cur.addr + cur.size))
                    for (size_t i = 0; i < events.size() && mapped < 0; ++i)
                        if (!events[i].in_sub && offsets[i] >= 0 && events[i].tick == cur.tick && events[i].type == cur.type && events[i].size == cur.size)
                            mapped = dest + offsets[i] + events[i].size;
            }
            if (mapped < 0 || mapped == p) return;
            if (std::getenv("BOOMSPC_DEBUG_EDIT")) std::fprintf(stderr, "%s v%d pointer %04X -> %04X\n", target == Engine::kLiveOnly ? "live" : "image", voice, at.voice_ptr[voice], mapped);
            std::vector<std::pair<uint16_t, uint8_t>> lp;
            drv->live_state_writes(ram, at, voice, uint16_t(mapped), remap, lp);
            for (auto& w : lp) eng.write_ram(w.first, &w.second, 1, target);
        };
        move_pointer(snap.ram, drv->locate(snap.ram, *sg, &pos), Engine::kLiveOnly);
        move_pointer(eng.image_ram(), drv->locate(eng.image_ram(), *sg, nullptr), Engine::kImageOnly);
    }
    // What the old bytes no longer hold goes back to the free pool: the whole
    // track when it moved, the tail when it shrank in place. A voice still
    // reading there keeps its bytes until it has left (flush_releases).
    if (t.addr && t.end_addr > t.addr) {
        const uint16_t lo = relocated ? t.addr : uint16_t(dest + bytes.size()), hi = t.end_addr;
        if (lo < hi) {
            const uint16_t live = pos.valid ? pos.voice_ptr[voice] : 0;
            if (std::getenv("BOOMSPC_DEBUG_FREE")) std::fprintf(stderr, "release %04X-%04X (live %04X)\n", lo, hi, live);
            if (live >= lo && live <= hi) pending_release.push_back({lo, hi});
            else release_bytes(snap, eng, lo, hi - lo, &t);
        }
    }
    if (!victims.empty()) { eng.snapshot(snap); reparse(snap); sweep_dead_songs(snap, eng, victims, dest, int(bytes.size())); }
    eng.end_edit();

    eng.snapshot(snap);
    reparse(snap);
    r.ok = true;
    r.msg = "written in place";
    if (relocated) { char b[32]; std::snprintf(b, sizeof b, "track relocated to $%04X", dest); r.msg = b; }
    r.msg += note;
    return r;
}
