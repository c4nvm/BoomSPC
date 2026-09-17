#include <cstdio>

#include "imgui.h"
#include "ui.hpp"

namespace {
void bitmask_row(const char* label, uint8_t mask) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn(); ImGui::PushTextWrapPos(-1.0f); ImGui::TextDisabled("%s", label); ImGui::PopTextWrapPos();
    ImGui::TableNextColumn();
    for (int v = 0; v < 8; ++v) {
        if (v) ImGui::SameLine(0, 2);
        ImGui::TextColored(mask & (1 << v) ? ImVec4(0.4f, 1, 0.4f, 1) : ImVec4(0.35f, 0.35f, 0.35f, 1),
                           "%d", v);
    }
    ImGui::SameLine(); ImGui::TextDisabled(" %02X", mask);
}

void label_cell(const char* label) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn(); ImGui::PushTextWrapPos(-1.0f); ImGui::TextDisabled("%s", label); ImGui::PopTextWrapPos();
    ImGui::TableNextColumn();
}

}

void draw_dsp_panel(App& app) {
    ImGui::SetNextWindowSize(ImVec2(400, 360), ImGuiCond_FirstUseEver);
    if (!panel_begin("DSP", &app.show_dsp)) { panel_end(); return; }

    const EngineSnapshot& s = app.snap;
    if (!s.loaded) { ImGui::TextDisabled("No file loaded."); panel_end(); return; }
    const uint8_t* d = s.dsp;

    if (ImGui::BeginTable("globals", 2, ImGuiTableFlags_SizingStretchProp)) {
        label_cell("Main vol"); ImGui::Text("L %4d  R %4d", int8_t(d[SPC_DSP::r_mvoll]), int8_t(d[SPC_DSP::r_mvolr]));
        label_cell("Echo vol"); ImGui::Text("L %4d  R %4d", int8_t(d[SPC_DSP::r_evoll]), int8_t(d[SPC_DSP::r_evolr]));
        label_cell("Echo fb"); ImGui::Text("%4d", int8_t(d[SPC_DSP::r_efb]));

        const uint8_t flg = d[SPC_DSP::r_flg];
        label_cell("FLG"); ImGui::Text("%02X  %s%s%s noise clk %02X", flg,
                  flg & 0x80 ? "RESET " : "", flg & 0x40 ? "MUTE " : "",
                  flg & 0x20 ? "ECHO-WR-OFF " : "", flg & 0x1F);

        const int esa = d[SPC_DSP::r_esa] << 8;
        const int edl = d[SPC_DSP::r_edl] & 0x0F;
        label_cell("Echo buf"); ImGui::Text("ESA %04X  EDL %X  (%d ms, %d bytes)", esa, edl,
                  edl * 16, edl ? edl * 2048 : 4);
        label_cell("Sample dir"); ImGui::Text("%04X", d[SPC_DSP::r_dir] << 8);

        bitmask_row("KON",  d[SPC_DSP::r_kon]);
        bitmask_row("KOFF", d[SPC_DSP::r_koff]);
        bitmask_row("ENDX", d[SPC_DSP::r_endx]);
        bitmask_row("PMON", d[SPC_DSP::r_pmon]);
        bitmask_row("NON",  d[SPC_DSP::r_non]);
        bitmask_row("EON",  d[SPC_DSP::r_eon]);
        ImGui::EndTable();
    }

    ImGui::SeparatorText("Echo FIR");
    {
        float taps[8];
        for (int i = 0; i < 8; ++i) taps[i] = float(int8_t(d[SPC_DSP::r_fir + i * 0x10]));
        ImGui::PlotHistogram("##fir", taps, 8, 0, nullptr, -128, 127, ImVec2(-1, 60));
        ImGui::Text("%4d %4d %4d %4d %4d %4d %4d %4d",
                    int(taps[0]), int(taps[1]), int(taps[2]), int(taps[3]),
                    int(taps[4]), int(taps[5]), int(taps[6]), int(taps[7]));
    }

    if (ImGui::CollapsingHeader("Raw registers")) {
        constexpr ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                          ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollX;
        if (ImGui::BeginTable("raw", 17, flags, ImVec2(0, (ImGui::GetTextLineHeight() + ImGui::GetStyle().CellPadding.y * 2) * 9 + ImGui::GetStyle().ScrollbarSize + 2))) {
            ImGui::PushTextWrapPos(-1.0f);
            ImGui::TableSetupColumn("");
            for (int c = 0; c < 16; ++c) {
                char h[4];
                std::snprintf(h, sizeof h, "%X", c);
                ImGui::TableSetupColumn(h);
            }
            ImGui::TableHeadersRow();
            for (int row = 0; row < 8; ++row) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextDisabled("%X0", row);
                for (int c = 0; c < 16; ++c) {
                    ImGui::TableNextColumn();
                    ImGui::Text("%02X", d[row * 16 + c]);
                }
            }
            ImGui::PopTextWrapPos();
            ImGui::EndTable();
        }
    }

    panel_end();
}
