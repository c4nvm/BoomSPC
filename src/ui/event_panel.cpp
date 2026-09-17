// Event panel: the events under the cursor (tracker row, or the selected
// note in the piano roll) with their arguments editable, plus a picker to
// add a command at that tick. Docked at the bottom next to Voices so the
// grid keeps the full width.
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "imgui.h"
#include "theme.hpp"
#include "ui.hpp"

using seq::Event;
using seq::EventType;

namespace {
ThemeColor fx_theme(seq::FxClass c) {
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

}

void draw_event_panel(App& app) {
    ImGui::SetNextWindowSize(ImVec2(520, 260), ImGuiCond_FirstUseEver);
    if (app.focus_event_editor) { ImGui::SetNextWindowFocus(); app.focus_event_editor = false; }
    if (!panel_begin("Event", &app.show_event_editor)) { panel_end(); return; }
    Tracker& T = app.tracker;
    if (!app.engine.loaded() || !T.drv || !T.song() || T.song()->orders.empty()) { ImGui::TextDisabled("No song loaded."); panel_end(); return; }
    const seq::Driver& D = *T.drv;
    const Theme& th = theme();
    seq::Song& song = *T.song();
    const int order = std::clamp(app.view_order, 0, int(song.orders.size()) - 1);
    const int pat_idx = song.pattern_index(song.orders[size_t(order)].pattern_addr);
    if (pat_idx < 0) { ImGui::TextDisabled("No pattern."); panel_end(); return; }
    seq::Pattern& pat = song.patterns[size_t(pat_idx)];
    const int tpr = std::max(1, app.ticks_per_row);
    const bool in_place = D.in_place_only();
    const bool stream = D.has_stream_edit();

    const bool roll = app.seq_tab == 1;
    int tick = -1;
    if (roll && app.roll_sel_tick >= 0) tick = app.roll_sel_tick;
    else if (!roll && app.sel_row >= 0) tick = app.sel_row * tpr;
    const int v = app.sel_voice;
    if (v < 0 || v > 7 || tick < 0) {
        ImGui::TextDisabled(roll ? "Select a note in the piano roll." : "Select a cell in the tracker.");
        panel_end();
        return;
    }
    seq::Track& t = pat.tracks[v];
    if (roll) ImGui::Text("v%d  tick %d", v + 1, tick);
    else ImGui::Text("v%d  row %d  tick %d", v + 1, app.sel_row, tick);
    if (!roll && app.sel_active) { ImGui::SameLine(); ImGui::TextDisabled("block v%d-%d rows %d-%d", std::min(app.sel_v0, app.sel_v1) + 1, std::max(app.sel_v0, app.sel_v1) + 1, std::min(app.sel_r0, app.sel_r1), std::max(app.sel_r0, app.sel_r1)); }
    ImGui::SameLine();
    if (!t.addr) ImGui::TextDisabled("(voice unused here; a note creates a track)");
    else if (stream) ImGui::TextDisabled("stream @$%04X%s", t.addr, t.loops ? " (loops)" : "");
    else if (in_place) ImGui::TextDisabled("stream @$%04X%s, patched in place", t.addr, t.loops ? " (loops)" : "");
    else {
        ImGui::TextDisabled("track $%04X-%04X%s", t.addr, t.end_addr, t.terminated ? "" : " (no own end)");
        for (int w = 0; w < 8; ++w)
            if (w != v && pat.tracks[w].addr == t.addr) { ImGui::SameLine(); ImGui::TextColored(ImVec4(1, 0.7f, 0.4f, 1), "shared with v%d", w + 1); break; }
    }
    ImGui::Separator();

    bool changed = false;
    std::vector<Event> ev(t.events.begin(), t.events.begin() + t.used_events);
    std::vector<int> idxs;
    for (int i = 0; i < int(ev.size()); ++i) {
        const Event& e = ev[size_t(i)];
        if (e.type == EventType::End) continue;
        if (roll ? e.tick == tick : e.tick / tpr == tick / tpr) idxs.push_back(i);
    }
    if (idxs.empty()) ImGui::TextDisabled("nothing starts here");
    child_begin("events", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 4));
    for (int ei : idxs) {
        Event& e = ev[size_t(ei)];
        ImGui::PushID(ei);
        ImU32 col = th.u32(TC_NOTE);
        if (e.type == EventType::Command) col = D.is_instrument_cmd(e.b[0]) ? th.instrument(e.b[1]) : th.u32(fx_theme(D.cmd_class(e.b[0])));
        else if (e.type == EventType::Rest) col = th.u32(TC_NOTE_OFF);
        else if (e.type == EventType::Tie) col = th.u32(TC_NOTE_TIE);
        else if (e.type == EventType::SubCall) col = th.u32(TC_FX_SONG);
        else if (e.type == EventType::Length) col = th.u32(TC_QUANT);
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::Text("t%d  %s%s", e.tick, D.event_text(e).c_str(), e.in_sub ? "  (shared: edits unroll)" : "");
        ImGui::PopStyleColor();
        if (!in_place || stream) {
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) { if (e.in_sub) D.unroll_at(ev, e.tick); int k = -1; for (int j = 0; j < int(ev.size()); ++j) if (ev[size_t(j)].tick == e.tick && ev[size_t(j)].type == e.type && ev[size_t(j)].b[0] == e.b[0] && !ev[size_t(j)].in_sub) { k = j; break; } if (k >= 0) { Tracker::remove_event(D, ev, k); changed = true; } ImGui::PopID(); break; }
            if (ImGui::IsItemHovered()) tooltip_spaced("remove this event");
        }
        if (e.type == EventType::Command && D.is_instrument_cmd(e.b[0])) {
            ImGui::SameLine();
            if (ImGui::SmallButton("edit")) { app.sel_instrument = D.instrument_index(app.snap.ram, e.b[1]); app.show_instruments = true; }
        }
        if (e.type == EventType::Length) {
            int len = e.b[0];
            ImGui::SetNextItemWidth(input_int_w(3));
            if (ImGui::InputInt("ticks", &len)) { e.b[0] = uint8_t(std::clamp(len, 1, 0x7F)); changed = true; }
            bool has_qv = e.size == 2;
            ImGui::SameLine();
            if (ImGui::Checkbox("quant/vel", &has_qv)) { e.size = has_qv ? 2 : 1; if (has_qv && !e.b[1]) e.b[1] = 0x7F; changed = true; }
            if (has_qv) {
                int q = e.b[1] >> 4, vel = e.b[1] & 15;
                ImGui::SetNextItemWidth(em(6.0f));
                if (ImGui::SliderInt("quant", &q, 0, 7)) { e.b[1] = uint8_t((q << 4) | vel); changed = true; }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(em(6.0f));
                if (ImGui::SliderInt("vel", &vel, 0, 15)) { e.b[1] = uint8_t((q << 4) | vel); changed = true; }
            }
        } else if (e.type == EventType::Command || e.type == EventType::SubCall) {
            for (int i = 1; i < e.size; ++i) {
                ImGui::PushID(i);
                int val = e.b[i];
                ImGui::SetNextItemWidth(em(4.0f));
                if (ImGui::InputInt("##arg", &val, 0, 0, ImGuiInputTextFlags_CharsHexadecimal)) { e.b[i] = uint8_t(val & 0xFF); changed = true; }
                if (ImGui::IsItemHovered()) tooltip_spaced("argument byte %d (hex)", i);
                if (i < e.size - 1) ImGui::SameLine();
                ImGui::PopID();
            }
        } else if (e.type == EventType::Note || e.type == EventType::Percussion) {
            int n = e.b[0];
            ImGui::SetNextItemWidth(em(8.0f));
            if (ImGui::SliderInt("note", &n, D.note_min(), D.note_max(), D.note_name(uint8_t(n)).c_str())) { D.apply_note_byte(e, uint8_t(n)); changed = true; }
        }
        if (stream && e.duration > 0) {
            int d = e.duration;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(input_int_w(4));
            if (ImGui::InputInt("ticks", &d)) {
                d = std::clamp(d, 1, 0xFFFF);
                if (d != e.duration) {
                    if (e.in_sub) D.unroll_at(ev, e.tick);
                    int k = -1; for (int j = 0; j < int(ev.size()); ++j) if (ev[size_t(j)].tick == e.tick && ev[size_t(j)].duration > 0 && !ev[size_t(j)].in_sub) { k = j; break; }
                    if (k >= 0 && D.set_duration(ev, k, d)) changed = true; else app.status = "this driver cannot express that duration here";
                }
                ImGui::PopID();
                break;
            }
        } else if ((e.type == EventType::Note || e.type == EventType::Rest) && e.size == 2 && !D.has_qv()) {
            int d = e.b[1];
            ImGui::SameLine();
            ImGui::SetNextItemWidth(input_int_w(3));
            if (ImGui::InputInt("ticks", &d)) { e.b[1] = uint8_t(std::clamp(d, 1, 0xFF)); changed = true; }
        }
        ImGui::PopID();
    }
    child_end();

    if (in_place && !stream) ImGui::TextDisabled("Commands cannot be added: this driver's streams are patched in place.");
    else {
        static int add_cmd = 0;
        ImGui::TextDisabled("Add:"); ImGui::SameLine();
        ImGui::SetNextItemWidth(-em(2.7f));
        const uint8_t base = D.first_command();
        if (ImGui::BeginCombo("##addcmd", D.cmd_name(uint8_t(base + add_cmd)))) {
            for (int i = 0; i < D.command_count(); ++i) {
                if (D.cmd_size(uint8_t(base + i)) <= 0) continue;
                char b[64]; std::snprintf(b, sizeof b, "$%02X %s", base + i, D.cmd_name(uint8_t(base + i)));
                ImGui::PushStyleColor(ImGuiCol_Text, th.colors[fx_theme(D.cmd_class(uint8_t(base + i)))]);
                if (ImGui::Selectable(b, i == add_cmd)) add_cmd = i;
                ImGui::PopStyleColor();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("+")) {
            uint8_t op = uint8_t(base + add_cmd);
            int size = D.cmd_size(op);
            if (size > 0 && size <= 16) {
                uint8_t bytes[16] = {op};
                Tracker::default_args(D, pat, v, tick, bytes, size);
                if (D.insert_command_at(ev, tick, bytes, size)) changed = true; else app.status = "cannot add a command there";
            }
        }
    }
    if (changed) {
        if (!stream) for (int ei : idxs) if (ei < int(ev.size()) && ev[size_t(ei)].in_sub) { D.unroll_at(ev, tick); break; }
        Tracker::Result r = T.write_track(app.engine, pat_idx, v, ev);
        app.status = r.msg;
    }
    panel_end();
}
