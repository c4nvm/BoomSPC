// Tracker state: what the driver parser found in the loaded SPC, the live
// playback position, and the edit operations that turn tracker-style
// changes into byte writes.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "driver/nspc.hpp"
#include "driver/seq.hpp"
#include "engine.hpp"

struct Tracker {
    std::shared_ptr<seq::Driver> drv;   // null = no supported driver; a seek in progress holds its own reference
    std::vector<seq::Song> songs;
    int                    song_index = -1;
    seq::Position          pos;
    std::string            driver_name = "unknown";
    bool                   analyzed = false;
    int                    rescans_left = 0;      // fresh dumps need the driver to run first
    double                 next_rescan = 0;
    int64_t                last_samples = -1;     // audio clock at the last update (a restart rewinds it)
    // The driver can switch songs on its own (a rip taken with the next song
    // queued in the ports, a Follin slot change): update() re-picks the
    // current song twice a second unless the user chose one by hand, and
    // every frame for the first second, when a dump's stale pointers give
    // way to the song it was really about to start.
    bool                   song_pinned = false;
    double                 next_repick = 0;
    int                    fast_repicks = 0;
    // Game rips usually carry the whole music bank; with this on, songs other
    // than the current one count as free space when a track has to grow.
    mutable bool           reclaim_other_songs = false;

    void reset();
    // Full driver detection + song discovery from a RAM snapshot.
    void analyze(const EngineSnapshot& s);
    // Per-frame: refresh the live position (and rescan if nothing was found yet).
    // repick = false keeps the song while RAM is in flux (a seek in progress).
    void update(const EngineSnapshot& s, double now, bool repick = true);
    // Re-parses the current song after RAM edits.
    void reparse(const EngineSnapshot& s);
    // Keeps the song at `order_addr` selected (a restart boots the rip's own song; the user's stays).
    void pin_song(const EngineSnapshot& s, uint16_t order_addr);

    const seq::Song* song() const { return song_index >= 0 && song_index < int(songs.size()) ? &songs[song_index] : nullptr; }
    seq::Song*       song()       { return song_index >= 0 && song_index < int(songs.size()) ? &songs[song_index] : nullptr; }
    // The N-SPC layout when that is the loaded driver (instrument editor, samples).
    const nspc::Layout* nspc_layout() const;

    // ---- editing --------------------------------------------------------

    struct Result { bool ok = false; std::string msg; };

    // Replaces a track's playable events (everything up to used_events) with
    // `events`, writing in place when the bytes fit and relocating the track
    // to free RAM otherwise. Drivers whose streams are not contiguous patch
    // every event at its own address instead. Re-parses afterwards.
    Result write_track(Engine& eng, int pattern_idx, int voice, std::vector<seq::Event> events);

    static void remove_event(const seq::Driver& d, std::vector<seq::Event>& ev, int index);
    // Argument bytes for a new `op` at `tick` on `voice`: copied from the
    // nearest earlier use of the same command on that voice, else from any
    // voice of the pattern, else the driver's own event bytes stay zero.
    // Returns true when something was found to copy.
    static bool default_args(const seq::Driver& d, const seq::Pattern& pat, int voice, int tick, uint8_t* bytes, int size);
    // Free RAM for `need` bytes: without touching other songs first, then
    // (when `reclaim_other_songs` is off) by taking their bytes anyway so an
    // edit never fails for lack of space; `reclaimed` reports that fallback.
    int  find_space_or_reclaim(const EngineSnapshot& s, int need, bool& reclaimed, int* run_len = nullptr);

    // Largest run of zero bytes that is not stack, echo buffer, directory or
    // sample data. Returns -1 if nothing big enough exists.
    static int extent_of(const seq::Track& t);
    void release_bytes(const EngineSnapshot& s, Engine& eng, uint16_t at, int len, const seq::Track* skip = nullptr);
    // Bytes a rewrite left behind while a voice was still playing them; freed
    // once every live pointer has left (flush_releases, called each frame).
    std::vector<std::pair<uint16_t, uint16_t>> pending_release;
    void flush_releases(const EngineSnapshot& s, Engine& eng);
    int find_free_space(const EngineSnapshot& s, int need, int* run_len = nullptr) const;
    // Free RAM as an edit would see it: total bytes and the largest run, with
    // and without the other songs' bytes. Costs a scan of RAM; call sparingly.
    struct FreeStats { int total = 0, largest = 0, total_reclaim = 0, largest_reclaim = 0; };
    FreeStats free_stats(const EngineSnapshot& s) const;
    std::vector<bool> free_map(const EngineSnapshot& s, uint8_t fill) const;
    bool bytes_free(const EngineSnapshot& s, int from, int len) const;
};
