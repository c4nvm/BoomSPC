#include <algorithm>
#include <cstdio>

#include "actions.hpp"
#include "imgui.h"
#include "ui.hpp"

namespace {
void time_label(double seconds, char* buf, size_t n) {
    int s = int(seconds);
    std::snprintf(buf, n, "%d:%02d.%02d", s / 60, s % 60, int((seconds - s) * 100));
}

void tag_row(const char* label, const std::string& value) {
    if (value.empty()) return;
    ImGui::TableNextRow();
    ImGui::TableNextColumn(); ImGui::PushTextWrapPos(-1.0f); ImGui::TextDisabled("%s", label); ImGui::PopTextWrapPos();
    ImGui::TableNextColumn(); text_wrapped("%s", value.c_str());
}

}

void draw_player_panel(App& app) {
    ImGui::SetNextWindowSize(ImVec2(420, 430), ImGuiCond_FirstUseEver);
    if (!panel_begin("Player", &app.show_player)) { panel_end(); return; }

    Engine& eng = app.engine;

    if (!eng.loaded()) draw_welcome_banner();
    if (ImGui::Button("Open...")) app.dialog_open();
    if (ImGui::IsItemHovered()) tooltip_spaced("Pick an .spc rip, an .snsf / .minisnsf set or a .boomspc project (%s)", action_shortcut(A_OPEN));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-(text_w("Load") + ImGui::GetStyle().ItemSpacing.x));
    if (app.focus_path_box) { ImGui::SetKeyboardFocusHere(); app.focus_path_box = false; }
    bool submit = ImGui::InputText("##path", app.path_buf, sizeof app.path_buf,
                                   ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Load") || submit) app.open_any(app.path_buf);
    text_wrapped("Or drop an .spc / .boomspc / .minisnsf onto the window, or type a path.");
    if (!app.status.empty()) text_wrapped("%s", app.status.c_str());

    ImGui::Separator();

    const bool loaded = eng.loaded();
    ImGui::BeginDisabled(!loaded);
    const float pad = ImGui::GetStyle().FramePadding.x * 2;
    const float play_w = std::max(ImGui::CalcTextSize("Pause").x, ImGui::CalcTextSize("Play").x) + pad;
    if (ImGui::Button(eng.playing() || eng.seeking() ? "Pause" : "Play", ImVec2(play_w, 0))) eng.toggle();
    if (ImGui::IsItemHovered()) tooltip_spaced("Play / pause (%s)", action_shortcut(A_PLAY_TOGGLE));
    same_line_if_fits("Restart");
    if (ImGui::Button("Restart")) eng.restart();
    if (ImGui::IsItemHovered()) tooltip_spaced("Back to the start of the rip and play (%s)", action_shortcut(A_PLAY_START));
    same_line_if_fits(ImGui::CalcTextSize("0:00.00 / 0:00.00").x);
    {
        char pos[32], tot[32];
        time_label(eng.position_seconds(), pos, sizeof pos);
        int total_ms = loaded ? eng.file().total_ms() : 0;
        if (total_ms > 0) {
            time_label(total_ms / 1000.0, tot, sizeof tot);
            ImGui::Text("%s / %s", pos, tot);
            float frac = float(eng.position_seconds() * 1000.0 / total_ms);
            ImGui::ProgressBar(frac, ImVec2(-1, 4), "");
        } else {
            ImGui::Text("%s", pos);
        }
    }
    ImGui::EndDisabled();

    if (loaded && eng.snsf()) {
        text_wrapped("SNSF set: the SNES CPU runs this game's sound engine and drives the SPC through the APU ports "
                     "(emulated here); there is no SPC image, so no export and no sequence view yet.");
    }
    if (loaded) {
        const SpcFile& f = eng.file();
        if (ImGui::BeginTable("tags", 2, ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
            tag_row("Title",    f.title);
            tag_row("Game",     f.game);
            tag_row("Artist",   f.artist);
            tag_row("OST",      f.ost);
            tag_row("Publisher",f.publisher);
            tag_row("Dumper",   f.dumper);
            tag_row("Date",     f.date);
            tag_row("Comment",  f.comments);
            ImGui::EndTable();
        }
        if (!f.has_id666) ImGui::TextDisabled("(no ID666 tag)");
    }

    ImGui::Separator();
    ImGui::SeparatorText("Output");

    if (double bpm = app.song_bpm(); bpm > 0) {
        static double edit_bpm = 0; static bool editing = false;
        if (!editing) edit_bpm = bpm;
        ImGui::SetNextItemWidth(input_int_w(5));
        if (ImGui::InputDouble("Song BPM", &edit_bpm, 1.0, 10.0, "%.1f", ImGuiInputTextFlags_EnterReturnsTrue)) { app.set_song_bpm(edit_bpm); editing = false; }
        else editing = ImGui::IsItemActive();
        if (ImGui::IsItemHovered())
            tooltip_spaced("The driver's live tempo (%d ticks per beat). Enter writes it into the driver, so\nthe song's own tempo commands take over again when they next run (usually at the loop).\nAt speed %d/256 you hear %.1f BPM.", app.ticks_per_beat, eng.tempo(), bpm * eng.tempo() / 256.0);
    } else if (eng.loaded()) ImGui::TextDisabled("Song BPM: unknown for this driver");
    int tempo = eng.tempo();
    static int tempo_pre = 0x100;
    const int tempo_before = tempo;
    if (ImGui::SliderInt("Speed", &tempo, 0x40, 0x400, "%d/256")) eng.set_tempo(tempo);
    if (ImGui::IsItemHovered()) tooltip_spaced("Emulator clock: pitch stays, everything runs faster or slower.\nDouble-click to type a value, right-click resets to 256.");
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) eng.set_tempo(0x100);
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) tempo_pre = tempo_before;
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { eng.set_tempo(tempo_pre); ImGui::OpenPopup("speedpop"); }
    const double song = app.song_bpm();
    if (song > 0) ImGui::Text("Actual BPM: %.1f", song * eng.tempo() / 256.0);
    else ImGui::TextDisabled("Actual BPM: unknown (x%.3f)", eng.tempo() / 256.0);
    if (ImGui::IsItemHovered()) tooltip_spaced("Song BPM x speed / 256.");
    if (ImGui::BeginPopup("speedpop")) {
        static int edit_speed = 0; static double edit_actual = 0;
        if (ImGui::IsWindowAppearing()) { edit_speed = eng.tempo(); edit_actual = song > 0 ? song * eng.tempo() / 256.0 : 0; }
        ImGui::TextDisabled("Speed");
        ImGui::SetNextItemWidth(input_int_w(5));
        if (ImGui::InputInt("/256", &edit_speed, 1, 16, ImGuiInputTextFlags_EnterReturnsTrue)) { eng.set_tempo(std::clamp(edit_speed, 0x40, 0x400)); ImGui::CloseCurrentPopup(); }
        if (song > 0) {
            ImGui::SetNextItemWidth(input_int_w(6));
            if (ImGui::InputDouble("actual BPM", &edit_actual, 1.0, 10.0, "%.1f", ImGuiInputTextFlags_EnterReturnsTrue)) {
                if (edit_actual > 0) eng.set_tempo(std::clamp(int(edit_actual / song * 256.0 + 0.5), 0x40, 0x400));
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered()) tooltip_spaced("Sets the speed so the song plays at this tempo (song BPM %.1f at 256).", song);
        }
        if (ImGui::Button("256 (normal)")) { eng.set_tempo(0x100); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }

    bool filt = eng.filter_enabled();
    int  gain = eng.gain();
    int  bass = eng.bass();
    bool changed = false;
    changed |= ImGui::Checkbox("SNES output filter", &filt);
    changed |= ImGui::SliderInt("Gain", &gain, 0, 0x400, "%d/256");
    changed |= ImGui::SliderInt("Bass", &bass, SPC_Filter::bass_none, SPC_Filter::bass_max);
    if (changed) eng.set_filter(filt, gain, bass);

    bool loop = eng.loop();
    if (ImGui::Checkbox("Loop at tagged length", &loop)) eng.set_loop(loop);
    bool clr = eng.clear_echo_on_load();
    if (ImGui::Checkbox("Clear echo buffer on load", &clr)) eng.set_clear_echo_on_load(clr);

    if (loaded) {
        ImGui::SeparatorText("SPC700");
        const auto& c = app.snap.cpu;
        ImGui::Text("PC %04X  A %02X  X %02X  Y %02X  SP %02X  PSW %02X",
                    c.pc, c.a, c.x, c.y, c.sp, c.psw);
        ImGui::Text("%c%c%c%c%c%c%c%c",
                    c.psw & 0x80 ? 'N' : 'n', c.psw & 0x40 ? 'V' : 'v',
                    c.psw & 0x20 ? 'P' : 'p', c.psw & 0x10 ? 'B' : 'b',
                    c.psw & 0x08 ? 'H' : 'h', c.psw & 0x04 ? 'I' : 'i',
                    c.psw & 0x02 ? 'Z' : 'z', c.psw & 0x01 ? 'C' : 'c');
        std::string err = eng.last_cpu_error();
        if (!err.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "CPU error: %s", err.c_str());
    }

    panel_end();
}
