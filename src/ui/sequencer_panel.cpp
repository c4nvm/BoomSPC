// Sequencer panel: order list, pattern grid (drawn with ImDrawList), the
// playhead clock, and the tracker-side edit operations. Cursor model as in
// Furnace: one field of one cell; keys act on the field under the cursor.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "actions.hpp"
#include "fonts.hpp"
#include "imgui.h"
#include "theme.hpp"
#include "ui.hpp"

using seq::Event;
using seq::EventType;

namespace {
constexpr int F_COUNT = App::F_COUNT;

ImVec4 scale(ImVec4 c, float k) { return ImVec4(c.x * k, c.y * k, c.z * k, c.w); }

const float kFieldText[F_COUNT]  = {0.5f, 4.0f, 7.0f, 8.0f, 10.0f, 13.0f};
const float kFieldLeft[F_COUNT]  = {0.0f, 3.75f, 6.75f, 7.75f, 9.25f, 13.0f};
const float kFieldRight[F_COUNT] = {3.75f, 6.75f, 7.75f, 9.25f, 13.0f, 17.75f};
constexpr float kVoiceChars = 18.0f;
constexpr float kCollapsedChars = 7.75f;
const float kCollText[F_COUNT]  = {0.5f, 3.75f, 3.75f, 3.75f, 4.0f, 7.0f};
const float kCollLeft[F_COUNT]  = {0.0f, 3.75f, 3.75f, 3.75f, 3.75f, 7.0f};
const float kCollRight[F_COUNT] = {3.75f, 3.75f, 3.75f, 3.75f, 7.0f, 7.0f};
inline bool field_hidden(const App& app, int v, int f) { return app.voice_collapsed[v] && f != App::F_NOTE && f != App::F_FX; }

ThemeColor fx_theme_color(seq::FxClass c) {
    using seq::FxClass;
    switch (c) {
        case FxClass::Invalid: return TC_FX_INVALID;
        case FxClass::Instrument: return TC_INS;
        case FxClass::Pitch: return TC_FX_PITCH;
        case FxClass::Volume: return TC_FX_VOLUME;
        case FxClass::Panning: return TC_FX_PANNING;
        case FxClass::Song: return TC_FX_SONG;
        case FxClass::Time: return TC_FX_TIME;
        case FxClass::Speed: return TC_FX_SPEED;
        case FxClass::Sys1: return TC_FX_SYS1;
        case FxClass::Sys2: return TC_FX_SYS2;
        case FxClass::Misc: break;
    }
    return TC_FX_MISC;
}

ThemeColor effect_color(const seq::Driver& D, uint8_t op) { return fx_theme_color(D.cmd_class(op)); }

int key_to_semitone(ImGuiKey* key_out = nullptr) {
    struct K { ImGuiKey key; int semi; };
    static const K keys[] = {
        {ImGuiKey_Z, 0}, {ImGuiKey_S, 1}, {ImGuiKey_X, 2}, {ImGuiKey_D, 3}, {ImGuiKey_C, 4}, {ImGuiKey_V, 5},
        {ImGuiKey_G, 6}, {ImGuiKey_B, 7}, {ImGuiKey_H, 8}, {ImGuiKey_N, 9}, {ImGuiKey_J, 10}, {ImGuiKey_M, 11},
        {ImGuiKey_Comma, 12}, {ImGuiKey_L, 13}, {ImGuiKey_Period, 14}, {ImGuiKey_Semicolon, 15}, {ImGuiKey_Slash, 16},
        {ImGuiKey_Q, 12}, {ImGuiKey_2, 13}, {ImGuiKey_W, 14}, {ImGuiKey_3, 15}, {ImGuiKey_E, 16}, {ImGuiKey_R, 17},
        {ImGuiKey_5, 18}, {ImGuiKey_T, 19}, {ImGuiKey_6, 20}, {ImGuiKey_Y, 21}, {ImGuiKey_7, 22}, {ImGuiKey_U, 23},
        {ImGuiKey_I, 24}, {ImGuiKey_9, 25}, {ImGuiKey_O, 26}, {ImGuiKey_0, 27}, {ImGuiKey_P, 28},
    };
    for (const K& k : keys) if (ImGui::IsKeyPressed(k.key, false)) { if (key_out) *key_out = k.key; return k.semi; }
    return -1;
}

int preview_instrument(const App& app, const seq::Pattern& pat, int voice, int tick) {
    const seq::Driver& D = *app.tracker.drv;
    const seq::Track& t = pat.tracks[voice];
    int ins = -1;
    for (int i = 0; i < t.used_events; ++i) {
        const Event& e = t.events[size_t(i)];
        if (e.tick > tick) break;
        if (e.type == EventType::Command && D.is_instrument_cmd(e.b[0])) ins = D.instrument_arg(e);
    }
    if (ins >= 0) return ins;
    const uint8_t* v = app.snap.dsp + voice * 0x10;
    int count = D.instrument_count(app.snap.ram);
    for (int i = 0; i < count; ++i) {
        seq::Instrument in = D.read_instrument(app.snap.ram, i);
        if (in.srcn == v[4] && (D.id() != std::string("nspc") || (in.adsr0 == v[5] && in.adsr1 == v[6] && in.gain == v[7])))
            return D.instrument_number(app.snap.ram, i);
    }
    return 0;
}

void preview_note(App& app, const seq::Pattern& pat, uint8_t note_byte) {
    const seq::Driver& D = *app.tracker.drv;
    int voice = preview_voice(app);
    const int tick = app.sel_row * app.ticks_per_row;
    int ins = preview_instrument(app, pat, app.sel_voice, tick);
    uint8_t regs[8];
    if (app.sel_voice >= 0 && app.sel_voice < 8 && D.is_note_byte(note_byte)) {
        if (app.preview_regs_heard(pat.tracks[app.sel_voice], app.sel_voice, -1, tick, D.note_semitone(note_byte), ins, regs)) app.engine.preview_on(voice, regs);
    } else if (D.preview_regs(app.snap.ram, note_byte, ins, regs)) app.engine.preview_on(voice, regs);
}

int key_to_hex() {
    for (int i = 0; i < 10; ++i) {
        if (ImGui::IsKeyPressed(ImGuiKey(ImGuiKey_0 + i), false)) return i;
        if (ImGui::IsKeyPressed(ImGuiKey(ImGuiKey_Keypad0 + i), false)) return i;
    }
    for (int i = 0; i < 6; ++i) if (ImGui::IsKeyPressed(ImGuiKey(ImGuiKey_A + i), false)) return 10 + i;
    return -1;
}

std::vector<Event> editable_events(const seq::Track& t) {
    return std::vector<Event>(t.events.begin(), t.events.begin() + t.used_events);
}

struct Cell {
    std::vector<int> events;     // indices into track events
    int  note_ev = -1;           // first note/tie/rest/perc
    int  ins_ev = -1;            // instrument command
    int  ins = -1;               // instrument number set here
    int  qv = -1;                // qv byte in effect for the note
    int  fx_ev = -1;             // first non-instrument command
    bool extra = false, in_sub = false, offgrid = false;
};

struct ClipCell { int note = -1, ins = -1, qv = -1; uint8_t fx[16]{}; int fx_size = 0; bool fx_sub = false; };
struct Clip { int voices = 0, rows = 0; int f0 = 0, f1 = 0; std::vector<ClipCell> cells; };
Clip g_clip;

bool note_like(EventType t) { return t == EventType::Note || t == EventType::Tie || t == EventType::Rest || t == EventType::Percussion; }

int timed_event_at(const std::vector<Event>& ev, int tick, bool allow_sub = false) {
    for (int i = 0; i < int(ev.size()); ++i)
        if (ev[size_t(i)].duration > 0 && ev[size_t(i)].tick == tick && (allow_sub || !ev[size_t(i)].in_sub)) return i;
    return -1;
}

int fx_event_in(const seq::Driver& D, const std::vector<Event>& ev, int t0, int t1) {
    for (int i = 0; i < int(ev.size()); ++i) {
        const Event& e = ev[size_t(i)];
        if (e.in_sub || e.tick < t0 || e.tick >= t1) continue;
        if (e.type == EventType::Command && !D.is_instrument_cmd(e.b[0])) return i;
        if (e.type == EventType::SubCall) return i;
    }
    return -1;
}

int ins_event_in(const seq::Driver& D, const std::vector<Event>& ev, int t0, int t1) {
    for (int i = 0; i < int(ev.size()); ++i) {
        const Event& e = ev[size_t(i)];
        if (!e.in_sub && e.type == EventType::Command && D.is_instrument_cmd(e.b[0]) && e.tick >= t0 && e.tick < t1) return i;
    }
    return -1;
}

}

int preview_voice(const App& app) {
    const Tracker& T = app.tracker;
    int voice = app.sel_voice >= 0 ? app.sel_voice : 7;
    const seq::Song* sg = T.song();
    if (app.engine.playing() && sg && !sg->orders.empty()) {
        int pi = sg->pattern_index(sg->orders[size_t(std::clamp(app.view_order, 0, int(sg->orders.size()) - 1))].pattern_addr);
        if (pi >= 0)
            for (int v = 7; v >= 0; --v) if (!sg->patterns[size_t(pi)].tracks[v].addr) { voice = v; break; }
    }
    return voice;
}

void draw_sequencer_panel(App& app) {
    ImGui::SetNextWindowSize(ImVec2(1290, 540), ImGuiCond_FirstUseEver);
    if (app.focus_sequencer) { ImGui::SetNextWindowFocus(); app.focus_sequencer = false; }
    if (!panel_begin("Sequencer", &app.show_sequencer)) { panel_end(); return; }

    Tracker& T = app.tracker;
    Theme& th = theme();

    ImGui::Text("Driver: %s", T.driver_name.c_str());
    if (!app.engine.loaded() || !T.drv || T.songs.empty()) {
        if (app.engine.loaded() && T.drv) ImGui::TextDisabled("No song structures found in RAM.");
        else if (app.engine.loaded()) text_wrapped("This driver is not supported for editing yet. Playback and the DSP/voice views still work.");
        panel_end();
        return;
    }

    const seq::Driver& D = *T.drv;
    const bool in_place = D.in_place_only();
    const bool stream = D.has_stream_edit();   // streams: shared bytes edit in place, the rest is rewritten

    same_line_if_fits(em(14.7f));
    ImGui::SetNextItemWidth(em(14.7f));
    {
        if (ImGui::BeginCombo("##song", T.song() ? T.song()->label.c_str() : "")) {
            for (size_t i = 0; i < T.songs.size(); ++i) {
                ImGui::PushID(int(i));
                if (ImGui::Selectable(T.songs[i].label.c_str(), int(i) == T.song_index)) { T.song_index = int(i); T.song_pinned = true; app.view_order = 0; app.sel_voice = -1; }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
    }
    same_line_if_fits(text_w("Stop") + text_w("Edit") + em(1));
    {
        const bool playing = app.engine.playing() || app.engine.seeking();
        if (playing) ImGui::PushStyleColor(ImGuiCol_Button, scale(th.colors[TC_STATUS_PLAY], 0.5f));
        if (ImGui::Button(playing ? "Stop" : "Play", ImVec2(std::max(text_w("Stop"), text_w("Play")), 0))) app.engine.toggle();
        if (playing) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) tooltip_spaced("%s", action_shortcut(A_PLAY_TOGGLE));
        ImGui::SameLine();
        const bool editing = app.edit_mode;   // the button flips it; the pop must match the push
        if (editing) ImGui::PushStyleColor(ImGuiCol_Button, scale(th.colors[TC_STATUS_EDIT], 0.6f));
        if (ImGui::Button("Edit")) app.edit_mode = !app.edit_mode;
        if (editing) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) tooltip_spaced("Edit mode (%s): keys write into the pattern.", action_shortcut(A_EDIT_TOGGLE));
    }
    same_line_if_fits(text_w("Follow") + em(1.5f)); ImGui::Checkbox("Follow", &app.follow);
    same_line_if_fits(text_w("Metronome") + em(1.5f)); ImGui::Checkbox("Metronome", &app.metronome);
    if (ImGui::IsItemHovered()) tooltip_spaced("Noise click on every highlighted row, louder on the strong\nhighlight, placed by the same clock as the playhead (%s).\nIf the clicks and the music disagree, the grid is off.", action_shortcut(A_METRONOME_TOGGLE));
    {
        static const char* rates[] = {"half", "x1", "double"};
        same_line_if_fits(em(5.5f));
        ImGui::SetNextItemWidth(em(5.5f));
        ImGui::Combo("##metrorate", &app.metronome_rate, rates, 3);
        if (ImGui::IsItemHovered()) tooltip_spaced("Metronome rate: clicks every highlight (x1), every second one (half)\nor twice per highlight (double), for songs whose beat is not the\nhighlight spacing.");
    }
    same_line_if_fits(input_int_w(3) + text_w("ticks/row")); ImGui::SetNextItemWidth(input_int_w(3)); ImGui::InputInt("ticks/row", &app.ticks_per_row);
    app.ticks_per_row = std::clamp(app.ticks_per_row, 1, 96);
    same_line_if_fits(text_w("Fit"));
    if (ImGui::Button("Fit")) { std::string fit = app.fit_grid(); app.status = fit.empty() ? "grid: the song's onsets follow no common unit; set ticks/row by hand" : "grid fitted: " + fit; }
    if (ImGui::IsItemHovered()) tooltip_spaced("Read the grid off the song: ticks/row from where its notes start, the beat from\nhow the voices repeat, the highlights to match (a 12/8 shuffle gets 3 units per\nbeat, straight time 4). Done on load; use it again after changing the settings.");
    same_line_if_fits(input_int_w(2) + text_w("oct")); ImGui::SetNextItemWidth(input_int_w(2)); ImGui::InputInt("oct", &app.octave);
    app.octave = std::clamp(app.octave, 1, 6);
    same_line_if_fits(input_int_w(2) + text_w("step")); ImGui::SetNextItemWidth(input_int_w(2)); ImGui::InputInt("step", &th.edit_step);
    th.edit_step = std::clamp(th.edit_step, 0, 64);
    same_line_if_fits(text_w("Rescan"));
    if (ImGui::Button("Rescan")) { T.analyze(app.snap); app.view_order = 0; }
    same_line_if_fits(text_w("Reclaim") + em(1.5f)); ImGui::Checkbox("Reclaim", &T.reclaim_other_songs);
    if (ImGui::IsItemHovered())
        tooltip_spaced("Game rips carry the whole music bank and ARAM is usually full.\n"
                          "When a track has to grow, allow overwriting the other songs' data.");
    {
        // Free RAM, refreshed once a second: what a grown track could move into.
        static Tracker::FreeStats fs;
        static double next = 0;
        static const void* last_drv = nullptr;
        if (ImGui::GetTime() >= next || last_drv != T.drv.get()) { fs = T.free_stats(app.snap); next = ImGui::GetTime() + 1.0; last_drv = T.drv.get(); }
        char b[64];
        std::snprintf(b, sizeof b, "free %d B", T.reclaim_other_songs ? fs.total_reclaim : fs.total);
        same_line_if_fits(text_w(b));
        const int largest = T.reclaim_other_songs ? fs.largest_reclaim : fs.largest;
        if (largest < 64) ImGui::TextColored(th.colors[TC_NOTE_OFF], "%s", b); else ImGui::TextDisabled("%s", b);
        if (ImGui::IsItemHovered())
            tooltip_spaced("ARAM a grown track can move into: %d bytes free, largest run %d.\n"
                              "With Reclaim: %d bytes, largest run %d (the other songs' data included).\n"
                              "A track that outgrows its place needs one run at least its size.",
                              fs.total, fs.largest, fs.total_reclaim, fs.largest_reclaim);
    }
    if (double tps = D.ticks_per_second(app.snap.ram); tps > 0) {
        same_line_if_fits(text_w("999.9 BPM"));
        ImGui::TextDisabled("%.1f BPM", app.song_bpm());
        if (ImGui::IsItemHovered())
            tooltip_spaced("%.1f ticks per second, %d ticks per beat (%.1f rows/s at %d ticks per row).\nChange the song tempo in the Player panel; ticks per beat with the field next to this.",
                              tps, app.ticks_per_beat, tps / app.ticks_per_row, app.ticks_per_row);
        same_line_if_fits(input_int_w(3) + text_w("beat")); ImGui::SetNextItemWidth(input_int_w(3)); ImGui::InputInt("beat", &app.ticks_per_beat);
        app.ticks_per_beat = std::clamp(app.ticks_per_beat, 1, 192);
        if (ImGui::IsItemHovered()) tooltip_spaced("Ticks per beat, for the BPM readout. 48 is the usual quarter note; halve it if the song reads twice too slow.");
    }
    same_line_if_fits(text_w("(?)")); ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        tooltip_spaced("Click a field, turn on Edit (%s), then type: piano keys on the note column,\n"
                          "hex digits on instrument / quantise / velocity / effect columns.\n"
                          "%s = note off, %s = tie, %s = clear field, %s = pull delete, %s = insert row.\n"
                          "Shift+arrows or drag select; %s / %s / %s copy / cut / paste.\n"
                          "Full list: %s (Keyboard shortcuts), rebind in Settings.",
                          action_shortcut(A_EDIT_TOGGLE), action_shortcut(A_NOTE_OFF), action_shortcut(A_NOTE_TIE),
                          action_shortcut(A_DELETE), action_shortcut(A_PULL_DELETE), action_shortcut(A_INSERT),
                          action_shortcut(A_COPY), action_shortcut(A_CUT), action_shortcut(A_PASTE),
                          action_shortcut(A_WIN_SHORTCUTS));

    if (ImGui::BeginTabBar("seqview")) {
        if (ImGui::BeginTabItem("Tracker")) { app.seq_tab = 0; ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Piano roll")) { app.seq_tab = 1; ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Arrangement")) { app.seq_tab = 2; ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }

    seq::Song& song = *T.song();
    const seq::Position& pos = T.pos;
    if (app.follow && pos.valid && pos.order_index >= 0) app.view_order = pos.order_index;
    app.view_order = std::clamp(app.view_order, 0, int(song.orders.size()) - 1);

    ImGui::PushFont(fonts().mono, th.font_size_pattern);

    child_begin("orders", ImVec2(ImGui::CalcTextSize("00 P00").x + 24, 0), ImGuiChildFlags_Borders, 0, false);
    ImGui::TextDisabled("Order");
    {
        int sel_pat = song.pattern_index(song.orders[size_t(app.view_order)].pattern_addr);
        for (size_t i = 0; i < song.orders.size(); ++i) {
            int pi = song.pattern_index(song.orders[i].pattern_addr);
            bool playing = pos.valid && int(i) == pos.order_index;
            bool selected = int(i) == app.view_order;
            ImVec2 p = ImGui::GetCursorScreenPos();
            float h = ImGui::GetTextLineHeightWithSpacing();
            float w = ImGui::GetContentRegionAvail().x;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            if (playing) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), th.u32(TC_ORDER_PLAYING));
            if (selected) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), th.u32(TC_ORDER_SELECTED));
            char id[24]; std::snprintf(id, sizeof id, "##o%u", unsigned(i));
            if (ImGui::InvisibleButton(id, ImVec2(w, h))) { app.view_order = int(i); app.follow = false; }
            char b[32];
            std::snprintf(b, sizeof b, th.hex_rows ? "%02zX" : "%02zu", i);
            dl->AddText(ImVec2(p.x + 4, p.y + 1), th.u32(TC_ORDER_INDEX), b);
            std::snprintf(b, sizeof b, "P%02d", pi);
            dl->AddText(ImVec2(p.x + 4 + ImGui::CalcTextSize("000").x, p.y + 1), pi == sel_pat && !selected ? th.u32(TC_ORDER_SIMILAR) : th.u32(TC_NOTE), b);
        }
    }
    if (song.loop_to >= 0) ImGui::TextDisabled("loop x%d\n -> %02d", song.loop_count, song.loop_to);
    else ImGui::TextDisabled("end");
    child_end();
    ImGui::SameLine();

    const int pat_idx = song.pattern_index(song.orders[size_t(app.view_order)].pattern_addr);
    if (pat_idx < 0) { ImGui::PopFont(); panel_end(); return; }
    seq::Pattern& pat = song.patterns[size_t(pat_idx)];
    const int tpr = app.ticks_per_row;
    const int rows = std::max(1, (pat.length_ticks + tpr - 1) / tpr);

    std::vector<Cell> cells[8];
    for (int v = 0; v < 8; ++v) {
        cells[v].assign(size_t(rows), {});
        const seq::Track& t = pat.tracks[v];
        int cur_qv = -1;
        for (int i = 0; i < t.used_events; ++i) {
            const Event& e = t.events[i];
            if (e.type == EventType::End) continue;
            int r = e.tick / tpr;
            if (r < 0 || r >= rows) continue;
            Cell& c = cells[v][size_t(r)];
            c.events.push_back(i);
            if (e.in_sub) c.in_sub = true;
            if (e.tick % tpr) c.offgrid = true;
            if (e.type == EventType::Length) { if (e.size == 2) cur_qv = e.b[1]; continue; }
            if (note_like(e.type)) {
                if (c.note_ev < 0) { c.note_ev = i; c.qv = cur_qv; } else c.extra = true;
            } else if (e.type == EventType::Command && D.is_instrument_cmd(e.b[0])) {
                if (c.ins < 0) { c.ins = D.instrument_arg(e); c.ins_ev = i; } else c.extra = true;
            } else if (e.type == EventType::Command || e.type == EventType::SubCall) {
                if (c.fx_ev < 0) c.fx_ev = i; else c.extra = true;
            }
        }
    }
    static struct {
        int  order = -1;             // pattern being tracked
        int  observed = -1;          // newest event-start tick seen
        int  sync_tick = -1;         // tick at the last boundary
        int64_t sync_samples = 0;    // audio position at that boundary
        int  first_tick = -1;        // first boundary seen in this pattern (for the rate)
        int64_t first_samples = 0;
        double tps = 0;              // ticks per second in use
        double cal_tps = 0;          // ...calibrated from boundaries (fallback)
        int  drops = 0;              // consecutive snapshots that went backwards
        int  aheads = 0;             // consecutive snapshots far ahead of the clock
        std::vector<double> offs;    // recent watch observation offsets from the clock
    } ph;
    static std::vector<Engine::WatchEvent> watch;
    float head_row_f = -1;           // fractional row of the playhead, <0 = unknown
    if (pos.valid && pos.track_ptr_base) app.engine.set_watch(pos.track_ptr_base, pos.track_ptr_span);
    else app.engine.set_watch(0, 0);
    watch.clear();
    app.engine.take_watch(watch);
    // While seeking the emulator runs far ahead of the clock in silence, so the
    // position read out of RAM would send the playhead flying. Park it instead.
    static bool was_seeking = false;
    const bool seeking = app.engine.seeking();
    if (was_seeking && !seeking) ph.sync_tick = -1;   // resync once the seek lands
    was_seeking = seeking;
    if (pos.valid && !seeking) {
        const int64_t samples = app.snap.sample_pairs;
        auto observed_of = [](const seq::Position& p) {
            int o = -1;
            for (int v = 0; v < 8; ++v) if (p.voice_tick[v] >= 0) o = std::max(o, p.voice_tick[v]);
            return o;
        };
        const double drv_tps = D.ticks_per_second(app.snap.ram) * app.engine.tempo() / 256.0;
        auto sync = [&](int observed, int64_t at, bool from_watch) {
            bool restarted = ph.order != app.view_order || at < ph.sync_samples || ph.sync_tick < 0;
            if (!restarted && observed < ph.observed) {
                if (from_watch) return;
                static const bool dbg2 = std::getenv("BOOMSPC_DEBUG_SYNC") != nullptr;
                if (dbg2) { std::fprintf(stderr, "drop: observed %d < %d at %lld voices", observed, ph.observed, (long long)at); for (int v = 0; v < 8; ++v) std::fprintf(stderr, " %d@%04X", pos.voice_tick[v], pos.voice_ptr[v]); std::fprintf(stderr, "\n"); }
                if (++ph.drops < 2 && observed + 200 >= ph.observed) return;
                restarted = true;
            } else if (!from_watch) ph.drops = 0;
            if (restarted) {
                ph.offs.clear();
                ph.aheads = 0;
                ph.order = app.view_order;
                ph.first_tick = observed; ph.first_samples = at;
                ph.sync_tick = observed; ph.sync_samples = at;
                ph.observed = observed;
                ph.drops = 0;
                return;
            }
            if (observed <= ph.observed) return;
            static const bool dbg = std::getenv("BOOMSPC_DEBUG_SYNC") != nullptr;
            const double rate = ph.tps > 0 ? ph.tps : 0;
            if (rate > 0) {
                const double pred = ph.sync_samples + double(observed - ph.sync_tick) / rate * Engine::kSampleRate;
                const double tol = std::max(tpr, 4) / rate * Engine::kSampleRate;
                if (from_watch && std::fabs(pred - double(at)) > tol) return;
                if (!from_watch) { if (double(at) < pred - tol) { if (++ph.aheads < 2) return; } else ph.aheads = 0; }
                const bool resync = !from_watch && (double(at) < pred - tol || double(at) > pred + Engine::kSampleRate / 2);
                if (dbg) std::fprintf(stderr, "sync tick %d sample %lld pred %.0f (%s%s)\n", observed, (long long)at, pred, from_watch ? "watch" : "snap", resync ? ", resync" : pred < at ? ", kept" : "");
                ph.observed = observed;
                ph.sync_tick = observed;
                if (resync) { ph.sync_samples = at; ph.offs.clear(); }
                else if (from_watch) {
                    const double off = double(at) - pred;
                    ph.offs.push_back(off);
                    if (ph.offs.size() > 12) ph.offs.erase(ph.offs.begin());
                    std::vector<double> sorted(ph.offs.begin(), ph.offs.end());
                    std::sort(sorted.begin(), sorted.end());
                    const double shift = sorted[sorted.size() / 4];
                    ph.sync_samples = int64_t(pred + shift + 0.5);
                    for (double& o : ph.offs) o -= shift;
                } else ph.sync_samples = std::min(int64_t(pred + 0.5), at);
            } else {
                if (dbg) std::fprintf(stderr, "sync tick %d sample %lld (%s)\n", observed, (long long)at, from_watch ? "watch" : "snap");
                ph.observed = observed;
                ph.sync_tick = observed; ph.sync_samples = at;
            }
            double secs = double(at - ph.first_samples) / Engine::kSampleRate;
            if (secs > 0.4 && observed > ph.first_tick) ph.cal_tps = (observed - ph.first_tick) / secs;
        };
        ph.tps = drv_tps > 0 ? drv_tps : ph.cal_tps;
        if (!watch.empty() && ph.order == app.view_order) {
            uint8_t saved[32];
            const int span = std::min<int>(pos.track_ptr_span, 32);
            std::memcpy(saved, app.snap.ram + pos.track_ptr_base, size_t(span));
            seq::Position prev = pos;
            for (const Engine::WatchEvent& e : watch) {
                std::memcpy(app.snap.ram + pos.track_ptr_base, e.bytes, size_t(span));
                seq::Position p = D.locate(app.snap.ram, song, &prev);
                if (!p.valid || (p.order_index >= 0 && p.order_index != app.view_order)) continue;
                for (int v = 0; v < 8; ++v) {
                    const int idx = p.voice_event[v];
                    if (idx < 0 || idx >= int(pat.tracks[v].events.size()) || idx == prev.voice_event[v]) continue;
                    const Event& ne = pat.tracks[v].events[size_t(idx)];
                    if (ne.type != EventType::Note || ne.addr == 0) continue;
                    const int pitch = e.regs[v][2] | (e.regs[v][3] << 8);
                    if (pitch == 0) continue;
                    App::Heard h; std::memcpy(h.regs, e.regs[v], 8); h.semitone = D.event_semitone(ne);
                    app.heard[App::heard_key(v, ne.addr)] = h;
                }
                int o = observed_of(p);
                if (o >= 0) sync(o, e.sample, true);
                prev = p;
            }
            std::memcpy(app.snap.ram + pos.track_ptr_base, saved, size_t(span));
        }
        sync(observed_of(pos), samples, false);
        ph.tps = drv_tps > 0 ? drv_tps : ph.cal_tps;
        double play_tick = ph.sync_tick;
        if (ph.tps > 0) {
            double ahead = double(samples - ph.sync_samples) / Engine::kSampleRate - th.latency_ms / 1000.0;
            play_tick = ph.sync_tick + std::max(0.0, ahead) * ph.tps;
            play_tick = std::min(play_tick, double(std::max(pat.length_ticks, 1)));
        }
        head_row_f = float(play_tick / tpr);
        for (int v = 0; v < 8; ++v) {
            const seq::Track& t = pat.tracks[v];
            if (!t.addr || t.total_ticks <= 0 || play_tick < t.total_ticks) { app.voice_head[v] = -1; continue; }
            int loop_tick = -1;
            if (t.loops && t.loop_event >= 0 && t.loop_event < int(t.events.size())) loop_tick = t.events[size_t(t.loop_event)].tick;
            else if (t.loops) loop_tick = 0;
            const int span = loop_tick >= 0 ? t.total_ticks - loop_tick : 0;
            app.voice_head[v] = span > 0 ? float(loop_tick + std::fmod(play_tick - loop_tick, double(span))) : float(t.total_ticks);
        }
    } else for (int v = 0; v < 8; ++v) app.voice_head[v] = -1;
    {
        Engine::Metronome m;
        m.on = app.metronome && pos.valid && ph.tps > 0 && app.engine.playing();
        const double scale = app.metronome_rate == 0 ? 0.5 : app.metronome_rate == 2 ? 2.0 : 1.0;
        m.ref_row = double(ph.sync_tick) / tpr * scale;
        m.ref_samples = ph.sync_samples;
        m.samples_per_row = Engine::kSampleRate / ph.tps * tpr / scale;
        m.hi1 = std::max(1, th.row_hi1);
        m.hi2 = std::max(1, th.row_hi2);
        app.engine.set_metronome(m);
    }
    const int cursor_row = head_row_f >= 0 ? int(head_row_f) : -1;

    if (app.seq_tab == 1) {
        ImGui::PopFont();
        bool wrote = draw_piano_roll(app, pat_idx, head_row_f >= 0 ? head_row_f * tpr : -1.0f);
        (void)wrote;
        panel_end();
        return;
    }
    if (app.seq_tab == 2) {
        ImGui::PopFont();
        if (!app.arrangement_ack) {
            // Blocks the view until acknowledged, once per run.
            child_begin("arrpane", ImVec2(0, 0), 0, 0, true);
            const float w = std::min(em(30), ImGui::GetContentRegionAvail().x - em(2));
            ImGui::Dummy(ImVec2(0, em(2)));
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x - w) * 0.5f);
            if (child_begin("arr_notice", ImVec2(w, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY)) {
                ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.3f);
                ImGui::TextColored(theme().colors[TC_STATUS_EDIT], "Heavily experimental");
                ImGui::PopFont();
                text_wrapped("The arrangement view is not fully implemented and generally behaves oddly right now: expect wrong lengths, edits that do not land where you clicked, and displays that disagree with the tracker. The tracker and piano roll are the reliable views; nothing here is safe to rely on yet.");
                ImGui::Spacing();
                if (ImGui::Button("Show it anyway")) app.arrangement_ack = true;
                ImGui::SameLine();
                if (ImGui::Button("Back to the tracker")) app.seq_tab = 0;
            }
            child_end();
            child_end();
            panel_end();
            return;
        }
        draw_arrangement(app, pat_idx, head_row_f >= 0 ? head_row_f * tpr : -1.0f);
        panel_end();
        return;
    }

    const int inst_count = D.instrument_count(app.snap.ram);

    const float cw = ImGui::CalcTextSize("0").x;          // monospace char width
    const float rh = ImGui::GetTextLineHeight() + 2;
    const float idx_w = cw * 4 + 6;
    float vx[9];   // x of each voice's column (offset from the index column), and the total
    vx[0] = 0;
    for (int v = 0; v < 8; ++v) vx[v + 1] = vx[v] + cw * (app.voice_collapsed[v] ? kCollapsedChars : kVoiceChars);
    auto voice_w_of = [&](int v) { return vx[v + 1] - vx[v]; };
    auto f_text = [&](int v, int f) { return app.voice_collapsed[v] ? kCollText[f] : kFieldText[f]; };
    auto f_left = [&](int v, int f) { return app.voice_collapsed[v] ? kCollLeft[f] : kFieldLeft[f]; };
    auto f_right = [&](int v, int f) { return app.voice_collapsed[v] ? kCollRight[f] : kFieldRight[f]; };
    const float header_h = rh * 2 + 4;
    const ImVec2 content(idx_w + vx[8], header_h + rows * rh);

    const bool smooth = th.follow_mode == 0;
    if (app.follow && smooth && head_row_f >= 0) {
        float avail_h = ImGui::GetContentRegionAvail().y;
        ImGui::SetNextWindowScroll(ImVec2(-1.0f, std::max(0.0f, header_h + head_row_f * rh - avail_h * th.playhead_pos)));
    }
    child_begin("grid", ImVec2(0, 0), ImGuiChildFlags_Borders, 0, false);
    const bool grid_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 win_pos = ImGui::GetWindowPos();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("gridbtn", ImVec2(std::max(content.x, ImGui::GetContentRegionAvail().x), content.y));
    const bool header_hovered = ImGui::IsWindowHovered() && ImGui::GetMousePos().y < win_pos.y + header_h && ImGui::GetMousePos().y >= win_pos.y;
    const bool grid_hovered = ImGui::IsItemHovered() || header_hovered;
    if (app.sel_row >= rows) app.sel_row = rows - 1;

    static int last_scrolled = -1;
    const float view_h = ImGui::GetWindowHeight();
    if (app.follow && head_row_f >= 0 && !smooth) {
        if (cursor_row != last_scrolled) {
            float y = header_h + cursor_row * rh;
            if (th.follow_mode == 1) ImGui::SetScrollY(std::max(0.0f, y - view_h * 0.5f));
            else if (y < ImGui::GetScrollY() + header_h + rh || y > ImGui::GetScrollY() + view_h - rh * 3)
                ImGui::SetScrollY(std::max(0.0f, y - view_h * 0.4f));
            last_scrolled = cursor_row;
        }
    }
    static int last_cursor_scrolled = -1;
    if (!app.follow && app.sel_row >= 0 && app.sel_row != last_cursor_scrolled && grid_focused) {
        float y = header_h + app.sel_row * rh;
        float view = ImGui::GetWindowHeight();
        if (y < ImGui::GetScrollY() + header_h) ImGui::SetScrollY(std::max(0.0f, y - header_h - rh * 3));
        else if (y + rh > ImGui::GetScrollY() + view) ImGui::SetScrollY(y + rh * 4 - view);
        last_cursor_scrolled = app.sel_row;
    }
    ImGuiIO& io = ImGui::GetIO();
    if (grid_hovered && io.MouseWheel != 0 && !io.KeyCtrl && app.follow) app.follow = false;
    if (grid_hovered && io.MouseWheel != 0 && io.KeyCtrl)
        th.font_size_pattern = std::clamp(th.font_size_pattern + (io.MouseWheel > 0 ? 1.0f : -1.0f), 6.0f, 40.0f);

    auto cell_at = [&](ImVec2 m, int& v, int& f, int& r) {
        const float xp = m.x - origin.x - idx_w;
        v = 8;
        for (int i = 0; i < 8; ++i) if (xp < vx[i + 1]) { v = i; break; }
        f = F_COUNT - 1;
        if (v < 8) {
            const float fx = (xp - vx[v]) / cw;
            for (int i = 0; i < F_COUNT; ++i) if (fx < f_right(v, i) && !field_hidden(app, v, i)) { f = i; break; }
            if (field_hidden(app, v, f)) f = App::F_FX;
        }
        r = int((m.y - origin.y - header_h) / rh);
        if (m.x < origin.x + idx_w) v = -1;
        if (m.y < origin.y + header_h || m.y < win_pos.y + header_h) r = -1;
    };
    static bool dragging = false;
    if (grid_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        int v, f, r; cell_at(ImGui::GetMousePos(), v, f, r);
        if (r < 0 && v >= 0 && v < 8) {
            const ImVec2 m = ImGui::GetMousePos();
            const bool on_toggle = m.x >= origin.x + idx_w + vx[v + 1] - cw * 1.5f;   // the column's right edge, any header row
            if (on_toggle) {
                app.voice_collapsed[v] = !app.voice_collapsed[v];
                if (app.sel_voice == v && field_hidden(app, v, app.sel_field)) app.sel_field = App::F_NOTE;
            } else app.engine.set_mute_mask(app.engine.mute_mask() ^ (1 << v));
        } else if (v < 0 && r >= 0 && r < rows) {
            app.play_from(app.view_order, r * tpr);
        } else if (v >= 0 && v < 8 && r >= 0 && r < rows) {
            if (io.KeyShift && app.sel_voice >= 0) {
                if (!app.sel_active) { app.sel_v0 = app.sel_voice; app.sel_f0 = app.sel_field; app.sel_r0 = app.sel_row; }
                app.sel_active = true; app.sel_v1 = v; app.sel_f1 = f; app.sel_r1 = r;
                app.sel_voice = v; app.sel_field = f; app.sel_row = r; app.cur_nibble = 0;
            } else {
                app.sel_voice = v; app.sel_field = f; app.sel_row = r; app.sel_active = false; app.cur_nibble = 0; app.sel_all_stage = 0;
                app.sel_v0 = app.sel_v1 = v; app.sel_f0 = app.sel_f1 = f; app.sel_r0 = app.sel_r1 = r;
                dragging = true;
            }
        }
    }
    bool open_context = false;
    if (grid_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        int v, f, r; cell_at(ImGui::GetMousePos(), v, f, r);
        if (r < 0 && v >= 0 && v < 8) {
            int solo = 0xFF & ~(1 << v);
            app.engine.set_mute_mask(app.engine.mute_mask() == solo ? 0 : solo);
        } else if (v >= 0 && v < 8 && r >= 0 && r < rows) {
            const bool inside = app.sel_active && v >= std::min(app.sel_v0, app.sel_v1) && v <= std::max(app.sel_v0, app.sel_v1) && r >= std::min(app.sel_r0, app.sel_r1) && r <= std::max(app.sel_r0, app.sel_r1);
            if (!inside) { app.sel_voice = v; app.sel_field = f; app.sel_row = r; app.sel_active = false; app.cur_nibble = 0; app.sel_all_stage = 0; }
            open_context = true;
        }
    }
    if (dragging) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            int v, f, r; cell_at(ImGui::GetMousePos(), v, f, r);
            v = std::clamp(v, 0, 7); r = std::clamp(r, 0, rows - 1);
            if (v != app.sel_v0 || r != app.sel_r0 || f != app.sel_f0) { app.sel_v1 = v; app.sel_f1 = f; app.sel_r1 = r; app.sel_active = true; }
        } else dragging = false;
    }

    const int c0 = std::min(app.sel_v0 * F_COUNT + app.sel_f0, app.sel_v1 * F_COUNT + app.sel_f1);
    const int c1 = std::max(app.sel_v0 * F_COUNT + app.sel_f0, app.sel_v1 * F_COUNT + app.sel_f1);
    const int sr0 = std::min(app.sel_r0, app.sel_r1), sr1 = std::max(app.sel_r0, app.sel_r1);
    const int sv0 = c0 / F_COUNT, sv1 = c1 / F_COUNT;
    auto field_x0 = [&](int v, int f) { return origin.x + idx_w + vx[v] + f_left(v, f) * cw; };
    auto field_x1 = [&](int v, int f) { return origin.x + idx_w + vx[v] + f_right(v, f) * cw; };

    const ImVec2 vis_min(win_pos.x, win_pos.y), vis_max(win_pos.x + ImGui::GetWindowWidth(), win_pos.y + ImGui::GetWindowHeight());
    dl->AddRectFilled(vis_min, vis_max, th.u32(TC_PATTERN_BG));
    const int first_row = std::max(0, int((ImGui::GetScrollY() - header_h) / rh));
    const int last_row = std::min(rows, int((ImGui::GetScrollY() + ImGui::GetWindowHeight()) / rh) + 1);
    const int mute_mask = app.engine.mute_mask();

    for (int r = first_row; r < last_row; ++r) {
        const float y = origin.y + header_h + r * rh;
        const bool hi2 = th.row_hi2 > 0 && r % th.row_hi2 == 0;
        const bool hi1 = !hi2 && th.row_hi1 > 0 && r % th.row_hi1 == 0;
        if (hi2) dl->AddRectFilled(ImVec2(origin.x, y), ImVec2(origin.x + content.x, y + rh), th.u32(TC_ROW_HI2));
        else if (hi1) dl->AddRectFilled(ImVec2(origin.x, y), ImVec2(origin.x + content.x, y + rh), th.u32(TC_ROW_HI1));
        if (th.cursor_row_tint && r == app.sel_row && app.sel_voice >= 0)
            dl->AddRectFilled(ImVec2(origin.x, y), ImVec2(origin.x + content.x, y + rh), th.u32(TC_ROW_CURSOR));
        if (r == cursor_row && !smooth) dl->AddRectFilled(ImVec2(origin.x, y), ImVec2(origin.x + content.x, y + rh), th.u32(TC_PLAYHEAD));
        if (app.sel_active && r >= sr0 && r <= sr1)
            dl->AddRectFilled(ImVec2(field_x0(sv0, c0 % F_COUNT), y), ImVec2(field_x1(sv1, c1 % F_COUNT), y + rh), th.u32(TC_SELECTION));

        char rb[8];
        std::snprintf(rb, sizeof rb, th.hex_rows ? "%03X" : "%3d", r);
        ThemeColor rc = r == cursor_row ? TC_ROW_INDEX_PLAYING : hi2 ? TC_ROW_INDEX_HI2 : hi1 ? TC_ROW_INDEX_HI1 : TC_ROW_INDEX;
        dl->AddText(ImVec2(origin.x + 3, y + 1), th.u32(rc), rb);

        for (int v = 0; v < 8; ++v) {
            const float x = origin.x + idx_w + vx[v];
            const float voice_w = voice_w_of(v);
            const bool coll = app.voice_collapsed[v];
            const seq::Track& t = pat.tracks[v];
            const Cell& c = cells[v][size_t(r)];
            const bool playing = pos.valid && pos.voice_tick[v] >= 0 && pos.voice_tick[v] / tpr == r;
            const float dim = (th.dim_muted && (mute_mask & (1 << v))) ? 0.35f : 1.0f;
            auto col = [&](ThemeColor tc) { return th.u32(tc, dim); };
            if (playing) dl->AddRectFilled(ImVec2(x, y), ImVec2(x + voice_w, y + rh), th.u32(TC_PLAYHEAD));
            if (app.sel_voice == v && app.sel_row == r) {
                ImU32 cc = th.u32(app.edit_mode ? TC_CURSOR_EDIT : TC_CURSOR);
                dl->AddRectFilled(ImVec2(field_x0(v, app.sel_field), y), ImVec2(field_x1(v, app.sel_field), y + rh), cc);
                if (app.cur_nibble > 0 && (app.sel_field == App::F_INS || app.sel_field == App::F_FX || app.sel_field == App::F_FXARG)) {
                    float nx = x + (f_text(v, app.sel_field) + app.cur_nibble) * cw;
                    dl->AddLine(ImVec2(nx, y + rh - 2), ImVec2(nx + cw, y + rh - 2), th.u32(TC_NOTE), 2.0f);
                }
            }

            const char* ntxt = "...";
            ImU32 ncol = col(TC_BLANK);
            char nb[8];
            if (c.note_ev >= 0) {
                const Event& e = t.events[size_t(c.note_ev)];
                switch (e.type) {
                    case EventType::Note: std::snprintf(nb, sizeof nb, "%s", D.note_name(e).c_str()); ntxt = nb; ncol = col(TC_NOTE); break;
                    case EventType::Tie: ntxt = "^^^"; ncol = col(TC_NOTE_TIE); break;
                    case EventType::Rest: ntxt = "OFF"; ncol = col(TC_NOTE_OFF); break;
                    case EventType::Percussion: std::snprintf(nb, sizeof nb, "P%02d", D.event_percussion(e)); ntxt = nb; ncol = col(TC_NOTE_PERC); break;
                    default: break;
                }
                if (e.in_sub || e.sub_iter > 0) ncol = col(TC_NOTE_SUB);
            }
            dl->AddText(ImVec2(x + f_text(v, App::F_NOTE) * cw, y + 1), ncol, ntxt);

            if (coll) {
                if (c.fx_ev >= 0) {
                    const Event& e = t.events[size_t(c.fx_ev)];
                    char fb[16];
                    if (e.type == EventType::SubCall) std::snprintf(fb, sizeof fb, "Sub");
                    else if (th.fx_hex_codes) std::snprintf(fb, sizeof fb, "%02X ", e.b[0]);
                    else std::snprintf(fb, sizeof fb, "%s", D.cmd_code(e.b[0]));
                    ImU32 fcol = col(e.type == EventType::SubCall ? TC_FX_SONG : effect_color(D, e.b[0]));
                    if (e.in_sub || e.sub_iter > 0) fcol = col(TC_NOTE_SUB);
                    dl->AddText(ImVec2(x + f_text(v, App::F_FX) * cw, y + 1), fcol, fb);
                } else dl->AddText(ImVec2(x + f_text(v, App::F_FX) * cw, y + 1), col(TC_BLANK), "...");
                if (c.extra) dl->AddText(ImVec2(x + (kCollapsedChars - 0.75f) * cw, y + 1), col(TC_EXTRA), "+");
                else if (c.offgrid) dl->AddText(ImVec2(x + (kCollapsedChars - 0.75f) * cw, y + 1), col(TC_OFFGRID), "'");
                continue;
            }

            char ib[4] = "..";
            ImU32 icol = col(TC_BLANK);
            if (c.ins >= 0) {
                std::snprintf(ib, sizeof ib, "%02X", c.ins & 0xFF);
                icol = D.instrument_index(app.snap.ram, c.ins) < inst_count || inst_count == 0 ? th.instrument(c.ins) : th.u32(TC_INS_INVALID);
                if (t.events[size_t(c.ins_ev)].in_sub || t.events[size_t(c.ins_ev)].sub_iter > 0) icol = th.u32(TC_NOTE_SUB);
                if (dim < 1) icol = (icol & 0x00FFFFFF) | (ImU32(dim * 255) << 24);
            }
            dl->AddText(ImVec2(x + kFieldText[App::F_INS] * cw, y + 1), icol, ib);

            if (c.qv >= 0 && c.note_ev >= 0 && t.events[size_t(c.note_ev)].type != EventType::Rest) {
                char qb[2] = {"0123456789ABCDEF"[c.qv >> 4], 0}, vb[2] = {"0123456789ABCDEF"[c.qv & 15], 0};
                dl->AddText(ImVec2(x + kFieldText[App::F_QUANT] * cw, y + 1), col(TC_QUANT), qb);
                ImU32 vc = th.volume((c.qv & 15) / 15.0f);
                if (dim < 1) vc = (vc & 0x00FFFFFF) | (ImU32(dim * 255) << 24);
                dl->AddText(ImVec2(x + kFieldText[App::F_VEL] * cw, y + 1), vc, vb);
            } else dl->AddText(ImVec2(x + kFieldText[App::F_QUANT] * cw, y + 1), col(TC_BLANK), "..");

            if (c.fx_ev >= 0) {
                const Event& e = t.events[size_t(c.fx_ev)];
                char fb[16], ab[16] = "";
                if (e.type == EventType::SubCall) { std::snprintf(fb, sizeof fb, "Sub"); std::snprintf(ab, sizeof ab, "%02X%02X", e.b[2], e.b[1]); }
                else {
                    if (th.fx_hex_codes) std::snprintf(fb, sizeof fb, "%02X ", e.b[0]);
                    else std::snprintf(fb, sizeof fb, "%s", D.cmd_code(e.b[0]));
                    if (e.size >= 3) std::snprintf(ab, sizeof ab, "%02X%02X", e.b[1], e.b[2]);
                    else if (e.size == 2) std::snprintf(ab, sizeof ab, "%02X", e.b[1]);
                }
                ImU32 fcol = col(e.type == EventType::SubCall ? TC_FX_SONG : effect_color(D, e.b[0]));
                if (e.in_sub || e.sub_iter > 0) fcol = col(TC_NOTE_SUB);
                dl->AddText(ImVec2(x + kFieldText[App::F_FX] * cw, y + 1), fcol, fb);
                dl->AddText(ImVec2(x + kFieldText[App::F_FXARG] * cw, y + 1), fcol, ab);
            } else dl->AddText(ImVec2(x + kFieldText[App::F_FX] * cw, y + 1), col(TC_BLANK), ".......");
            if (c.extra) dl->AddText(ImVec2(x + cw * 17.25f, y + 1), col(TC_EXTRA), "+");
            else if (c.offgrid) dl->AddText(ImVec2(x + cw * 17.25f, y + 1), col(TC_OFFGRID), "'");
        }
    }

    for (int v = 0; v <= 8; ++v) {
        float x = origin.x + idx_w + vx[v];
        dl->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + content.y), IM_COL32(255, 255, 255, 25));
    }
    for (int v = 0; v < 8; ++v) {
        const seq::Track& t = pat.tracks[v];
        if (!t.loops || t.loop_event < 0 || t.loop_event >= t.used_events) continue;
        int r = t.events[size_t(t.loop_event)].tick / tpr;
        if (r < first_row || r > last_row) continue;
        float x = origin.x + idx_w + vx[v], y = origin.y + header_h + r * rh;
        dl->AddLine(ImVec2(x + 1, y), ImVec2(x + voice_w_of(v) - 1, y), th.u32(TC_FX_SONG), 2.0f);
    }
    if (smooth && head_row_f >= 0) {
        float ly = origin.y + header_h + head_row_f * rh;
        float x1 = std::max(origin.x + content.x, win_pos.x + ImGui::GetWindowWidth());
        dl->AddRectFilled(ImVec2(origin.x, ly), ImVec2(x1, ly + rh), th.u32(TC_PLAYHEAD, 0.5f));
        dl->AddLine(ImVec2(origin.x, ly), ImVec2(x1, ly), th.u32(TC_PLAYHEAD_LINE), 2.0f);
    }

    {
        const float hy = win_pos.y;
        dl->AddRectFilled(ImVec2(win_pos.x, hy), ImVec2(win_pos.x + std::max(content.x, ImGui::GetWindowWidth()), hy + header_h), th.u32(TC_PATTERN_BG));
        dl->AddLine(ImVec2(win_pos.x, hy + header_h), ImVec2(win_pos.x + content.x + 100, hy + header_h), IM_COL32(255, 255, 255, 60));
        for (int v = 0; v < 8; ++v) {
            const float x = origin.x + idx_w + vx[v];
            const float voice_w = voice_w_of(v);
            const bool muted = mute_mask & (1 << v);
            const bool solo = mute_mask == (0xFF & ~(1 << v));
            const bool at_cursor = app.sel_voice == v;
            const seq::Track& t = pat.tracks[v];
            ImVec4 bar = th.colors[muted ? TC_CHANNEL_MUTED : solo ? TC_CHANNEL_SOLO : at_cursor ? TC_CHANNEL_CURSOR : TC_CHANNEL_HEADER_BG];
            dl->AddRectFilled(ImVec2(x + 1, hy + 1), ImVec2(x + voice_w - 1, hy + rh + 1), ImGui::ColorConvertFloat4ToU32(bar), 3.0f);
            char hb[48];
            std::snprintf(hb, sizeof hb, "%d%s%s", v + 1, t.addr ? "" : " -", muted ? "  MUTE" : solo ? "  SOLO" : "");
            ImU32 tc = th.u32(TC_CHANNEL_HEADER);
            if (muted) tc = th.u32(TC_CHANNEL_HEADER, 0.6f);
            dl->AddText(ImVec2(x + cw * 0.5f, hy + 2), tc, hb);
            dl->AddText(ImVec2(x + voice_w - cw * 1.25f, hy + 2), th.u32(TC_CHANNEL_HEADER, 0.7f), app.voice_collapsed[v] ? ">" : "<");
            if (th.show_meters) {
                float lvl = (app.snap.dsp[v * 0x10 + 8] & 0x7F) / 127.0f;
                if (muted) lvl = 0;
                ImVec2 m0(x + cw * 0.5f, hy + rh + 4), m1(x + voice_w - cw * 0.5f, hy + rh + rh - 2);
                dl->AddRectFilled(m0, m1, IM_COL32(255, 255, 255, 20));
                dl->AddRectFilled(m0, ImVec2(m0.x + (m1.x - m0.x) * lvl, m1.y), th.meter(lvl));
            } else {
                static const char* caps[F_COUNT] = {"not", "in", "q", "v", "fx", "arg"};
                for (int f = 0; f < F_COUNT; ++f)
                    if (!field_hidden(app, v, f)) dl->AddText(ImVec2(x + f_text(v, f) * cw, hy + rh + 3), th.u32(TC_BLANK), caps[f]);
            }
        }
        char pb[64];
        std::snprintf(pb, sizeof pb, "P%02d", pat_idx);
        dl->AddText(ImVec2(win_pos.x + 3, hy + 2), th.u32(TC_ROW_INDEX), pb);
    }

    if (grid_hovered) {
        int v, f, r; cell_at(ImGui::GetMousePos(), v, f, r);
        if (r < 0 && v >= 0 && v < 8) {
            tooltip_spaced("Voice %d: click = mute, right-click = solo; the %s at the right %s the column (note + effect code only)", v + 1, app.voice_collapsed[v] ? ">" : "<", app.voice_collapsed[v] ? "expands" : "collapses");
        } else if (v >= 0 && v < 8 && r >= 0 && r < rows && !cells[v][size_t(r)].events.empty()) {
            ImGui::BeginTooltip();
            const seq::Track& t = pat.tracks[v];
            if (t.loops && t.loop_event >= 0 && t.loop_event < t.used_events && t.events[size_t(t.loop_event)].tick / tpr == r)
                ImGui::TextColored(th.colors[TC_FX_SONG], "voice loops back to this row");
            for (int ei : cells[v][size_t(r)].events) {
                const Event& e = t.events[size_t(ei)];
                ImGui::Text("t%-4d $%04X  %s%s", e.tick, e.addr, D.event_text(e).c_str(), e.in_sub ? "  (sub)" : e.sub_iter > 0 ? "  (repeat: shared bytes)" : "");
            }
            ImGui::EndTooltip();
        }
    }
    child_end();
    ImGui::PopFont();

    bool edited = false;
    // A block edit writes voice by voice; if one voice cannot be written the
    // voices already done are undone, so the block is all or nothing.
    int block_depth = 0, block_written = 0;
    std::string block_fail;
    auto commit = [&](int voice, std::vector<Event>& ev) {
        if (!block_fail.empty()) return false;
        Tracker::Result r = T.write_track(app.engine, pat_idx, voice, ev);
        app.status = r.msg;
        edited = true;
        if (r.ok) ++block_written;
        else if (block_depth > 0) block_fail = "voice " + std::to_string(voice + 1) + ": " + r.msg;
        return r.ok;
    };
    auto begin_block = [&]() {
        if (block_depth++ == 0) { block_written = 0; block_fail.clear(); }
        app.engine.begin_edit();
    };
    auto end_block = [&]() {
        app.engine.end_edit();
        if (--block_depth > 0 || block_fail.empty()) return;
        if (block_written > 0) { app.engine.revert_edit(); T.pending_release.clear(); app.after_edit(); block_fail += " (the block was left as it was)"; }
        app.status = block_fail;
        block_fail.clear();
    };
    auto track_events = [&](int v) { return editable_events(T.song()->patterns[size_t(pat_idx)].tracks[v]); };
    auto cells_of = [&](int v) -> const std::vector<Cell>& { return cells[v]; };
    auto has_track = [&](int v) { return T.song()->patterns[size_t(pat_idx)].tracks[v].addr != 0; };
    struct Block { int v0, v1, r0, r1, c0, c1; };
    auto block = [&]() -> Block {
        if (app.sel_active) return {sv0, sv1, sr0, sr1, c0, c1};
        int c = app.sel_voice * F_COUNT + app.sel_field;
        return {app.sel_voice, app.sel_voice, app.sel_row, app.sel_row, c, c};
    };
    auto block_has = [&](const Block& b, int v, int f) { int c = v * F_COUNT + f; return c >= b.c0 && c <= b.c1; };
    auto advance = [&](int step) {
        if (step <= 0) return;
        int nr = app.sel_row + step;
        if (nr >= rows) nr = th.wrap_cursor ? nr % rows : rows - 1;
        app.sel_row = nr;
        app.cur_nibble = 0;
    };

    auto set_note_block = [&](uint8_t byte) {
        Block b = block();
        begin_block();
        for (int v = b.v0; v <= b.v1; ++v) {
            if (!block_has(b, v, App::F_NOTE) || !has_track(v)) continue;
            std::vector<Event> ev = track_events(v);
            for (int guard = 0; guard < 64; ++guard) { int hit = -1; for (const Event& e : ev) if (e.in_sub && note_like(e.type) && e.tick / tpr >= b.r0 && e.tick / tpr <= b.r1) { hit = e.tick; break; } if (hit < 0 || !D.unroll_at(ev, hit)) break; }
            bool any = false;
            for (Event& e : ev)
                if (note_like(e.type) && e.tick / tpr >= b.r0 && e.tick / tpr <= b.r1) {
                    D.apply_note_byte(e, byte); any = true;
                }
            if (any) commit(v, ev);
        }
        end_block();
    };
    auto transpose_block = [&](int semis) {
        Block b = block();
        if (!app.sel_active) b.c0 = b.c1 = app.sel_voice * F_COUNT + App::F_NOTE;
        begin_block();
        for (int v = b.v0; v <= b.v1; ++v) {
            if (!block_has(b, v, App::F_NOTE) || !has_track(v)) continue;
            std::vector<Event> ev = track_events(v);
            for (int guard = 0; guard < 64; ++guard) { int hit = -1; for (const Event& e : ev) if (e.in_sub && e.type == EventType::Note && e.tick / tpr >= b.r0 && e.tick / tpr <= b.r1) { hit = e.tick; break; } if (hit < 0 || !D.unroll_at(ev, hit)) break; }
            bool any = false;
            for (Event& e : ev)
                if (e.type == EventType::Note && e.tick / tpr >= b.r0 && e.tick / tpr <= b.r1) {
                    if (D.transpose_event(e, semis)) any = true;
                }
            if (any) commit(v, ev);
        }
        end_block();
    };
    auto value_block = [&](int delta) {
        Block b = block();
        begin_block();
        for (int v = b.v0; v <= b.v1; ++v) {
            if (!has_track(v)) continue;
            std::vector<Event> ev = track_events(v);
            bool any = false;
            for (int r = b.r0; r <= b.r1; ++r) {
                int t0 = r * tpr, t1 = t0 + tpr;
                if (block_has(b, v, App::F_INS)) {
                    int i = ins_event_in(D, ev, t0, t1);
                    if (i >= 0) { ev[size_t(i)].b[1] = uint8_t(std::clamp(int(ev[size_t(i)].b[1]) + delta, 0, 255)); any = true; }
                }
                if (block_has(b, v, App::F_QUANT) || block_has(b, v, App::F_VEL)) {
                    int i = timed_event_at(ev, t0);
                    if (i >= 0 && ev[size_t(i)].type != EventType::Rest) {
                        int qv = -1;
                        for (int k = i - 1; k >= 0; --k) if (ev[size_t(k)].type == EventType::Length && ev[size_t(k)].size == 2) { qv = ev[size_t(k)].b[1]; break; }
                        if (qv >= 0) {
                            int q = qv >> 4, vel = qv & 15;
                            if (block_has(b, v, App::F_QUANT)) q = std::clamp(q + (delta > 0 ? 1 : -1), 0, 7);
                            if (block_has(b, v, App::F_VEL)) vel = std::clamp(vel + (delta > 0 ? 1 : -1), 0, 15);
                            if (D.set_qv(ev, i, q, vel)) any = true;
                        }
                    }
                }
                if (block_has(b, v, App::F_FXARG)) {
                    int i = fx_event_in(D, ev, t0, t1);
                    if (i >= 0 && ev[size_t(i)].type == EventType::Command && ev[size_t(i)].size >= 2) {
                        uint8_t& last = ev[size_t(i)].b[ev[size_t(i)].size - 1];
                        last = uint8_t(std::clamp(int(last) + delta, 0, 255)); any = true;
                    }
                }
            }
            if (any) commit(v, ev);
        }
        end_block();
    };
    auto delete_block = [&]() {
        Block b = block();
        begin_block();
        for (int v = b.v0; v <= b.v1; ++v) {
            if (!has_track(v)) continue;
            std::vector<Event> ev = track_events(v);
            bool any = false;
            for (int r = b.r0; r <= b.r1; ++r) {
                int t0 = r * tpr, t1 = t0 + tpr;
                if (block_has(b, v, App::F_NOTE)) {
                    int i = timed_event_at(ev, t0, stream);
                    if (i >= 0 && (ev[size_t(i)].type == EventType::Note || ev[size_t(i)].type == EventType::Percussion || ev[size_t(i)].type == EventType::Rest)) {
                        D.apply_note_byte(ev[size_t(i)], D.tie_byte() ? D.tie_byte() : D.rest_byte());
                        any = true;
                    }
                }
                if (block_has(b, v, App::F_INS)) {
                    int i = ins_event_in(D, ev, t0, t1);
                    if (i >= 0 && (!in_place || stream)) { Tracker::remove_event(D, ev, i); any = true; }
                }
                if (block_has(b, v, App::F_FX) || block_has(b, v, App::F_FXARG)) {
                    int i = fx_event_in(D, ev, t0, t1);
                    if (i >= 0) {
                        if (block_has(b, v, App::F_FX) && (!in_place || stream)) { Tracker::remove_event(D, ev, i); any = true; }
                        else { for (int k = 1; k < ev[size_t(i)].size; ++k) ev[size_t(i)].b[k] = 0; any = true; }
                    }
                }
            }
            if (any) commit(v, ev);
        }
        end_block();
    };
    auto copy_block = [&](bool cut) {
        Block b = block();
        g_clip.voices = b.v1 - b.v0 + 1; g_clip.rows = b.r1 - b.r0 + 1;
        g_clip.f0 = b.c0 % F_COUNT; g_clip.f1 = b.c1 % F_COUNT;
        g_clip.cells.assign(size_t(g_clip.voices * g_clip.rows), {});
        for (int v = b.v0; v <= b.v1; ++v)
            for (int r = b.r0; r <= b.r1; ++r) {
                const Cell& c = cells[v][size_t(r)];
                const seq::Track& t = pat.tracks[v];
                ClipCell& cc = g_clip.cells[size_t((v - b.v0) * g_clip.rows + (r - b.r0))];
                if (block_has(b, v, App::F_NOTE) && c.note_ev >= 0 && !t.events[size_t(c.note_ev)].in_sub) cc.note = t.events[size_t(c.note_ev)].b[0];
                if (block_has(b, v, App::F_INS) && c.ins_ev >= 0 && !t.events[size_t(c.ins_ev)].in_sub) cc.ins = c.ins;
                if ((block_has(b, v, App::F_QUANT) || block_has(b, v, App::F_VEL)) && c.qv >= 0 && c.note_ev >= 0) cc.qv = c.qv;
                if ((block_has(b, v, App::F_FX) || block_has(b, v, App::F_FXARG)) && c.fx_ev >= 0 && !t.events[size_t(c.fx_ev)].in_sub) {
                    const Event& e = t.events[size_t(c.fx_ev)];
                    cc.fx_size = e.size; std::memcpy(cc.fx, e.b, sizeof cc.fx); cc.fx_sub = e.type == EventType::SubCall;
                }
            }
        char m[64]; std::snprintf(m, sizeof m, "%s %dx%d cells", cut ? "cut" : "copied", g_clip.voices, g_clip.rows);
        app.status = m;
        if (cut) delete_block();
    };
    auto paste_block = [&](bool mix) {
        if (g_clip.voices <= 0) return;
        begin_block();
        for (int cv = 0; cv < g_clip.voices; ++cv) {
            int v = app.sel_voice + cv;
            if (v > 7) break;
            std::vector<Event> ev = track_events(v);
            bool any = false;
            for (int cr = 0; cr < g_clip.rows; ++cr) {
                int r = app.sel_row + cr;
                if (r >= rows) break;
                const ClipCell& cc = g_clip.cells[size_t(cv * g_clip.rows + cr)];
                int t0 = r * tpr, t1 = t0 + tpr;
                if (cc.note >= 0) {
                    int cur = timed_event_at(ev, t0);
                    bool empty = cur < 0 || ev[size_t(cur)].type == EventType::Tie;
                    if (!mix || empty)
                        if (D.set_note_at(ev, t0, uint8_t(cc.note), pat.length_ticks)) any = true;
                }
                if (cc.qv >= 0) {
                    int i = timed_event_at(ev, t0);
                    if (i >= 0 && D.set_qv(ev, i, cc.qv >> 4, cc.qv & 15)) any = true;
                }
                if (cc.ins >= 0 && (!mix || ins_event_in(D, ev, t0, t1) < 0))
                    if (D.set_instrument(ev, t0, t1, uint8_t(cc.ins))) any = true;
                if (cc.fx_size > 0 && !cc.fx_sub) {
                    int i = fx_event_in(D, ev, t0, t1);
                    if (i >= 0 && !mix && (!in_place || stream || ev[size_t(i)].size == cc.fx_size)) { ev[size_t(i)].size = uint8_t(cc.fx_size); std::memcpy(ev[size_t(i)].b, cc.fx, sizeof cc.fx); ev[size_t(i)].type = EventType::Command; any = true; }
                    else if (i < 0) { if (D.insert_command_at(ev, t0, cc.fx, cc.fx_size)) any = true; }
                }
            }
            if (any) commit(v, ev);
        }
        end_block();
    };
    auto interpolate_block = [&]() {
        Block b = block();
        if (b.r1 - b.r0 < 2) { app.status = "select at least three rows to interpolate"; return; }
        begin_block();
        for (int v = b.v0; v <= b.v1; ++v) {
            if (!has_track(v)) continue;
            std::vector<Event> ev = track_events(v);
            bool any = false;
            auto lerp_field = [&](int f, auto get, auto set) {
                if (!block_has(b, v, f)) return;
                int a = get(b.r0), z = get(b.r1);
                if (a < 0 || z < 0) return;
                for (int r = b.r0 + 1; r < b.r1; ++r) {
                    int val = a + (z - a) * (r - b.r0) / (b.r1 - b.r0);
                    if (set(r, val)) any = true;
                }
            };
            auto qv_before = [&](int i) {
                for (int k = i - 1; k >= 0; --k)
                    if (ev[size_t(k)].type == EventType::Length && ev[size_t(k)].size == 2) return int(ev[size_t(k)].b[1]);
                return -1;
            };
            auto get_vel = [&](int r) {
                int i = timed_event_at(ev, r * tpr);
                if (i < 0) return -1;
                int qv = qv_before(i);
                return qv < 0 ? -1 : qv & 15;
            };
            auto set_vel = [&](int r, int val) {
                int i = timed_event_at(ev, r * tpr);
                if (i < 0) return false;
                int qv = qv_before(i);
                return D.set_qv(ev, i, (qv < 0 ? 0x70 : qv) >> 4, val);
            };
            auto get_arg = [&](int r) { int i = fx_event_in(D, ev, r * tpr, r * tpr + tpr); if (i < 0 || ev[size_t(i)].size < 2) return -1; return int(ev[size_t(i)].b[ev[size_t(i)].size - 1]); };
            auto set_arg = [&](int r, int val) { int i = fx_event_in(D, ev, r * tpr, r * tpr + tpr); if (i < 0 || ev[size_t(i)].size < 2) return false; ev[size_t(i)].b[ev[size_t(i)].size - 1] = uint8_t(val); return true; };
            lerp_field(App::F_VEL, get_vel, set_vel);
            lerp_field(App::F_FXARG, get_arg, set_arg);
            if (any) commit(v, ev);
        }
        end_block();
    };
    auto transform_values = [&](auto fn) {
        Block b = block();
        begin_block();
        const int n = b.r1 - b.r0 + 1;
        for (int v = b.v0; v <= b.v1; ++v) {
            if (!has_track(v)) continue;
            std::vector<Event> ev = track_events(v);
            bool any = false;
            for (int r = b.r0; r <= b.r1; ++r) {
                const int t0 = r * tpr, t1 = t0 + tpr, k = r - b.r0;
                if (block_has(b, v, App::F_INS)) {
                    int i = ins_event_in(D, ev, t0, t1);
                    if (i >= 0) { ev[size_t(i)].b[1] = uint8_t(std::clamp(fn(k, n, int(ev[size_t(i)].b[1]), 255), 0, 255)); any = true; }
                }
                if (block_has(b, v, App::F_QUANT) || block_has(b, v, App::F_VEL)) {
                    int i = timed_event_at(ev, t0);
                    if (i >= 0 && ev[size_t(i)].type != EventType::Rest) {
                        int qv = -1;
                        for (int q = i - 1; q >= 0; --q) if (ev[size_t(q)].type == EventType::Length && ev[size_t(q)].size == 2) { qv = ev[size_t(q)].b[1]; break; }
                        if (qv >= 0) {
                            int qq = qv >> 4, vel = qv & 15;
                            if (block_has(b, v, App::F_QUANT)) qq = std::clamp(fn(k, n, qq, 7), 0, 7);
                            if (block_has(b, v, App::F_VEL)) vel = std::clamp(fn(k, n, vel, 15), 0, 15);
                            if (D.set_qv(ev, i, qq, vel)) any = true;
                        }
                    }
                }
                if (block_has(b, v, App::F_FXARG)) {
                    int i = fx_event_in(D, ev, t0, t1);
                    if (i >= 0 && ev[size_t(i)].type == EventType::Command && ev[size_t(i)].size >= 2) {
                        uint8_t& last = ev[size_t(i)].b[ev[size_t(i)].size - 1];
                        last = uint8_t(std::clamp(fn(k, n, int(last), 255), 0, 255)); any = true;
                    }
                }
            }
            if (any) commit(v, ev);
        }
        end_block();
    };
    auto remap_rows = [&](auto map) {
        Block b = block();
        const int n = b.r1 - b.r0 + 1;
        std::vector<ClipCell> cells(size_t((b.v1 - b.v0 + 1) * n));
        for (int v = b.v0; v <= b.v1; ++v)
            for (int r = b.r0; r <= b.r1; ++r) {
                const Cell& c = cells_of(v)[size_t(r)];
                const seq::Track& t = pat.tracks[v];
                ClipCell& cc = cells[size_t((v - b.v0) * n + (r - b.r0))];
                if (block_has(b, v, App::F_NOTE) && c.note_ev >= 0 && !t.events[size_t(c.note_ev)].in_sub) cc.note = t.events[size_t(c.note_ev)].b[0];
                if (block_has(b, v, App::F_INS) && c.ins_ev >= 0 && !t.events[size_t(c.ins_ev)].in_sub) cc.ins = c.ins;
                if ((block_has(b, v, App::F_QUANT) || block_has(b, v, App::F_VEL)) && c.qv >= 0 && c.note_ev >= 0) cc.qv = c.qv;
                if ((block_has(b, v, App::F_FX) || block_has(b, v, App::F_FXARG)) && c.fx_ev >= 0 && !t.events[size_t(c.fx_ev)].in_sub && t.events[size_t(c.fx_ev)].type == EventType::Command) {
                    cc.fx_size = t.events[size_t(c.fx_ev)].size; std::memcpy(cc.fx, t.events[size_t(c.fx_ev)].b, sizeof cc.fx);
                }
            }
        begin_block();
        delete_block();
        for (int v = b.v0; v <= b.v1; ++v) {
            if (!has_track(v)) continue;
            std::vector<Event> ev = track_events(v);
            bool any = false;
            for (int k = 0; k < n; ++k) {
                int dst = map(k, n);
                if (dst < 0 || dst >= n) continue;
                const ClipCell& cc = cells[size_t((v - b.v0) * n + k)];
                const int r = b.r0 + dst, t0 = r * tpr, t1 = t0 + tpr;
                if (r >= rows) continue;
                if (cc.note >= 0 && D.set_note_at(ev, t0, uint8_t(cc.note), pat.length_ticks)) any = true;
                if (cc.qv >= 0) { int i = timed_event_at(ev, t0); if (i >= 0 && D.set_qv(ev, i, cc.qv >> 4, cc.qv & 15)) any = true; }
                if (cc.ins >= 0 && D.set_instrument(ev, t0, t1, uint8_t(cc.ins))) any = true;
                if (cc.fx_size > 0 && fx_event_in(D, ev, t0, t1) < 0 && D.insert_command_at(ev, t0, cc.fx, cc.fx_size)) any = true;
            }
            if (any) commit(v, ev);
        }
        end_block();
    };
    auto paste_flood = [&]() {
        if (g_clip.voices <= 0 || g_clip.rows <= 0) return;
        const int last = app.sel_active ? std::max(app.sel_r0, app.sel_r1) : rows - 1;
        const int start_row = app.sel_active ? std::min(app.sel_r0, app.sel_r1) : app.sel_row;
        const int start_voice = app.sel_active ? std::min(app.sel_v0, app.sel_v1) : app.sel_voice;
        const int keep_row = app.sel_row, keep_voice = app.sel_voice;
        begin_block();
        app.sel_voice = start_voice;
        for (int r = start_row; r <= last; r += g_clip.rows) { app.sel_row = r; paste_block(false); }
        end_block();
        app.sel_row = keep_row; app.sel_voice = keep_voice;
    };
    auto current_instrument = [&]() -> int {
        int count = D.instrument_count(app.snap.ram);
        if (!D.has_instruments() || app.sel_instrument < 0 || app.sel_instrument >= count) return -1;
        return D.instrument_number(app.snap.ram, app.sel_instrument);
    };
    auto ins_set_block = [&]() {
        int ins = current_instrument();
        if (ins < 0) { app.status = "no instrument selected in the Instruments panel"; return; }
        Block b = block();
        begin_block();
        for (int v = b.v0; v <= b.v1; ++v) {
            if (!has_track(v)) continue;
            std::vector<Event> ev = track_events(v);
            bool any = false;
            for (int r = b.r0; r <= b.r1; ++r) {
                const int t0 = r * tpr, t1 = t0 + tpr;
                int i = ins_event_in(D, ev, t0, t1);
                if (i >= 0) { if (ev[size_t(i)].b[1] != ins) { ev[size_t(i)].b[1] = uint8_t(ins); any = true; } }
                else if (timed_event_at(ev, t0) >= 0 && ev[size_t(timed_event_at(ev, t0))].type == EventType::Note && D.set_instrument(ev, t0, t1, uint8_t(ins))) any = true;
            }
            if (any) commit(v, ev);
        }
        end_block();
    };
    auto pull_delete = [&]() {
        if (!has_track(app.sel_voice)) return;
        std::vector<Event> ev = track_events(app.sel_voice);
        if (D.remove_span(ev, app.sel_row * tpr, tpr, true)) commit(app.sel_voice, ev);
        else app.status = stream ? "cannot remove that row: it lies in a subroutine (shared bytes)" : in_place ? "this driver's streams cannot change length" : "cannot remove that row (subroutine data)";
    };
    auto insert_row = [&]() {
        if (!has_track(app.sel_voice)) return;
        std::vector<Event> ev = track_events(app.sel_voice);
        if (D.insert_span(ev, app.sel_row * tpr, tpr, D.tie_byte() ? D.tie_byte() : D.rest_byte())) commit(app.sel_voice, ev);
        else app.status = stream ? "cannot insert a row there: it lies in a subroutine (shared bytes)" : in_place ? "this driver's streams cannot change length" : "cannot insert a row there (subroutine data or end of track)";
    };

    auto move_cursor = [&](int dv_fields, int dr, bool extend) {
        int col = app.sel_voice * F_COUNT + app.sel_field + dv_fields;
        int maxc = 8 * F_COUNT - 1;
        if (col < 0) col = th.wrap_cursor ? maxc : 0;
        if (col > maxc) col = th.wrap_cursor ? 0 : maxc;
        int nr = app.sel_row + dr;
        if (dr == 1 && nr >= rows) nr = th.wrap_cursor ? 0 : rows - 1;
        else if (dr == -1 && nr < 0) nr = th.wrap_cursor ? rows - 1 : 0;
        nr = std::clamp(nr, 0, rows - 1);
        int nv = col / F_COUNT, nf = col % F_COUNT;
        for (int guard = 0; guard < 8 * F_COUNT && field_hidden(app, nv, nf); ++guard) {
            col += dv_fields > 0 ? 1 : dv_fields < 0 ? -1 : 1;
            if (col < 0) col = th.wrap_cursor ? maxc : 0;
            if (col > maxc) col = th.wrap_cursor ? 0 : maxc;
            nv = col / F_COUNT; nf = col % F_COUNT;
            if (!th.wrap_cursor && (col == 0 || col == maxc) && field_hidden(app, nv, nf)) { nf = App::F_NOTE; break; }
        }
        if (extend) {
            if (!app.sel_active) { app.sel_v0 = app.sel_voice; app.sel_f0 = app.sel_field; app.sel_r0 = app.sel_row; }
            app.sel_active = true; app.sel_v1 = nv; app.sel_f1 = nf; app.sel_r1 = nr;
        } else app.sel_active = false;
        app.sel_voice = nv; app.sel_field = nf; app.sel_row = nr; app.cur_nibble = 0; app.sel_all_stage = 0;
    };
    auto move_channel = [&](int dv, bool extend) {
        int nv = app.sel_voice + dv;
        if (nv < 0) nv = th.wrap_cursor ? 7 : 0;
        if (nv > 7) nv = th.wrap_cursor ? 0 : 7;
        move_cursor((nv - app.sel_voice) * F_COUNT, 0, extend);
    };
    const int coarse = std::max(4, th.row_hi2);

    if (app.preview_key >= 0 && (ImGui::IsKeyReleased(ImGuiKey(app.preview_key)) || !grid_focused)) {
        app.engine.preview_off();
        app.preview_key = -1;
    }

    if (app.insert_fx_op >= 0 && app.sel_voice >= 0 && app.sel_row >= 0) {
        const uint8_t op = uint8_t(app.insert_fx_op);
        app.insert_fx_op = -1;
        if (!app.edit_mode) app.status = std::string("edit mode is off - press ") + action_shortcut(A_EDIT_TOGGLE) + " to add commands";
        else if (!has_track(app.sel_voice)) app.status = "this voice has no track yet: enter a note first";
        else if (in_place && !stream) app.status = "commands cannot be added: this driver's streams are patched in place";
        else {
            int size = D.cmd_size(op);
            std::vector<Event> ev = track_events(app.sel_voice);
            uint8_t bytes[16] = {op};
            const bool copied = Tracker::default_args(D, pat, app.sel_voice, app.sel_row * tpr, bytes, size);
            if (size > 0 && size <= 16 && D.insert_command_at(ev, app.sel_row * tpr, bytes, size)) {
                if (commit(app.sel_voice, ev) && size > 1) app.status += copied ? " - arguments copied from the last use; edit them on the row" : " - arguments are 00; edit them on the row";
            } else app.status = "cannot add a command on that row";
        }
    } else if (app.insert_fx_op >= 0) { app.insert_fx_op = -1; app.status = "click a cell first: the command goes at the cursor"; }
    const int pending = app.pending_action;
    app.pending_action = -1;
    auto act = [&](Action a) { return pending == a || (grid_focused && !io.WantTextInput && action_pressed(a)); };
    if (((grid_focused && !io.WantTextInput) || pending >= 0) && app.sel_voice >= 0 && app.sel_row >= 0) {
        app.sel_row = std::min(app.sel_row, rows - 1);
        const bool on_note = app.sel_field == App::F_NOTE;
        if (on_note && !io.KeyCtrl && !io.KeyAlt) {
            ImGuiKey key = ImGuiKey_None;
            int semi = key_to_semitone(&key);
            if (semi >= 0) {
                uint8_t byte = D.note_byte(app.octave * 12 + semi);
                preview_note(app, pat, byte);
                app.preview_key = key;
            }
        }
        bool handled = true;
        if      (act(A_CUR_UP))          move_cursor(0, -1, false);
        else if (act(A_CUR_DOWN))        move_cursor(0, 1, false);
        else if (act(A_CUR_LEFT))        move_cursor(-1, 0, false);
        else if (act(A_CUR_RIGHT))       move_cursor(1, 0, false);
        else if (act(A_CUR_UP_COARSE))   move_cursor(0, -std::min(coarse, app.sel_row), false);
        else if (act(A_CUR_DOWN_COARSE)) move_cursor(0, std::min(coarse, rows - 1 - app.sel_row), false);
        else if (act(A_CUR_BEGIN))       move_cursor(0, -app.sel_row, false);
        else if (act(A_CUR_END))         move_cursor(0, rows - 1 - app.sel_row, false);
        else if (act(A_CUR_PREV_CH))     move_channel(-1, false);
        else if (act(A_CUR_NEXT_CH))     move_channel(1, false);
        else if (act(A_SEL_UP))          move_cursor(0, -1, true);
        else if (act(A_SEL_DOWN))        move_cursor(0, 1, true);
        else if (act(A_SEL_LEFT))        move_cursor(-1, 0, true);
        else if (act(A_SEL_RIGHT))       move_cursor(1, 0, true);
        else if (act(A_SEL_UP_COARSE))   move_cursor(0, -std::min(coarse, app.sel_row), true);
        else if (act(A_SEL_DOWN_COARSE)) move_cursor(0, std::min(coarse, rows - 1 - app.sel_row), true);
        else if (act(A_SEL_BEGIN))       move_cursor(0, -app.sel_row, true);
        else if (act(A_SEL_END))         move_cursor(0, rows - 1 - app.sel_row, true);
        else if (act(A_SEL_CLEAR))       { app.sel_active = false; app.sel_all_stage = 0; app.cur_nibble = 0; }
        else if (act(A_SEL_ALL)) {
            app.sel_active = true; app.sel_r0 = 0; app.sel_r1 = rows - 1;
            if (app.sel_all_stage == 0) { app.sel_v0 = app.sel_v1 = app.sel_voice; app.sel_f0 = 0; app.sel_f1 = F_COUNT - 1; app.sel_all_stage = 1; }
            else { app.sel_v0 = 0; app.sel_f0 = 0; app.sel_v1 = 7; app.sel_f1 = F_COUNT - 1; app.sel_all_stage = 2; }
        }
        else if (act(A_MUTE_CURSOR))     app.engine.set_mute_mask(app.engine.mute_mask() ^ (1 << app.sel_voice));
        else if (act(A_SOLO_CURSOR)) {
            int solo = 0xFF & ~(1 << app.sel_voice);
            app.engine.set_mute_mask(app.engine.mute_mask() == solo ? 0 : solo);
        }
        else if (act(A_COPY))            copy_block(false);
        else if (act(A_VOICE_FOLD)) {
            app.voice_collapsed[app.sel_voice] = !app.voice_collapsed[app.sel_voice];
            if (field_hidden(app, app.sel_voice, app.sel_field)) app.sel_field = App::F_NOTE;
        }
        else if (!app.edit_mode) {
            handled = false;
            if (!io.KeyCtrl && !io.KeyAlt && !on_note && key_to_hex() >= 0)
                app.status = std::string("edit mode is off - press ") + action_shortcut(A_EDIT_TOGGLE) + " to edit";
        }
        else if (act(A_CUT))             copy_block(true);
        else if (act(A_PASTE))           paste_block(false);
        else if (act(A_PASTE_MIX))       paste_block(true);
        else if (act(A_TRANSPOSE_UP))    transpose_block(1);
        else if (act(A_TRANSPOSE_DOWN))  transpose_block(-1);
        else if (act(A_TRANSPOSE_OCT_UP))   transpose_block(12);
        else if (act(A_TRANSPOSE_OCT_DOWN)) transpose_block(-12);
        else if (act(A_VALUE_UP))        value_block(1);
        else if (act(A_VALUE_DOWN))      value_block(-1);
        else if (act(A_VALUE_UP_COARSE)) value_block(16);
        else if (act(A_VALUE_DOWN_COARSE)) value_block(-16);
        else if (act(A_INTERPOLATE))     interpolate_block();
        else if (act(A_FADE))            app.value_dialog = A_FADE;
        else if (act(A_SCALE))           app.value_dialog = A_SCALE;
        else if (act(A_RANDOMIZE))       app.value_dialog = A_RANDOMIZE;
        else if (act(A_INVERT))          transform_values([](int, int, int val, int max) { return max - val; });
        else if (act(A_FLIP))            remap_rows([](int k, int n) { return n - 1 - k; });
        else if (act(A_COLLAPSE))        remap_rows([](int k, int) { return k % 2 == 0 ? k / 2 : -1; });
        else if (act(A_EXPAND))          remap_rows([](int k, int n) { return k * 2 < n ? k * 2 : -1; });
        else if (act(A_INS_SET))         ins_set_block();
        else if (act(A_PASTE_FLOOD))     paste_flood();
        else if (act(A_DELETE))          delete_block();
        else if (act(A_PULL_DELETE))     pull_delete();
        else if (act(A_INSERT))          insert_row();
        else if ((on_note || app.sel_active) && act(A_NOTE_OFF)) { set_note_block(D.rest_byte()); if (!app.sel_active) advance(th.edit_step); }
        else if ((on_note || app.sel_active) && act(A_NOTE_TIE)) {
            if (D.tie_byte()) { set_note_block(D.tie_byte()); if (!app.sel_active) advance(th.edit_step); }
            else app.status = "this driver has no tie";
        }
        else handled = false;

        if (!handled && grid_focused && !io.WantTextInput && app.edit_mode && !io.KeyCtrl && !io.KeyAlt) {
            const int v = app.sel_voice, t0 = app.sel_row * tpr, t1 = t0 + tpr;
            if (on_note) {
                int semi = key_to_semitone();
                if (semi >= 0) {
                    std::vector<Event> ev = track_events(v);
                    if (D.enter_note(ev, t0, app.octave * 12 + semi, pat.length_ticks)) {
                        if (th.note_writes_ins) {
                            int ins = current_instrument();
                            if (ins >= 0 && preview_instrument(app, pat, v, t0) != ins) D.set_instrument(ev, t0, t1, uint8_t(ins));
                        }
                        if (commit(v, ev)) advance(th.edit_step);
                    } else app.status = "could not place the note there (the driver refused the encoding)";
                }
            } else {
                int hex = key_to_hex();
                if (hex >= 0 && !has_track(v)) app.status = "this voice has no track yet: enter a note first";
                if (hex >= 0 && has_track(v)) {
                    std::vector<Event> ev = track_events(v);
                    bool any = false, complete = false;
                    switch (app.sel_field) {
                        case App::F_INS: {
                            int i = ins_event_in(D, ev, t0, t1);
                            int cur = i >= 0 ? ev[size_t(i)].b[1] : 0;
                            int val = app.cur_nibble == 0 ? (hex << 4) | (cur & 0x0F) : (cur & 0xF0) | hex;
                            any = D.set_instrument(ev, t0, t1, uint8_t(val));
                            if (!any) app.status = "no instrument command on this row to change";
                            complete = app.cur_nibble == 1;
                            app.cur_nibble ^= 1;
                            break;
                        }
                        case App::F_QUANT: case App::F_VEL: {
                            int i = timed_event_at(ev, t0);
                            if (i < 0) { app.status = "no note starts on this row"; break; }
                            int qv = 0x7F;
                            for (int k = i - 1; k >= 0; --k) if (ev[size_t(k)].type == EventType::Length && ev[size_t(k)].size == 2) { qv = ev[size_t(k)].b[1]; break; }
                            int q = qv >> 4, vel = qv & 15;
                            if (app.sel_field == App::F_QUANT) { if (hex > 7) break; q = hex; } else vel = hex;
                            any = D.set_qv(ev, i, q, vel);
                            if (!any) app.status = "this driver has no quantise/velocity";
                            complete = true;
                            break;
                        }
                        case App::F_FX: {
                            static int pending = 0;
                            if (app.cur_nibble == 0) { pending = hex; app.cur_nibble = 1; break; }
                            int op = (pending << 4) | hex;
                            app.cur_nibble = 0;
                            int size = D.is_command(uint8_t(op)) ? D.cmd_size(uint8_t(op)) : 0;
                            if (size <= 0 || size > 16 || D.is_instrument_cmd(uint8_t(op))) { app.status = "not a command opcode for this driver"; break; }
                            int i = fx_event_in(D, ev, t0, t1);
                            if (i >= 0 && ev[size_t(i)].type == EventType::Command) {
                                Event& e = ev[size_t(i)];
                                const int old_size = e.size;
                                if (in_place && !stream && old_size != size) { app.status = "in place: the new command must have the same size"; break; }
                                if (e.in_sub && old_size != size) { app.status = "inside a subroutine (shared bytes): the new command must have the same size"; break; }
                                if (old_size != size) e.addr = 0;
                                e.b[0] = uint8_t(op);
                                for (int k = 1; k < 16; ++k) if (k >= old_size || k >= size) e.b[k] = 0;
                                e.size = uint8_t(size);
                            } else {
                                uint8_t bytes[16] = {uint8_t(op)};
                                if (!D.insert_command_at(ev, t0, bytes, size)) { app.status = "cannot insert a command here"; break; }
                            }
                            any = true; complete = true;
                            break;
                        }
                        case App::F_FXARG: {
                            int i = fx_event_in(D, ev, t0, t1);
                            if (i < 0 || ev[size_t(i)].type != EventType::Command || ev[size_t(i)].size < 2) { app.status = "no effect with arguments on this row"; break; }
                            Event& e = ev[size_t(i)];
                            int nibbles = std::min(4, (e.size - 1) * 2);
                            int n = app.cur_nibble % nibbles;
                            int byte = 1 + n / 2;
                            e.b[byte] = uint8_t(n % 2 == 0 ? (hex << 4) | (e.b[byte] & 0x0F) : (e.b[byte] & 0xF0) | hex);
                            app.cur_nibble = (n + 1) % nibbles;
                            complete = app.cur_nibble == 0;
                            any = true;
                            break;
                        }
                        default: break;
                    }
                    if (any) {
                        int keep_nibble = app.cur_nibble;
                        if (commit(v, ev)) { app.cur_nibble = keep_nibble; if (complete && th.step_on_hex) advance(th.edit_step); }
                    }
                }
            }
        }
    }
    if (open_context) ImGui::OpenPopup("patmenu");
    if (ImGui::BeginPopup("patmenu")) {
        auto item = [&](const char* label, Action a, bool enabled = true) {
            if (ImGui::MenuItem(label, action_shortcut(a), false, enabled)) app.pending_action = a;
        };
        const bool can_edit = app.edit_mode;
        item("Cut", A_CUT); item("Copy", A_COPY);
        item("Paste", A_PASTE, can_edit); item("Paste mix", A_PASTE_MIX, can_edit); item("Paste flood", A_PASTE_FLOOD, can_edit);
        ImGui::Separator();
        item("Delete", A_DELETE, can_edit); item("Pull delete", A_PULL_DELETE, can_edit); item("Insert row", A_INSERT, can_edit);
        ImGui::Separator();
        item("Note up", A_TRANSPOSE_UP, can_edit); item("Note down", A_TRANSPOSE_DOWN, can_edit);
        item("Octave up", A_TRANSPOSE_OCT_UP, can_edit); item("Octave down", A_TRANSPOSE_OCT_DOWN, can_edit);
        ImGui::Separator();
        item("Value up", A_VALUE_UP, can_edit); item("Value down", A_VALUE_DOWN, can_edit);
        item("Value up (coarse)", A_VALUE_UP_COARSE, can_edit); item("Value down (coarse)", A_VALUE_DOWN_COARSE, can_edit);
        ImGui::Separator();
        item("Interpolate", A_INTERPOLATE, can_edit); item("Fade...", A_FADE, can_edit); item("Scale...", A_SCALE, can_edit);
        item("Randomize...", A_RANDOMIZE, can_edit); item("Invert values", A_INVERT, can_edit);
        ImGui::Separator();
        item("Flip selection", A_FLIP, can_edit); item("Collapse rows", A_COLLAPSE, can_edit); item("Expand rows", A_EXPAND, can_edit);
        ImGui::Separator();
        item("Set instrument to current", A_INS_SET, can_edit);
        item("Select all", A_SEL_ALL);
        item(app.sel_voice >= 0 && app.voice_collapsed[app.sel_voice] ? "Unfold voice column" : "Fold voice column (note + fx only)", A_VOICE_FOLD);
        if (!can_edit) { ImGui::Separator(); ImGui::TextDisabled("edit mode is off (%s)", action_shortcut(A_EDIT_TOGGLE)); }
        ImGui::EndPopup();
    }
    {
        static int dlg_kind = -1;
        static int fade_from = 0, fade_to = 255, scale_pct = 100, rnd_min = 0, rnd_max = 255;
        if (app.value_dialog >= 0) { dlg_kind = app.value_dialog; app.value_dialog = -1; ImGui::OpenPopup("Values"); }
        if (ImGui::BeginPopupModal("Values", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextDisabled("Applies to the instrument, quantise / velocity and effect argument\nfields inside the selection (values scale to each field's range).");
            bool apply = false;
            ImGui::PushItemWidth(input_int_w(4));
            if (dlg_kind == A_FADE) {
                ImGui::Text("Fade");
                ImGui::InputInt("from (0-255)", &fade_from); ImGui::InputInt("to (0-255)", &fade_to);
                fade_from = std::clamp(fade_from, 0, 255); fade_to = std::clamp(fade_to, 0, 255);
            } else if (dlg_kind == A_SCALE) {
                ImGui::Text("Scale");
                ImGui::InputInt("percent", &scale_pct); scale_pct = std::clamp(scale_pct, 0, 400);
            } else {
                ImGui::Text("Randomize");
                ImGui::InputInt("min (0-255)", &rnd_min); ImGui::InputInt("max (0-255)", &rnd_max);
                rnd_min = std::clamp(rnd_min, 0, 255); rnd_max = std::clamp(rnd_max, rnd_min, 255);
            }
            ImGui::PopItemWidth();
            if (ImGui::Button("Apply") || ImGui::IsKeyPressed(ImGuiKey_Enter)) apply = true;
            ImGui::SameLine();
            if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
            if (apply) {
                const int a = fade_from, z = fade_to, pct = scale_pct, lo = rnd_min, hi = rnd_max;
                if (dlg_kind == A_FADE) transform_values([a, z](int k, int n, int, int max) { int v = n > 1 ? a + (z - a) * k / (n - 1) : a; return (v * max + 127) / 255; });
                else if (dlg_kind == A_SCALE) transform_values([pct](int, int, int val, int) { return (val * pct + 50) / 100; });
                else transform_values([lo, hi](int, int, int, int max) { int v = lo + (hi > lo ? std::rand() % (hi - lo + 1) : 0); return (v * max + 127) / 255; });
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
    if (edited) { panel_end(); return; }

    panel_end();
}
