// parsecheck: plays an SPC in the emulator and checks that the sequence of
// notes the driver actually fetches matches what the BoomSPC parser predicts
// for every voice. Works for drivers whose songs are one stream per voice
// (Follin, AKAO); N-SPC's order-list playback is not modelled here.
//   parsecheck file.spc [seconds]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "SNES_SPC.h"
#include "driver/seq.hpp"
#include "spc_file.hpp"

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: parsecheck file.spc [seconds]\n"); return 2; }
    SpcFile f;
    if (std::string err = load_spc_file(argv[1], f); !err.empty()) { std::fprintf(stderr, "%s\n", err.c_str()); return 2; }
    const double secs = argc > 2 ? std::atof(argv[2]) : 20;
    SNES_SPC spc;
    spc.init();
    spc.load_spc(f.data.data(), long(f.data.size()));
    spc.play(4096, nullptr);   // dumps taken before the driver started have no pointers yet
    uint8_t dsp[128];
    for (int i = 0; i < 128; ++i) dsp[i] = uint8_t(spc.dsp_ref().read(i));
    std::unique_ptr<seq::Driver> D = seq::detect_driver(spc.ram(), dsp);
    if (!D) { std::printf("%s: no driver\n", argv[1]); return 1; }
    const bool order_list = !D->in_place_only();
    std::vector<seq::Song> songs = D->find_songs(spc.ram(), dsp);
    int cur = D->pick_current_song(spc.ram(), songs);
    if (cur < 0) { std::printf("%s: no song\n", argv[1]); return 1; }
    seq::Position p0 = D->locate(spc.ram(), songs[size_t(cur)], nullptr);
    const seq::Pattern& p = songs[size_t(cur)].patterns[0];

    std::vector<uint16_t> seen[8];
    uint16_t prev[8], cand[8];
    for (int v = 0; v < 8; ++v) prev[v] = cand[v] = p0.voice_ptr[v];
    for (int i = 0, n = int(secs * 1000); i < n; ++i) {
        spc.play(64, nullptr);
        for (int v = 0; v < 8; ++v) {
            uint16_t ptr = D->live_ptr(spc.ram(), p0, v);
            if (ptr != prev[v] && ptr == cand[v]) { seen[v].push_back(ptr); prev[v] = ptr; }
            cand[v] = ptr;
        }
    }
    int bad = 0, checked = 0;
    const seq::Song& song = songs[size_t(cur)];
    for (int v = 0; v < 8; ++v) {
        const seq::Track& t = p.tracks[v];
        if (seen[v].empty()) continue;
        std::vector<uint16_t> parsed;
        size_t loop_pi = 0;
        bool loops = t.loops;
        uint16_t first_addr = t.addr;
        auto add_track = [&](const seq::Track& tr) {
            for (size_t ei = 0; ei < tr.events.size(); ++ei) {
                const seq::Event& e = tr.events[ei];
                if (!order_list && tr.loops && int(ei) == std::max(0, tr.loop_event)) loop_pi = parsed.size();
                if (e.duration > 0 || e.type == seq::EventType::End) {
                    uint16_t a = e.type == seq::EventType::End ? uint16_t(e.addr + D->end_park_offset()) : uint16_t(e.addr + e.size);
                    if (parsed.empty() || parsed.back() != a) parsed.push_back(a);
                }
            }
        };
        if (!order_list) {
            if (!t.addr) continue;
            add_track(t);
        } else {
            // Walk the order list (and its loop) far enough to cover the run.
            loops = false;
            first_addr = 0;
            int oi = std::max(0, p0.order_index), passes = 0;
            while (oi < int(song.orders.size()) && parsed.size() < 20000 && passes < 16) {
                int pi = song.pattern_index(song.orders[size_t(oi)].pattern_addr);
                if (pi >= 0) {
                    const seq::Track& tr = song.patterns[size_t(pi)].tracks[v];
                    if (tr.addr) { if (!first_addr) first_addr = tr.addr; add_track(tr); }
                    else if (parsed.empty() || parsed.back() != 0) parsed.push_back(0);   // the voice rests this pattern
                }
                if (++oi >= int(song.orders.size()) && song.loop_to >= 0) { oi = song.loop_to; ++passes; }
            }
        }
        if (parsed.empty()) continue;
        if (std::getenv("PARSECHECK_VERBOSE")) {
            std::printf("v%d live:  ", v); for (size_t k = 0; k < std::min<size_t>(16, seen[v].size()); ++k) std::printf("%04X ", seen[v][k]); std::printf("\n");
            std::printf("v%d parse: ", v); for (size_t k = 0; k < std::min<size_t>(16, parsed.size()); ++k) std::printf("%04X ", parsed[k]); std::printf("\n");
        }
        if (seen[v].size() > 1 && seen[v][0] == first_addr) seen[v].erase(seen[v].begin());
        // Align on whichever occurrence of the first live value matches the longest.
        auto run_from = [&](size_t start, size_t* fail_k, size_t* fail_pi) {
            size_t pi = start, matched = 0;
            for (size_t k = 0; k < seen[v].size(); ++k) {
                if (parsed[pi] != seen[v][k]) {
                    if (seen[v][k] == 0 || ((seen[v][k] & 0xFF) == (parsed[pi] & 0xFF) && (seen[v][k] >> 8) == 0)) return std::make_pair(matched, true);
                    if (k + 1 < seen[v].size() && seen[v][k + 1] == parsed[pi]) continue;   // a value caught mid-event
                    if (pi + 1 < parsed.size() && seen[v][k] == parsed[pi + 1]) { ++pi; ++matched; continue; }   // a parked position the sampling missed
                    if (pi + 1 < parsed.size() && k + 1 < seen[v].size() && seen[v][k + 1] == parsed[pi + 1]) { ++pi; ++matched; continue; }   // both at once
                    if (fail_k) { *fail_k = k; *fail_pi = pi; }
                    return std::make_pair(matched, false);
                }
                ++matched;
                if (++pi >= parsed.size()) { if (!loops) return std::make_pair(matched, true); pi = loop_pi; if (pi < parsed.size() && parsed[pi] == seen[v][k]) ++pi; }
            }
            return std::make_pair(matched, true);
        };
        const std::vector<uint16_t> orig = seen[v];
        bool found = false, ok = false;
        size_t best_pi = 0, best_len = 0, best_k = 0, best_fail_pi = 0;
        for (int skip = 0; !ok && skip < 64 && !seen[v].empty(); ++skip) {   // the dump's own pointer may sit mid-command, or in a section an edit replaced
            if (skip) seen[v].erase(seen[v].begin());
            for (size_t k = 0; k < parsed.size(); ++k) {
                if (parsed[k] != seen[v][0]) continue;
                found = true;
                size_t fk = 0, fpi = 0;
                auto [len, done] = run_from(k, &fk, &fpi);
                if (done) { ok = true; best_pi = k; break; }
                if (len >= best_len) { best_len = len; best_pi = k; best_k = fk; best_fail_pi = fpi; }
            }
        }
        if (!found) { std::printf("%s v%d: live pointer %04X is not in the parse\n", argv[1], v, seen[v][0]); ++bad; continue; }
        ++checked;
        if (!ok) {
            const size_t k = best_k, pi = best_fail_pi;
            (void)best_pi;
            std::printf("%s v%d: diverged after %zu notes: live %04X, parsed %04X\n", argv[1], v, best_len, k < seen[v].size() ? seen[v][k] : 0, parsed[pi]);
            if (std::getenv("PARSECHECK_VERBOSE")) { std::printf("   all live:"); for (size_t j = 0; j < orig.size(); ++j) std::printf(" %04X", orig[j]); std::printf("\n"); }
            std::printf("   live:  "); for (size_t j = k >= 6 ? k - 6 : 0; j < std::min(seen[v].size(), k + 6); ++j) std::printf("%s%04X ", j == k ? ">" : "", seen[v][j]); std::printf("\n");
            std::printf("   parse: "); for (size_t j = pi >= 6 ? pi - 6 : 0; j < std::min(parsed.size(), pi + 6); ++j) std::printf("%s%04X ", j == pi ? ">" : "", parsed[j]); std::printf("\n");
            ++bad;
        }
    }
    if (!bad) std::printf("%s: OK, %d voices match over %.0f s (%s)\n", argv[1], checked, secs, D->name().c_str());
    return bad ? 1 : 0;
}
