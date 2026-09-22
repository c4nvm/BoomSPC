#include "ui.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "actions.hpp"
#include "crash.hpp"
#include "paths.hpp"
#include "snsf.hpp"
#include "file_dialog.hpp"
#include "fonts.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "theme.hpp"

namespace {
std::string dir_of(const std::string& path) {
    size_t k = path.find_last_of("/\\");
    return k == std::string::npos ? std::string() : path.substr(0, k);
}

std::string base_of(const std::string& path) {
    size_t k = path.find_last_of("/\\");
    return k == std::string::npos ? path : path.substr(k + 1);
}

void remember_dir(const std::string& path) {
    std::string d = dir_of(path);
    if (!d.empty()) std::snprintf(theme().last_dir, sizeof theme().last_dir, "%s", d.c_str());
}

bool load_into_app(App& app, SpcFile& file, const std::string& shown_name) {
    std::string err = app.engine.load(file);
    if (!err.empty()) { app.status = "Emulator rejected file: " + err; return false; }
    app.status = "Loaded " + shown_name;
    app.engine.snapshot(app.snap);
    app.tracker.reset();
    app.tracker.analyze(app.snap);
    app.heard.clear();
    app.view_order = 0; app.sel_voice = app.sel_row = app.sel_event = -1; app.sel_active = false; app.cur_nibble = 0;
    app.sel_instrument = -1; app.roll_ins = -1;
    if (app.tracker.drv) app.ticks_per_beat = app.tracker.drv->default_ticks_per_beat();
    if (app.tracker.song()) { std::string fit = app.fit_grid(); if (!fit.empty()) app.status += "; grid " + fit; }
    app.engine.play();
    if (app.show_updates) app.focus_sequencer = true;
    return true;
}

}

void App::open_file(const std::string& path) {
    SpcFile file;
    std::string err = load_spc_file(path, file);
    if (!err.empty()) { status = "Load failed: " + err; return; }
    std::strncpy(path_buf, path.c_str(), sizeof path_buf - 1);
    path_buf[sizeof path_buf - 1] = 0;
    if (!load_into_app(*this, file, file.title.empty() ? path : file.title)) return;
    source_path = path;
    project_path.clear();
    remember_dir(path);
    update_title();
}

void App::open_any(const std::string& path) {
    if (is_project_path(path)) open_project(path);
    else if (is_snsf_path(path)) open_snsf(path);
    else open_file(path);
}

void App::open_snsf(const std::string& path) {
    SnsfFile file;
    std::string err = load_snsf_file(path, file);
    if (!err.empty()) { status = "Load failed: " + err; return; }
    err = engine.load_snsf(file);
    if (!err.empty()) { status = "SNES emulation rejected the ROM: " + err; return; }
    std::strncpy(path_buf, path.c_str(), sizeof path_buf - 1);
    path_buf[sizeof path_buf - 1] = 0;
    const std::string name = !file.title.empty() ? file.title : path.substr(path.find_last_of("/\\") + 1);
    status = "Loaded " + name + " (SNSF: the sequencer runs on the SNES CPU; playback only)";
    engine.snapshot(snap);
    tracker.reset();
    tracker.analyze(snap);
    if (tracker.drv && tracker.song()) {
        const int bank = tracker.drv->song_bank(*tracker.song());
        if (bank >= 0) { engine.set_bank_window(bank); engine.snapshot(snap); tracker.reparse(snap); }
        status = "Loaded " + name + " (SNSF: " + tracker.drv->name() + ")";
    }
    view_order = 0; sel_voice = sel_row = sel_event = -1; sel_active = false; cur_nibble = 0;
    heard.clear(); muted.clear();
    if (tracker.drv) ticks_per_beat = tracker.drv->default_ticks_per_beat();
    fitted_song = -1;
    source_path = path;
    project_path.clear();
    remember_dir(path);
    update_title();
    engine.play();
}

bool App::open_project(const std::string& path) {
    ProjectMeta meta;
    std::vector<uint8_t> bytes;
    std::string err = load_project(path, meta, bytes);
    if (err.empty()) {
        SpcFile file;
        err = parse_spc(std::move(bytes), file);
        if (err.empty()) {
            file.path = meta.source;
            if (!load_into_app(*this, file, base_of(path))) return false;
        }
    }
    if (!err.empty()) { status = "Project load failed: " + err; return false; }
    source_path = meta.source;
    project_path = path;
    std::strncpy(path_buf, path.c_str(), sizeof path_buf - 1);
    path_buf[sizeof path_buf - 1] = 0;
    int song = -1;
    if (meta.song_addr >= 0)
        for (size_t i = 0; i < tracker.songs.size(); ++i) if (int(tracker.songs[i].order_addr) == meta.song_addr) song = int(i);
    if (song < 0 && meta.song_addr < 0 && meta.song >= 0 && meta.song < int(tracker.songs.size())) song = meta.song;
    if (song >= 0) { tracker.song_index = song; tracker.song_pinned = true; }
    if (meta.ticks_per_row > 0) ticks_per_row = std::clamp(meta.ticks_per_row, 1, 96);
    if (meta.ticks_per_beat > 0) ticks_per_beat = std::clamp(meta.ticks_per_beat, 1, 192);
    octave = std::clamp(meta.octave, 1, 6);
    theme().edit_step = std::clamp(meta.edit_step, 0, 64);
    view_order = meta.view_order;
    tracker.reclaim_other_songs = meta.reclaim;
    remember_dir(path);
    update_title();
    return true;
}

bool App::save_project_to(const std::string& path) {
    if (!engine.loaded()) { status = "nothing to save"; return false; }
    ProjectMeta meta;
    meta.source = source_path;
    meta.song = tracker.song_index;
    meta.song_addr = tracker.song() ? int(tracker.song()->order_addr) : -1;
    meta.ticks_per_row = ticks_per_row;
    meta.ticks_per_beat = ticks_per_beat;
    meta.octave = octave;
    meta.edit_step = theme().edit_step;
    meta.view_order = view_order;
    meta.reclaim = tracker.reclaim_other_songs;
    std::string err = save_project(path, meta, engine.file_image());
    if (!err.empty()) { status = "Save failed: " + err; return false; }
    project_path = path;
    engine.mark_clean();
    status = "saved " + base_of(path);
    remember_dir(path);
    update_title();
    return true;
}

void App::update_title() {
    std::string name = !project_path.empty() ? base_of(project_path) : !source_path.empty() ? base_of(source_path) : "";
    window_title = name.empty() ? "BoomSPC" : name + " - BoomSPC";
}

void App::dialog_open() {
    std::string start = theme().last_dir[0] ? theme().last_dir : dir_of(source_path);
    std::string path = filedlg::open_file("Open SPC, SNSF or BoomSPC project",
                                          {{"SPC, SNSF and BoomSPC projects", "*.spc *.SPC *.boomspc *.snsf *.minisnsf"}, {"SPC files", "*.spc *.SPC"},
                                           {"SNSF sets", "*.snsf *.minisnsf"}, {"BoomSPC projects", "*.boomspc"}, {"All files", "*"}}, start);
    if (!path.empty()) open_any(path);
    else if (!filedlg::last_error().empty()) { status = filedlg::last_error(); show_player = true; focus_path_box = true; }
}

void App::dialog_save_project(bool always_ask) {
    if (!engine.loaded()) { status = "nothing to save"; return; }
    if (!always_ask && !project_path.empty()) { save_project_to(project_path); return; }
    std::string suggested = !project_path.empty() ? project_path
                          : (theme().last_dir[0] ? std::string(theme().last_dir) : dir_of(source_path)) + "/" +
                            (source_path.empty() ? std::string("song") : base_of(source_path).substr(0, base_of(source_path).find_last_of('.'))) + ".boomspc";
    std::string path = filedlg::save_file("Save BoomSPC project", {{"BoomSPC projects", "*.boomspc"}}, suggested);
    if (path.empty()) { if (!filedlg::last_error().empty()) status = filedlg::last_error(); return; }
    if (!is_project_path(path)) path += ".boomspc";
    save_project_to(path);
}

void App::dialog_export_spc() {
    if (!engine.loaded()) return;
    std::string base = source_path.empty() ? std::string("edited") : base_of(source_path).substr(0, base_of(source_path).find_last_of('.'));
    const bool snsf = engine.snsf();
    std::string suggested = (theme().last_dir[0] ? std::string(theme().last_dir) : dir_of(source_path)) + "/" + base + (snsf ? " (edited).snsf" : " (edited).spc");
    std::string path = snsf ? filedlg::save_file("Export SNSF", {{"SNSF files", "*.snsf"}}, suggested)
                            : filedlg::save_file("Export SPC", {{"SPC files", "*.spc"}}, suggested);
    if (path.empty()) { if (!filedlg::last_error().empty()) { status = filedlg::last_error(); export_spc_open = true; } return; }
    std::string err = snsf ? engine.export_snsf(path) : engine.export_spc(path);
    status = err.empty() ? "wrote " + base_of(path) : err;
    remember_dir(path);
}

void App::after_edit() {
    tracker.song_pinned = true;   // the song being edited must not be swapped out from under the user
    engine.snapshot(snap);
    tracker.reparse(snap);
}

double App::song_bpm() const {
    if (!tracker.drv || !engine.loaded() || ticks_per_beat <= 0) return 0;
    double tps = tracker.drv->ticks_per_second(snap.ram);
    return tps > 0 ? tps * 60.0 / ticks_per_beat : 0;
}

bool App::preview_regs_heard(const seq::Track& track, int voice, int ev_index, int tick, int semitone, int ins, uint8_t regs[8]) const {
    const seq::Driver& D = *tracker.drv;
    if (!D.preview_regs(snap.ram, D.note_byte(semitone), ins >= 0 ? ins : 0, regs)) return false;
    if (ev_index >= 0 && ev_index < int(track.events.size())) {
        const seq::Event& e = track.events[size_t(ev_index)];
        auto it = heard.find(heard_key(voice, e.addr));
        if (it != heard.end() && it->second.semitone == semitone) {
            if (std::getenv("BOOMSPC_DEBUG_PREVIEW")) std::fprintf(stderr, "preview v%d: note at $%04X as heard: pitch %04X (nominal %04X) srcn %02X\n", voice, e.addr, it->second.regs[2] | (it->second.regs[3] << 8), regs[2] | (regs[3] << 8), it->second.regs[4]);
            for (int r = 2; r < 8; ++r) regs[r] = it->second.regs[r];
            return true;
        }
    }
    int best = -1, best_dist = 1 << 30;
    for (int i = 0; i < track.used_events; ++i) {
        const seq::Event& e = track.events[size_t(i)];
        if (e.type != seq::EventType::Note || e.duration <= 0) continue;
        auto it = heard.find(heard_key(voice, e.addr));
        if (it == heard.end() || it->second.semitone != D.event_semitone(e)) continue;
        int dist = std::abs(e.tick - tick);
        if (dist < best_dist) { best_dist = dist; best = i; }
    }
    if (best >= 0) {
        const seq::Event& e = track.events[size_t(best)];
        const Heard& h = heard.at(heard_key(voice, e.addr));
        uint8_t nom[8];
        int ins_there = ins;
        for (int i = best - 1; i >= 0 && ins >= 0; --i) if (track.events[size_t(i)].type == seq::EventType::Command && D.is_instrument_cmd(track.events[size_t(i)].b[0])) { ins_there = D.instrument_arg(track.events[size_t(i)]); break; }
        if (D.preview_regs(snap.ram, D.note_byte(D.event_semitone(e)), ins_there >= 0 ? ins_there : 0, nom)) {
            const int heard_p = h.regs[2] | (h.regs[3] << 8), nom_p = nom[2] | (nom[3] << 8), mine = regs[2] | (regs[3] << 8);
            if (heard_p > 0 && nom_p > 0) {
                const int p = std::clamp(int(double(mine) * heard_p / nom_p + 0.5), 1, 0x3FFF);
                if (std::getenv("BOOMSPC_DEBUG_PREVIEW")) std::fprintf(stderr, "preview v%d: nominal %04X, tuned %04X by the note at tick %d (heard %04X, nominal %04X)\n", voice, mine, p, e.tick, heard_p, nom_p);
                regs[2] = uint8_t(p & 0xFF); regs[3] = uint8_t(p >> 8);
            }
        }
    }
    return true;
}

// Rows between ruler labels: a multiple of the highlight spacing, doubled
// until the numbers have room to sit side by side at this zoom.
int ruler_label_step(int rows, float px_per_row, float char_w) {
    const Theme& th = theme();
    const int base = th.hex_rows ? 16 : 10;
    int digits = 1;
    for (int r = std::max(1, rows - 1); r >= base; r /= base) ++digits;
    digits = std::max(digits, 2);
    const float need = (digits + 1) * char_w;
    int step = std::max(1, th.row_hi1);
    while (px_per_row > 0 && step * px_per_row < need && step < rows) step *= 2;
    return step;
}

void App::play_from(int order, int tick) {
    if (!tracker.drv || !tracker.song() || !engine.loaded()) return;
    const seq::Song* sg = tracker.song();
    if (order < 0 || order >= int(sg->orders.size())) return;
    const int pat_idx = sg->pattern_index(sg->orders[size_t(order)].pattern_addr);
    if (pat_idx < 0) return;
    tick = std::clamp(tick, 0, std::max(0, sg->patterns[size_t(pat_idx)].length_ticks - 1));
    const seq::Position& now = tracker.pos;
    int now_tick = -1;
    for (int v = 0; v < 8; ++v) now_tick = std::max(now_tick, now.voice_tick[v]);
    const int now_order = now.valid ? (now.order_index >= 0 ? now.order_index : 0) : -1;
    const bool from_start = !(now.valid && !engine.seeking() && (now_order < order || (now_order == order && now_tick >= 0 && now_tick <= tick)));
    struct State {
        std::shared_ptr<seq::Song> song;
        std::shared_ptr<seq::Driver> drv;   // kept alive here: a rescan on the UI thread may replace the tracker's
        int order, tick;
        double tps;                 // song ticks per output sample second
        seq::Position prev;
        uint8_t bytes[32]; bool have_bytes = false;
        int last_tick = -1; int64_t last_sample = 0;
        int last_order = -1, wraps = 0;      // orders crossed without reaching the target
        std::shared_ptr<SeekResult> result;
    };
    auto st = std::make_shared<State>();
    st->song = std::make_shared<seq::Song>(*sg);
    st->drv = tracker.drv;
    st->order = order; st->tick = tick;
    st->tps = tracker.drv->ticks_per_second(snap.ram) * engine.tempo() / 256.0;
    seek_result = std::make_shared<SeekResult>();
    seek_result->order = order; seek_result->row = tick / std::max(1, ticks_per_row);
    st->result = seek_result;
    const int order_count = int(sg->orders.size());
    const uint16_t base = now.track_ptr_base; const int span = std::min<int>(now.track_ptr_span, 32);
    auto reached = [st, base, span, order_count](const uint8_t* ram, int64_t sample) {
        bool changed = !st->have_bytes || !base || std::memcmp(ram + base, st->bytes, size_t(span)) != 0;
        if (changed) {
            if (base) { std::memcpy(st->bytes, ram + base, size_t(span)); st->have_bytes = true; }
            seq::Position p = st->drv->locate(ram, *st->song, st->prev.valid ? &st->prev : nullptr);
            if (p.valid) {
                st->prev = p;
                const int ord = p.order_index >= 0 ? p.order_index : 0;
                // A rip that starts past the target, or loops back over it, would
                // otherwise run the whole limit out in silence.
                if (ord != st->last_order) {
                    st->last_order = ord;
                    if (++st->wraps > order_count + 1) return true;
                }
                if (ord == st->order) {
                    int o = -1;
                    for (int v = 0; v < 8; ++v) o = std::max(o, p.voice_tick[v]);
                    if (o >= st->tick) { st->result->found = true; return true; }
                    if (o > st->last_tick) { st->last_tick = o; st->last_sample = sample; }
                } else st->last_tick = -1;
            }
        }
        if (st->tps > 0 && st->last_tick >= 0 && double(sample - st->last_sample) >= (st->tick - st->last_tick) / st->tps * Engine::kSampleRate) { st->result->found = true; return true; }
        return false;
    };
    double limit = 600.0;
    if (st->tps > 0) { double total = 0; for (const seq::Pattern& p : sg->patterns) total += p.length_ticks; limit = std::clamp(2.0 * total / st->tps + 5.0, 5.0, 120.0); }
    tracker.song_pinned = true;
    engine.seek(reached, from_start, limit);
    char b[96]; std::snprintf(b, sizeof b, "seeking to order %d row %d%s", order, tick / std::max(1, ticks_per_row), from_start ? "" : " (ahead: no restart)");
    status = b;
}

std::string App::fit_grid() {
    const seq::Song* sg = tracker.song();
    if (!sg || !tracker.drv) return "";
    std::vector<std::vector<int>> on(8);
    for (const seq::Pattern& p : sg->patterns)
        for (int v = 0; v < 8; ++v) {
            const seq::Track& t = p.tracks[v];
            for (int i = 0; i < t.used_events; ++i) {
                const seq::Event& e = t.events[size_t(i)];
                if (e.duration > 0 && (e.type == seq::EventType::Note || e.type == seq::EventType::Percussion)) on[size_t(v)].push_back(e.tick);
            }
        }
    int total = 0;
    for (auto& o : on) total += int(o.size());
    if (total < 8) return "";
    auto frac_on = [&](int u) {
        int hit = 0;
        for (auto& o : on) for (int t : o) if (t % u == 0) ++hit;
        return double(hit) / total;
    };
    std::vector<int> units;
    for (int u = 48; u >= 2; --u) units.push_back(u);
    int unit = 0;
    for (int u : units) if (frac_on(u) >= 0.9) { unit = u; break; }
    bool swung = false;
    if (!unit) { for (int u : units) if (u >= 3 && frac_on(u) >= 0.65) { unit = u; swung = true; break; } }
    if (!unit) return "";
    auto score = [&](int P) {
        int hit = 0, n = 0;
        for (auto& o : on) {
            std::vector<bool> has;
            int last = 0; for (int t : o) last = std::max(last, t);
            has.assign(size_t(last + P + 1), false);
            for (int t : o) has[size_t(t)] = true;
            for (int t : o) { if (t + P > last) continue; ++n; if (has[size_t(t + P)]) ++hit; }
        }
        return n > 0 ? double(hit) / n : 0.0;
    };
    const double tps = tracker.drv->ticks_per_second(snap.ram);
    struct Cand { int P; double score, strength; bool plausible; };
    std::vector<Cand> cands;
    for (int k = 2; k * unit <= 192; ++k) {
        const int P = k * unit;
        const double sc = score(P), nb = std::max(score(P - unit), score(P + unit));
        const double bpm = tps > 0 ? tps * 60.0 / P : 0;
        cands.push_back({P, sc, sc - nb, tps <= 0 || (bpm >= 55 && bpm <= 230)});
    }
    double best = 0, best_score = 0;
    for (const Cand& c : cands) if (c.plausible) { best = std::max(best, c.strength); best_score = std::max(best_score, c.score); }
    if (std::getenv("BOOMSPC_DEBUG_GRID")) { std::fprintf(stderr, "grid: unit %d (%.2f) tps %.1f", unit, frac_on(unit), tps); for (int u : units) std::fprintf(stderr, " u%d=%.2f", u, frac_on(u)); std::fprintf(stderr, "\n"); for (const Cand& c : cands) std::fprintf(stderr, "  P%d=%.2f/%.2f%s", c.P, c.score, c.strength, c.plausible ? "" : "x"); std::fprintf(stderr, "\n"); }
    if (best <= 0) return "";
    int beat = 0;
    for (const Cand& c : cands) if (c.plausible && c.strength >= 0.75 * best) { beat = c.P; break; }
    if (ticks_per_beat % unit == 0 && ticks_per_beat > unit && score(ticks_per_beat) >= 0.8 * best_score) beat = ticks_per_beat;
    if (!beat) return "";
    if (beat / unit > 12)
        for (int u : units) if (u < beat && u > unit && beat % u == 0 && beat / u <= 12 && frac_on(u) >= 0.8) { unit = u; break; }
    int bar = 4;
    {
        const double b4 = score(4 * beat);
        double bb = b4; int bk = 4;
        for (int k : {2, 3, 6, 8}) { double sc = score(k * beat); if (sc > bb && sc > b4 * 1.15) { bb = sc; bk = k; } }
        bar = bk;
    }
    ticks_per_row = std::clamp(unit, 1, 96);
    ticks_per_beat = std::clamp(beat, 1, 192);
    Theme& th = theme();
    th.row_hi1 = std::max(1, beat / unit);
    th.row_hi2 = std::max(th.row_hi1, th.row_hi1 * bar);
    roll_snap = 1;
    char b[96];
    std::snprintf(b, sizeof b, "%d ticks/row, beat %d (%d rows), bar %d beats%s", ticks_per_row, ticks_per_beat, th.row_hi1, bar, swung ? " (swung: some onsets fall between rows)" : "");
    return b;
}

bool App::set_song_bpm(double bpm) {
    if (!tracker.drv || !engine.loaded() || bpm <= 0 || ticks_per_beat <= 0) return false;
    std::vector<std::pair<uint16_t, uint8_t>> writes;
    if (!tracker.drv->tempo_writes(snap.ram, bpm * ticks_per_beat / 60.0, writes)) { status = "this driver's tempo state is not known"; return false; }
    engine.begin_edit();
    for (const auto& [addr, value] : writes) engine.write_ram(addr, &value, 1);
    engine.end_edit();
    after_edit();
    char b[64]; std::snprintf(b, sizeof b, "tempo set: %.1f BPM (%.1f ticks/s)", song_bpm(), tracker.drv->ticks_per_second(snap.ram));
    status = b;
    return true;
}

namespace {
const char* sc(Action a) { const char* s = action_shortcut(a); return *s ? s : nullptr; }

bool action_is_global(int a) { return action_def(Action(a)).scope == SCOPE_GLOBAL; }

void next_order(App& app, int d) {
    const seq::Song* sg = app.tracker.song();
    if (!sg || sg->orders.empty()) return;
    app.follow = false;
    app.view_order = std::clamp(app.view_order + d, 0, int(sg->orders.size()) - 1);
}

}

void run_action(App& app, int action) {
    Theme& th = theme();
    Engine& eng = app.engine;
    if (action >= 0 && action < A_COUNT) crash::set_context(crash::CTX_ACTION, action_def(Action(action)).id);
    switch (action) {
        case A_PLAY_TOGGLE: if (eng.loaded()) eng.toggle(); break;
        case A_PLAY:        if (eng.loaded()) eng.play(); break;
        case A_STOP:        if (eng.loaded()) eng.pause(); break;
        case A_PLAY_START:  if (eng.loaded()) { eng.restart(); eng.play(); } break;
        case A_PLAY_CURSOR: if (eng.loaded()) {
            const int tick = app.seq_tab == 1 ? (app.roll_sel_tick >= 0 ? app.roll_sel_tick : app.roll_sel_t1 > app.roll_sel_t0 ? app.roll_sel_t0 : 0) : std::max(0, app.sel_row) * app.ticks_per_row;
            app.play_from(app.view_order, tick);
        } break;
        case A_EDIT_TOGGLE: app.edit_mode = !app.edit_mode; app.cur_nibble = 0; break;
        case A_FOLLOW_TOGGLE: app.follow = !app.follow; break;
        case A_METRONOME_TOGGLE: app.metronome = !app.metronome; break;
        case A_OPEN:        app.dialog_open(); break;
        case A_SAVE_PROJECT:    app.dialog_save_project(false); break;
        case A_SAVE_PROJECT_AS: app.dialog_save_project(true); break;
        case A_EXPORT_SPC:  app.dialog_export_spc(); break;
        case A_EXPORT_WAV:  if (eng.loaded()) app.export_wav_open = true; break;
        // Bytes a rewrite left for a voice to finish reading are in use again after an undo.
        case A_UNDO:        if (eng.can_undo()) { eng.undo(); app.tracker.pending_release.clear(); app.after_edit(); app.status = "undo"; } break;
        case A_REDO:        if (eng.can_redo()) { eng.redo(); app.tracker.pending_release.clear(); app.after_edit(); app.status = "redo"; } break;
        case A_QUIT: { SDL_Event quit{}; quit.type = SDL_QUIT; SDL_PushEvent(&quit); break; }
        case A_OCTAVE_UP:   app.octave = std::min(6, app.octave + 1); break;
        case A_OCTAVE_DOWN: app.octave = std::max(1, app.octave - 1); break;
        case A_STEP_UP:     th.edit_step = std::min(64, th.edit_step + 1); break;
        case A_STEP_DOWN:   th.edit_step = std::max(0, th.edit_step - 1); break;
        case A_MUTE_1: case A_MUTE_2: case A_MUTE_3: case A_MUTE_4:
        case A_MUTE_5: case A_MUTE_6: case A_MUTE_7: case A_MUTE_8:
            eng.set_mute_mask(eng.mute_mask() ^ (1 << (action - A_MUTE_1))); break;
        case A_UNMUTE_ALL:  eng.set_mute_mask(0); break;
        case A_NEXT_ORDER:  next_order(app, 1); break;
        case A_PREV_ORDER:  next_order(app, -1); break;
        case A_ZOOM_IN:     th.font_size_pattern = std::min(40.0f, th.font_size_pattern + 1); break;
        case A_ZOOM_OUT:    th.font_size_pattern = std::max(6.0f, th.font_size_pattern - 1); break;
        case A_ZOOM_RESET:  th.font_size_pattern = 15.0f; break;
        case A_WIN_SEQUENCER:   app.show_sequencer = !app.show_sequencer; break;
        case A_WIN_INSTRUMENTS: app.show_instruments = !app.show_instruments; break;
        case A_WIN_EFFECTS:     app.show_effects = !app.show_effects; break;
        case A_WIN_EVENT:       app.show_event_editor = !app.show_event_editor; break;
        case A_WIN_SAMPLES:     app.show_samples = !app.show_samples; break;
        case A_WIN_PLAYER:      app.show_player = !app.show_player; break;
        case A_WIN_VOICES:      app.show_voices = !app.show_voices; break;
        case A_WIN_DSP:         app.show_dsp = !app.show_dsp; break;
        case A_WIN_MEMORY:      app.show_memory = !app.show_memory; break;
        case A_WIN_DISASM:      app.show_disasm = !app.show_disasm; break;
        case A_WIN_SETTINGS:    app.show_settings = !app.show_settings; break;
        case A_WIN_SHORTCUTS:   app.show_shortcuts = !app.show_shortcuts; break;
        case A_WIN_ABOUT:       app.show_about = !app.show_about; break;
        case A_WIN_UPDATES:     app.show_updates = !app.show_updates; break;
        case A_COMMAND_PALETTE: app.show_palette = !app.show_palette; break;
        case A_FULLSCREEN:      app.toggle_fullscreen = true; break;
        default:
            if (action >= 0 && action < A_COUNT && !action_is_global(action)) app.pending_action = action;
            break;
    }
}

void draw_status_bar(App& app) {
    const Theme& th = theme();
    const float h = ImGui::GetFrameHeight();
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 2));
    if (ImGui::BeginViewportSideBar("##statusbar", ImGui::GetMainViewport(), ImGuiDir_Down, h, flags)) {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        auto sep = [] { ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine(); };
        if (app.engine.loaded()) {
            if (app.engine.seeking()) ImGui::TextColored(th.colors[TC_STATUS_PLAY], "SEEKING");
            else if (app.engine.playing()) ImGui::TextColored(th.colors[TC_STATUS_PLAY], "PLAYING");
            else ImGui::TextDisabled("STOPPED");
        } else ImGui::TextDisabled("NO FILE");
        sep();
        if (app.edit_mode) ImGui::TextColored(th.colors[TC_STATUS_EDIT], "EDIT");
        else ImGui::TextDisabled("view");
        if (ImGui::IsItemHovered()) tooltip_spaced("Edit mode: %s", action_shortcut(A_EDIT_TOGGLE));
        sep();
        ImGui::Text("oct %d", app.octave);
        sep();
        ImGui::Text("step %d", th.edit_step);
        sep();
        ImGui::Text("%s", app.follow ? "follow" : "no follow");
        if (app.engine.loaded() && app.tracker.song()) {
            sep();
            ImGui::Text("order %02d", app.view_order);
            if (app.sel_voice >= 0 && app.sel_row >= 0) {
                static const char* fields[App::F_COUNT] = {"note", "ins", "quant", "vel", "fx", "fx arg"};
                sep();
                ImGui::Text("v%d row %d %s", app.sel_voice + 1, app.sel_row, fields[std::clamp(app.sel_field, 0, App::F_COUNT - 1)]);
            }
            if (app.sel_active) {
                sep();
                ImGui::TextDisabled("block");
            }
        }
        if (app.engine.dirty()) { sep(); ImGui::TextColored(th.colors[TC_STATUS_EDIT], "modified"); }
        if (!app.project_path.empty()) { sep(); ImGui::TextDisabled("%s", base_of(app.project_path).c_str()); }
        if (!app.status.empty()) {
            sep();
            const ImVec2 p0 = ImGui::GetCursorScreenPos();
            const float avail = ImGui::GetContentRegionAvail().x;
            const ImVec2 sz = ImGui::CalcTextSize(app.status.c_str());
            ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), p0, ImVec2(p0.x + avail, p0.y + sz.y), p0.x + avail, app.status.c_str(), nullptr, &sz);
            ImGui::Dummy(ImVec2(std::min(avail, sz.x), sz.y));
            if (sz.x > avail && ImGui::IsItemHovered()) tooltip_spaced("%s", app.status.c_str());
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

namespace {
bool fuzzy_match(const char* needle, const char* hay) {
    const char* n = needle;
    for (const char* h = hay; *h && *n; ++h)
        if (std::tolower(uint8_t(*h)) == std::tolower(uint8_t(*n))) ++n;
    return *n == 0;
}

}

void draw_command_palette(App& app) {
    if (!app.show_palette) return;
    static char query[128];
    static int highlighted = 0;
    static bool just_opened = true;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + 80), ImGuiCond_Always, ImVec2(0.5f, 0));
    ImGui::SetNextWindowSize(ImVec2(520, 0));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
    if (!ImGui::Begin("##palette", nullptr, flags)) { ImGui::End(); return; }
    if (just_opened) { ImGui::SetKeyboardFocusHere(); query[0] = 0; highlighted = 0; just_opened = false; }
    ImGui::SetNextItemWidth(-1);
    bool enter = ImGui::InputText("##q", query, sizeof query, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    ImGui::SetItemDefaultFocus();

    std::vector<int> hits;
    for (int i = 0; i < A_COUNT; ++i) {
        const ActionDef& d = action_def(Action(i));
        if (!action_is_global(i)) continue;
        if (query[0] && !fuzzy_match(query, d.name) && !fuzzy_match(query, d.group)) continue;
        hits.push_back(i);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) ++highlighted;
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) --highlighted;
    if (!hits.empty()) highlighted = (highlighted % int(hits.size()) + int(hits.size())) % int(hits.size());
    else highlighted = 0;

    ImGui::BeginChild("hits", ImVec2(0, std::min(12, std::max(1, int(hits.size()))) * ImGui::GetTextLineHeightWithSpacing() + 4));
    int chosen = -1;
    for (int k = 0; k < int(hits.size()); ++k) {
        const ActionDef& d = action_def(Action(hits[size_t(k)]));
        char label[160];
        std::snprintf(label, sizeof label, "%s##%d", d.name, hits[size_t(k)]);
        if (ImGui::Selectable(label, k == highlighted)) chosen = hits[size_t(k)];
        if (k == highlighted && (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow))) ImGui::SetScrollHereY();
        const char* s = action_shortcut(Action(hits[size_t(k)]));
        if (*s) { ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - ImGui::CalcTextSize(s).x - 8); ImGui::TextDisabled("%s", s); }
    }
    if (hits.empty()) ImGui::TextDisabled("no matching command");
    ImGui::EndChild();
    ImGui::TextDisabled("Enter runs, Esc closes. Pattern edits live in the Sequencer's keys.");
    if (enter && !hits.empty()) chosen = hits[size_t(highlighted)];
    bool close = ImGui::IsKeyPressed(ImGuiKey_Escape, false) || (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsWindowAppearing());
    ImGui::End();
    if (chosen >= 0) { app.show_palette = false; just_opened = true; run_action(app, chosen); }
    else if (close) { app.show_palette = false; just_opened = true; }
}

static void draw_dockspace(App& app) {
    const ImGuiID id = ImGui::GetID("boomspc.dock");
    const bool build = app.reset_layout || ImGui::DockBuilderGetNode(id) == nullptr;
    app.reset_layout = false;
    ImGui::DockSpaceOverViewport(id, ImGui::GetMainViewport(), ImGuiDockNodeFlags_None);
    if (!build) return;
    // Player over Instruments on the left, Sequencer with Effects at its
    // right in the middle, Voices / Event and the rest along the bottom.
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(id);
    ImGui::DockBuilderAddNode(id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(id, vp->WorkSize);
    ImGuiID left, right, left_bottom, right_bottom, right_top, effects;
    ImGui::DockBuilderSplitNode(id, ImGuiDir_Left, 0.27f, &left, &right);
    ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.48f, &left_bottom, &left);
    ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.17f, &right_bottom, &right_top);
    ImGui::DockBuilderSplitNode(right_top, ImGuiDir_Right, 0.21f, &effects, &right_top);
    ImGui::DockBuilderDockWindow("Player", left);
    ImGui::DockBuilderDockWindow("Instruments", left_bottom);
    ImGui::DockBuilderDockWindow("Samples", left_bottom);
    ImGui::DockBuilderDockWindow("Sequencer", right_top);
    ImGui::DockBuilderDockWindow("Effects", effects);
    ImGui::DockBuilderDockWindow("Voices", right_bottom);
    ImGui::DockBuilderDockWindow("Event", right_bottom);
    ImGui::DockBuilderDockWindow("DSP", right_bottom);
    ImGui::DockBuilderDockWindow("Memory", right_bottom);
    ImGui::DockBuilderDockWindow("Disassembly", right_bottom);
    ImGui::DockBuilderDockWindow("Settings", right_bottom);
    ImGui::DockBuilderDockWindow("Keyboard shortcuts", right_bottom);
    ImGui::DockBuilderDockWindow("Updates", right_top);
    ImGui::DockBuilderFinish(id);
}

// What the crash log says about this session; refreshed when it changes.
void refresh_crash_context(const App& app) {
    static std::string file, project, driver, status;
    static int song = -2;
    if (app.source_path != file) crash::set_context(crash::CTX_FILE, (file = app.source_path).c_str());
    if (app.project_path != project) crash::set_context(crash::CTX_PROJECT, (project = app.project_path).c_str());
    if (app.tracker.driver_name != driver) crash::set_context(crash::CTX_DRIVER, (driver = app.tracker.driver_name).c_str());
    if (app.tracker.song_index != song) {
        song = app.tracker.song_index;
        char b[160];
        const seq::Song* s = app.tracker.song();
        std::snprintf(b, sizeof b, "%d%s%s", song, s ? " " : "", s ? s->label.c_str() : "");
        crash::set_context(crash::CTX_SONG, b);
    }
    if (app.status != status) crash::set_context(crash::CTX_STATUS, (status = app.status).c_str());
}

void draw_crash_notice(App& app) {
    const char* id = "BoomSPC crashed last time";
    if (app.crash_notice.empty()) return;
    if (!ImGui::IsPopupOpen(id)) ImGui::OpenPopup(id);
    ImGui::SetNextWindowSize(ImVec2(em(30), 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::PushTextWrapPos(em(29));
    text_wrapped("The previous session ended with a crash. What happened was written to crash.log; please send that file (and the .spc or project you had open) to the BoomSPC Discord or GitHub issues so it can be fixed.");
    ImGui::Spacing();
    ImGui::PushFont(fonts().mono, ImGui::GetStyle().FontSizeBase * 0.85f);
    text_wrapped("%s", app.crash_notice.c_str());
    ImGui::PopFont();
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    if (ImGui::Button("Open folder")) open_config_dir();
    ImGui::SameLine();
    if (ImGui::Button("Copy path")) ImGui::SetClipboardText(app.crash_notice.c_str());
    ImGui::SameLine();
    if (ImGui::Button("OK")) { app.crash_notice.clear(); ImGui::CloseCurrentPopup(); }
    ImGui::EndPopup();
}

void ui_draw(App& app) {
    theme().apply_widget_colors();
    logo_refresh();
    app.engine.snapshot(app.snap);
    refresh_crash_context(app);
    app.tracker.update(app.snap, ImGui::GetTime(), !app.engine.seeking());
    app.tracker.flush_releases(app.snap, app.engine);
    if (app.seek_result && !app.engine.seeking()) {
        char b[96];
        if (app.seek_result->found) std::snprintf(b, sizeof b, "playing from order %d row %d", app.seek_result->order, app.seek_result->row);
        else std::snprintf(b, sizeof b, "order %d row %d never came round: the rip starts past it", app.seek_result->order, app.seek_result->row);
        app.status = b;
        app.seek_result.reset();
    }
    if (app.engine.snsf() && app.tracker.drv && app.tracker.song()) {
        const int bank = app.tracker.drv->song_bank(*app.tracker.song());
        if (bank >= 0 && bank != app.engine.bank_window()) { app.engine.set_bank_window(bank); app.engine.snapshot(app.snap); app.tracker.reparse(app.snap); }
        if (app.tracker.song_index != app.fitted_song) {
            app.fitted_song = app.tracker.song_index;
            std::string fit = app.fit_grid();
            if (!fit.empty()) app.status = app.tracker.song()->label + ": grid " + fit;
        }
    }

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open...", sc(A_OPEN))) run_action(app, A_OPEN);
            if (ImGui::MenuItem("Save project", sc(A_SAVE_PROJECT), false, app.engine.loaded())) run_action(app, A_SAVE_PROJECT);
            if (ImGui::MenuItem("Save project as...", sc(A_SAVE_PROJECT_AS), false, app.engine.loaded())) run_action(app, A_SAVE_PROJECT_AS);
            ImGui::Separator();
            if (ImGui::MenuItem("Export SPC...", sc(A_EXPORT_SPC), false, app.engine.loaded())) run_action(app, A_EXPORT_SPC);
            if (ImGui::MenuItem("Export WAV...", sc(A_EXPORT_WAV), false, app.engine.loaded())) run_action(app, A_EXPORT_WAV);
            ImGui::Separator();
            if (ImGui::MenuItem("Quit", sc(A_QUIT))) run_action(app, A_QUIT);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Playback")) {
            const bool on = app.engine.loaded();
            if (ImGui::MenuItem(app.engine.playing() || app.engine.seeking() ? "Pause" : "Play", sc(A_PLAY_TOGGLE), false, on)) run_action(app, A_PLAY_TOGGLE);
            if (ImGui::MenuItem("Stop", sc(A_STOP), false, on)) run_action(app, A_STOP);
            ImGui::Separator();
            if (ImGui::MenuItem("Play from start", sc(A_PLAY_START), false, on)) run_action(app, A_PLAY_START);
            if (ImGui::MenuItem("Play from cursor", sc(A_PLAY_CURSOR), false, on)) run_action(app, A_PLAY_CURSOR);
            ImGui::Separator();
            ImGui::MenuItem("Follow playback", sc(A_FOLLOW_TOGGLE), &app.follow);
            ImGui::MenuItem("Metronome", sc(A_METRONOME_TOGGLE), &app.metronome);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", sc(A_UNDO), false, app.engine.can_undo())) run_action(app, A_UNDO);
            if (ImGui::MenuItem("Redo", sc(A_REDO), false, app.engine.can_redo())) run_action(app, A_REDO);
            ImGui::Separator();
            ImGui::MenuItem("Edit mode", sc(A_EDIT_TOGGLE), &app.edit_mode);
            ImGui::MenuItem("Follow playback", sc(A_FOLLOW_TOGGLE), &app.follow);
            ImGui::MenuItem("Metronome", sc(A_METRONOME_TOGGLE), &app.metronome);
            ImGui::Separator();
            if (ImGui::MenuItem("Octave up", sc(A_OCTAVE_UP))) run_action(app, A_OCTAVE_UP);
            if (ImGui::MenuItem("Octave down", sc(A_OCTAVE_DOWN))) run_action(app, A_OCTAVE_DOWN);
            if (ImGui::MenuItem("Edit step +", sc(A_STEP_UP))) run_action(app, A_STEP_UP);
            if (ImGui::MenuItem("Edit step -", sc(A_STEP_DOWN))) run_action(app, A_STEP_DOWN);
            ImGui::Separator();
            if (ImGui::BeginMenu("Pattern")) {
                auto pa = [&](const char* label, int a) { if (ImGui::MenuItem(label, sc(Action(a)))) run_action(app, a); };
                pa("Cut", A_CUT); pa("Copy", A_COPY); pa("Paste", A_PASTE); pa("Paste mix", A_PASTE_MIX); pa("Paste flood", A_PASTE_FLOOD);
                ImGui::Separator();
                pa("Delete", A_DELETE); pa("Pull delete", A_PULL_DELETE); pa("Insert row", A_INSERT);
                ImGui::Separator();
                pa("Note up", A_TRANSPOSE_UP); pa("Note down", A_TRANSPOSE_DOWN); pa("Octave up", A_TRANSPOSE_OCT_UP); pa("Octave down", A_TRANSPOSE_OCT_DOWN);
                ImGui::Separator();
                pa("Value up", A_VALUE_UP); pa("Value down", A_VALUE_DOWN); pa("Value up (coarse)", A_VALUE_UP_COARSE); pa("Value down (coarse)", A_VALUE_DOWN_COARSE);
                ImGui::Separator();
                pa("Interpolate", A_INTERPOLATE); pa("Fade...", A_FADE); pa("Scale...", A_SCALE); pa("Randomize...", A_RANDOMIZE); pa("Invert values", A_INVERT);
                ImGui::Separator();
                pa("Flip selection", A_FLIP); pa("Collapse rows", A_COLLAPSE); pa("Expand rows", A_EXPAND);
                ImGui::Separator();
                pa("Set instrument to current", A_INS_SET); pa("Select all", A_SEL_ALL);
                ImGui::Separator();
                pa("Fold / unfold voice column", A_VOICE_FOLD);
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Unmute all", sc(A_UNMUTE_ALL))) run_action(app, A_UNMUTE_ALL);
            ImGui::Separator();
            if (ImGui::MenuItem("Command palette...", sc(A_COMMAND_PALETTE))) run_action(app, A_COMMAND_PALETTE);
            if (ImGui::MenuItem("Settings...", sc(A_WIN_SETTINGS))) run_action(app, A_WIN_SETTINGS);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Sequencer", sc(A_WIN_SEQUENCER), &app.show_sequencer);
            ImGui::MenuItem("Instruments", sc(A_WIN_INSTRUMENTS), &app.show_instruments);
            ImGui::MenuItem("Effects", sc(A_WIN_EFFECTS), &app.show_effects);
            ImGui::MenuItem("Event", sc(A_WIN_EVENT), &app.show_event_editor);
            ImGui::MenuItem("Samples", sc(A_WIN_SAMPLES), &app.show_samples);
            ImGui::Separator();
            ImGui::MenuItem("Player", sc(A_WIN_PLAYER), &app.show_player);
            ImGui::MenuItem("Voices", sc(A_WIN_VOICES), &app.show_voices);
            ImGui::MenuItem("DSP", sc(A_WIN_DSP), &app.show_dsp);
            ImGui::MenuItem("Memory", sc(A_WIN_MEMORY), &app.show_memory);
            ImGui::MenuItem("Disassembly", sc(A_WIN_DISASM), &app.show_disasm);
            ImGui::Separator();
            if (ImGui::MenuItem("Pattern font bigger", sc(A_ZOOM_IN))) run_action(app, A_ZOOM_IN);
            if (ImGui::MenuItem("Pattern font smaller", sc(A_ZOOM_OUT))) run_action(app, A_ZOOM_OUT);
            if (ImGui::MenuItem("Full screen", sc(A_FULLSCREEN))) run_action(app, A_FULLSCREEN);
            if (ImGui::MenuItem("Reset panel layout")) app.reset_layout = true;
            ImGui::Separator();
            ImGui::MenuItem("Settings (colours, fonts, keys)", sc(A_WIN_SETTINGS), &app.show_settings);
            ImGui::Separator();
            ImGui::MenuItem("ImGui demo", nullptr, &app.show_demo);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            ImGui::MenuItem("Keyboard shortcuts", sc(A_WIN_SHORTCUTS), &app.show_shortcuts);
            ImGui::MenuItem("Updates and changelog", sc(A_WIN_UPDATES), &app.show_updates);
            if (ImGui::MenuItem("Open settings folder")) open_config_dir();
            ImGui::SetItemTooltip("%s\nini files and crash.log", config_dir().c_str());
            ImGui::Separator();
            ImGui::MenuItem("About BoomSPC", sc(A_WIN_ABOUT), &app.show_about);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantTextInput && !app.show_palette && !settings_capturing_key()) {
        for (int a = 0; a < A_COUNT; ++a)
            if (action_is_global(a) && action_pressed(Action(a))) { run_action(app, a); break; }
    }

    draw_status_bar(app);
    draw_dockspace(app);

    if (app.show_sequencer)   draw_sequencer_panel(app);
    if (app.show_instruments) draw_instruments_panel(app);
    static std::map<std::string, std::pair<bool, int>> fresh;   // name -> (had settings at start, frames tried)
    auto dock_beside = [](const char* window, const char* neighbour) {
        auto it = fresh.find(window);
        if (it == fresh.end()) it = fresh.emplace(window, std::make_pair(ImGui::FindWindowSettingsByID(ImHashStr(window)) != nullptr, 0)).first;
        if (it->second.first || it->second.second > 30) return;
        ImGuiWindow* n = ImGui::FindWindowByName(neighbour);
        if (n && n->DockId) { ImGui::SetNextWindowDockID(n->DockId, ImGuiCond_Always); it->second.second = 31; }
        else ++it->second.second;
    };
    if (app.show_effects)     draw_effects_panel(app);
    if (app.show_event_editor) { dock_beside("Event", "Voices"); draw_event_panel(app); }
    if (app.show_samples)     draw_samples_panel(app);
    if (app.show_disasm)      draw_disasm_panel(app);
    if (app.show_settings)    draw_settings_window(&app.show_settings);
    if (app.show_about)       draw_about_window(app);
    if (app.show_updates)     { dock_beside("Updates", "Sequencer"); draw_updates_panel(app); }
    if (app.show_shortcuts)   draw_shortcuts_window(&app.show_shortcuts);
    draw_export_dialogs(app);
    if (app.show_player) draw_player_panel(app);
    if (app.show_voices) draw_voices_panel(app);
    if (app.show_dsp)    draw_dsp_panel(app);
    if (app.show_memory) draw_memory_panel(app);
    if (app.show_demo)   ImGui::ShowDemoWindow(&app.show_demo);
    draw_crash_notice(app);
    draw_command_palette(app);
}
