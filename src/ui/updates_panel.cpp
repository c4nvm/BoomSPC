// Updates panel: the build's changelog (its git log, embedded at build
// time), what GitHub has that this build lacks, and the pull-and-rebuild
// updater with its log.
#include <cstring>
#include <string>

#include <SDL.h>

#include "fonts.hpp"
#include "imgui.h"
#include "theme.hpp"
#include "ui.hpp"
#include "update.hpp"
#include "version.hpp"

namespace {

void commit_entry(const char* hash, const char* date, const char* subject, const char* body, bool mark_new) {
    ImGui::PushID(hash);
    const bool has_body = body && *body;
    ImGuiTreeNodeFlags fl = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (!has_body) fl |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet;
    char label[64];
    std::snprintf(label, sizeof label, "%.7s  %s", hash, date);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    const bool open = ImGui::TreeNodeEx("##c", fl, "%s", label);
    ImGui::PopStyleColor();
    if (mark_new) {
        ImGui::SameLine();
        ImGui::TextColored(theme().colors[TC_NOTE], "new");
    }
    ImGui::Indent(em(1.2f));
    ImGui::TextUnformatted(subject);
    if (open && has_body) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        text_spaced(body);
        ImGui::PopStyleColor();
    }
    ImGui::Unindent(em(1.2f));
    ImGui::PopID();
}


// The build path found tools missing: install them, or go to the GitHub
// page for a download instead.
void draw_tools_prompt(const update::State& st) {
    const char* id = "Missing tools";
    if (st.stage == update::Stage::NeedTools && !ImGui::IsPopupOpen(id)) ImGui::OpenPopup(id);
    ImGui::SetNextWindowSize(ImVec2(em(28), 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) return;
    ImGui::PushTextWrapPos(em(27));
    std::string list;
    for (size_t i = 0; i < st.missing_tools.size(); ++i)
        list += (i == 0 ? "" : i + 1 == st.missing_tools.size() ? " and " : ", ") + st.missing_tools[i];
    text_wrapped("It seems you don't have %s. Would you like to install them now? Otherwise, the GitHub page will be opened where you can download the new version instead.", list.c_str());
    if (st.tools_command.empty()) {
        ImGui::Spacing();
        text_wrapped("No package manager this updater knows how to drive was found, so they have to be installed by hand.");
    } else {
        ImGui::Spacing();
        ImGui::TextDisabled("This will run:");
        ImGui::PushFont(fonts().mono, ImGui::GetStyle().FontSizeBase * 0.85f);
        text_wrapped("%s", st.tools_command.c_str());
        ImGui::PopFont();
    }
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    bool close = false;
    ImGui::BeginDisabled(st.tools_command.empty());
    if (ImGui::Button("Install them")) { update::install_tools(); close = true; }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Open GitHub page")) { SDL_OpenURL(update::releases_url().c_str()); update::dismiss_tools(); close = true; }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) { update::dismiss_tools(); close = true; }
    if (close) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

}

void draw_updates_panel(App& app) {
    ImGui::SetNextWindowSize(ImVec2(560, 620), ImGuiCond_FirstUseEver);
    ImGuiWindowFlags flags = 0;
    if (app.updates_quiet) { flags |= ImGuiWindowFlags_NoFocusOnAppearing; app.focus_sequencer = app.show_sequencer; app.updates_quiet = false; }
    if (!panel_begin("Updates", &app.show_updates, flags)) { panel_end(); return; }
    const BuildInfo& bi = build_info();
    const update::State st = update::state();
    Theme& th = theme();

    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.3f);
    ImGui::Text("BoomSPC %s", bi.version);
    ImGui::PopFont();
    const size_t hl = std::strlen(bi.commit);
    const bool dirty = hl && bi.commit[hl - 1] == '+';
    if (hl) text_wrapped("Built from %.7s on %s (%s)%s", bi.commit, bi.branch, bi.date, dirty ? ", with uncommitted changes" : "");
    else ImGui::TextDisabled("Built outside a git checkout.");
    ImGui::TextDisabled("%s: %s", update::has_source() ? "Source" : "Source (cloned on update)", update::source_dir().c_str());
    ImGui::Spacing();

    if (st.busy()) {
        const char* spin = "|/-\\";
        ImGui::Text("%c %s", spin[int(ImGui::GetTime() * 8) & 3], st.message.c_str());
    } else if (st.stage != update::Stage::Idle) {
        ImVec4 col = st.stage == update::Stage::Failed ? th.colors[TC_INS_INVALID] : st.stage == update::Stage::UpToDate || st.stage == update::Stage::Built ? th.colors[TC_NOTE] : ImGui::GetStyleColorVec4(ImGuiCol_Text);
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        text_wrapped("%s", st.message.c_str());
        ImGui::PopStyleColor();
    } else ImGui::TextDisabled("Not checked yet.");

    ImGui::BeginDisabled(st.busy() || st.stage == update::Stage::NeedTools);
    if (ImGui::Button("Check for updates")) update::check();
    if (st.stage == update::Stage::Available) {
        if (!st.release_tag.empty()) {
            char label[128];
            std::snprintf(label, sizeof label, "Download %s", st.release_tag.c_str());
            same_line_if_fits(label);
            if (ImGui::Button(label)) update::download();
            if (ImGui::IsItemHovered()) tooltip_spaced("Installs %s over this copy. No tools needed.", st.release_asset.c_str());
        }
        same_line_if_fits("Update and rebuild");
        if (ImGui::Button("Update and rebuild")) update::build();
        if (ImGui::IsItemHovered()) tooltip_spaced("git pull, then cmake --build. Needs git, cmake, a C++ compiler and the SDL2 and zlib development files; offers to install them when they are missing.");
    }
    if (st.stage == update::Stage::Built) {
        same_line_if_fits("Restart BoomSPC");
        if (ImGui::Button("Restart BoomSPC")) app.restart = true;
    }
    ImGui::EndDisabled();
    draw_tools_prompt(st);
    ImGui::Checkbox("Check at startup", &th.check_updates);
    same_line_if_fits("Show this panel at startup");
    ImGui::Checkbox("Show this panel at startup", &th.updates_at_startup);

    if (!st.incoming.empty()) {
        ImGui::SeparatorText("New on GitHub");
        for (const update::Incoming& c : st.incoming) commit_entry(c.hash.c_str(), c.date.c_str(), c.subject.c_str(), c.body.c_str(), true);
    }

    ImGui::SeparatorText("Changelog");
    if (bi.log_count == 0) ImGui::TextDisabled("No history: this copy was built outside a git checkout.");
    // Commits above the one that ran last time are what an update brought.
    int seen_at = -1;
    if (*th.last_seen_commit)
        for (int i = 0; i < bi.log_count && seen_at < 0; ++i)
            if (!std::strncmp(bi.log[i].hash, th.last_seen_commit, 40)) seen_at = i;
    for (int i = 0; i < bi.log_count; ++i) {
        const Commit& c = bi.log[i];
        commit_entry(c.hash, c.date, c.subject, c.body, seen_at > 0 && i < seen_at);
    }

    if (!st.log.empty()) {
        ImGui::SeparatorText("Log");
        if (child_begin("log", ImVec2(0, em(12)), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY, 0, false)) {
            ImGui::PushFont(fonts().mono, ImGui::GetStyle().FontSizeBase * 0.9f);
            ImGui::TextUnformatted(st.log.c_str(), st.log.c_str() + st.log.size());
            ImGui::PopFont();
            if (st.busy() && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - em(2)) ImGui::SetScrollHereY(1.0f);
        }
        child_end();
    }
    panel_end();
}
