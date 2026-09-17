// Effects panel: every command the loaded driver understands, like Furnace's
// effect list. One row per opcode with its hex value, grid code, name and
// argument count, coloured by class; a filter box narrows it; clicking a row
// inserts the command at the cursor (grid) or the selected note (piano roll)
// when edit mode is on.
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

#include "imgui.h"
#include "theme.hpp"
#include "ui.hpp"

namespace {
ThemeColor class_colour(seq::FxClass c) {
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

const char* class_name(seq::FxClass c) {
    switch (c) {
        case seq::FxClass::Instrument: return "instrument";
        case seq::FxClass::Pitch: return "pitch";
        case seq::FxClass::Volume: return "volume";
        case seq::FxClass::Panning: return "panning";
        case seq::FxClass::Song: return "song / flow";
        case seq::FxClass::Time: return "time";
        case seq::FxClass::Speed: return "tempo";
        case seq::FxClass::Sys1: return "echo / DSP";
        case seq::FxClass::Sys2: return "driver";
        case seq::FxClass::Misc: return "misc";
        default: return "unused";
    }
}

bool contains_ci(const char* hay, const char* needle) {
    if (!*needle) return true;
    std::string h = hay, n = needle;
    for (char& c : h) c = char(std::tolower(uint8_t(c)));
    for (char& c : n) c = char(std::tolower(uint8_t(c)));
    return h.find(n) != std::string::npos;
}

}

void draw_effects_panel(App& app) {
    ImGui::SetNextWindowSize(ImVec2(520, 420), ImGuiCond_FirstUseEver);
    if (!panel_begin("Effects", &app.show_effects)) { panel_end(); return; }
    if (!app.engine.loaded() || !app.tracker.drv) { ImGui::TextDisabled("No supported driver loaded."); panel_end(); return; }
    const seq::Driver& D = *app.tracker.drv;
    const Theme& th = theme();

    static char filter[64] = {};
    ImGui::TextDisabled("%s", D.name().c_str());
    ImGui::SetNextItemWidth(-em(1.0f));
    ImGui::InputTextWithHint("##fxfilter", "filter by code, name or class", filter, sizeof filter);
    if (ImGui::IsItemHovered()) tooltip_spaced("Click a row to add that command at the cursor (edit mode on).\nArguments are typed on the effect column or in the event editor.");

    if (ImGui::CollapsingHeader("How commands work")) {
        text_spaced("A command sits on a row of one voice and takes effect from there.\n"
                    "Instrument, volume, pan and the like stay in force until the next command of the same kind; "
                    "vibrato runs until its off command; pitch bends and envelopes shape the note that follows.\n"
                    "Add one from this list, the Event panel or the piano roll's right-click menu, then set its "
                    "arguments on the row or in the Event panel.\n"
                    "Song / flow commands (loops, calls, jumps) are the engine's own structure: the tracker follows "
                    "them, and editing inside a repeated or called section writes that pass out as plain data.");
    }
    if (ImGui::CollapsingHeader("Notes", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::BulletText("notes $%02X..$%02X  (%s .. %s)", D.note_min(), D.note_max(), D.note_name(D.note_min()).c_str(), D.note_name(D.note_max()).c_str());
        ImGui::BulletText("rest / note off: $%02X", D.rest_byte());
        if (D.tie_byte()) ImGui::BulletText("tie: $%02X", D.tie_byte());
        else ImGui::BulletText("no tie byte (durations are re-encoded instead)");
        int perc_lo = -1, perc_hi = -1;
        for (int b = 0; b < 256; ++b) if (D.is_percussion(uint8_t(b))) { if (perc_lo < 0) perc_lo = b; perc_hi = b; }
        if (perc_lo >= 0) ImGui::BulletText("percussion: $%02X..$%02X", perc_lo, perc_hi);
        if (D.has_qv()) ImGui::BulletText("length byte, then quantise / velocity (qv) byte per note");
    }

    const int base = D.first_command(), count = D.command_count();
    if (ImGui::BeginTable("fxlist", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("op");
        ImGui::TableSetupColumn("code");
        ImGui::TableSetupColumn("args");
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (int i = 0; i < count; ++i) {
            const uint8_t op = uint8_t(base + i);
            const int size = D.cmd_size(op);
            const seq::FxClass cls = D.cmd_class(op);
            if (size <= 0 && cls == seq::FxClass::Invalid) continue;
            char hex[8]; std::snprintf(hex, sizeof hex, "$%02X", op);
            if (filter[0] && !contains_ci(hex, filter) && !contains_ci(D.cmd_code(op), filter) && !contains_ci(D.cmd_name(op), filter) && !contains_ci(class_name(cls), filter)) continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(i);
            const ImU32 col = th.u32(class_colour(cls));
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            const bool clicked = ImGui::Selectable(hex, false, ImGuiSelectableFlags_SpanAllColumns);
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) {
                if (size > 0) {
                    const char* where = cls == seq::FxClass::Pitch ? "pitch: shapes the note it precedes, or the sounding one" :
                                        cls == seq::FxClass::Volume || cls == seq::FxClass::Panning ? "stays in force until the next one" :
                                        cls == seq::FxClass::Song ? "song / flow: loops, calls, jumps (addresses)" :
                                        cls == seq::FxClass::Instrument ? "instrument for the notes that follow" :
                                        cls == seq::FxClass::Time || cls == seq::FxClass::Speed ? "timing: applies from this row on" : "applies from this row on";
                    tooltip_spaced("%s\n%s, %d byte%s.\n%s\nClick to add at the cursor.", D.cmd_name(op), class_name(cls), size, size == 1 ? "" : "s", where);
                }
                else tooltip_spaced("%s\nnot usable", D.cmd_name(op));
            }
            if (clicked && size > 0) app.insert_fx_op = op;
            ImGui::TableNextColumn(); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col), "%s", D.cmd_code(op));
            ImGui::TableNextColumn(); if (size > 0) ImGui::Text("%d", size - 1); else ImGui::TextDisabled("-");
            ImGui::TableNextColumn(); ImGui::TextUnformatted(D.cmd_name(op));
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    panel_end();
}
