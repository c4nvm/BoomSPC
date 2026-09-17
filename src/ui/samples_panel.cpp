// Sample directory browser with BRR decode, WAV export and WAV import.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "brr.hpp"
#include "imgui.h"
#include "file_dialog.hpp"
#include "ui.hpp"
#include "wav.hpp"

namespace {
struct DirEntry {
    uint16_t start, loop;
    brr::Info info;
    bool valid;
};

DirEntry read_entry(const EngineSnapshot& s, int index) {
    DirEntry d{};
    int dir = s.dsp[0x5D] << 8;
    int e = (dir + index * 4) & 0xFFFF;
    d.start = uint16_t(s.ram[e] | (s.ram[(e + 1) & 0xFFFF] << 8));
    d.loop  = uint16_t(s.ram[(e + 2) & 0xFFFF] | (s.ram[(e + 3) & 0xFFFF] << 8));
    d.valid = d.start >= 0x200 && d.start < 0xFFC0;
    if (d.valid) {
        d.info = brr::scan(s.ram, d.start);
        d.valid = !d.info.truncated && d.info.blocks <= 0x1000;
    }
    return d;
}

std::string import_sample(App& app, int index, const std::string& path, int rate, int loop_samples) {
    WavData wav;
    if (std::string err = read_wav(path, wav); !err.empty()) return err;
    std::vector<int16_t> pcm = wav.mono;
    if (rate > 0 && rate != wav.sample_rate) {
        pcm = resample(pcm, wav.sample_rate, rate);
        if (loop_samples >= 0) loop_samples = int(double(loop_samples) * rate / wav.sample_rate);
    }
    if (pcm.empty()) return "empty WAV";
    if (pcm.size() > 0x10000 / 9 * 16) return "sample longer than ARAM";

    int loop = loop_samples;
    std::vector<uint8_t> enc = brr::encode(pcm, loop);

    DirEntry old = read_entry(app.snap, index);
    int dest;
    std::string where;
    if (old.valid && int(enc.size()) <= old.info.blocks * 9) {
        dest = old.start;
        where = "in place";
    } else {
        dest = app.tracker.find_free_space(app.snap, int(enc.size()));
        if (dest < 0) return "no free RAM for a sample of " + std::to_string(enc.size()) + " bytes";
        char b[16]; std::snprintf(b, sizeof b, "at $%04X", dest);
        where = b;
    }
    app.engine.write_ram(uint16_t(dest), enc.data(), enc.size());
    int dir = app.snap.dsp[0x5D] << 8;
    uint16_t loop_addr = uint16_t(loop >= 0 ? dest + (loop / 16) * 9 : dest);
    uint8_t entry[4] = {uint8_t(dest & 0xFF), uint8_t(dest >> 8), uint8_t(loop_addr & 0xFF), uint8_t(loop_addr >> 8)};
    app.engine.write_ram(uint16_t(dir + index * 4), entry, 4);
    app.after_edit();
    char b[96];
    std::snprintf(b, sizeof b, "imported %zu samples as %zu BRR bytes %s", pcm.size(), enc.size(), where.c_str());
    return std::string("OK: ") + b;
}

}

void draw_samples_panel(App& app) {
    ImGui::SetNextWindowSize(ImVec2(620, 360), ImGuiCond_FirstUseEver);
    if (!panel_begin("Samples", &app.show_samples)) { panel_end(); return; }
    if (!app.engine.loaded()) { ImGui::TextDisabled("No file loaded."); panel_end(); return; }

    const EngineSnapshot& s = app.snap;
    int dir = s.dsp[0x5D] << 8;
    ImGui::Text("Directory at $%04X", dir);

    std::vector<DirEntry> entries;
    int invalid_run = 0;
    for (int i = 0; i < 128; ++i) {
        DirEntry d = read_entry(s, i);
        entries.push_back(d);
        invalid_run = d.valid ? 0 : invalid_run + 1;
        if (invalid_run >= 4) { entries.resize(entries.size() - 4); break; }
    }

    const bool stacked = list_detail_split("00  $0000 000 blk L");
    for (size_t i = 0; i < entries.size(); ++i) {
        const DirEntry& d = entries[i];
        char b[64];
        if (d.valid) std::snprintf(b, sizeof b, "%02zX  $%04X %4d blk%s", i, d.start, d.info.blocks, d.info.loops ? " L" : "");
        else std::snprintf(b, sizeof b, "%02zX  --", i);
        if (ImGui::Selectable(b, app.sel_sample == int(i))) app.sel_sample = int(i);
    }
    list_detail_split_next(stacked);
    child_begin("detail");
    if (app.sel_sample >= 0 && app.sel_sample < int(entries.size())) {
        const DirEntry& d = entries[size_t(app.sel_sample)];
        if (!d.valid) ImGui::TextDisabled("Empty / invalid entry.");
        else {
            std::vector<int16_t> pcm = brr::decode(s.ram, d.start, d.info.blocks);
            int loop_sample = d.info.loops && d.loop >= d.start ? (d.loop - d.start) / 9 * 16 : -1;
            ImGui::Text("Sample %02X: $%04X, %d blocks (%d bytes), %zu samples", app.sel_sample, d.start, d.info.blocks, d.info.blocks * 9, pcm.size());
            if (loop_sample >= 0) ImGui::Text("Loop at $%04X (sample %d)", d.loop, loop_sample);
            else ImGui::TextDisabled("One-shot");
            ImGui::TextDisabled("At pitch $1000 this plays %.2f s", pcm.size() / 32000.0);

            std::vector<float> plot(pcm.size());
            for (size_t i = 0; i < pcm.size(); ++i) plot[i] = pcm[i] / 32768.0f;
            ImVec2 size(-1, 140);
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            float w = ImGui::GetContentRegionAvail().x;
            ImGui::PlotLines("##wave", plot.data(), int(plot.size()), 0, nullptr, -1.0f, 1.0f, size);
            if (loop_sample >= 0 && !pcm.empty()) {
                float x = p0.x + w * float(loop_sample) / float(pcm.size());
                ImGui::GetWindowDrawList()->AddLine(ImVec2(x, p0.y), ImVec2(x, p0.y + 140), IM_COL32(255, 200, 60, 255), 2.0f);
            }

            if (ImGui::Button("Export WAV...")) {
                std::snprintf(app.export_path, sizeof app.export_path, "sample_%02X.wav", app.sel_sample);
                ImGui::OpenPopup("Export sample");
            }
            same_line_if_fits("Import WAV...");
            if (ImGui::Button("Import WAV...")) { app.import_wav_open = true; app.import_loop = loop_sample; app.import_rate = 0; }

            if (ImGui::BeginPopupModal("Export sample", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::InputText("path", app.export_path, sizeof app.export_path);
                if (ImGui::Button("Write")) {
                    std::string err = write_wav_mono(app.export_path, pcm, 32000);
                    app.status = err.empty() ? "wrote " + std::string(app.export_path) : err;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        }

        if (app.import_wav_open) { ImGui::OpenPopup("Import sample"); app.import_wav_open = false; }
        if (ImGui::BeginPopupModal("Import sample", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Replace sample %02X", app.sel_sample);
            ImGui::InputText("WAV path", app.import_path, sizeof app.import_path);
            ImGui::InputInt("resample to Hz (0 = keep)", &app.import_rate);
            ImGui::InputInt("loop start sample (-1 = none)", &app.import_loop);
            ImGui::TextDisabled("Replaces the directory entry. Written in place if it fits,\notherwise into free RAM. Ctrl+Z undoes.");
            if (ImGui::Button("Import")) {
                app.status = import_sample(app, app.sel_sample, app.import_path, app.import_rate, app.import_loop);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }
    child_end();
    panel_end();
}

void draw_export_dialogs(App& app) {
    if (app.export_spc_open) {
        ImGui::OpenPopup("Export SPC");
        app.export_spc_open = false;
        if (!app.export_path[0] || !std::strstr(app.export_path, ".spc")) std::snprintf(app.export_path, sizeof app.export_path, "edited.spc");
    }
    if (app.export_wav_open) {
        ImGui::OpenPopup("Export WAV");
        app.export_wav_open = false;
        if (!app.export_path[0] || !std::strstr(app.export_path, ".wav")) std::snprintf(app.export_path, sizeof app.export_path, "song.wav");
        if (app.export_seconds <= 0 && app.engine.loaded()) app.export_seconds = std::max(1, app.engine.file().total_ms() / 1000);
    }
    if (ImGui::BeginPopupModal("Export SPC", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextDisabled("Writes the loaded file with all RAM edits applied.\nTags and the initial state stay as they were.");
        ImGui::InputText("path", app.export_path, sizeof app.export_path);
        ImGui::SameLine();
        if (ImGui::Button("Browse...")) {
            std::string p = filedlg::save_file("Export SPC", {{"SPC files", "*.spc"}}, app.export_path);
            if (!p.empty()) std::snprintf(app.export_path, sizeof app.export_path, "%s", p.c_str());
        }
        if (ImGui::Button("Write")) {
            std::string err = app.engine.export_spc(app.export_path);
            app.status = err.empty() ? "wrote " + std::string(app.export_path) : err;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("Export WAV", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextDisabled("Renders the edited song from the start with the tagged fade.");
        ImGui::InputText("path", app.export_path, sizeof app.export_path);
        ImGui::SameLine();
        if (ImGui::Button("Browse...")) {
            std::string p = filedlg::save_file("Export WAV", {{"WAV files", "*.wav"}}, app.export_path);
            if (!p.empty()) std::snprintf(app.export_path, sizeof app.export_path, "%s", p.c_str());
        }
        ImGui::InputInt("seconds", &app.export_seconds);
        if (ImGui::Button("Render")) {
            std::string err = app.engine.export_wav(app.export_path, app.export_seconds);
            app.status = err.empty() ? "wrote " + std::string(app.export_path) : err;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
