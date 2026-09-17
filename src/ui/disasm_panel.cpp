// Live SPC700 disassembly of ARAM, following PC or pinned to an address.
#include <cstdio>
#include <vector>

#include "imgui.h"
#include "spc700.hpp"
#include "ui.hpp"

void draw_disasm_panel(App& app) {
    ImGui::SetNextWindowSize(ImVec2(520, 420), ImGuiCond_FirstUseEver);
    if (!panel_begin("Disassembly", &app.show_disasm)) { panel_end(); return; }
    if (!app.engine.loaded()) { ImGui::TextDisabled("No file loaded."); panel_end(); return; }

    static bool follow_pc = true;
    static int  pinned = 0x500;
    ImGui::Checkbox("Follow PC", &follow_pc);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(em(5.3f));
    if (ImGui::InputInt("address", &pinned, 0, 0, ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue)) follow_pc = false;
    pinned &= 0xFFFF;
    ImGui::SameLine();
    if (ImGui::SmallButton("Pause") && app.engine.playing()) app.engine.pause();
    same_line_if_fits("(pause to read a stable listing)");
    ImGui::TextDisabled("(pause to read a stable listing)");

    const EngineSnapshot& s = app.snap;
    const int pc = s.cpu.pc;
    int anchor = follow_pc ? pc : pinned;
    uint16_t a = uint16_t(anchor - 24);
    std::vector<spc700::Insn> lines;
    while (lines.size() < 60) {
        spc700::Insn in = spc700::disassemble(s.ram, a);
        lines.push_back(in);
        a = uint16_t(a + in.len);
    }

    child_begin("code", ImVec2(0, 0), ImGuiChildFlags_Borders, 0, false);
    for (const spc700::Insn& in : lines) {
        const bool is_pc = in.addr == pc;
        if (is_pc) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.85f, 0.4f, 1));
        char bytes[16] = {0};
        for (int k = 0; k < in.len; ++k) std::snprintf(bytes + k * 3, sizeof bytes - size_t(k) * 3, "%02X ", in.bytes[k]);
        ImGui::Text("%c %04X  %-9s %s", is_pc ? '>' : ' ', in.addr, bytes, in.text.c_str());
        if (is_pc) ImGui::PopStyleColor();
        if (in.target >= 0 && ImGui::IsItemHovered()) {
            tooltip_spaced("-> $%04X  (click to view)", in.target);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { pinned = in.target; follow_pc = false; }
        }
    }
    child_end();
    panel_end();
}
