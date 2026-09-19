// Piano roll tab: one voice as bars on a pitch/time canvas, other voices as
// ghosts. Every edit goes through the same driver primitives as the grid.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "fonts.hpp"
#include "imgui.h"
#include "theme.hpp"
#include "ui.hpp"

using seq::Event;
using seq::EventType;

namespace {
constexpr int kSemitones = 120;   // C-0 .. B-9
constexpr float kZoomMin = 0.05f, kZoomMax = 32.0f;     // horizontal: a row from a sliver to a screen
constexpr float kZoomYMin = 0.4f, kZoomYMax = 4.0f;     // vertical: semitone row height factor

struct Bar {
    int ev = -1;          // first event index
    int tick = 0, dur = 0;
    int semitone = -1;    // -1 for percussion
    int perc = -1;        // percussion index when semitone < 0
    int ins = -1;         // instrument in force
    bool in_sub = false;
    bool attack = true;   // keys on (false: legato / slur continuation)
    int release = -1;     // ticks after which the note keys off early (N-SPC quantise), -1 = none
    bool muted = false;   // a rest in the data standing in for this note (App::muted)
};

std::vector<Bar> bars_of(const seq::Driver& D, const seq::Track& t) {
    std::vector<Bar> out;
    int ins = -1, qv = -1;
    for (int i = 0; i < t.used_events; ++i) {
        const Event& e = t.events[size_t(i)];
        if (e.type == EventType::Command && D.is_instrument_cmd(e.b[0])) { ins = D.instrument_arg(e); continue; }
        if (e.type == EventType::Length) { if (e.size == 2) qv = e.b[1]; continue; }
        if (e.type == EventType::Tie && !out.empty() && out.back().tick + out.back().dur == e.tick && out.back().semitone >= 0) {
            Bar& b = out.back();
            if (b.release >= 0) b.release = b.dur + e.duration * (qv >= 0 ? ((qv >> 4) + 1) : 8) / 8;
            b.dur += e.duration;
            continue;
        }
        if (e.duration <= 0) continue;
        if (e.type == EventType::Note || e.type == EventType::Percussion) {
            Bar b;
            b.ev = i; b.tick = e.tick; b.dur = e.duration; b.ins = ins; b.in_sub = e.in_sub;
            if (e.type == EventType::Note) b.semitone = D.event_semitone(e); else b.perc = D.event_percussion(e);
            b.attack = D.note_retriggers(t.events, i);
            if (D.has_qv() && qv >= 0 && (qv >> 4) < 7) b.release = std::max(1, e.duration * ((qv >> 4) + 1) / 8);
            out.push_back(b);
        }
    }
    return out;
}

int covering(const std::vector<Event>& ev, int tick) {
    for (int i = 0; i < int(ev.size()); ++i) {
        const Event& e = ev[size_t(i)];
        if (e.duration > 0 && !e.in_sub && e.tick <= tick && tick < e.tick + e.duration) return i;
    }
    return -1;
}
int timed_at(const std::vector<Event>& ev, int tick) {
    for (int i = 0; i < int(ev.size()); ++i)
        if (ev[size_t(i)].duration > 0 && !ev[size_t(i)].in_sub && ev[size_t(i)].tick == tick) return i;
    return -1;
}
int end_tick(const std::vector<Event>& ev) {
    int t = 0;
    for (const Event& e : ev) t = std::max(t, e.tick + e.duration);
    return t;
}
int instrument_at(const seq::Driver& D, const std::vector<Event>& ev, int tick) {
    int ins = -1;
    for (const Event& e : ev) {
        if (e.tick > tick) break;
        if (e.type == EventType::Command && D.is_instrument_cmd(e.b[0])) ins = D.instrument_arg(e);
    }
    return ins;
}

bool erase_note(const seq::Driver& D, std::vector<Event>& ev, int tick, int pat_len, std::string& why) {
    int i = timed_at(ev, tick);
    if (i < 0) { why = "no note starts there"; return false; }
    if (!D.set_note_at(ev, tick, D.rest_byte(), pat_len)) { why = "cannot change that note (shared bytes?)"; return false; }
    D.retime(ev);
    if (D.tie_byte()) {
        int t = tick;
        for (;;) {
            int k = timed_at(ev, t);
            if (k < 0) break;
            t = ev[size_t(k)].tick + ev[size_t(k)].duration;
            int n = timed_at(ev, t);
            if (n < 0 || ev[size_t(n)].type != EventType::Tie) break;
            if (!D.set_note_at(ev, t, D.rest_byte(), pat_len)) break;
            D.retime(ev);
        }
    }
    return true;
}

bool place_note(const seq::Driver& D, std::vector<Event>& ev, int tick, int semitone, int len, int pat_len, int ins, std::string& why) {
    D.retime(ev);
    if (tick < 0) { why = "before the start"; return false; }
    if (tick + std::max(1, len) > end_tick(ev)) D.extend(ev, std::max(pat_len, tick + std::max(1, len)));
    D.retime(ev);
    const int track_end = end_tick(ev);
    if (tick >= track_end) { why = "outside the track"; return false; }
    int end = std::min(tick + std::max(1, len), track_end);
    if (end < track_end) {
        int c = covering(ev, end);
        if (c >= 0 && ev[size_t(c)].tick < end) {
            uint8_t keep = ev[size_t(c)].b[0];
            if (!D.set_note_at(ev, end, keep, pat_len)) { why = "cannot split the note under the end (shared bytes?)"; return false; }
            D.retime(ev);
        }
    }
    if (!D.enter_note(ev, tick, semitone, pat_len)) { why = "cannot start a note there (shared bytes, or the octave is out of range)"; return false; }
    D.retime(ev);
    int i = timed_at(ev, tick);
    if (i < 0) { why = "note lost after the split"; return false; }
    int have = ev[size_t(i)].duration;
    if (tick + have < end) {
        if (D.tie_byte()) {
            for (int t = tick + have; t < end;) {
                int k = timed_at(ev, t);
                if (k < 0) break;
                if (ev[size_t(k)].type != EventType::Tie && !D.set_note_at(ev, t, D.tie_byte(), pat_len)) break;
                D.retime(ev);
                k = timed_at(ev, t);
                t = k >= 0 ? ev[size_t(k)].tick + ev[size_t(k)].duration : end;
            }
        } else {
            const int extra = end - (tick + have);
            bool ok = D.remove_span(ev, tick + have, extra, false);
            if (ok) { D.retime(ev); int k = timed_at(ev, tick); ok = k >= 0 && D.set_duration(ev, k, ev[size_t(k)].duration + extra); }
            if (ok) D.retime(ev); else why = "could not lengthen the note; placed shorter";
        }
    }
    if (ins >= 0 && instrument_at(D, ev, tick) != ins) {
        if (!D.set_instrument(ev, tick, tick + 1, uint8_t(ins))) why = "note placed, but the instrument command could not be added";
        D.retime(ev);
    }
    return true;
}

ThemeColor fx_colour(seq::FxClass c) {
    switch (c) {
        case seq::FxClass::Instrument: return TC_INS;
        case seq::FxClass::Pitch: return TC_FX_PITCH;
        case seq::FxClass::Volume: return TC_FX_VOLUME;
        case seq::FxClass::Panning: return TC_FX_PANNING;
        case seq::FxClass::Song: return TC_FX_SONG;
        case seq::FxClass::Time: return TC_FX_TIME;
        case seq::FxClass::Speed: return TC_FX_SPEED;
        case seq::FxClass::Sys1: return TC_FX_SYS1;
        case seq::FxClass::Sys2: return TC_FX_SYS2;
        case seq::FxClass::Misc: return TC_FX_MISC;
        default: return TC_FX_INVALID;
    }
}

struct PitchSpan {
    seq::PitchFx fx;
    int ev = -1;             // the command; for a glide, the note that glides
    int t0 = 0, t1 = 0;      // ticks it covers
    int from = -1, to = -1;  // semitones for a slide (-1 = unknown)
    bool glide = false;      // a legato note pulled from the previous pitch (Follin $90)
    bool wobble = false;     // a sweep that reverses: drawn as a triangle wave about `from`
};

double bend_semitones(const seq::Driver& D, int semitone, double units) {
    const double p = D.pitch_units(semitone);
    return 12.0 * std::log2(std::max(1.0, p + units) / p);
}

std::vector<PitchSpan> pitch_spans(const seq::Driver& D, const seq::Track& t, const std::vector<Bar>& bars, int track_end) {
    std::vector<PitchSpan> out;
    int vib_open = -1;   // index into out of the running vibrato
    auto bar_at = [&](int tick, bool next) -> const Bar* {
        for (const Bar& b : bars) {
            if (!next && b.tick <= tick && tick < b.tick + b.dur) return &b;
            if (next && b.tick >= tick) return &b;
        }
        return nullptr;
    };
    struct Region { seq::PitchFx fx; int ev; int t0, t1; };
    std::vector<Region> sweeps, glides;
    auto close = [](std::vector<Region>& r, int tick) { if (!r.empty() && r.back().t1 < 0) r.back().t1 = tick; };
    for (int i = 0; i < t.used_events; ++i) {
        const Event& e = t.events[size_t(i)];
        seq::PitchFx fx;
        if (!D.pitch_fx(e, fx)) continue;
        switch (fx.kind) {
            case seq::PitchFx::Vibrato: {
                if (vib_open >= 0) out[size_t(vib_open)].t1 = e.tick;
                PitchSpan ps; ps.fx = fx; ps.ev = i; ps.t0 = e.tick + fx.delay; ps.t1 = track_end;
                out.push_back(ps); vib_open = int(out.size()) - 1;
                break;
            }
            case seq::PitchFx::VibratoOff:
                if (vib_open >= 0) { out[size_t(vib_open)].t1 = e.tick; vib_open = -1; }
                break;
            case seq::PitchFx::Glide:
                close(glides, e.tick);
                if (fx.units > 0) glides.push_back({fx, i, e.tick, -1});
                break;
            case seq::PitchFx::Slide: case seq::PitchFx::Portamento: case seq::PitchFx::SlideOff: {
                if (fx.sticky) {
                    close(sweeps, e.tick);
                    if (fx.kind == seq::PitchFx::Slide && fx.units) sweeps.push_back({fx, i, e.tick, -1});
                    break;
                }
                PitchSpan ps; ps.fx = fx; ps.ev = i; ps.t0 = e.tick; ps.t1 = e.tick;
                const Bar* b = bar_at(e.tick, fx.next_note);
                if (!b && fx.next_note) b = bar_at(e.tick, false);
                if (b && fx.kind == seq::PitchFx::Slide && (fx.target >= 0 || fx.delta != 0) && b->semitone >= 0) {
                    ps.t0 = std::min(b->tick + fx.delay, b->tick + b->dur - 1);
                    ps.t1 = fx.length > 0 ? std::min(ps.t0 + fx.length, b->tick + b->dur) : b->tick + b->dur;
                    ps.from = b->semitone;
                    ps.to = fx.target >= 0 ? fx.target : b->semitone + fx.delta;
                }
                out.push_back(ps);
                break;
            }
            default: break;
        }
    }
    close(sweeps, track_end); close(glides, track_end);
    for (const Region& r : sweeps)
        for (const Bar& b : bars) {
            if (b.tick < r.t0 || b.tick >= r.t1 || b.semitone < 0 || !b.attack) continue;
            PitchSpan ps; ps.fx = r.fx; ps.ev = r.ev; ps.from = b.semitone;
            ps.t0 = std::min(b.tick + r.fx.delay, b.tick + b.dur - 1); ps.t1 = b.tick + b.dur;
            if (r.fx.length > 0) ps.wobble = true;
            else ps.to = std::clamp(int(std::lround(b.semitone + bend_semitones(D, b.semitone, double(r.fx.units) * (ps.t1 - ps.t0)))), 0, kSemitones - 1);
            out.push_back(ps);
        }
    for (const Region& r : glides)
        for (size_t k = 1; k < bars.size(); ++k) {
            const Bar& b = bars[k]; const Bar& p = bars[k - 1];
            if (b.tick < r.t0 || b.tick >= r.t1 || b.attack || b.semitone < 0 || p.semitone < 0 || p.semitone == b.semitone) continue;
            if (p.tick + p.dur != b.tick) continue;
            const double gap = std::fabs(D.pitch_units(b.semitone) - D.pitch_units(p.semitone));
            PitchSpan ps; ps.fx = r.fx; ps.ev = b.ev; ps.glide = true;
            ps.from = p.semitone; ps.to = b.semitone;
            ps.t0 = b.tick; ps.t1 = b.tick + std::clamp(int(std::ceil(gap / std::max(1, r.fx.units))), 1, b.dur);
            out.push_back(ps);
        }
    return out;
}

bool bar_selected(const App& app, const Bar& b) {
    if (app.roll_sel_t1 > app.roll_sel_t0) return b.tick >= app.roll_sel_t0 && b.tick < app.roll_sel_t1;
    return b.tick == app.roll_sel_tick;
}

}

bool draw_piano_roll(App& app, int pat_idx, float head_tick) {
    Tracker& T = app.tracker;
    const seq::Driver& D = *T.drv;
    Theme& th = theme();
    seq::Song& song = *T.song();
    seq::Pattern& pat = song.patterns[size_t(pat_idx)];
    const int tpr = std::max(1, app.ticks_per_row);
    const int length = std::max(pat.length_ticks, 1);
    if (app.sel_voice < 0 || app.sel_voice > 7) app.sel_voice = 0;

    ImGuiIO& io = ImGui::GetIO();

    child_begin("rollpane", ImVec2(0, 0), 0, 0, true);
    ImGui::TextDisabled("Voice");
    for (int v = 0; v < 8; ++v) {
        ImGui::SameLine();
        char b[4]; std::snprintf(b, sizeof b, "%d", v);
        const bool has = pat.tracks[v].addr != 0;
        const bool muted = (app.engine.mute_mask() >> v) & 1;
        const bool pushed = v == app.sel_voice || !has;
        if (v == app.sel_voice) ImGui::PushStyleColor(ImGuiCol_Button, th.colors[TC_CHANNEL_CURSOR]);
        else if (!has) { ImVec4 c = ImGui::GetStyleColorVec4(ImGuiCol_Button); ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(c.x * 0.5f, c.y * 0.5f, c.z * 0.5f, c.w)); }
        if (muted) ImGui::PushStyleColor(ImGuiCol_Text, th.colors[TC_CHANNEL_MUTED]);
        if (ImGui::SmallButton(b)) { app.sel_voice = v; app.roll_sel_tick = -1; }
        if (muted) ImGui::PopStyleColor();
        if (pushed) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) tooltip_spaced(has ? "Edit voice %d (right-click: mute)" : "Voice %d has no track in this pattern yet: placing a note creates one", v);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) app.engine.set_mute_mask(app.engine.mute_mask() ^ (1 << v));
    }
    const int inst_count = D.instrument_count(app.snap.ram);
    same_line_if_fits(em(9.0f));
    ImGui::SetNextItemWidth(em(9.0f));
    {
        char cur[24];
        if (app.roll_ins < 0) std::snprintf(cur, sizeof cur, "ins: track's");
        else std::snprintf(cur, sizeof cur, "ins %02X", app.roll_ins);
        if (ImGui::BeginCombo("##rollins", cur)) {
            if (ImGui::Selectable("track's", app.roll_ins < 0)) app.roll_ins = -1;
            if (ImGui::IsItemHovered()) tooltip_spaced("New notes use whatever instrument the track has at that point.");
            for (int i = 0; i < inst_count; ++i) {
                if (!D.instrument_used(app.snap.ram, i)) continue;
                int n = D.instrument_number(app.snap.ram, i);
                seq::Instrument in = D.read_instrument(app.snap.ram, i);
                char b[32]; std::snprintf(b, sizeof b, "%02X  smp %02X", n, in.srcn);
                ImGui::PushStyleColor(ImGuiCol_Text, th.instrument(n));
                if (ImGui::Selectable(b, app.roll_ins == n)) app.roll_ins = n;
                ImGui::PopStyleColor();
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) tooltip_spaced("Instrument written with each placed note (an instrument command\nis added when the track is on another one). Shift-click an entry\nin the Instruments panel to hear it.");
    }
    same_line_if_fits(input_int_w(2) + text_w("rows"));
    ImGui::SetNextItemWidth(input_int_w(2));
    ImGui::InputInt("rows", &app.roll_len_rows);
    app.roll_len_rows = std::clamp(app.roll_len_rows, 1, 64);
    if (ImGui::IsItemHovered()) tooltip_spaced("Length of a placed note, in rows of %d ticks (the grid's ticks/row).", tpr);
    {
        static const char* snaps[] = {"snap off", "snap row", "snap 1/2", "snap 1/4"};
        int si = app.roll_snap == 0 ? 0 : app.roll_snap == 1 ? 1 : app.roll_snap == 2 ? 2 : 3;
        same_line_if_fits(em(7.0f));
        ImGui::SetNextItemWidth(em(7.0f));
        if (ImGui::Combo("##snap", &si, snaps, 4)) app.roll_snap = si == 0 ? 0 : si == 1 ? 1 : si == 2 ? 2 : 4;
        if (ImGui::IsItemHovered()) tooltip_spaced("Grid that placed and dragged notes snap to (fractions of a row). Hold Alt to bypass it.");
    }
    same_line_if_fits(text_w("Ghosts") + em(1.5f)); ImGui::Checkbox("Ghosts", &app.roll_ghost);
    if (ImGui::IsItemHovered()) tooltip_spaced("Show the other voices' notes faintly.");
    same_line_if_fits(text_w("-") + text_w("+") + text_w("zoom") + text_w("-") + text_w("+") + em(3));
    if (ImGui::SmallButton("-")) app.roll_zoom = std::max(kZoomMin, app.roll_zoom / 1.25f);
    ImGui::SameLine(); if (ImGui::SmallButton("+")) app.roll_zoom = std::min(kZoomMax, app.roll_zoom * 1.25f);
    ImGui::SameLine(); ImGui::TextDisabled("zoom");
    if (ImGui::IsItemHovered()) tooltip_spaced("Horizontal zoom (Ctrl + wheel over the roll, around the mouse).");
    ImGui::SameLine(); if (ImGui::SmallButton("-##zy")) app.roll_zoom_y = std::max(kZoomYMin, app.roll_zoom_y / 1.25f);
    ImGui::SameLine(); if (ImGui::SmallButton("+##zy")) app.roll_zoom_y = std::min(kZoomYMax, app.roll_zoom_y * 1.25f);
    ImGui::SameLine(); ImGui::TextDisabled("v");
    if (ImGui::IsItemHovered()) tooltip_spaced("Vertical zoom: the height of a semitone (Ctrl + Shift + wheel over the roll).");
    static int add_cmd = 0;
    bool add_fx_now = false;
    if (!D.in_place_only() || D.has_stream_edit()) {
        same_line_if_fits(em(8.0f) + text_w("+"));
        ImGui::SetNextItemWidth(em(8.0f));
        const uint8_t base = D.first_command();
        if (ImGui::BeginCombo("##addfx", D.cmd_code(uint8_t(base + add_cmd)))) {
            for (int i = 0; i < D.command_count(); ++i) {
                if (D.cmd_size(uint8_t(base + i)) <= 0) continue;
                char b[64]; std::snprintf(b, sizeof b, "$%02X %s", base + i, D.cmd_name(uint8_t(base + i)));
                ImGui::PushStyleColor(ImGuiCol_Text, th.colors[fx_colour(D.cmd_class(uint8_t(base + i)))]);
                if (ImGui::Selectable(b, i == add_cmd)) add_cmd = i;
                ImGui::PopStyleColor();
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) tooltip_spaced("Command to add with + at the selected note's tick (edit its arguments in the tracker's event editor).");
        ImGui::SameLine();
        if (ImGui::SmallButton("+")) add_fx_now = true;
    }
    {
        same_line_if_fits(em(9.0f));
        ImGui::SetNextItemWidth(em(9.0f));
        char cur[40];
        if (app.roll_fx_view < 0) std::snprintf(cur, sizeof cur, "lane: all");
        else std::snprintf(cur, sizeof cur, "lane: %s", D.cmd_code(uint8_t(app.roll_fx_view)));
        if (ImGui::BeginCombo("##fxview", cur)) {
            if (ImGui::Selectable("all commands (tags)", app.roll_fx_view < 0)) app.roll_fx_view = -1;
            const uint8_t base = D.first_command();
            for (int i = 0; i < D.command_count(); ++i) {
                const uint8_t op = uint8_t(base + i);
                if (D.cmd_size(op) < 2) continue;
                char b[64]; std::snprintf(b, sizeof b, "$%02X %s", op, D.cmd_name(op));
                ImGui::PushStyleColor(ImGuiCol_Text, th.colors[fx_colour(D.cmd_class(op))]);
                if (ImGui::Selectable(b, app.roll_fx_view == op)) app.roll_fx_view = op;
                ImGui::PopStyleColor();
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered()) tooltip_spaced("Effect lane: all commands as tags, or one command's argument as value bars\n(drag a bar to change it, click an empty spot to add one, double-click to remove).");
    }
    same_line_if_fits(text_w("(?)")); ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        tooltip_spaced("Click and drag on empty space: place a note and set its length.  Drag a note: move /\n"
                          "transpose (Ctrl: duplicate).  Drag its right edge: resize.  Double-click a note: its\n"
                          "editor (pitch, length, instrument, Plays on/off, the effects on it).  Shift-click: delete\n"
                          "(keep the button down to erase more).  Right-drag on empty space: select a range;\n"
                          "arrows then move / transpose all, Delete removes them, Ctrl+C / Ctrl+V copy and paste.\n"
                          "S + drag on a note: pitch slide to the target (drivers with a bend command); drag the\n"
                          "slide's end dot to change it.  I + click: pick the note's instrument.  Alt: no snapping.\n"
                          "Click the time ruler: play from there.  Ctrl + wheel: zoom time; Ctrl + Shift + wheel: zoom pitch rows.\n"
                          "Dark block at a note's start = key-on; a thin tail = the part cut by quantise.\n"
                          "Right-click a note or empty space: add Volume / Pan / Vibrato / any command there, slides, delete.\n"
                          "Vibrato draws as a wave, slides as a ramp; the lane below shows commands at their tick\n"
                          "(click a tag to edit its arguments, right-click to remove; `lane:` shows one as value bars).");

    ImGui::PushFont(fonts().mono, th.font_size_pattern);
    const float cw = ImGui::CalcTextSize("0").x;
    const float row_h = std::max(3.0f, ImGui::GetTextLineHeight() * 0.85f * app.roll_zoom_y);
    const float key_w = cw * 5 + 8;
    const float head_h = ImGui::GetTextLineHeight() + 4;
    const float ppt = (cw * 3.0f * app.roll_zoom) / float(tpr);    // pixels per tick: a row is 3 chars at zoom 1
    const ImVec2 content(key_w + length * ppt + cw * 2, head_h + kSemitones * row_h);

    {
        ImVec2 scroll(-1.0f, -1.0f);
        static int last_order = -1;
        const bool switched = app.view_order != last_order;
        if (switched) app.roll_sel_tick = -1;
        last_order = app.view_order;
        if (app.follow && head_tick >= 0) scroll.x = std::max(0.0f, key_w + head_tick * ppt - ImGui::GetContentRegionAvail().x * std::clamp(th.playhead_pos, 0.1f, 0.9f));
        else if (app.follow && switched) scroll.x = 0.0f;
        if (app.roll_centre_frames > 0) { scroll.y = std::max(0.0f, head_h + (kSemitones - 1 - 48) * row_h - ImGui::GetContentRegionAvail().y * 0.5f); --app.roll_centre_frames; }
        if (app.roll_anchor_tick >= 0 && !(app.follow && head_tick >= 0)) scroll.x = std::max(0.0f, key_w + app.roll_anchor_tick * ppt - app.roll_anchor_px);
        if (app.roll_anchor_semi >= 0) scroll.y = std::max(0.0f, head_h + app.roll_anchor_semi * row_h - app.roll_anchor_py);
        app.roll_anchor_tick = -1; app.roll_anchor_semi = -1;
        if (scroll.x >= 0 || scroll.y >= 0) ImGui::SetNextWindowScroll(scroll);
    }
    const float lane_h = ImGui::GetTextLineHeight() * 2 + 10;
    child_begin("roll", ImVec2(0, -lane_h - ImGui::GetStyle().ItemSpacing.y), ImGuiChildFlags_Borders, 0, false);
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 win = ImGui::GetWindowPos();
    const ImVec2 win_size = ImGui::GetWindowSize();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("rollbtn", ImVec2(std::max(content.x, ImGui::GetContentRegionAvail().x), content.y));
    const bool hovered = ImGui::IsItemHovered();
    if (hovered && io.MouseWheel != 0 && !io.KeyCtrl && app.follow) app.follow = false;
    if (hovered && io.MouseWheel != 0 && io.KeyCtrl) {
        const float f = io.MouseWheel > 0 ? 1.25f : 0.8f;
        const ImVec2 m = ImGui::GetMousePos();
        if (io.KeyShift) {
            app.roll_zoom_y = std::clamp(app.roll_zoom_y * f, kZoomYMin, kZoomYMax);
            app.roll_anchor_semi = (m.y - origin.y - head_h) / row_h;
            app.roll_anchor_py = m.y - win.y;
        } else {
            app.roll_zoom = std::clamp(app.roll_zoom * f, kZoomMin, kZoomMax);
            app.roll_anchor_tick = (m.x - origin.x - key_w) / ppt;
            app.roll_anchor_px = m.x - win.x;
        }
    }

    const float sx = ImGui::GetScrollX();
    auto tick_x = [&](float tick) { return origin.x + key_w + tick * ppt; };
    auto semi_y = [&](int semi) { return origin.y + head_h + (kSemitones - 1 - semi) * row_h; };
    const float view_x0 = win.x + key_w, view_x1 = win.x + win_size.x;
    const float view_y0 = win.y + head_h, view_y1 = win.y + win_size.y;
    const float sy_top_pad = 2.0f;

    dl->AddRectFilled(ImVec2(win.x, win.y), ImVec2(win.x + win_size.x, win.y + win_size.y), th.u32(TC_PATTERN_BG));
    dl->PushClipRect(ImVec2(view_x0, view_y0), ImVec2(view_x1, view_y1), true);
    static const bool black[12] = {false, true, false, true, false, false, true, false, true, false, true, false};
    for (int s = 0; s < kSemitones; ++s) {
        float y = semi_y(s);
        if (y + row_h < view_y0 || y > view_y1) continue;
        if (black[s % 12]) dl->AddRectFilled(ImVec2(view_x0, y), ImVec2(tick_x(float(length)), y + row_h), th.u32(TC_ROW_HI1, 0.35f));
        if (s % 12 == 0) dl->AddLine(ImVec2(view_x0, y + row_h), ImVec2(tick_x(float(length)), y + row_h), th.u32(TC_ROW_INDEX_HI2, 0.6f));
    }
    const int rows = (length + tpr - 1) / tpr;
    const int r0 = std::max(0, int((sx - key_w) / (ppt * tpr)) - 1), r1 = std::min(rows, int((sx - key_w + win_size.x) / (ppt * tpr)) + 2);
    const float row_px = ppt * tpr;   // zoomed out, a line per row would be a solid wall
    for (int r = r0; r <= r1; ++r) {
        float x = tick_x(float(r * tpr));
        bool hi2 = th.row_hi2 > 0 && r % th.row_hi2 == 0, hi1 = th.row_hi1 > 0 && r % th.row_hi1 == 0;
        if (row_px * std::max(1, hi2 ? th.row_hi2 : hi1 ? th.row_hi1 : 1) < 4.0f) continue;
        dl->AddLine(ImVec2(x, view_y0), ImVec2(x, semi_y(0) + row_h), th.u32(hi2 ? TC_ROW_INDEX_HI2 : hi1 ? TC_ROW_INDEX_HI1 : TC_ROW_INDEX, hi2 ? 0.7f : hi1 ? 0.45f : 0.2f));
    }

    std::vector<Bar> bars = bars_of(D, pat.tracks[app.sel_voice]);
    for (auto it = app.muted.begin(); it != app.muted.end();) {
        const int v = int(it->first >> 20), tick = int(it->first & 0xFFFFF);
        if (v != app.sel_voice) { ++it; continue; }
        bool alive = false;
        for (const Event& e : pat.tracks[v].events) if (e.type == EventType::Rest && e.tick == tick && e.duration == it->second.dur) { alive = true; break; }
        if (!alive) { it = app.muted.erase(it); continue; }
        Bar b; b.tick = tick; b.dur = it->second.dur; b.semitone = it->second.semitone; b.ins = it->second.ins; b.muted = true;
        bars.push_back(b);
        ++it;
    }
    auto draw_bar = [&](const Bar& b, ImU32 fill, ImU32 border, bool label) {
        int row = b.semitone >= 0 ? b.semitone : b.perc;
        if (row < 0 || row >= kSemitones) return;
        ImVec2 p0(tick_x(float(b.tick)), semi_y(row) + 1), p1(tick_x(float(b.tick + b.dur)) - 1, semi_y(row) + row_h - 1);
        if (p1.x < view_x0 || p0.x > view_x1 || p1.y < view_y0 || p0.y > view_y1) return;
        if (b.release > 0 && b.release < b.dur) {
            float xr = tick_x(float(b.tick + b.release));
            dl->AddRectFilled(p0, ImVec2(xr, p1.y), fill, 2.0f);
            float mid = (p0.y + p1.y) * 0.5f, t = std::max(1.5f, row_h * 0.18f);
            dl->AddRectFilled(ImVec2(xr, mid - t), ImVec2(p1.x, mid + t), fill);
        } else if (b.muted) dl->AddRect(p0, p1, fill, 2.0f, 0, 1.5f);
        else dl->AddRectFilled(p0, p1, fill, 2.0f);
        if (label && b.attack && !b.muted && p1.x - p0.x > 6) dl->AddRectFilled(ImVec2(p0.x + 1, p0.y + 1), ImVec2(p0.x + std::min(4.0f, (p1.x - p0.x) * 0.2f), p1.y - 1), th.u32(TC_PATTERN_BG, 0.8f));
        if (border) dl->AddRect(p0, p1, border, 2.0f);
        if (label && p1.x - p0.x > cw * 3.5f && row_h >= ImGui::GetTextLineHeight()) {
            std::string name = b.semitone >= 0 ? seq::note_name(b.semitone) : "P" + std::to_string(b.perc);
            dl->AddText(ImVec2(p0.x + 3, p0.y + (row_h - ImGui::GetTextLineHeight()) * 0.5f), b.muted ? fill : th.u32(TC_PATTERN_BG), name.c_str());
        }
    };
    if (app.roll_ghost)
        for (int v = 0; v < 8; ++v) {
            if (v == app.sel_voice || !pat.tracks[v].addr) continue;
            for (const Bar& b : bars_of(D, pat.tracks[v])) draw_bar(b, th.u32(TC_NOTE, 0.18f), 0, false);
        }
    const bool muted = (app.engine.mute_mask() >> app.sel_voice) & 1;
    const seq::Track& cur_track = pat.tracks[app.sel_voice];
    auto note_effects = [&](const Bar& b) {
        std::vector<int> out;
        for (int i = 0; i < cur_track.used_events; ++i) {
            const Event& e = cur_track.events[size_t(i)];
            if (e.type == EventType::Command && !D.is_instrument_cmd(e.b[0]) && e.tick >= b.tick && e.tick < b.tick + b.dur) out.push_back(i);
        }
        return out;
    };
    for (const Bar& b : bars) {
        ImU32 fill = b.in_sub ? th.u32(TC_NOTE_SUB) : b.semitone < 0 ? th.u32(TC_NOTE_PERC) : th.ins_colors && b.ins >= 0 ? th.instrument(b.ins) : th.u32(TC_NOTE);
        if (muted && th.dim_muted) fill = (fill & 0x00FFFFFF) | 0x60000000;
        draw_bar(b, fill, bar_selected(app, b) ? th.u32(TC_CURSOR_EDIT) : th.u32(TC_PATTERN_BG, 0.6f), true);
        const int row = b.semitone >= 0 ? b.semitone : b.perc;
        if (row < 0 || row >= kSemitones) continue;
        const std::vector<int> fx = note_effects(b);
        if (fx.empty()) continue;
        const float x_end = tick_x(float(b.tick + b.dur)) - 2;
        const float x_start = tick_x(float(b.tick)) + 1;
        const float note_w = std::max(x_end - x_start, 1.0f);
        const float gap = note_w / float(fx.size()) >= 5.0f ? 1.0f : 0.0f;
        const float tab_w = std::clamp((note_w - gap * float(fx.size() - 1)) / float(fx.size()), 1.0f, std::clamp(cw * 0.8f, 4.0f, 12.0f));
        const float inset = row_h >= 9 ? 1.0f : 0.0f;   // inside the bar's outline when there is room
        const float y0 = semi_y(row) + 1 + inset, y1 = semi_y(row) + row_h - 1 - inset;
        if (y1 - y0 < 1.0f) continue;
        const float rounding = tab_w >= 4 && y1 - y0 >= 6 ? 2.0f : 0.0f;
        float xr = x_end;
        for (size_t k = 0; k < fx.size(); ++k) {
            const Event& e = cur_track.events[size_t(fx[k])];
            const float x0 = std::max(x_start, xr - tab_w);
            if (x0 >= xr) break;
            if (xr >= view_x0 && x0 <= view_x1) {
                const ImU32 c = th.u32(fx_colour(D.cmd_class(e.b[0])));
                dl->AddRectFilled(ImVec2(x0, y0), ImVec2(xr, y1), c, rounding);
                if (tab_w >= 4 && y1 - y0 >= 6) dl->AddRect(ImVec2(x0, y0), ImVec2(xr, y1), th.u32(TC_PATTERN_BG, 0.9f), rounding);
            }
            xr = x0 - gap;
        }
    }
    if (app.roll_sel_t1 > app.roll_sel_t0)
        dl->AddRectFilled(ImVec2(tick_x(float(app.roll_sel_t0)), view_y0), ImVec2(tick_x(float(app.roll_sel_t1)), view_y1), th.u32(TC_SELECTION, 0.25f));
    const std::vector<PitchSpan> spans = pitch_spans(D, pat.tracks[app.sel_voice], bars, pat.tracks[app.sel_voice].total_ticks);
    const ImU32 fxc = th.u32(TC_FX_PITCH);
    for (const PitchSpan& ps : spans) {
        const Event& e = pat.tracks[app.sel_voice].events[size_t(ps.ev)];
        if (ps.fx.kind == seq::PitchFx::Vibrato) {
            const float amp = std::clamp(row_h * (0.15f + std::min(ps.fx.depth, 64) / 64.0f * 0.3f), 2.0f, row_h * 0.45f);
            const float wl = std::clamp(14.0f - std::min(ps.fx.rate, 96) / 8.0f, 4.0f, 14.0f);
            for (const Bar& b : bars) {
                if (b.semitone < 0) continue;
                int a = std::max(b.tick, ps.t0), z = std::min(b.tick + b.dur, ps.t1);
                if (z <= a) continue;
                float x0 = tick_x(float(a)), x1 = tick_x(float(z)) - 1, yc = semi_y(b.semitone) + row_h * 0.5f;
                if (x1 < view_x0 || x0 > view_x1) continue;
                ImVec2 prev(x0, yc);
                for (float x = x0 + 2; x <= x1; x += 2) {
                    ImVec2 pt(x, yc + std::sin((x - x0) / wl * 6.2832f) * amp);
                    dl->AddLine(prev, pt, fxc, 1.5f);
                    prev = pt;
                }
            }
        } else if (ps.wobble && ps.from >= 0) {
            const int leg = std::max(1, ps.fx.length);
            const float amp = std::clamp(float(bend_semitones(D, ps.from, std::fabs(double(ps.fx.units)) * leg / 2.0)) * row_h, 2.0f, row_h * 4.0f);
            float x0 = tick_x(float(ps.t0)), x1 = tick_x(float(ps.t1)) - 1, yc = semi_y(ps.from) + row_h * 0.5f;
            if (x1 < view_x0 || x0 > view_x1) continue;
            const float px_per_tick = tick_x(1.0f) - tick_x(0.0f);
            const float dir = ps.fx.units > 0 ? -1.0f : 1.0f;
            ImVec2 prev(x0, yc);
            for (float x = x0 + 2; x <= x1; x += 2) {
                const float phase = (x - x0) / std::max(px_per_tick, 0.01f) + leg * 0.5f;   // ticks, half a leg in
                const float u = std::fmod(phase, float(2 * leg));
                const float v = u < leg ? u : 2 * leg - u;
                ImVec2 pt(x, yc + dir * (v - leg * 0.5f) / (leg * 0.5f) * amp);
                dl->AddLine(prev, pt, fxc, 1.5f);
                prev = pt;
            }
        } else if (ps.from >= 0 && ps.to >= 0) {
            float x0 = tick_x(float(ps.t0)), x1 = tick_x(float(ps.t1));
            float y0 = semi_y(ps.from) + row_h * 0.5f, y1 = semi_y(std::clamp(ps.to, 0, kSemitones - 1)) + row_h * 0.5f;
            dl->AddTriangleFilled(ImVec2(x0, y0), ImVec2(x1, y1), ImVec2(x1, y0), th.u32(TC_FX_PITCH, 0.25f));
            dl->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), fxc, 2.0f);
            dl->AddCircleFilled(ImVec2(x1, y1), 3.5f, fxc);
        } else {
            float x = tick_x(float(e.tick));
            const Bar* b = nullptr;
            for (const Bar& c : bars) if (c.tick <= e.tick && e.tick < c.tick + c.dur) { b = &c; break; }
            float y = b && b->semitone >= 0 ? semi_y(b->semitone) - ImGui::GetTextLineHeight() : view_y0 + sy_top_pad;
            dl->AddText(ImVec2(x + 1, y), fxc, D.cmd_code(e.b[0]));
        }
    }
    if (head_tick >= 0) {
        float x = tick_x(head_tick);
        dl->AddLine(ImVec2(x, view_y0), ImVec2(x, view_y1), th.u32(TC_PLAYHEAD_LINE), 2.0f);
        for (int v = 0; v < 8; ++v) {
            const float vt = app.voice_head[v];
            if (vt < 0 || std::fabs(vt - head_tick) < tpr * 0.5f) continue;
            bool dup = false;   // one line per distinct position
            for (int w = 0; w < v; ++w) if (app.voice_head[w] >= 0 && std::fabs(app.voice_head[w] - vt) < tpr * 0.5f && std::fabs(app.voice_head[w] - head_tick) >= tpr * 0.5f) dup = true;
            const float gx = tick_x(vt);
            if (gx < view_x0 || gx > view_x1) continue;
            const bool mine = v == app.sel_voice;
            if (!dup) dl->AddLine(ImVec2(gx, view_y0), ImVec2(gx, view_y1), th.u32(TC_PLAYHEAD_LINE, mine ? 0.7f : 0.35f), mine ? 2.0f : 1.0f);
            char lb[8]; std::snprintf(lb, sizeof lb, "v%d", v + 1);
            int n = 0; for (int w = 0; w < v; ++w) if (app.voice_head[w] >= 0 && std::fabs(app.voice_head[w] - vt) < tpr * 0.5f) ++n;
            dl->AddText(ImVec2(gx + 2, view_y0 + 2 + n * ImGui::GetTextLineHeight()), th.u32(TC_PLAYHEAD_LINE, mine ? 0.9f : 0.6f), lb);
        }
    }
    dl->PopClipRect();

    dl->PushClipRect(ImVec2(win.x, view_y0), ImVec2(view_x0, view_y1), true);
    for (int s = 0; s < kSemitones; ++s) {
        float y = semi_y(s);
        if (y + row_h < view_y0 || y > view_y1) continue;
        dl->AddRectFilled(ImVec2(win.x, y), ImVec2(view_x0, y + row_h), black[s % 12] ? th.u32(TC_CHANNEL_HEADER_BG) : th.u32(TC_ROW_HI2, 0.6f));
        if (s % 12 == 0 || row_h >= ImGui::GetTextLineHeight())
            dl->AddText(ImVec2(win.x + 4, y + (row_h - ImGui::GetTextLineHeight()) * 0.5f), black[s % 12] ? th.u32(TC_ROW_INDEX) : th.u32(TC_NOTE), seq::note_name(s).c_str());
    }
    dl->PopClipRect();
    dl->PushClipRect(ImVec2(win.x, win.y), ImVec2(view_x1, view_y0), true);
    dl->AddRectFilled(ImVec2(win.x, win.y), ImVec2(view_x1, view_y0), th.u32(TC_CHANNEL_HEADER_BG));
    const int lab_step = ruler_label_step(rows, row_px, cw);
    for (int r = r0; r <= r1; ++r) {
        if (r % lab_step) continue;
        float x = tick_x(float(r * tpr));
        char b[16]; std::snprintf(b, sizeof b, th.hex_rows ? "%02X" : "%d", r);
        dl->AddText(ImVec2(x + 2, win.y + 2), th.u32(th.row_hi2 > 0 && r % th.row_hi2 == 0 ? TC_ROW_INDEX_HI2 : TC_ROW_INDEX_HI1), b);
    }
    char vt[48]; std::snprintf(vt, sizeof vt, "v%d", app.sel_voice);
    dl->AddText(ImVec2(win.x + 4, win.y + 2), th.u32(TC_CHANNEL_CURSOR), vt);
    dl->PopClipRect();

    bool wrote = false;
    std::string why;
    static struct { bool open = false; int tick = -1; } note_edit;
    auto commit = [&](std::vector<Event>& ev) {
        Tracker::Result r = T.write_track(app.engine, pat_idx, app.sel_voice, ev);
        app.status = why.empty() ? r.msg : why + " (" + r.msg + ")";
        wrote = true;
        return r.ok;
    };
    auto events = [&]() { const seq::Track& t = pat.tracks[app.sel_voice]; return std::vector<Event>(t.events.begin(), t.events.begin() + t.used_events); };
    const bool has_track = pat.tracks[app.sel_voice].addr != 0;

    struct ClipNote { int tick, semitone, dur, ins; };
    static std::vector<ClipNote> clip;
    static struct {
        enum Mode { None, Create, Move, Resize, Slide, SlideTarget, Erase, Select } mode = None;
        bool moved = false, dup = false;
        int tick = 0, dur = 0, semitone = 0;   // the bar (or new note) the gesture started on
        float start_tick = 0;                  // mouse tick at the press
        ImVec2 start;
        int slide_ev = -1;                     // SlideTarget: the command being dragged
        int anchor = 0;                        // Select: tick where the drag began
        int sounding = 0;                      // preview: -1 = until the button is released, >0 = frames left
    } drag;
    using Mode = decltype(drag)::Mode;
    static int menu_tick = -1, menu_semitone = -1, menu_ins = -1;
    static bool menu_note = false;
    bool open_menu = false;
    const ImVec2 m = ImGui::GetMousePos();
    const bool in_canvas = hovered && m.x >= view_x0 && m.y >= view_y0;
    const bool in_header = hovered && m.x >= view_x0 && m.y < view_y0 && m.y >= win.y;
    const float mtick = (m.x - origin.x - key_w) / ppt;
    const int msemi = kSemitones - 1 - int(std::floor((m.y - origin.y - head_h) / row_h));
    const int unit = io.KeyAlt || app.roll_snap == 0 ? 1 : std::max(1, tpr / app.roll_snap);
    auto snap = [&](float t) { return std::clamp(int(std::floor(t / unit)) * unit, 0, std::max(0, length - 1)); };
    const Bar* under = nullptr;
    for (const Bar& b : bars) {
        int row = b.semitone >= 0 ? b.semitone : b.perc;
        if (row == msemi && mtick >= b.tick && mtick < b.tick + b.dur) { under = &b; break; }
    }
    int hover_slide = -1;
    for (size_t k = 0; k < spans.size(); ++k) {
        const PitchSpan& ps = spans[k];
        if (ps.from < 0 || ps.to < 0) continue;
        float x1 = tick_x(float(ps.t1)), y1 = semi_y(std::clamp(ps.to, 0, kSemitones - 1)) + row_h * 0.5f;
        if (in_canvas && std::fabs(m.x - x1) <= 6 && std::fabs(m.y - y1) <= 6) hover_slide = int(k);
    }
    auto preview = [&](int semitone, int ins, int frames, const Bar* b = nullptr, int tick = -1) {
        uint8_t regs[8];
        if (app.preview_regs_heard(pat.tracks[app.sel_voice], app.sel_voice, b ? b->ev : -1, b ? b->tick : tick, semitone, ins, regs)) { app.engine.preview_on(preview_voice(app), regs); drag.sounding = frames; }
    };
    auto selected_bars = [&]() {
        std::vector<Bar> out;
        for (const Bar& b : bars) if (bar_selected(app, b) && b.semitone >= 0 && !b.muted) out.push_back(b);
        return out;
    };
    auto clear_selection = [&]() { app.roll_sel_tick = -1; app.roll_sel_t0 = app.roll_sel_t1 = 0; };
    auto move_selected = [&](std::vector<Bar> sel, int dtick, int dsemi, bool dup, int new_dur = -1) {
        if (sel.empty()) return false;
        std::sort(sel.begin(), sel.end(), [](const Bar& a, const Bar& b) { return a.tick < b.tick; });
        std::vector<Event> ev = events();
        bool ok = true;
        if (!dup && dtick != 0) for (const Bar& b : sel) if (!erase_note(D, ev, b.tick, length, why)) { ok = false; break; }
        for (const Bar& b : sel) {
            if (!ok) break;
            int nt = b.tick + dtick, ns = std::clamp(b.semitone + dsemi, 0, kSemitones - 1);
            if (nt < 0 || nt >= length) continue;
            ok = place_note(D, ev, nt, ns, new_dur > 0 ? new_dur : b.dur, length, dtick != 0 || dup ? b.ins : -1, why);
        }
        if (ok) {
            commit(ev);
            if (app.roll_sel_t1 > app.roll_sel_t0) { app.roll_sel_t0 += dtick; app.roll_sel_t1 += dtick; }
            else app.roll_sel_tick = sel[0].tick + dtick;
        } else app.status = why;
        return ok;
    };
    auto erase_selected = [&]() {
        std::vector<Bar> sel = selected_bars();
        if (sel.empty()) return;
        std::vector<Event> ev = events();
        bool any = false;
        for (const Bar& b : sel) if (erase_note(D, ev, b.tick, length, why)) any = true;
        if (any) commit(ev); else app.status = why;
        clear_selection();
    };
    auto slide_on = [&](const Bar& b) {
        for (size_t k = 0; k < spans.size(); ++k) {
            const PitchSpan& ps = spans[size_t(k)];
            if (ps.from >= 0 && ps.to >= 0 && ps.ev >= 0 && ps.t0 >= b.tick && ps.t0 < b.tick + b.dur) return int(k);
        }
        return -1;
    };
    auto write_slide = [&](std::vector<Event>& ev, const Bar& b, int from, int target, int span) {
        if (!D.can_slide()) { why = "this driver has no pitch slide command the roll can write"; return false; }
        const int existing = span >= 0 && span < int(spans.size()) ? spans[size_t(span)].ev : -1;
        if (!D.set_slide(ev, b.tick, b.dur, from, target, existing)) { why = "cannot add a slide there (it needs a note of at least two ticks)"; return false; }
        return true;
    };

    if ((in_canvas || in_header) && drag.mode == Mode::None) {
        const bool key_s = ImGui::IsKeyDown(ImGuiKey_S), key_i = ImGui::IsKeyDown(ImGuiKey_I);
        if (hover_slide >= 0) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            if (ImGui::BeginTooltip()) { ImGui::Text("slide target %s  (drag)", seq::note_name(spans[size_t(hover_slide)].to).c_str()); ImGui::EndTooltip(); }
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { drag = {}; drag.mode = Mode::SlideTarget; drag.slide_ev = hover_slide; drag.start = m; }
        } else if (under && in_canvas) {
            const bool edge = m.x > tick_x(float(under->tick + under->dur)) - std::max(6.0f, cw);
            ImGui::SetMouseCursor(key_s ? ImGuiMouseCursor_ResizeNS : edge ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_Hand);
            if (ImGui::BeginTooltip()) {
                ImGui::Text("%s  tick %d  %d ticks%s%s", under->semitone >= 0 ? seq::note_name(under->semitone).c_str() : "percussion", under->tick, under->dur, under->in_sub ? "  (shared bytes)" : "", under->attack ? "" : "  (no key-on)");
                if (under->ins >= 0) ImGui::Text("instrument %02X", under->ins);
                if (under->release > 0 && under->release < under->dur) ImGui::Text("keys off after %d ticks (quantise)", under->release);
                for (int i : note_effects(*under)) {
                    const Event& e = cur_track.events[size_t(i)];
                    ImGui::TextColored(th.colors[fx_colour(D.cmd_class(e.b[0]))], "%s%s%s", e.tick == under->tick ? "" : "+", e.tick == under->tick ? "" : std::to_string(e.tick - under->tick).c_str(), e.tick == under->tick ? "" : " ");
                    ImGui::SameLine(0, 0);
                    ImGui::TextColored(th.colors[fx_colour(D.cmd_class(e.b[0]))], "%s", D.event_text(e).c_str());
                }
                if (!note_effects(*under).empty()) ImGui::TextDisabled("double-click to edit the effects");
                ImGui::EndTooltip();
            }
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                drag = {};
                note_edit.open = true; note_edit.tick = under->tick;
                app.roll_sel_t0 = app.roll_sel_t1 = 0; app.roll_sel_tick = under->tick;
            } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && io.KeyShift) {
                if (has_track && !under->muted) { std::vector<Event> ev = events(); if (erase_note(D, ev, under->tick, length, why)) commit(ev); else app.status = why; }
                else if (under->muted) app.muted.erase(App::muted_key(app.sel_voice, under->tick));
                clear_selection();
                drag = {}; drag.mode = Mode::Erase; drag.start = m;
            } else if (under->muted && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                app.roll_sel_t0 = app.roll_sel_t1 = 0; app.roll_sel_tick = under->tick;
            } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && key_i) {
                if (under->ins >= 0) { app.roll_ins = under->ins; app.sel_instrument = D.instrument_index(app.snap.ram, under->ins); app.status = "instrument picked from the note"; }
            } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && key_s && under->semitone >= 0) {
                drag = {}; drag.mode = Mode::Slide; drag.tick = under->tick; drag.dur = under->dur; drag.semitone = under->semitone; drag.start = m;
            } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                if (!bar_selected(app, *under)) { app.roll_sel_t0 = app.roll_sel_t1 = 0; app.roll_sel_tick = under->tick; }
                drag = {}; drag.mode = edge ? Mode::Resize : Mode::Move; drag.dup = io.KeyCtrl && !edge;
                drag.tick = under->tick; drag.dur = under->dur; drag.semitone = under->semitone; drag.start = m; drag.start_tick = mtick;
                if (under->semitone >= 0) preview(under->semitone, under->ins, -1, under);
            } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                if (!bar_selected(app, *under)) { app.roll_sel_t0 = app.roll_sel_t1 = 0; app.roll_sel_tick = under->tick; }
                menu_tick = under->tick; menu_semitone = under->semitone; menu_ins = under->ins; menu_note = true;
                open_menu = true;
            }
        } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && in_canvas && msemi >= 0 && msemi < kSemitones && mtick >= 0 && mtick < length) {
            if (io.KeyShift) { drag = {}; drag.mode = Mode::Erase; drag.start = m; }
            else {
                clear_selection();
                drag = {}; drag.mode = Mode::Create; drag.tick = snap(mtick); drag.semitone = msemi; drag.start = m; drag.start_tick = mtick;
                drag.dur = std::max(unit, app.roll_len_rows * tpr);
                preview(msemi, app.roll_ins >= 0 ? app.roll_ins : instrument_at(D, events(), drag.tick), -1, nullptr, drag.tick);
            }
        } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && in_header && mtick >= 0 && mtick < length) {
            app.play_from(app.view_order, snap(mtick));
        } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && mtick >= 0) {
            drag = {}; drag.mode = Mode::Select; drag.anchor = snap(mtick); drag.start = m;
            app.roll_sel_tick = -1; app.roll_sel_t0 = app.roll_sel_t1 = 0;
        }
    }
    if (drag.mode != Mode::None) {
        if (std::abs(m.x - drag.start.x) > 3 || std::abs(m.y - drag.start.y) > 3) drag.moved = true;
        dl->PushClipRect(ImVec2(view_x0, view_y0), ImVec2(view_x1, view_y1), true);
        const bool down = ImGui::IsMouseDown(drag.mode == Mode::Select ? ImGuiMouseButton_Right : ImGuiMouseButton_Left);
        switch (drag.mode) {
            case Mode::Create: {
                int end = std::max(drag.tick + unit, snap(mtick) + unit);
                int dur = drag.moved ? std::min(end, length) - drag.tick : drag.dur;
                ImVec2 p0(tick_x(float(drag.tick)), semi_y(drag.semitone) + 1), p1(tick_x(float(drag.tick + dur)) - 1, semi_y(drag.semitone) + row_h - 1);
                dl->AddRectFilled(p0, p1, th.u32(TC_NOTE, 0.5f), 2.0f);
                dl->AddRect(p0, p1, th.u32(TC_CURSOR_EDIT), 2.0f, 0, 2.0f);
                if (ImGui::BeginTooltip()) { ImGui::Text("%s  tick %d  %d ticks", seq::note_name(drag.semitone).c_str(), drag.tick, dur); ImGui::EndTooltip(); }
                if (!down) {
                    std::vector<Event> ev = events();
                    int ins = app.roll_ins;
                    if (place_note(D, ev, drag.tick, drag.semitone, dur, length, ins, why)) { commit(ev); app.roll_sel_tick = drag.tick; }
                    else app.status = why.empty() ? "could not place the note" : why;
                    drag.mode = Mode::None;
                }
                break;
            }
            case Mode::Move: case Mode::Resize: {
                const bool resize = drag.mode == Mode::Resize;
                const int dsemi = resize || drag.semitone < 0 ? 0 : std::clamp(msemi, 0, kSemitones - 1) - drag.semitone;
                const int dtick = resize ? 0 : snap(std::max(0.0f, drag.tick + mtick - drag.start_tick)) - drag.tick;
                const int new_dur = resize ? std::max(unit, snap(mtick) + unit - drag.tick) : drag.dur;
                if (drag.moved && drag.semitone >= 0) {
                    ImGui::SetMouseCursor(resize ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_Hand);
                    for (const Bar& b : selected_bars()) {
                        int row = std::clamp(b.semitone + dsemi, 0, kSemitones - 1);
                        int d = resize ? (b.tick == drag.tick ? new_dur : b.dur) : b.dur;
                        ImVec2 p0(tick_x(float(b.tick + dtick)), semi_y(row) + 1), p1(tick_x(float(b.tick + dtick + d)) - 1, semi_y(row) + row_h - 1);
                        dl->AddRect(p0, p1, th.u32(TC_CURSOR_EDIT), 2.0f, 0, 2.0f);
                    }
                    if (ImGui::BeginTooltip()) { ImGui::Text("%s  tick %d  %d ticks%s", seq::note_name(std::clamp(drag.semitone + dsemi, 0, kSemitones - 1)).c_str(), drag.tick + dtick, new_dur, drag.dup ? "  (copy)" : ""); ImGui::EndTooltip(); }
                }
                if (!down) {
                    if (drag.moved && drag.semitone >= 0 && has_track && (dsemi != 0 || dtick != 0 || new_dur != drag.dur)) {
                        if (resize) {
                            std::vector<Event> ev = events();
                            if (place_note(D, ev, drag.tick, drag.semitone, new_dur, length, -1, why)) commit(ev); else app.status = why;
                        } else move_selected(selected_bars(), dtick, dsemi, drag.dup);
                    }
                    drag.mode = Mode::None;
                }
                break;
            }
            case Mode::Slide: case Mode::SlideTarget: {
                const int target = std::clamp(msemi, 0, kSemitones - 1);
                const Bar* b = nullptr;
                int from = drag.semitone, t0 = drag.tick, t1 = drag.tick + drag.dur;
                if (drag.mode == Mode::SlideTarget && drag.slide_ev >= 0 && drag.slide_ev < int(spans.size())) {
                    const PitchSpan& ps = spans[size_t(drag.slide_ev)];
                    from = ps.from; t0 = ps.t0; t1 = ps.t1;
                    for (const Bar& c : bars) if (c.tick <= t0 && t0 < c.tick + c.dur) { b = &c; break; }
                } else for (const Bar& c : bars) if (c.tick == drag.tick) { b = &c; break; }
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
                float x0 = tick_x(float(t0)), x1 = tick_x(float(t1));
                float y0 = semi_y(from) + row_h * 0.5f, y1 = semi_y(target) + row_h * 0.5f;
                dl->AddTriangleFilled(ImVec2(x0, y0), ImVec2(x1, y1), ImVec2(x1, y0), th.u32(TC_FX_PITCH, 0.25f));
                dl->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), th.u32(TC_FX_PITCH), 2.0f);
                if (ImGui::BeginTooltip()) { ImGui::Text("slide %s -> %s", seq::note_name(from).c_str(), seq::note_name(target).c_str()); ImGui::EndTooltip(); }
                if (!down) {
                    const int span = drag.mode == Mode::SlideTarget ? drag.slide_ev : b ? slide_on(*b) : -1;
                    const bool sticky = span >= 0 && span < int(spans.size()) && (spans[size_t(span)].glide || spans[size_t(span)].fx.sticky);
                    if (b && has_track && (target != from || sticky)) {
                        const int from_eff = span >= 0 && spans[size_t(span)].glide ? spans[size_t(span)].from : from;
                        std::vector<Event> ev = events();
                        if (write_slide(ev, *b, from_eff, target, span)) commit(ev); else app.status = why;
                    } else if (b && has_track && drag.mode == Mode::SlideTarget && target == from) {
                        std::vector<Event> ev = events();
                        int i = spans[size_t(drag.slide_ev)].ev;
                        if (i >= 0 && i < int(ev.size()) && !ev[size_t(i)].in_sub) { Tracker::remove_event(D, ev, i); commit(ev); }
                    }
                    drag.mode = Mode::None;
                }
                break;
            }
            case Mode::Erase: {
                ImGui::SetMouseCursor(ImGuiMouseCursor_NotAllowed);
                if (under && has_track && !wrote) { std::vector<Event> ev = events(); if (erase_note(D, ev, under->tick, length, why)) commit(ev); }
                if (!down) drag.mode = Mode::None;
                break;
            }
            case Mode::Select: {
                int a = drag.anchor, z = snap(mtick) + unit;
                app.roll_sel_t0 = std::min(a, z); app.roll_sel_t1 = std::max(a, z);
                if (!down) {
                    if (!drag.moved) {
                        app.roll_sel_t0 = app.roll_sel_t1 = 0;
                        menu_tick = drag.anchor; menu_semitone = -1; menu_ins = -1; menu_note = false;
                        if (mtick >= 0 && mtick < length) open_menu = true;
                    }
                    drag.mode = Mode::None;
                }
                break;
            }
            default: drag.mode = Mode::None; break;
        }
        dl->PopClipRect();
    }
    if (drag.sounding < 0 && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) { app.engine.preview_off(); drag.sounding = 0; }
    else if (drag.sounding > 0 && --drag.sounding == 0) app.engine.preview_off();

    if (focused && has_track && !wrote && !io.WantTextInput) {
        std::vector<Bar> sel = selected_bars();
        int dsemi = 0, dtick = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) dsemi = io.KeyShift ? 12 : 1;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) dsemi = io.KeyShift ? -12 : -1;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) dtick = -unit;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) dtick = unit;
        if (!sel.empty() && (dsemi || dtick)) { if (move_selected(sel, dtick, dsemi, false)) preview(std::clamp(sel[0].semitone + dsemi, 0, kSemitones - 1), sel[0].ins, 12, nullptr, sel[0].tick + dtick); }
        else if (!sel.empty() && (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace))) erase_selected();
        else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C) && !sel.empty()) {
            clip.clear();
            int base = sel[0].tick;
            for (const Bar& b : sel) base = std::min(base, b.tick);
            for (const Bar& b : sel) clip.push_back({b.tick - base, b.semitone, b.dur, b.ins});
            app.status = std::to_string(clip.size()) + " note(s) copied";
        } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V) && !clip.empty()) {
            int at = in_canvas && mtick >= 0 ? snap(mtick) : app.roll_sel_t1 > app.roll_sel_t0 ? app.roll_sel_t0 : std::max(0, app.roll_sel_tick);
            std::vector<Bar> pasted;
            for (const ClipNote& c : clip) { Bar b; b.tick = c.tick; b.semitone = c.semitone; b.dur = c.dur; b.ins = c.ins; pasted.push_back(b); }
            if (move_selected(pasted, at, 0, true)) { app.roll_sel_t0 = at; app.roll_sel_t1 = at; for (const ClipNote& c : clip) app.roll_sel_t1 = std::max(app.roll_sel_t1, at + c.tick + 1); app.roll_sel_tick = -1; }
        } else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) clear_selection();
    }
    auto add_command_at = [&](int at, uint8_t op, bool quiet) {
        int size = D.cmd_size(op);
        std::vector<Event> ev = events();
        uint8_t bytes[16] = {op};
        const bool copied = Tracker::default_args(D, pat, app.sel_voice, at, bytes, size);
        if (size > 0 && size <= 16 && D.insert_command_at(ev, at, bytes, size)) {
            commit(ev);
            app.roll_sel_tick = at; app.roll_sel_t0 = app.roll_sel_t1 = 0;
            if (!quiet) {
                app.show_event_editor = true; app.focus_event_editor = size > 1;
                if (size > 1) app.status += copied ? " - arguments copied from the last use; adjust them in the Event panel" : " - set its arguments in the Event panel";
            }
        } else app.status = "cannot add a command there";
    };
    auto find_cmd = [&](const char* want, const char* avoid) -> int {
        for (int i = 0; i < D.command_count(); ++i) {
            uint8_t op = uint8_t(D.first_command() + i);
            if (D.cmd_size(op) <= 1) continue;
            std::string n = D.cmd_name(op);
            for (char& c : n) c = char(std::tolower(uint8_t(c)));
            if (n.find(want) == std::string::npos) continue;
            bool bad = false;
            std::string a = avoid ? avoid : "";
            size_t pos = 0;
            while (pos < a.size()) { size_t sp = a.find(' ', pos); std::string w = a.substr(pos, sp == std::string::npos ? std::string::npos : sp - pos); if (!w.empty() && n.find(w) != std::string::npos) bad = true; pos = sp == std::string::npos ? a.size() : sp + 1; }
            if (!bad) return op;
        }
        return -1;
    };
    struct Quick { const char* label; const char* want; const char* avoid; };
    static const Quick quick[] = {{"Volume", "volume", "fade global master mono scale"}, {"Panning", "pan", "fade sweep lfo"}, {"Vibrato", "vibrato", "off fade restart"}, {"Tempo", "tempo", "fade add"}};
    const bool can_edit_cmds = !D.in_place_only() || D.has_stream_edit();

    if (note_edit.open) { ImGui::OpenPopup("noteedit"); note_edit.open = false; }
    if (ImGui::BeginPopup("noteedit")) {
        const Bar* nb = nullptr;
        for (const Bar& b : bars) if (b.tick == note_edit.tick) { nb = &b; break; }
        if (!nb || wrote) { ImGui::TextDisabled("(note gone)"); if (!wrote) ImGui::CloseCurrentPopup(); }
        else {
            const Bar b = *nb;   // a copy: edits below re-parse the pattern
            ImGui::TextColored(th.colors[b.muted ? TC_NOTE_OFF : TC_NOTE], "%s", b.semitone >= 0 ? seq::note_name(b.semitone).c_str() : "percussion");
            ImGui::SameLine(); ImGui::TextDisabled("tick %d (row %d)  %d ticks%s", b.tick, b.tick / tpr, b.dur, b.in_sub ? "  shared bytes" : "");
            ImGui::Separator();
            bool plays = !b.muted;
            if (ImGui::Checkbox("Plays", &plays) && has_track) {
                std::vector<Event> ev = events();
                if (!plays) {
                    if (erase_note(D, ev, b.tick, length, why) && commit(ev)) app.muted[App::muted_key(app.sel_voice, b.tick)] = {b.semitone, b.dur, b.ins};
                    else app.status = why;
                } else {
                    if (place_note(D, ev, b.tick, b.semitone, b.dur, length, b.ins, why) && commit(ev)) app.muted.erase(App::muted_key(app.sel_voice, b.tick));
                    else app.status = why;
                }
            }
            if (ImGui::IsItemHovered()) tooltip_spaced("Off: the note is written out as a rest (the song data has no mute flag)\nand kept here hollow, so it can be turned back on. Session only.");
            if (b.semitone >= 0) {
                int semi = b.semitone;
                ImGui::SetNextItemWidth(em(8.0f));
                if (ImGui::SliderInt("pitch", &semi, 0, kSemitones - 1, seq::note_name(semi).c_str()) && semi != b.semitone) {
                    if (b.muted) app.muted[App::muted_key(app.sel_voice, b.tick)].semitone = semi;
                    else if (has_track) { std::vector<Event> ev = events(); if (D.enter_note(ev, b.tick, semi, length)) commit(ev); else app.status = "cannot change that note (shared bytes?)"; }
                }
            }
            int rows_len = std::max(1, (b.dur + tpr - 1) / tpr);
            ImGui::SetNextItemWidth(input_int_w(3));
            if (ImGui::InputInt("rows", &rows_len, 1, 4, ImGuiInputTextFlags_EnterReturnsTrue) && rows_len >= 1 && rows_len * tpr != b.dur) {
                if (b.muted) app.status = "turn the note on to change its length";
                else if (has_track) { std::vector<Event> ev = events(); if (place_note(D, ev, b.tick, b.semitone, rows_len * tpr, length, -1, why)) commit(ev); else app.status = why; }
            }
            if (ImGui::IsItemHovered()) tooltip_spaced("Length in rows (Enter applies). %d ticks per row.", tpr);
            if (D.has_instruments()) {
                ImGui::SameLine();
                int ins = std::max(0, b.ins);
                ImGui::SetNextItemWidth(input_int_w(3));
                if (ImGui::InputInt("ins", &ins, 1, 1, ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue) && ins >= 0 && ins != b.ins) {
                    if (b.muted) app.muted[App::muted_key(app.sel_voice, b.tick)].ins = ins;
                    else if (has_track) { std::vector<Event> ev = events(); if (D.set_instrument(ev, b.tick, b.tick + 1, uint8_t(ins))) commit(ev); else app.status = "cannot set the instrument there"; }
                }
                if (ImGui::IsItemHovered()) tooltip_spaced("Instrument number (hex, Enter applies).");
            }
            ImGui::Separator();
            ImGui::TextDisabled("Effects on this note");
            std::vector<Event> ev = events();
            int shown = 0;
            for (int i = 0; i < int(ev.size()) && !wrote; ++i) {
                const Event& e = ev[size_t(i)];
                if (e.type != EventType::Command || e.tick < b.tick || e.tick >= b.tick + b.dur || D.is_instrument_cmd(e.b[0])) continue;
                ++shown;
                ImGui::PushID(i);
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(th.u32(fx_colour(D.cmd_class(e.b[0])))), "%s", D.cmd_name(e.b[0]));
                if (e.tick != b.tick) { ImGui::SameLine(); ImGui::TextDisabled("+%d", e.tick - b.tick); }
                bool changed = false;
                Event edited = e;
                for (int k = 1; k < e.size; ++k) {
                    ImGui::SameLine();
                    ImGui::PushID(k);
                    int val = e.b[k];
                    ImGui::SetNextItemWidth(em(3.2f));
                    if (ImGui::InputInt("##arg", &val, 0, 0, ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue)) { edited.b[k] = uint8_t(val & 0xFF); changed = true; }
                    if (ImGui::IsItemHovered()) tooltip_spaced("argument %d (hex, Enter applies)", k);
                    ImGui::PopID();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("x")) {
                    if (e.in_sub) { D.unroll_at(ev, e.tick); }
                    int j = -1; for (int q = 0; q < int(ev.size()); ++q) if (ev[size_t(q)].tick == e.tick && ev[size_t(q)].type == EventType::Command && ev[size_t(q)].b[0] == e.b[0] && !ev[size_t(q)].in_sub) { j = q; break; }
                    if (j >= 0) { Tracker::remove_event(D, ev, j); commit(ev); }
                    ImGui::PopID();
                    break;
                }
                if (ImGui::IsItemHovered()) tooltip_spaced("remove this effect");
                if (changed) {
                    if (e.in_sub) D.unroll_at(ev, e.tick);
                    int j = -1; for (int q = 0; q < int(ev.size()); ++q) if (ev[size_t(q)].tick == e.tick && ev[size_t(q)].type == EventType::Command && ev[size_t(q)].b[0] == e.b[0] && !ev[size_t(q)].in_sub) { j = q; break; }
                    if (j >= 0) { std::memcpy(ev[size_t(j)].b, edited.b, 16); commit(ev); }
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }
            if (!shown) ImGui::TextDisabled("(none)");
            if (can_edit_cmds && !wrote) {
                ImGui::TextDisabled("Add:");
                for (const Quick& q : quick) {
                    int op = find_cmd(q.want, q.avoid);
                    if (op < 0) continue;
                    ImGui::SameLine();
                    if (ImGui::SmallButton(q.label)) add_command_at(b.tick, uint8_t(op), true);
                    if (ImGui::IsItemHovered()) tooltip_spaced("$%02X %s", op, D.cmd_name(uint8_t(op)));
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(em(7.0f));
                if (ImGui::BeginCombo("##addfx2", "more...")) {
                    for (int i = 0; i < D.command_count(); ++i) {
                        uint8_t op = uint8_t(D.first_command() + i);
                        if (D.cmd_size(op) <= 0 || D.cmd_class(op) == seq::FxClass::Song) continue;
                        char lb[64]; std::snprintf(lb, sizeof lb, "$%02X %s", op, D.cmd_name(op));
                        ImGui::PushStyleColor(ImGuiCol_Text, th.colors[fx_colour(D.cmd_class(op))]);
                        if (ImGui::Selectable(lb)) add_command_at(b.tick, op, true);
                        ImGui::PopStyleColor();
                    }
                    ImGui::EndCombo();
                }
            } else if (!can_edit_cmds) ImGui::TextDisabled("this driver's streams are patched in place: no new commands");
            ImGui::Separator();
            if (ImGui::Button("Delete note") && has_track && !wrote) {
                if (b.muted) app.muted.erase(App::muted_key(app.sel_voice, b.tick));
                else { std::vector<Event> ev2 = events(); if (erase_note(D, ev2, b.tick, length, why)) commit(ev2); else app.status = why; }
                clear_selection();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (open_menu) ImGui::OpenPopup("rollmenu");
    if (ImGui::BeginPopup("rollmenu")) {
        const int at = menu_tick;
        auto add_command = [&](uint8_t op) { add_command_at(at, op, false); };
        if (menu_note) ImGui::TextDisabled("%s at tick %d", seq::note_name(menu_semitone).c_str(), at);
        else ImGui::TextDisabled("tick %d", at);
        ImGui::Separator();
        const bool can_edit = can_edit_cmds;
        for (const Quick& q : quick) {
            int op = find_cmd(q.want, q.avoid);
            if (op < 0) continue;
            char b[64]; std::snprintf(b, sizeof b, "Add %s  ($%02X %s)", q.label, op, D.cmd_name(uint8_t(op)));
            if (ImGui::MenuItem(b, nullptr, false, can_edit)) add_command(uint8_t(op));
        }
        if (D.has_instruments()) {
            int ins = app.roll_ins >= 0 ? app.roll_ins : -1;
            if (ins >= 0) {
                char b[64]; std::snprintf(b, sizeof b, "Set instrument %02X here", ins);
                if (ImGui::MenuItem(b, nullptr, false, can_edit)) {
                    std::vector<Event> ev = events();
                    if (D.set_instrument(ev, at, at + 1, uint8_t(ins))) commit(ev); else app.status = "cannot set the instrument there";
                }
            }
        }
        if (ImGui::BeginMenu("Add command", can_edit)) {
            static const struct { seq::FxClass cls; const char* name; } groups[] = {
                {seq::FxClass::Instrument, "instrument"}, {seq::FxClass::Volume, "volume"}, {seq::FxClass::Panning, "panning"},
                {seq::FxClass::Pitch, "pitch"}, {seq::FxClass::Time, "time"}, {seq::FxClass::Speed, "tempo"},
                {seq::FxClass::Sys1, "echo / DSP"}, {seq::FxClass::Sys2, "driver"}, {seq::FxClass::Misc, "misc"}, {seq::FxClass::Song, "song / flow"}};
            for (const auto& g : groups) {
                bool any = false;
                for (int i = 0; i < D.command_count() && !any; ++i) { uint8_t op = uint8_t(D.first_command() + i); if (D.cmd_size(op) > 0 && D.cmd_class(op) == g.cls) any = true; }
                if (!any) continue;
                ImGui::PushStyleColor(ImGuiCol_Text, th.colors[fx_colour(g.cls)]);
                const bool open = ImGui::BeginMenu(g.name);
                ImGui::PopStyleColor();
                if (!open) continue;
                for (int i = 0; i < D.command_count(); ++i) {
                    uint8_t op = uint8_t(D.first_command() + i);
                    if (D.cmd_size(op) <= 0 || D.cmd_class(op) != g.cls) continue;
                    char b[80]; std::snprintf(b, sizeof b, "$%02X  %s", op, D.cmd_name(op));
                    if (ImGui::MenuItem(b)) add_command(op);
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        if (menu_note) {
            ImGui::Separator();
            const bool slides = menu_semitone >= 0 && D.can_slide();
            if (ImGui::BeginMenu("Slide to", slides)) {
                static const int steps[] = {12, 7, 5, 4, 3, 2, 1, -1, -2, -3, -4, -5, -7, -12};
                for (int d : steps) {
                    int to = menu_semitone + d;
                    if (to < 0 || to >= kSemitones) continue;
                    char b[48]; std::snprintf(b, sizeof b, "%s  (%+d)", seq::note_name(to).c_str(), d);
                    if (ImGui::MenuItem(b)) {
                        const Bar* bp = nullptr;
                        for (const Bar& c : bars) if (c.tick == at) { bp = &c; break; }
                        if (bp) {
                            const int span = slide_on(*bp);
                            const int from = span >= 0 && spans[size_t(span)].glide ? spans[size_t(span)].from : bp->semitone;
                            std::vector<Event> ev = events(); if (write_slide(ev, *bp, from, to, span)) commit(ev); else app.status = why;
                        }
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::IsItemHovered() && !slides) tooltip_spaced("this driver has no pitch slide command the roll can write");
            if (menu_ins >= 0 && ImGui::MenuItem("Make its instrument current")) { app.roll_ins = menu_ins; app.sel_instrument = D.instrument_index(app.snap.ram, menu_ins); }
            if (ImGui::MenuItem("Edit note...", "double-click")) { note_edit.open = true; note_edit.tick = at; }
            if (ImGui::MenuItem("Delete note", "Shift-click")) {
                if (app.muted.count(App::muted_key(app.sel_voice, at))) app.muted.erase(App::muted_key(app.sel_voice, at));
                else { std::vector<Event> ev = events(); if (erase_note(D, ev, at, length, why)) commit(ev); else app.status = why; }
                clear_selection();
            }
        } else if (!clip.empty() && ImGui::MenuItem("Paste here", "Ctrl+V")) {
            std::vector<Bar> pasted;
            for (const ClipNote& c : clip) { Bar b; b.tick = c.tick; b.semitone = c.semitone; b.dur = c.dur; b.ins = c.ins; pasted.push_back(b); }
            move_selected(pasted, at, 0, true);
        }
        {
            const seq::Track& lt = pat.tracks[app.sel_voice];
            bool header = false;
            for (int i = 0; i < lt.used_events && !wrote; ++i) {
                const Event& e = lt.events[size_t(i)];
                if (e.tick != at || (e.type != EventType::Command && e.type != EventType::SubCall)) continue;
                if (!header) { ImGui::Separator(); ImGui::TextDisabled("commands here:"); header = true; }
                char b[96]; std::snprintf(b, sizeof b, "remove %s", D.event_text(e).c_str());
                ImGui::PushID(i);
                if (ImGui::MenuItem(b, nullptr, false, can_edit)) { std::vector<Event> ev = events(); if (e.in_sub) D.unroll_at(ev, at); int k = -1; for (int j = 0; j < int(ev.size()); ++j) if (ev[size_t(j)].tick == at && ev[size_t(j)].type == e.type && ev[size_t(j)].b[0] == e.b[0] && !ev[size_t(j)].in_sub) { k = j; break; } if (k >= 0) { Tracker::remove_event(D, ev, k); commit(ev); } }
                ImGui::PopID();
            }
        }
        ImGui::EndPopup();
    }
    const float roll_scroll_x = ImGui::GetScrollX();
    child_end();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    child_begin("fxlane", ImVec2(0, lane_h), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse, false);
    ImGui::PopStyleVar();
    if (!wrote) {
        ImGui::SetScrollX(roll_scroll_x);
        ImDrawList* ld = ImGui::GetWindowDrawList();
        const ImVec2 lwin = ImGui::GetWindowPos(), lsize = ImGui::GetWindowSize();
        const ImVec2 lorigin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("lanebtn", ImVec2(std::max(content.x, ImGui::GetContentRegionAvail().x), lane_h - 2));
        const bool lane_hovered = ImGui::IsItemHovered();
        const float lh = ImGui::GetTextLineHeight() + 2;
        ld->AddRectFilled(lwin, ImVec2(lwin.x + lsize.x, lwin.y + lsize.y), th.u32(TC_PATTERN_BG));
        ld->PushClipRect(ImVec2(lwin.x + key_w, lwin.y), ImVec2(lwin.x + lsize.x, lwin.y + lsize.y), true);
        const seq::Track& lt = pat.tracks[app.sel_voice];
        const bool value_mode = app.roll_fx_view >= 0 && D.cmd_size(uint8_t(app.roll_fx_view)) >= 2;
        struct Tag { int ev; float x0, x1; int row; };
        std::vector<Tag> tags;
        float row_end[2] = {-1e9f, -1e9f};
        static struct { int ev = -1; int tick = 0; bool active = false; int value = 0; } lane_drag;
        if (value_mode) {
            const uint8_t vop = uint8_t(app.roll_fx_view);
            const float lane_y0 = lwin.y + 2, lane_y1 = lwin.y + lsize.y - 3, lane_hh = lane_y1 - lane_y0;
            const float bw = std::max(4.0f, unit * ppt - 1);
            int hover_ev = -1;
            for (int i = 0; i < lt.used_events; ++i) {
                const Event& e = lt.events[size_t(i)];
                if (e.type != EventType::Command || e.b[0] != vop) continue;
                int val = e.b[e.size - 1];
                if (lane_drag.active && lane_drag.ev == i) val = lane_drag.value;
                float x0 = lorigin.x + key_w + e.tick * ppt, h = lane_hh * val / 255.0f;
                ImU32 col = th.u32(fx_colour(D.cmd_class(vop)), e.in_sub ? 0.4f : 0.9f);
                ld->AddRectFilled(ImVec2(x0, lane_y1 - h), ImVec2(x0 + bw, lane_y1), col);
                ld->AddRect(ImVec2(x0, lane_y1 - h), ImVec2(x0 + bw, lane_y1), th.u32(TC_PATTERN_BG, 0.7f));
                const ImVec2 mp = ImGui::GetMousePos();
                if (lane_hovered && mp.x >= x0 && mp.x < x0 + bw + 2) hover_ev = i;
            }
            const ImVec2 mp = ImGui::GetMousePos();
            const float ltick = (mp.x - lorigin.x - key_w) / ppt;
            auto value_at = [&](float y) { return std::clamp(int((lane_y1 - y) / lane_hh * 255.0f + 0.5f), 0, 255); };
            if (lane_hovered && !lane_drag.active && !wrote) {
                if (hover_ev >= 0) {
                    const Event& e = lt.events[size_t(hover_ev)];
                    if (ImGui::BeginTooltip()) { ImGui::Text("tick %d  %s", e.tick, D.event_text(e).c_str()); ImGui::TextDisabled("drag to change, double-click to remove"); ImGui::EndTooltip(); }
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        if (e.in_sub) app.status = "that command is in shared bytes";
                        else { std::vector<Event> ev = events(); Tracker::remove_event(D, ev, hover_ev); commit(ev); }
                    } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !e.in_sub) { lane_drag = {}; lane_drag.active = true; lane_drag.ev = hover_ev; lane_drag.value = e.b[e.size - 1]; }
                } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ltick >= 0 && ltick < length && has_track) {
                    lane_drag = {}; lane_drag.active = true; lane_drag.ev = -1; lane_drag.tick = snap(ltick); lane_drag.value = value_at(mp.y);
                } else if (ImGui::BeginTooltip()) { ImGui::Text("%s: click to add, drag a bar to change its value", D.cmd_name(vop)); ImGui::EndTooltip(); }
            }
            if (lane_drag.active) {
                lane_drag.value = value_at(mp.y);
                if (lane_drag.ev < 0) {
                    float x0 = lorigin.x + key_w + lane_drag.tick * ppt, h = lane_hh * lane_drag.value / 255.0f;
                    ld->AddRectFilled(ImVec2(x0, lane_y1 - h), ImVec2(x0 + bw, lane_y1), th.u32(fx_colour(D.cmd_class(vop)), 0.6f));
                }
                if (ImGui::BeginTooltip()) { ImGui::Text("%s = %02X (%d)", D.cmd_code(vop), lane_drag.value, lane_drag.value); ImGui::EndTooltip(); }
                if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    std::vector<Event> ev = events();
                    if (lane_drag.ev >= 0 && lane_drag.ev < int(ev.size())) { ev[size_t(lane_drag.ev)].b[ev[size_t(lane_drag.ev)].size - 1] = uint8_t(lane_drag.value); commit(ev); }
                    else {
                        uint8_t bytes[16] = {vop}; int size = D.cmd_size(vop);
                        bytes[size - 1] = uint8_t(lane_drag.value);
                        if (D.insert_command_at(ev, lane_drag.tick, bytes, size)) commit(ev); else app.status = "cannot add a command there";
                    }
                    lane_drag.active = false;
                }
            }
        }
        for (int i = 0; i < lt.used_events && !value_mode; ++i) {
            const Event& e = lt.events[size_t(i)];
            if (e.type != EventType::Command && e.type != EventType::SubCall) continue;
            const char* code = e.type == EventType::SubCall ? "Sub" : D.cmd_code(e.b[0]);
            float x0 = lorigin.x + key_w + e.tick * ppt;
            float w = ImGui::CalcTextSize(code).x + 6;
            int row = x0 < row_end[0] + 2 ? 1 : 0;
            if (row == 1 && x0 < row_end[1] + 2) { row = row_end[0] <= row_end[1] ? 0 : 1; x0 = row_end[row] + 2; }
            row_end[row] = x0 + w;
            tags.push_back({i, x0, x0 + w, row});
        }
        int hover_tag = -1;
        for (size_t k = 0; k < tags.size(); ++k) {
            const Tag& tg = tags[k];
            const Event& e = lt.events[size_t(tg.ev)];
            if (tg.x1 < lwin.x + key_w || tg.x0 > lwin.x + lsize.x) continue;
            ImVec2 p0(tg.x0, lorigin.y + 2 + tg.row * lh), p1(tg.x1, lorigin.y + 2 + tg.row * lh + lh - 2);
            ThemeColor tc = e.type == EventType::SubCall ? TC_FX_SONG : fx_colour(D.cmd_class(e.b[0]));
            ImU32 fill = th.u32(tc, e.in_sub ? 0.35f : 0.8f);
            ld->AddRectFilled(p0, p1, fill, 2.0f);
            ld->AddText(ImVec2(p0.x + 3, p0.y), th.u32(TC_PATTERN_BG), e.type == EventType::SubCall ? "Sub" : D.cmd_code(e.b[0]));
            const ImVec2 mp = ImGui::GetMousePos();
            if (lane_hovered && mp.x >= p0.x && mp.x < p1.x && mp.y >= p0.y && mp.y < p1.y) hover_tag = int(k);
        }
        if (head_tick >= 0) { float x = lorigin.x + key_w + head_tick * ppt; ld->AddLine(ImVec2(x, lwin.y), ImVec2(x, lwin.y + lsize.y), th.u32(TC_PLAYHEAD_LINE), 2.0f); }
        ld->PopClipRect();
        ld->PushClipRect(lwin, ImVec2(lwin.x + key_w, lwin.y + lsize.y), true);
        ld->AddRectFilled(lwin, ImVec2(lwin.x + key_w, lwin.y + lsize.y), th.u32(TC_CHANNEL_HEADER_BG));
        ld->AddText(ImVec2(lwin.x + 4, lorigin.y + 2), th.u32(TC_CHANNEL_HEADER), value_mode ? D.cmd_code(uint8_t(app.roll_fx_view)) : "fx");
        ld->PopClipRect();
        static struct { int tick = -1; uint8_t op = 0; uint8_t bytes[16] = {}; int size = 0; bool open = false; } tag_edit;
        if (hover_tag >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !wrote) {
            const Event& e = lt.events[size_t(tags[size_t(hover_tag)].ev)];
            if (e.type == EventType::Command) {
                tag_edit.tick = e.tick; tag_edit.op = e.b[0]; tag_edit.size = e.size; std::memcpy(tag_edit.bytes, e.b, 16); tag_edit.open = true;
                ImGui::OpenPopup("tagedit");
            }
        }
        if (ImGui::BeginPopup("tagedit")) {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(th.u32(fx_colour(D.cmd_class(tag_edit.op)))), "$%02X %s", tag_edit.op, D.cmd_name(tag_edit.op));
            ImGui::TextDisabled("tick %d", tag_edit.tick);
            for (int i = 1; i < tag_edit.size; ++i) {
                ImGui::PushID(i);
                int val = tag_edit.bytes[i];
                ImGui::SetNextItemWidth(em(4.0f));
                if (ImGui::InputInt("##b", &val, 0, 0, ImGuiInputTextFlags_CharsHexadecimal)) tag_edit.bytes[i] = uint8_t(val & 0xFF);
                if (ImGui::IsItemHovered()) tooltip_spaced("argument byte %d (hex)", i);
                if (i < tag_edit.size - 1) ImGui::SameLine();
                ImGui::PopID();
            }
            if (tag_edit.size <= 1) ImGui::TextDisabled("(no arguments)");
            auto find_it = [&](std::vector<Event>& ev) {
                for (int j = 0; j < int(ev.size()); ++j) if (ev[size_t(j)].tick == tag_edit.tick && ev[size_t(j)].type == EventType::Command && ev[size_t(j)].b[0] == tag_edit.op) return j;
                return -1;
            };
            if (ImGui::Button("Apply") && !wrote) {
                std::vector<Event> ev = events();
                int j = find_it(ev);
                if (j >= 0 && ev[size_t(j)].in_sub) { D.unroll_at(ev, tag_edit.tick); j = find_it(ev); }
                if (j >= 0) { std::memcpy(ev[size_t(j)].b, tag_edit.bytes, 16); commit(ev); }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Remove") && !wrote) {
                std::vector<Event> ev = events();
                int j = find_it(ev);
                if (j >= 0 && ev[size_t(j)].in_sub) { D.unroll_at(ev, tag_edit.tick); j = find_it(ev); }
                if (j >= 0) { Tracker::remove_event(D, ev, j); commit(ev); }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (hover_tag >= 0) {
            const Event& e = lt.events[size_t(tags[size_t(hover_tag)].ev)];
            if (ImGui::BeginTooltip()) {
                ImGui::Text("tick %d  %s%s", e.tick, D.event_text(e).c_str(), e.in_sub ? "  (shared bytes)" : "");
                ImGui::TextDisabled("click to edit its arguments, right-click to remove");
                ImGui::EndTooltip();
            }
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !wrote) {
                if (D.in_place_only() && !D.has_stream_edit()) app.status = "commands cannot be removed: this driver's streams are patched in place";
                else {
                    std::vector<Event> ev = events();
                    int j = tags[size_t(hover_tag)].ev;
                    if (e.in_sub) { D.unroll_at(ev, e.tick); j = -1; for (int k = 0; k < int(ev.size()); ++k) if (ev[size_t(k)].tick == e.tick && ev[size_t(k)].type == e.type && ev[size_t(k)].b[0] == e.b[0] && !ev[size_t(k)].in_sub) { j = k; break; } }
                    if (j >= 0) { Tracker::remove_event(D, ev, j); commit(ev); }
                }
            }
        }
    }
    child_end();
    if (app.insert_fx_op >= 0 && !add_fx_now) { add_fx_now = true; add_cmd = app.insert_fx_op - D.first_command(); app.insert_fx_op = -1; }
    if (add_fx_now && !wrote) {
        if (app.roll_sel_tick < 0 || !has_track) app.status = "select a note first: the command goes at its tick";
        else {
            uint8_t op = uint8_t(D.first_command() + add_cmd);
            int size = D.cmd_size(op);
            std::vector<Event> ev = events();
            uint8_t bytes[16] = {op};
            Tracker::default_args(D, pat, app.sel_voice, app.roll_sel_tick, bytes, size);
            if (size > 0 && size <= 16 && D.insert_command_at(ev, app.roll_sel_tick, bytes, size)) commit(ev);
            else app.status = "cannot add a command there";
        }
    }
    ImGui::PopFont();
    child_end();
    return wrote;
}
