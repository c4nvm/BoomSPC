// N-SPC instrument table editor. Edits write straight to ARAM; the driver
// picks them up the next time a track selects the instrument.
#include <algorithm>
#include <cstdio>

#include "driver/follin.hpp"
#include "imgui.h"
#include "theme.hpp"
#include "ui.hpp"

namespace {
bool g_ins_preview = false;
double g_ins_preview_t0 = 0;
constexpr double kPreviewMinSeconds = 1.0;   // a click sounds the note for at least this long; holding sustains it
void preview_instrument_regs(App& app, const uint8_t regs[8]) {
    app.engine.preview_on(preview_voice(app), regs);
    g_ins_preview = true;
    g_ins_preview_t0 = ImGui::GetTime();
}
void preview_instrument(App& app, int number) {
    const seq::Driver& D = *app.tracker.drv;
    uint8_t regs[8];
    if (D.preview_regs(app.snap.ram, D.note_byte(app.octave * 12), number, regs)) preview_instrument_regs(app, regs);
}
void preview_release_check(App& app) {
    if (g_ins_preview && !ImGui::IsMouseDown(ImGuiMouseButton_Left) && ImGui::GetTime() - g_ins_preview_t0 >= kPreviewMinSeconds) { app.engine.preview_off(); g_ins_preview = false; }
}
bool shift_clicked() {
    return ImGui::IsItemClicked(ImGuiMouseButton_Left) && (ImGui::GetIO().KeyShift || ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift));
}

void adsr_decode(uint8_t a0, uint8_t a1, int& attack, int& decay, int& sustain_lvl, int& sustain_rate, bool& enabled) {
    enabled = a0 & 0x80;
    attack = a0 & 0x0F;
    decay = (a0 >> 4) & 7;
    sustain_lvl = (a1 >> 5) & 7;
    sustain_rate = a1 & 0x1F;
}

bool instrument_editor(App& app, uint16_t addr, int stride, bool percussion) {
    const uint8_t* e = app.snap.ram + addr;
    uint8_t bytes[6];
    for (int i = 0; i < 6; ++i) bytes[i] = e[i];
    bool changed = false;

    int srcn = bytes[0];
    ImGui::SetNextItemWidth(input_int_w(3));
    // Past $7F ends the table scan, which would empty the list under the cursor.
    if (ImGui::InputInt("Sample (SRCN)", &srcn)) { bytes[0] = uint8_t(std::clamp(srcn, 0, 0x7F)); changed = true; }
    same_line_if_fits("view");
    if (ImGui::SmallButton("view")) { app.sel_sample = bytes[0]; app.show_samples = true; }

    int attack, decay, slvl, srate; bool adsr;
    adsr_decode(bytes[1], bytes[2], attack, decay, slvl, srate, adsr);
    ImGui::PushItemWidth(-std::max(em(6.0f), ImGui::CalcTextSize("Pitch mult (frac)").x + em(0.5f)));
    if (ImGui::Checkbox("ADSR", &adsr)) { bytes[1] = uint8_t((bytes[1] & 0x7F) | (adsr ? 0x80 : 0)); changed = true; }
    ImGui::BeginDisabled(!adsr);
    if (ImGui::SliderInt("Attack", &attack, 0, 15)) { bytes[1] = uint8_t((bytes[1] & 0xF0) | attack); changed = true; }
    if (ImGui::SliderInt("Decay", &decay, 0, 7)) { bytes[1] = uint8_t((bytes[1] & 0x8F) | (decay << 4)); changed = true; }
    if (ImGui::SliderInt("Sustain level", &slvl, 0, 7)) { bytes[2] = uint8_t((bytes[2] & 0x1F) | (slvl << 5)); changed = true; }
    if (ImGui::SliderInt("Sustain rate", &srate, 0, 31)) { bytes[2] = uint8_t((bytes[2] & 0xE0) | srate); changed = true; }
    ImGui::EndDisabled();

    int gain = bytes[3];
    ImGui::BeginDisabled(adsr);
    if (ImGui::SliderInt("GAIN", &gain, 0, 255, "%02X")) { bytes[3] = uint8_t(gain); changed = true; }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) tooltip_spaced("Used when ADSR is off. <0x80 = direct level, >=0x80 = mode in bits 5-6, rate in bits 0-4.");

    int mult = bytes[4];
    if (ImGui::SliderInt(percussion ? "Pitch mult" : "Pitch mult (int)", &mult, 0, 255, "%02X")) { bytes[4] = uint8_t(mult); changed = true; }
    if (stride >= 6) {
        int frac = bytes[5];
        if (percussion) {
            if (ImGui::SliderInt("Note", &frac, 0x80, 0xC5, seq::note_name(frac - 0x80 + 12).c_str())) { bytes[5] = uint8_t(frac); changed = true; }
        } else if (ImGui::SliderInt("Pitch mult (frac)", &frac, 0, 255, "%02X")) { bytes[5] = uint8_t(frac); changed = true; }
    }
    ImGui::PopItemWidth();
    ImGui::TextDisabled("raw @%04X: %02X %02X %02X %02X %02X%s", addr, bytes[0], bytes[1], bytes[2], bytes[3], bytes[4],
                        stride >= 6 ? (std::string(" ") + [&]{ char b[4]; std::snprintf(b, sizeof b, "%02X", bytes[5]); return std::string(b); }()).c_str() : "");

    if (changed) {
        app.engine.write_ram(addr, bytes, size_t(stride));
        app.after_edit();
    }
    return changed;
}

// A "None" row: with it selected, entering a note leaves whatever instrument
// the track already had in force (issue #2).
void instrument_none_row(App& app) {
    if (ImGui::Selectable("--  none", app.sel_instrument < 0)) app.sel_instrument = -1;
    if (ImGui::IsItemHovered()) tooltip_spaced("Notes you enter keep the instrument already in force");
}

void draw_generic_instruments(App& app) {
    const seq::Driver& D = *app.tracker.drv;
    const Theme& th = theme();
    if (!D.has_instruments()) { ImGui::TextDisabled("This driver has no instrument table."); return; }
    const int count = D.instrument_count(app.snap.ram);
    const bool stacked = list_detail_split("00  smp 00");
    instrument_none_row(app);
    for (int i = 0; i < count; ++i) {
        if (!D.instrument_used(app.snap.ram, i)) continue;
        seq::Instrument in = D.read_instrument(app.snap.ram, i);
        char b[48];
        std::snprintf(b, sizeof b, "%02X  smp %02X", D.instrument_number(app.snap.ram, i), in.srcn);
        ImGui::PushStyleColor(ImGuiCol_Text, th.instrument(D.instrument_number(app.snap.ram, i)));
        if (ImGui::Selectable(b, app.sel_instrument == i)) app.sel_instrument = i;
        ImGui::PopStyleColor();
        if (shift_clicked()) preview_instrument(app, D.instrument_number(app.snap.ram, i));
        if (ImGui::IsItemHovered()) tooltip_spaced("Shift-click to hear it");
    }
    preview_release_check(app);
    list_detail_split_next(stacked);
    child_begin("edit");
    int i = app.sel_instrument;
    if (i < 0) text_wrapped("No instrument selected: notes you enter keep the one already in force.");
    if (i >= 0 && i < count) {
        seq::Instrument in = D.read_instrument(app.snap.ram, i);
        text_wrapped("Instrument %02X", D.instrument_number(app.snap.ram, i));
        text_wrapped("Sample (SRCN): %02X", in.srcn);
        if (in.adsr0 & 0x80) text_wrapped("ADSR: %02X %02X", in.adsr0, in.adsr1);
        if (in.pitch_hi || in.pitch_lo) text_wrapped("Tuning: %02X.%02X", in.pitch_hi, in.pitch_lo);
        if (ImGui::SmallButton("view sample")) { app.sel_sample = in.srcn; app.show_samples = true; }
        if (const auto* F = dynamic_cast<const follin::FollinDriver*>(&D)) {
            int tr = in.transpose, tune = in.pitch_hi;
            ImGui::SetNextItemWidth(em(10.7f));
            if (ImGui::SliderInt("Transpose (semitones)", &tr, -24, 24)) {
                uint8_t v = uint8_t(int8_t(tr));
                app.engine.write_ram(uint16_t(F->L.transpose_table + i), &v, 1);
                app.after_edit();
            }
            ImGui::SetNextItemWidth(em(10.7f));
            if (ImGui::SliderInt("Fine tune (x/256 up)", &tune, 0, 255)) {
                uint8_t v = uint8_t(tune);
                app.engine.write_ram(uint16_t(F->L.mult_table + i), &v, 1);
                app.after_edit();
            }
            ImGui::TextDisabled("Envelopes are per note (commands 8C / 97 / A2), GAIN-based; no ADSR.");
        }
    }
    child_end();
}

}

void draw_instruments_panel(App& app) {
    ImGui::SetNextWindowSize(ImVec2(420, 380), ImGuiCond_FirstUseEver);
    if (!panel_begin("Instruments", &app.show_instruments)) { panel_end(); return; }

    const nspc::Layout* NL = app.tracker.nspc_layout();
    if (!app.engine.loaded() || !app.tracker.drv) { ImGui::TextDisabled("No supported driver loaded."); panel_end(); return; }
    if (!NL) { draw_generic_instruments(app); panel_end(); return; }
    const nspc::Layout& L = *NL;
    if (!L.inst_table) {
        text_wrapped("Instrument table not located in this driver. It is usually found once the song has played for a moment; try Rescan in the Sequencer.");
        panel_end();
        return;
    }

    const int count = nspc::instrument_count(app.snap.ram, L);
    int perc_count = 0;
    if (L.perc_table) {
        while (perc_count < 32) {
            const uint8_t* e = app.snap.ram + ((L.perc_table + perc_count * 6) & 0xFFFF);
            if (e[0] > 0x7F || e[5] < 0x80) break;
            ++perc_count;
        }
    }
    const int total = count + L.inst2_count + perc_count;

    const Theme& th = theme();
    const bool stacked = list_detail_split("00  smp 00");
    instrument_none_row(app);
    for (int i = 0; i < total; ++i) {
        char b[48];
        uint16_t addr; int number = i;
        if (i < count) { addr = uint16_t(L.inst_table + i * L.inst_stride); std::snprintf(b, sizeof b, "%02X  smp %02X", i, app.snap.ram[addr]); }
        else if (i < count + L.inst2_count) { int k = i - count; addr = uint16_t(L.inst_table2 + k * 6); number = L.inst2_first + k; std::snprintf(b, sizeof b, "%02X* smp %02X", number, app.snap.ram[addr]); }
        else { int k = i - count - L.inst2_count; addr = uint16_t(L.perc_table + k * 6); number = -1; std::snprintf(b, sizeof b, "P%02d smp %02X", k, app.snap.ram[addr]); }
        ImGui::PushStyleColor(ImGuiCol_Text, number >= 0 ? th.instrument(number) : th.u32(TC_NOTE_PERC));
        if (ImGui::Selectable(b, app.sel_instrument == i)) app.sel_instrument = i;
        ImGui::PopStyleColor();
        if (shift_clicked()) {
            if (number >= 0) preview_instrument(app, number);
            else {
                const uint8_t* e = app.snap.ram + addr;
                int pitch = nspc::note_pitch(app.snap.ram, nspc::note_semitone(L, e[5]), e[4], 0);
                uint8_t regs[8] = {0x30, 0x30, uint8_t(pitch & 0xFF), uint8_t(pitch >> 8), e[0], e[1], e[2], e[3]};
                preview_instrument_regs(app, regs);
            }
        }
        if (ImGui::IsItemHovered()) tooltip_spaced("Shift-click to hear it");
    }
    preview_release_check(app);
    list_detail_split_next(stacked);
    child_begin("edit");
    int i = app.sel_instrument;
    if (i < 0) text_wrapped("No instrument selected: notes you enter keep the one already in force.");
    if (i >= 0 && i < total) {
        if (i < count) {
            text_wrapped("Instrument %02X  (table $%04X)", i, L.inst_table);
            instrument_editor(app, uint16_t(L.inst_table + i * L.inst_stride), L.inst_stride, false);
        } else if (i < count + L.inst2_count) {
            int k = i - count;
            ImGui::Text("Custom instrument %02X  (AddmusicK, $%04X)", L.inst2_first + k, L.inst_table2);
            instrument_editor(app, uint16_t(L.inst_table2 + k * 6), 6, false);
        } else {
            int k = i - count - L.inst2_count;
            ImGui::Text("Percussion %02d  (table $%04X)", k, L.perc_table);
            instrument_editor(app, uint16_t(L.perc_table + k * 6), 6, true);
        }
    }
    child_end();
    panel_end();
}
