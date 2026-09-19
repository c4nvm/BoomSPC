// Where the ini files and the crash log live: next to the executable when
// that folder is writable (a portable copy), else the per-user data folder.
// Never the working directory, which changes with how the app was launched.
#pragma once

#include <string>

const std::string& config_dir();
std::string config_path(const char* name);
void open_config_dir();          // in the system file browser
void migrate_legacy_config();   // ini files an older build left in the working directory
