// Native file pickers. Linux uses zenity (GNOME) or kdialog (KDE), whichever
// the desktop has; Windows the common dialog (GetOpenFileName); macOS
// AppleScript's chooser through osascript. Only the Linux path has been run.
#pragma once

#include <string>
#include <vector>

namespace filedlg {
struct Filter {
    std::string name;       // "SPC files"
    std::string patterns;   // "*.spc *.boomspc" (space separated globs)
};

std::string open_file(const std::string& title, const std::vector<Filter>& filters, const std::string& start_dir);
std::string save_file(const std::string& title, const std::vector<Filter>& filters, const std::string& suggested_path);
const std::string& last_error();

}
