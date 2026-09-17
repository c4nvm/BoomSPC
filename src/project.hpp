// BoomSPC project files (.boomspc): a small text header with the tracker's
// view settings, then the edited SPC image as-is. Opening one restores the
// song exactly where you left it; exporting an .spc is a separate step.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct ProjectMeta {
    std::string source;        // the .spc the project started from
    int  song = -1;            // song index in the driver's list
    int  ticks_per_row = 0;
    int  ticks_per_beat = 0;
    int  octave = 4;
    int  edit_step = 1;
    int  view_order = 0;
    bool reclaim = false;
};

std::string save_project(const std::string& path, const ProjectMeta& meta, const std::vector<uint8_t>& spc);
std::string load_project(const std::string& path, ProjectMeta& meta, std::vector<uint8_t>& spc);
bool is_project_path(const std::string& path);
