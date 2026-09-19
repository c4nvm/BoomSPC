// Arrangement tab: each voice as a lane of clips, one clip per run of bytes
// between jumps / calls / repeat passes; recurring bytes share a letter.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "fonts.hpp"
#include "imgui.h"
#include "theme.hpp"
#include "ui.hpp"

using seq::Event;
using seq::EventType;

namespace {
struct Clip {
    int voice = 0;
    int t0 = 0, t1 = 0;          // ticks
    uint16_t addr0 = 0, addr1 = 0;   // byte range (addr1 exclusive)
    int ev0 = 0, ev1 = 0;        // event index range (ev1 exclusive)
    int id = 0;                  // clip identity (same start address = same clip)
    int pass = 1, passes = 1;    // which recurrence this is
    bool shared = false;         // reached through a call / repeat (shared bytes)
    int nest = 0;
    int notes = 0;
};

std::vector<Clip> clips_of(const seq::Pattern& pat) {
    std::vector<Clip> out;
    std::map<uint16_t, int> ids;   // start address -> clip id, in order of first appearance
    for (int v = 0; v < 8; ++v) {
        const seq::Track& t = pat.tracks[v];
        Clip cur; bool open = false;
        const Event* prev = nullptr;
        auto flush = [&]() {
            if (!open) return;
            if (cur.t1 > cur.t0) out.push_back(cur);
            open = false;
        };
        for (int i = 0; i < t.used_events; ++i) {
            const Event& e = t.events[size_t(i)];
            if (e.type == EventType::End || e.addr == 0) continue;
            const bool contiguous = prev && e.addr == uint16_t(prev->addr + prev->size) && e.in_sub == prev->in_sub;
            if (!open || !contiguous) {
                flush();
                cur = Clip{};
                cur.voice = v; cur.t0 = e.tick; cur.t1 = e.tick; cur.addr0 = e.addr; cur.addr1 = e.addr;
                cur.ev0 = i; cur.shared = e.in_sub; cur.nest = e.nest;
                if (!ids.count(e.addr)) ids[e.addr] = int(ids.size());
                cur.id = ids[e.addr];
                open = true;
            }
            cur.ev1 = i + 1;
            cur.addr1 = uint16_t(e.addr + e.size);
            cur.t1 = std::max(cur.t1, e.tick + std::max(e.duration, 0));
            if (e.type == EventType::Note || e.type == EventType::Percussion) ++cur.notes;
            prev = &e;
        }
        flush();
    }
    std::map<int, int> seen;
    for (Clip& c : out) c.pass = ++seen[c.id];
    for (Clip& c : out) c.passes = seen[c.id];
    std::map<int, int> letters;
    for (Clip& c : out) {
        if (c.passes > 1 || c.shared) { if (!letters.count(c.id)) letters[c.id] = int(letters.size()); c.id = letters[c.id]; }
        else c.id = -1;
    }
    return out;
}

std::string clip_name(int id) {
    std::string s;
    do { s.insert(s.begin(), char('A' + id % 26)); id = id / 26 - 1; } while (id >= 0);
    return s;
}

ImU32 clip_colour(const Theme& th, int id, float alpha) {
    const float h = std::fmod(0.11f + id * 0.618034f, 1.0f);
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(h, 0.55f, 0.85f, r, g, b);
    const ImVec4 base = th.colors[TC_NOTE];
    return ImGui::ColorConvertFloat4ToU32(ImVec4(r * 0.8f + base.x * 0.2f, g * 0.8f + base.y * 0.2f, b * 0.8f + base.z * 0.2f, alpha));
}

}

void draw_arrangement(App& app, int pat_idx, float head_tick) {
    Tracker& T = app.tracker;
    const seq::Driver& D = *T.drv;
    seq::Song& song = *T.song();
    seq::Pattern& pat = song.patterns[size_t(pat_idx)];
    const Theme& th = theme();
    ImGuiIO& io = ImGui::GetIO();
    const int tpr = std::max(1, app.ticks_per_row);
    const int length = std::max(1, pat.length_ticks);

    std::vector<Clip> clips = clips_of(pat);
    int nclips = 0, once = 0; for (const Clip& c : clips) { nclips = std::max(nclips, c.id + 1); if (c.id < 0) ++once; }

    child_begin("arrpane", ImVec2(0, 0), 0, 0, true);
    ImGui::TextDisabled("%d parts, %zu plays, %d runs", nclips, clips.size() - size_t(once), once);
    if (ImGui::IsItemHovered()) tooltip_spaced("%d parts that come back (repeats, calls), played %zu times in all;\n%d one-off runs between them (grey).", nclips, clips.size() - size_t(once), once);
    same_line_if_fits(text_w("-") + text_w("+") + text_w("zoom") + text_w("(?)") + em(3));
    if (ImGui::SmallButton("-")) app.roll_zoom = std::max(0.05f, app.roll_zoom / 1.25f);
    ImGui::SameLine(); if (ImGui::SmallButton("+")) app.roll_zoom = std::min(32.0f, app.roll_zoom * 1.25f);
    ImGui::SameLine(); ImGui::TextDisabled("zoom");
    ImGui::SameLine(); ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        tooltip_spaced("Each lane is a voice; each block is a run of bytes the driver plays straight through.\n"
                       "Jumps, calls and repeat passes start a new block; blocks with the same letter are the\n"
                       "same bytes played again (striped = shared: an edit there is written out as plain data).\n"
                       "Click: select it (the roll and tracker go there).  Double-click: play from it.\n"
                       "Right-click: play from here, open in the roll or tracker, select its range.\n"
                       "One playhead: the song laid out straight. A lane dimmed past its end is a voice whose\n"
                       "track is shorter (it loops a part, or ended): the roll and tracker show it jumping there.");
    (void)D;

    ImGui::PushFont(fonts().mono, th.font_size_pattern);
    const float cw = ImGui::CalcTextSize("0").x;
    const float line_h = ImGui::GetTextLineHeight();
    const float lane_h = line_h * 2.2f;
    const float key_w = cw * 4 + 8;
    const float head_h = line_h + 4;
    const float ppt = (cw * 3.0f * app.roll_zoom) / float(tpr);
    const ImVec2 content(key_w + length * ppt + cw * 2, head_h + 8 * lane_h + 4);

    static float anchor_tick = -1, anchor_px = 0;
    if (app.follow && head_tick >= 0)
        ImGui::SetNextWindowScroll(ImVec2(std::max(0.0f, key_w + head_tick * ppt - ImGui::GetContentRegionAvail().x * std::clamp(th.playhead_pos, 0.1f, 0.9f)), -1.0f));
    else if (anchor_tick >= 0) ImGui::SetNextWindowScroll(ImVec2(std::max(0.0f, key_w + anchor_tick * ppt - anchor_px), -1.0f));
    anchor_tick = -1;
    child_begin("arr", ImVec2(0, 0), ImGuiChildFlags_Borders, 0, false);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 win = ImGui::GetWindowPos();
    const ImVec2 win_size = ImGui::GetWindowSize();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("arrbtn", ImVec2(std::max(content.x, ImGui::GetContentRegionAvail().x), std::max(content.y, ImGui::GetContentRegionAvail().y)));
    const bool hovered = ImGui::IsItemHovered();
    if (hovered && io.MouseWheel != 0 && !io.KeyCtrl && app.follow) app.follow = false;
    if (hovered && io.MouseWheel != 0 && io.KeyCtrl) {
        app.roll_zoom = std::clamp(app.roll_zoom * (io.MouseWheel > 0 ? 1.25f : 0.8f), 0.05f, 32.0f);
        anchor_tick = (ImGui::GetMousePos().x - origin.x - key_w) / ppt; anchor_px = ImGui::GetMousePos().x - win.x;
    }
    const float sx = ImGui::GetScrollX();
    const float view_x0 = win.x + key_w, view_x1 = win.x + win_size.x;
    const float view_y0 = win.y + head_h, view_y1 = win.y + win_size.y;
    auto tick_x = [&](float tick) { return origin.x + key_w + tick * ppt; };
    auto lane_y = [&](int v) { return origin.y + head_h + v * lane_h; };

    dl->PushClipRect(ImVec2(view_x0, view_y0), ImVec2(view_x1, view_y1), true);
    for (int v = 0; v < 8; ++v) {
        const float y = lane_y(v);
        dl->AddRectFilled(ImVec2(view_x0, y), ImVec2(view_x1, y + lane_h), th.u32(v % 2 ? TC_ROW_HI1 : TC_PATTERN_BG, v % 2 ? 0.25f : 1.0f));
    }
    const int rows = (length + tpr - 1) / tpr;
    const int r0 = std::max(0, int((sx - key_w) / (ppt * tpr)) - 1), r1 = std::min(rows, int((sx - key_w + win_size.x) / (ppt * tpr)) + 2);
    const float row_px = ppt * tpr;   // zoomed out, a line per highlight would be a solid wall
    for (int r = r0; r <= r1; ++r) {
        const bool hi2 = th.row_hi2 > 0 && r % th.row_hi2 == 0, hi1 = th.row_hi1 > 0 && r % th.row_hi1 == 0;
        if (!hi1 && !hi2) continue;
        if (row_px * std::max(1, hi2 ? th.row_hi2 : th.row_hi1) < 4.0f) continue;
        const float x = tick_x(float(r * tpr));
        dl->AddLine(ImVec2(x, view_y0), ImVec2(x, lane_y(8)), th.u32(hi2 ? TC_ROW_INDEX_HI2 : TC_ROW_INDEX_HI1, hi2 ? 0.6f : 0.3f));
    }

    const ImVec2 m = ImGui::GetMousePos();
    const Clip* under = nullptr;
    for (const Clip& c : clips) {
        const float x0 = tick_x(float(c.t0)), x1 = tick_x(float(c.t1)) - 1;
        const float y0 = lane_y(c.voice) + 2, y1 = lane_y(c.voice) + lane_h - 2;
        if (x1 < view_x0 || x0 > view_x1) continue;
        const bool selected = c.voice == app.sel_voice && app.roll_sel_t1 > app.roll_sel_t0 && app.roll_sel_t0 == c.t0 && app.roll_sel_t1 == c.t1;
        const ImU32 fill = c.id < 0 ? th.u32(TC_NOTE_TIE, 0.45f) : clip_colour(th, c.id, c.shared ? 0.55f : 0.85f);
        dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), fill, 3.0f);
        if (c.shared) {
            for (float x = x0 + 6; x < x1; x += 8) dl->AddLine(ImVec2(x, y1), ImVec2(std::min(x + 5, x1), y1 - 5), th.u32(TC_PATTERN_BG, 0.6f), 1.0f);
        }
        dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), selected ? th.u32(TC_CURSOR_EDIT) : th.u32(TC_PATTERN_BG, 0.8f), 3.0f, 0, selected ? 2.0f : 1.0f);
        if (x1 - x0 > cw * 2 && c.id >= 0) {
            char lb[32];
            if (c.passes > 1) std::snprintf(lb, sizeof lb, "%s %d/%d", clip_name(c.id).c_str(), c.pass, c.passes);
            else std::snprintf(lb, sizeof lb, "%s", clip_name(c.id).c_str());
            dl->PushClipRect(ImVec2(std::max(x0, view_x0), y0), ImVec2(x1, y1), true);
            dl->AddText(ImVec2(std::max(x0, view_x0) + 3, y0 + (y1 - y0 - line_h) * 0.5f), th.u32(TC_PATTERN_BG), lb);
            dl->PopClipRect();
        }
        if (hovered && m.x >= x0 && m.x <= x1 && m.y >= y0 && m.y <= y1) under = &c;
    }
    for (int v = 0; v < 8; ++v) {
        const seq::Track& t = pat.tracks[v];
        if (!t.addr || t.total_ticks >= length) continue;
        dl->AddRectFilled(ImVec2(tick_x(float(t.total_ticks)), lane_y(v) + 1), ImVec2(tick_x(float(length)), lane_y(v) + lane_h - 1), th.u32(TC_PATTERN_BG, 0.5f));
    }
    if (head_tick >= 0) {
        const float x = tick_x(head_tick);
        dl->AddLine(ImVec2(x, view_y0), ImVec2(x, lane_y(8)), th.u32(TC_PLAYHEAD_LINE), 2.0f);
    }
    dl->PopClipRect();

    dl->PushClipRect(ImVec2(win.x, view_y0), ImVec2(view_x0, view_y1), true);
    for (int v = 0; v < 8; ++v) {
        const float y = lane_y(v);
        dl->AddRectFilled(ImVec2(win.x, y), ImVec2(view_x0, y + lane_h), th.u32(TC_CHANNEL_HEADER_BG));
        char lb[8]; std::snprintf(lb, sizeof lb, "v%d", v + 1);
        dl->AddText(ImVec2(win.x + 4, y + (lane_h - line_h) * 0.5f), th.u32(v == app.sel_voice ? TC_CHANNEL_CURSOR : TC_NOTE), lb);
        if (!pat.tracks[v].addr) dl->AddText(ImVec2(win.x + 4 + cw * 2.5f, y + (lane_h - line_h) * 0.5f), th.u32(TC_ROW_INDEX), "-");
    }
    dl->PopClipRect();
    dl->PushClipRect(ImVec2(win.x, win.y), ImVec2(view_x1, view_y0), true);
    dl->AddRectFilled(ImVec2(win.x, win.y), ImVec2(view_x1, view_y0), th.u32(TC_CHANNEL_HEADER_BG));
    const int lab_step = ruler_label_step(rows, ppt * tpr, cw);
    for (int r = r0; r <= r1; ++r) {
        if (r % lab_step) continue;
        const float x = tick_x(float(r * tpr));
        char b[16]; std::snprintf(b, sizeof b, th.hex_rows ? "%02X" : "%d", r);
        dl->AddText(ImVec2(x + 2, win.y + 2), th.u32(th.row_hi2 > 0 && r % th.row_hi2 == 0 ? TC_ROW_INDEX_HI2 : TC_ROW_INDEX_HI1), b);
    }
    dl->PopClipRect();
    ImGui::PopFont();

    static const Clip* menu_clip = nullptr;
    static Clip menu_copy;
    auto select_clip = [&](const Clip& c) {
        app.sel_voice = c.voice;
        app.roll_sel_t0 = c.t0; app.roll_sel_t1 = c.t1; app.roll_sel_tick = -1;
        app.sel_row = c.t0 / tpr; app.sel_field = App::F_NOTE; app.sel_active = false; app.cur_nibble = 0;
    };
    auto go_roll = [&](const Clip& c) {
        app.seq_tab = 1; app.follow = false;
        app.roll_anchor_tick = float(c.t0); app.roll_anchor_px = key_w + cw * 2;
    };
    auto go_tracker = [&](const Clip& c) { app.seq_tab = 0; app.follow = false; app.sel_row = c.t0 / tpr; };
    auto describe = [&](const Clip& c, char* b, size_t n) {
        std::snprintf(b, n, "v%d %s%s  $%04X-%04X  ticks %d-%d (rows %d-%d)  %d notes%s%s",
                      c.voice + 1, c.id >= 0 ? "part " : "run", c.id >= 0 ? clip_name(c.id).c_str() : "", c.addr0, c.addr1, c.t0, c.t1, c.t0 / tpr, (c.t1 + tpr - 1) / tpr, c.notes,
                      c.passes > 1 ? "" : "  (plays once)", c.shared ? "  shared bytes" : "");
    };
    if (under) {
        char b[160]; describe(*under, b, sizeof b);
        if (ImGui::BeginTooltip()) {
            ImGui::TextUnformatted(b);
            if (under->passes > 1) ImGui::TextDisabled("pass %d of %d: the same bytes play %d times", under->pass, under->passes, under->passes);
            ImGui::EndTooltip();
        }
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { select_clip(*under); app.play_from(app.view_order, under->t0); }
        else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { select_clip(*under); app.status = b; }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) { select_clip(*under); menu_copy = *under; menu_clip = &menu_copy; ImGui::OpenPopup("arrmenu"); }
    } else if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && m.y < view_y0 && m.x >= view_x0) {
        app.play_from(app.view_order, std::clamp(int((m.x - origin.x - key_w) / ppt), 0, length - 1));
    }
    if (ImGui::BeginPopup("arrmenu")) {
        if (menu_clip) {
            const Clip& c = *menu_clip;
            char b[160]; describe(c, b, sizeof b);
            ImGui::TextDisabled("%s", b);
            ImGui::Separator();
            if (ImGui::MenuItem("Play from here")) app.play_from(app.view_order, c.t0);
            if (ImGui::MenuItem("Open in piano roll")) go_roll(c);
            if (ImGui::MenuItem("Open in tracker")) go_tracker(c);
            if (ImGui::MenuItem("Select its range in the roll")) { select_clip(c); go_roll(c); }
            if (c.passes > 1) {
                ImGui::Separator();
                ImGui::TextDisabled("Editing any pass writes that pass out as plain data;");
                ImGui::TextDisabled("the other passes keep playing the shared bytes.");
            }
        }
        ImGui::EndPopup();
    }
    child_end();
    child_end();
}
