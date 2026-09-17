// Font loading. ImGui 1.92's dynamic atlas lets us pick any size per
// PushFont(), so only the font *files* need a reload; sizes are live.
#pragma once

#include <string>
#include <vector>

#include "imgui.h"

struct Fonts {
    ImFont* ui = nullptr;      // proportional (or the mono if nothing else is found)
    ImFont* mono = nullptr;    // pattern grid, hex views, disassembly
    std::string ui_path, mono_path;   // what was actually loaded ("" = built-in vector font)
};

Fonts& fonts();

struct FontChoice { std::string name, path; float min_size = 0; };
const std::vector<FontChoice>& bundled_fonts();
void fonts_set_base_path(const std::string& base);
void fonts_load();
void fonts_request_reload();
void fonts_begin_frame();
