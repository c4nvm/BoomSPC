#include "paths.hpp"

#include <SDL.h>

#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

namespace {
std::string g_dir;

bool writable(const std::string& dir) {
    const std::string probe = dir + "/.boomspc-write-test";
    FILE* f = std::fopen(probe.c_str(), "w");
    if (!f) return false;
    std::fclose(f);
    std::error_code ec;
    fs::remove(probe, ec);
    return true;
}

std::string from_sdl(char* s) {
    std::string out = s ? s : "";
    if (s) SDL_free(s);
    while (!out.empty() && (out.back() == '/' || out.back() == '\\')) out.pop_back();
    return out;
}
}

const std::string& config_dir() {
    if (!g_dir.empty()) return g_dir;
    std::string dir = from_sdl(SDL_GetBasePath());
    if (dir.empty() || !writable(dir)) {
        std::string pref = from_sdl(SDL_GetPrefPath("", "BoomSPC"));
        if (!pref.empty()) dir = pref;
    }
    g_dir = dir.empty() ? "." : dir;
    return g_dir;
}

std::string config_path(const char* name) { return config_dir() + "/" + name; }

void open_config_dir() {
#ifdef _WIN32
    SDL_OpenURL(config_dir().c_str());   // ShellExecute opens a folder path in Explorer
#else
    SDL_OpenURL(("file://" + config_dir()).c_str());
#endif
}

void migrate_legacy_config() {
    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    if (ec || fs::equivalent(cwd, config_dir(), ec)) return;
    for (const char* name : {"imgui.ini", "boomspc_theme.ini", "boomspc_keys.ini"}) {
        const fs::path from = cwd / name, to = config_path(name);
        if (fs::exists(from, ec) && !fs::exists(to, ec)) fs::copy_file(from, to, ec);
    }
}
