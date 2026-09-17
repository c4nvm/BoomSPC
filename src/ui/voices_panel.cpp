#include <cmath>
#include <cstdio>

#include "imgui.h"
#include "ui.hpp"

namespace {
inline uint8_t vreg(const EngineSnapshot& s, int v, int r) { return s.dsp[v * 0x10 + r]; }

const char* gain_mode_name(uint8_t gain) {
    if (!(gain & 0x80)) return "direct";
    switch ((gain >> 5) & 3) {
        case 0: return "lin dec";
        case 1: return "exp dec";
        case 2: return "lin inc";
        default: return "bent inc";
    }
}

}

void draw_voices_panel(App& app) {
    ImGui::SetNextWindowSize(ImVec2(1290, 270), ImGuiCond_FirstUseEver);
    if (!panel_begin("Voices", &app.show_voices)) { panel_end(); return; }

    const EngineSnapshot& s = app.snap;
    Engine& eng = app.engine;
    int mask = eng.mute_mask();

    if (ImGui::SmallButton("Unmute all")) eng.set_mute_mask(0);
    same_line_if_fits("Mute all");
    if (ImGui::SmallButton("Mute all")) eng.set_mute_mask(0xFF);
    same_line_if_fits("F1-F8 toggle, click # to solo");
    ImGui::TextDisabled("F1-F8 toggle, click # to solo");

    constexpr ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX;
    if (!ImGui::BeginTable("voices", 12, flags)) { panel_end(); return; }
    ImGui::PushTextWrapPos(-1.0f);

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("#");
    ImGui::TableSetupColumn("On");
    ImGui::TableSetupColumn("VolL");
    ImGui::TableSetupColumn("VolR");
    ImGui::TableSetupColumn("Pitch");
    ImGui::TableSetupColumn("Rate");
    ImGui::TableSetupColumn("Src");
    ImGui::TableSetupColumn("Envelope");
    ImGui::TableSetupColumn("ENVX", ImGuiTableColumnFlags_WidthFixed, 110);
    ImGui::TableSetupColumn("OUTX");
    ImGui::TableSetupColumn("Flags");
    ImGui::TableSetupColumn("BRR", ImGuiTableColumnFlags_WidthFixed, 110);
    ImGui::TableHeadersRow();

    const uint8_t pmon = s.dsp[SPC_DSP::r_pmon];
    const uint8_t non  = s.dsp[SPC_DSP::r_non];
    const uint8_t eon  = s.dsp[SPC_DSP::r_eon];
    const uint8_t endx = s.dsp[SPC_DSP::r_endx];
    const int dir_base = s.dsp[SPC_DSP::r_dir] << 8;

    for (int v = 0; v < 8; ++v) {
        ImGui::PushID(v);
        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        if (ImGui::Selectable(std::to_string(v).c_str(), false, ImGuiSelectableFlags_None, ImVec2(em(1.0f), 0)))
            eng.set_mute_mask(mask == (0xFF & ~(1 << v)) ? 0 : (0xFF & ~(1 << v)));

        ImGui::TableNextColumn();
        bool on = !(mask & (1 << v));
        if (ImGui::Checkbox("##on", &on)) eng.set_mute_mask(on ? mask & ~(1 << v) : mask | (1 << v));

        if (!s.loaded) { ImGui::PopID(); continue; }

        const int8_t voll = int8_t(vreg(s, v, SPC_DSP::v_voll));
        const int8_t volr = int8_t(vreg(s, v, SPC_DSP::v_volr));
        ImGui::TableNextColumn(); ImGui::Text("%4d", voll);
        ImGui::TableNextColumn(); ImGui::Text("%4d", volr);

        const int pitch = (vreg(s, v, SPC_DSP::v_pitchl) | (vreg(s, v, SPC_DSP::v_pitchh) << 8)) & 0x3FFF;
        ImGui::TableNextColumn(); ImGui::Text("%04X", pitch);
        ImGui::TableNextColumn(); ImGui::Text("%6.0f Hz", 32000.0 * pitch / 0x1000);

        const uint8_t srcn = vreg(s, v, SPC_DSP::v_srcn);
        ImGui::TableNextColumn(); ImGui::Text("%02X", srcn);

        const uint8_t adsr0 = vreg(s, v, SPC_DSP::v_adsr0);
        const uint8_t adsr1 = vreg(s, v, SPC_DSP::v_adsr1);
        const uint8_t gain  = vreg(s, v, SPC_DSP::v_gain);
        ImGui::TableNextColumn();
        if (adsr0 & 0x80) {
            ImGui::Text("ADSR A%X D%X S%X R%02X",
                        adsr0 & 0x0F, (adsr0 >> 4) & 7, (adsr1 >> 5) & 7, adsr1 & 0x1F);
        } else if (gain & 0x80) {
            ImGui::Text("GAIN %s %02X", gain_mode_name(gain), gain & 0x1F);
        } else {
            ImGui::Text("GAIN direct %3d", gain & 0x7F);
        }

        const uint8_t envx = vreg(s, v, SPC_DSP::v_envx) & 0x7F;
        ImGui::TableNextColumn();
        {
            char lbl[8];
            std::snprintf(lbl, sizeof lbl, "%3d", envx);
            ImGui::ProgressBar(envx / 127.0f, ImVec2(-1, 0), lbl);
        }

        const int8_t outx = int8_t(vreg(s, v, SPC_DSP::v_outx));
        ImGui::TableNextColumn(); ImGui::Text("%4d", outx);

        ImGui::TableNextColumn();
        ImGui::Text("%c%c%c%c",
                    pmon & (1 << v) ? 'P' : '.',
                    non  & (1 << v) ? 'N' : '.',
                    eon  & (1 << v) ? 'E' : '.',
                    endx & (1 << v) ? 'X' : '.');
        if (ImGui::IsItemHovered())
            tooltip_spaced("P pitch modulation\nN noise\nE echo\nX reached BRR end block");

        ImGui::TableNextColumn();
        {
            const int e = (dir_base + srcn * 4) & 0xFFFF;
            const uint8_t* r = s.ram;
            int start = r[e] | (r[(e + 1) & 0xFFFF] << 8);
            int loop  = r[(e + 2) & 0xFFFF] | (r[(e + 3) & 0xFFFF] << 8);
            ImGui::Text("%04X/%04X", start, loop);
            if (ImGui::IsItemClicked()) { app.mem_goto = start; app.show_memory = true; }
            if (ImGui::IsItemHovered()) tooltip_spaced("start / loop address. Click to view in memory.");
        }

        ImGui::PopID();
    }

    ImGui::PopTextWrapPos();
    ImGui::EndTable();
    panel_end();
}
