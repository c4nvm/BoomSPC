#include <cctype>
#include <cstdio>

#include "imgui.h"
#include "ui.hpp"

void draw_memory_panel(App& app) {
    ImGui::SetNextWindowSize(ImVec2(620, 360), ImGuiCond_FirstUseEver);
    if (!panel_begin("Memory", &app.show_memory)) { panel_end(); return; }

    static int goto_addr = 0;
    ImGui::SetNextItemWidth(em(6.0f));
    bool go = ImGui::InputInt("##goto", &goto_addr, 0, 0,
                              ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Go") || go) app.mem_goto = goto_addr & 0xFFFF;
    same_line_if_fits("hex address");
    ImGui::TextDisabled("hex address");

    const EngineSnapshot& s = app.snap;
    if (!s.loaded) { ImGui::TextDisabled("No file loaded."); panel_end(); return; }

    child_begin("hex", ImVec2(0, 0), ImGuiChildFlags_None, 0, false);

    const float row_h = ImGui::GetTextLineHeightWithSpacing();
    if (app.mem_goto >= 0) {
        ImGui::SetScrollY(float(app.mem_goto / 16) * row_h);
        app.mem_goto = -1;
    }

    ImGuiListClipper clipper;
    clipper.Begin(0x10000 / 16, row_h);
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const int base = row * 16;
            char line[128];
            int n = std::snprintf(line, sizeof line, "%04X  ", base);
            for (int i = 0; i < 16; ++i)
                n += std::snprintf(line + n, sizeof line - n, "%02X%s", s.ram[base + i], i == 7 ? "  " : " ");
            n += std::snprintf(line + n, sizeof line - n, " ");
            for (int i = 0; i < 16; ++i) {
                uint8_t c = s.ram[base + i];
                line[n++] = std::isprint(c) ? char(c) : '.';
            }
            line[n] = 0;

            const bool has_pc = s.cpu.pc >= base && s.cpu.pc < base + 16;
            if (has_pc) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.85f, 0.4f, 1));
            ImGui::TextUnformatted(line);
            if (has_pc) ImGui::PopStyleColor();
        }
    }

    child_end();
    panel_end();
}
