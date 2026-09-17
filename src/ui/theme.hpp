// Colour roles for the tracker views, in the spirit of Furnace's pattern
// colouring: every field of a cell gets its own colour so a pattern can be
// read at a glance. Editable in the Colors window and saved next to imgui.ini.
#pragma once

#include <cstdarg>
#include <cstdio>
#include <string>

#include "imgui.h"

enum ThemeColor {
    TC_PATTERN_BG,
    TC_ROW_INDEX, TC_ROW_INDEX_HI1, TC_ROW_INDEX_HI2, TC_ROW_INDEX_PLAYING,
    TC_ROW_HI1, TC_ROW_HI2, TC_ROW_CURSOR,
    TC_PLAYHEAD, TC_PLAYHEAD_LINE, TC_CURSOR, TC_CURSOR_EDIT, TC_SELECTION,
    TC_NOTE, TC_BLANK, TC_NOTE_OFF, TC_NOTE_TIE, TC_NOTE_PERC, TC_NOTE_SUB,
    TC_INS, TC_INS_INVALID,
    TC_QUANT, TC_VOL_MAX, TC_VOL_HALF, TC_VOL_MIN,
    TC_FX_INVALID, TC_FX_PITCH, TC_FX_VOLUME, TC_FX_PANNING, TC_FX_SONG, TC_FX_TIME,
    TC_FX_SPEED, TC_FX_SYS1, TC_FX_SYS2, TC_FX_MISC,
    TC_OFFGRID, TC_EXTRA,
    TC_CHANNEL_HEADER, TC_CHANNEL_HEADER_BG, TC_CHANNEL_MUTED, TC_CHANNEL_SOLO, TC_CHANNEL_CURSOR,
    TC_ORDER_INDEX, TC_ORDER_PLAYING, TC_ORDER_SELECTED, TC_ORDER_SIMILAR,
    TC_METER_LOW, TC_METER_HIGH,
    TC_STATUS_EDIT, TC_STATUS_PLAY,
    TC_COUNT
};

struct Theme {
    ImVec4 colors[TC_COUNT];
    const char* names[TC_COUNT];

    int  row_hi1 = 2;         // highlight every N rows
    int  row_hi2 = 8;
    bool hex_rows = false;
    int  follow_mode = 1;     // 0 = smooth scroll against a fixed line, 1 = jump, keep centred, 2 = page at the edges
    float playhead_pos = 0.5f;// where the line sits in the view (fraction of the height)
    int  latency_ms = 50;     // audio buffering: the emulator runs this far ahead of what you hear
    int  edit_step = 1;
    bool ins_colors = false;  // colour instrument numbers by a per-number hue
    bool fx_hex_codes = false;// show command opcodes as hex instead of 3-letter codes
    bool dim_muted = true;    // fade the contents of muted channels
    bool show_meters = true;  // level meters in the channel headers
    bool cursor_row_tint = true;
    bool wrap_cursor = true;  // Up at row 0 wraps to the last row
    bool step_on_hex = true;  // advance by the edit step after a complete hex entry
    bool note_writes_ins = true; // a typed note also writes the instrument selected in the Instruments panel

    char  last_dir[512] = {};   // where the file dialogs start

    char  font_ui[256] = "";                   // empty = system font (Auto); bundled pixel fonts by file name
    char  font_mono[256] = "";
    float font_size_ui = 22.0f;
    float font_tracking = 0.0f;                // extra pixels between glyphs (negative tightens wide pixel fonts)
    float font_size_pattern = 9.0f;
    float widget_gap = 1.0f;

    Theme();
    void reset_default();
    void preset(int which);
    ImU32 u32(ThemeColor c) const { return ImGui::ColorConvertFloat4ToU32(colors[c]); }
    ImU32 u32(ThemeColor c, float alpha_mul) const;
    ImU32 volume(float t) const;
    ImU32 meter(float t) const;
    ImU32 instrument(int n) const;

    bool load(const char* path);
    bool save(const char* path) const;

    void apply_widget_colors() const;
};

Theme& theme();
inline float em(float k) { return ImGui::GetFontSize() * k; }
inline void same_line_if_fits(float width) {
    float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    float x = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x;   // where SameLine() would put the next item
    if (x + width <= right) ImGui::SameLine();
}
inline float text_w(const char* s) { return ImGui::CalcTextSize(s).x + ImGui::GetStyle().FramePadding.x * 2; }
inline void text_spaced_width(const char* text, float width) {
    if (width <= 0) width = ImGui::GetContentRegionAvail().x;
    const float gap = ImGui::GetStyle().ItemSpacing.y * 0.5f;
    const float x0 = ImGui::GetCursorPosX();
    std::string line, word;
    bool first = true;
    auto flush = [&]() {
        if (!first) ImGui::SetCursorPosX(x0);
        ImGui::TextUnformatted(line.c_str());
        ImGui::Dummy(ImVec2(0, gap));
        line.clear(); first = false;
    };
    for (const char* p = text;; ++p) {
        const char c = *p;
        if (c == ' ' || c == '\n' || c == 0) {
            if (!word.empty()) {
                std::string candidate = line.empty() ? word : line + " " + word;
                if (!line.empty() && ImGui::CalcTextSize(candidate.c_str()).x > width) { flush(); line = word; }
                else line = candidate;
                word.clear();
            }
            if (c == '\n' || c == 0) { if (!line.empty() || c == '\n') flush(); }
            if (c == 0) break;
        } else word += c;
    }
}
inline void text_spaced(const char* text) { text_spaced_width(text, 0); }
inline void text_wrapped(const char* fmt, ...) {
    char buf[4096];
    va_list args; va_start(args, fmt); std::vsnprintf(buf, sizeof buf, fmt, args); va_end(args);
    text_spaced_width(buf, 0);
}
inline void tooltip_spaced(const char* fmt, ...) {
    char buf[4096];
    va_list args; va_start(args, fmt); std::vsnprintf(buf, sizeof buf, fmt, args); va_end(args);
    if (ImGui::BeginTooltip()) { text_spaced_width(buf, 1e9f); ImGui::EndTooltip(); }
}
inline void same_line_if_fits(const char* label) { same_line_if_fits(text_w(label)); }
inline bool panel_begin(const char* name, bool* open = nullptr, ImGuiWindowFlags flags = 0) {
    bool r = ImGui::Begin(name, open, flags);
    ImGui::PushTextWrapPos(0.0f);
    return r;
}
inline void panel_end() { ImGui::PopTextWrapPos(); ImGui::End(); }
inline bool child_begin(const char* id, ImVec2 size = ImVec2(0, 0), ImGuiChildFlags cf = 0, ImGuiWindowFlags wf = 0, bool wrap = true) {
    bool r = ImGui::BeginChild(id, size, cf, wf | (wrap ? 0 : ImGuiWindowFlags_HorizontalScrollbar));
    ImGui::PushTextWrapPos(wrap ? 0.0f : -1.0f);
    return r;
}
inline void child_end() { ImGui::PopTextWrapPos(); ImGui::EndChild(); }
inline bool list_detail_split(const char* widest, float min_detail_em = 14.0f) {
    const ImGuiStyle& st = ImGui::GetStyle();
    const float list_w = ImGui::CalcTextSize(widest).x + st.WindowPadding.x * 2 + st.ScrollbarSize + em(0.5f);
    const bool stacked = ImGui::GetContentRegionAvail().x < list_w + em(min_detail_em);
    child_begin("list", ImVec2(stacked ? 0 : list_w, stacked ? ImGui::GetContentRegionAvail().y * 0.4f : 0), ImGuiChildFlags_Borders, 0, false);
    return stacked;
}
inline void list_detail_split_next(bool stacked) { child_end(); if (!stacked) ImGui::SameLine(); }
inline float input_int_w(int digits) {
    const ImGuiStyle& st = ImGui::GetStyle();
    return ImGui::CalcTextSize("0").x * float(digits) + st.FramePadding.x * 2 + 2 * (ImGui::GetFrameHeight() + st.ItemInnerSpacing.x);
}
void draw_settings_window(bool* open);
void draw_shortcuts_window(bool* open);
bool settings_capturing_key();
