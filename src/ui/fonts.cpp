#include "fonts.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include "theme.hpp"

namespace {
Fonts g_fonts;
bool  g_reload = false;
std::string g_base;
std::vector<FontChoice> g_bundled;
bool g_scanned = false;

std::string pretty_name(const std::string& file) {
    std::string n = file.substr(0, file.find_last_of('.'));
    if (n.size() > 5 && n.compare(n.size() - 5, 5, ".colr") == 0) n.resize(n.size() - 5);
    bool up = true;
    for (char& c : n) {
        if (c == '-' || c == '_') { c = ' '; up = true; }
        else if (up) { c = char(std::toupper(uint8_t(c))); up = false; }
    }
    return n;
}

void scan_bundled() {
    g_scanned = true;
    g_bundled.clear();
    namespace fs = std::filesystem;
    for (const std::string& base : {g_base, std::string()}) {
        fs::path dir = fs::path(base) / "assets" / "fonts";
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) continue;
        std::vector<fs::path> files;
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            std::string ext = e.path().extension().string();
            for (char& c : ext) c = char(std::tolower(uint8_t(c)));
            if (ext == ".ttf" || ext == ".otf") files.push_back(e.path());
        }
        std::sort(files.begin(), files.end());
        struct Meta { std::string name; float min_size; };
        std::vector<std::pair<std::string, Meta>> meta;
        if (FILE* m = std::fopen((dir / "fonts.txt").string().c_str(), "r")) {
            char line[512];
            while (std::fgets(line, sizeof line, m)) {
                if (line[0] == '#') continue;
                std::string l = line;
                size_t a = l.find('|'), b = a == std::string::npos ? a : l.find('|', a + 1);
                if (a == std::string::npos || b == std::string::npos) continue;
                auto trim = [](std::string x) { size_t s0 = x.find_first_not_of(" \t\r\n"), s1 = x.find_last_not_of(" \t\r\n"); return s0 == std::string::npos ? std::string() : x.substr(s0, s1 - s0 + 1); };
                meta.push_back({trim(l.substr(0, a)), {trim(l.substr(a + 1, b - a - 1)), float(std::atof(trim(l.substr(b + 1)).c_str()))}});
            }
            std::fclose(m);
        }
        for (const fs::path& f : files) {
            FontChoice c{pretty_name(f.filename().string()), f.string(), 0};
            for (const auto& [file, mt] : meta) if (file == f.filename().string()) { c.name = mt.name; c.min_size = mt.min_size; }
            g_bundled.push_back(c);
        }
        if (!g_bundled.empty()) break;
    }
}

bool file_exists(const char* p) {
    if (!p || !*p) return false;
    FILE* f = std::fopen(p, "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

const char* const kMonoCandidates[] = {
    "/usr/share/fonts/dejavu-sans-mono-fonts/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
    "/usr/share/fonts/liberation-mono/LiberationMono-Regular.ttf",
    "/usr/share/fonts/liberation-mono-fonts/LiberationMono-Regular.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
    "/usr/share/fonts/google-noto/NotoSansMono-Regular.ttf",
    "/usr/share/fonts/truetype/noto/NotoSansMono-Regular.ttf",
    "/usr/share/fonts/jetbrains-mono-fonts/JetBrainsMono-Regular.ttf",
    "/usr/share/fonts/adobe-source-code-pro-fonts/SourceCodePro-Regular.otf",
    "/usr/share/fonts/TTF/Hack-Regular.ttf",
    "/usr/share/fonts/hack/Hack-Regular.ttf",
    "C:\\Windows\\Fonts\\consola.ttf",
    "C:\\Windows\\Fonts\\lucon.ttf",
    "/System/Library/Fonts/Menlo.ttc",
    "/System/Library/Fonts/Monaco.ttf",
    nullptr,
};
const char* const kUiCandidates[] = {
    "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf",
    "/usr/share/fonts/liberation-sans-fonts/LiberationSans-Regular.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    "/usr/share/fonts/google-noto/NotoSans-Regular.ttf",
    "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
    "/usr/share/fonts/open-sans/OpenSans-Regular.ttf",
    "C:\\Windows\\Fonts\\segoeui.ttf",
    "C:\\Windows\\Fonts\\arial.ttf",
    "/System/Library/Fonts/Helvetica.ttc",
    nullptr,
};

std::string pick(const char* preferred, const char* const* candidates) {
    if (file_exists(preferred)) return preferred;
    if (preferred && *preferred && !std::strchr(preferred, '/') && !std::strchr(preferred, '\\'))
        for (const FontChoice& f : bundled_fonts())
            if (f.path.size() >= std::strlen(preferred) && f.path.compare(f.path.size() - std::strlen(preferred), std::strlen(preferred), preferred) == 0) return f.path;
    for (const char* const* c = candidates; *c; ++c)
        if (file_exists(*c)) return *c;
    return {};
}

ImFont* add(const std::string& path, float size, float tracking = 0.0f) {
    ImGuiIO& io = ImGui::GetIO();
    if (!path.empty()) {
        ImFontConfig cfg;
        cfg.OversampleH = 2;
        cfg.GlyphExtraAdvanceX = tracking;
        if (ImFont* f = io.Fonts->AddFontFromFileTTF(path.c_str(), size, &cfg)) {
            if (!f->IsGlyphInFont('a') && f->IsGlyphInFont('A'))
                for (ImWchar c = 'a'; c <= 'z'; ++c) if (!f->IsGlyphInFont(c)) f->AddRemapChar(c, ImWchar(c - 'a' + 'A'));
            ImFontConfig fb;
            fb.MergeMode = true;
            fb.SizePixels = size;
            io.Fonts->AddFontDefaultVector(&fb);
            return f;
        }
    }
    ImFontConfig cfg;
    cfg.SizePixels = size;
    return io.Fonts->AddFontDefaultVector(&cfg);
}

}

Fonts& fonts() { return g_fonts; }
void fonts_request_reload() { g_reload = true; }
void fonts_set_base_path(const std::string& base) { g_base = base; g_scanned = false; }
const std::vector<FontChoice>& bundled_fonts() { if (!g_scanned) scan_bundled(); return g_bundled; }

void fonts_load() {
    ImGuiIO& io = ImGui::GetIO();
    Theme& t = theme();
    if (g_fonts.ui)   io.Fonts->RemoveFont(g_fonts.ui);
    if (g_fonts.mono && g_fonts.mono != g_fonts.ui) io.Fonts->RemoveFont(g_fonts.mono);
    g_fonts = Fonts{};
    std::string ui = pick(t.font_ui, kUiCandidates);
    std::string mono = pick(t.font_mono, kMonoCandidates);
    if (ui.empty()) ui = mono;
    g_fonts.ui = add(ui, t.font_size_ui, t.font_tracking);
    g_fonts.ui_path = ui;
    if (mono == ui && g_fonts.ui) { g_fonts.mono = g_fonts.ui; g_fonts.mono_path = mono; }
    else { g_fonts.mono = add(mono, t.font_size_pattern); g_fonts.mono_path = mono; }
    io.FontDefault = g_fonts.ui;
}

void fonts_begin_frame() {
    if (g_reload) { fonts_load(); g_reload = false; }
    Theme& t = theme();
    if (t.font_size_ui < 8) t.font_size_ui = 8;
    if (t.font_size_pattern < 6) t.font_size_pattern = 6;
    ImGui::GetStyle().FontSizeBase = t.font_size_ui;
}
