// BoomSPC: SPC700 player and tracker.
#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "crash.hpp"
#include "imgui_impl_sdlrenderer2.h"
#include "paths.hpp"
#include "ui/actions.hpp"
#include "ui/fonts.hpp"
#include "ui/theme.hpp"
#include "ui/ui.hpp"
#include "update.hpp"
#include "version.hpp"
#include "wav.hpp"

namespace {
struct Options {
    std::string file, screenshot, script, record;   // --record out.wav: mono mix of everything played
    int frames = 120;
    int width = 0, height = 0;   // --size WxH
};

struct Step { std::string cmd; std::string arg; int x = 0, y = 0; };

std::vector<Step> parse_script(const std::string& text) {
    std::vector<Step> steps;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find(';', pos);
        if (end == std::string::npos) end = text.size();
        std::string item = text.substr(pos, end - pos);
        pos = end + 1;
        size_t a = item.find_first_not_of(' ');
        if (a == std::string::npos) continue;
        item = item.substr(a);
        Step st;
        size_t sp = item.find(' ');
        st.cmd = item.substr(0, sp);
        if (sp != std::string::npos) st.arg = item.substr(sp + 1);
        if (st.cmd == "click" || st.cmd == "rclick" || st.cmd == "mdown" || st.cmd == "rdown") std::sscanf(st.arg.c_str(), "%d %d", &st.x, &st.y);
        steps.push_back(st);
    }
    return steps;
}

Options parse_args(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--screenshot" && i + 1 < argc) o.screenshot = argv[++i];
        else if (a == "--frames" && i + 1 < argc) o.frames = std::atoi(argv[++i]);
        else if (a == "--size" && i + 1 < argc) std::sscanf(argv[++i], "%dx%d", &o.width, &o.height);
        else if (a == "--script" && i + 1 < argc) o.script = argv[++i];
        else if (a == "--record" && i + 1 < argc) o.record = argv[++i];
        else o.file = a;
    }
    return o;
}

void save_screenshot(SDL_Renderer* r, const std::string& path) {
    int w, h;
    SDL_GetRendererOutputSize(r, &w, &h);
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!s) return;
    if (SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) == 0) SDL_SaveBMP(s, path.c_str());
    SDL_FreeSurface(s);
}

}

int main(int argc, char** argv) {
    const Options opt = parse_args(argc, argv);
    const bool headless = !opt.screenshot.empty() || !opt.script.empty();
    update::cleanup_old();
#ifdef _WIN32
    // Sharp on high-DPI desktops: the window is in pixels (no ALLOW_HIGHDPI
    // below), and the first run scales the default font sizes by the DPI.
    SDL_SetHint("SDL_WINDOWS_DPI_AWARENESS", "permonitorv2");
#endif
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    float dpi_scale = 1.0f;
    { float ddpi = 0; if (SDL_GetDisplayDPI(0, &ddpi, nullptr, nullptr) == 0 && ddpi > 0) dpi_scale = std::clamp(ddpi / 96.0f, 1.0f, 4.0f); }
#ifdef _WIN32
    const Uint32 win_flags = SDL_WINDOW_RESIZABLE;   // pixels; the first run scales the fonts instead
    int win_w = int(1740 * dpi_scale), win_h = int(900 * dpi_scale);
#else
    const Uint32 win_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    int win_w = 1740, win_h = 900;
#endif
    SDL_DisplayMode dm;
    if (SDL_GetCurrentDisplayMode(0, &dm) == 0) {
        win_w = std::min(win_w, dm.w - 60);
        win_h = std::min(win_h, dm.h - 120);
    }
    if (opt.width > 0 && opt.height > 0) { win_w = opt.width; win_h = opt.height; }
    SDL_Window* window = SDL_CreateWindow("BoomSPC", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, win_w, win_h, win_flags);
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        std::fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return 1;
    }

    migrate_legacy_config();
    crash::install(config_path("crash.log"));
    const std::string theme_ini = config_path("boomspc_theme.ini"), keys_ini = config_path("boomspc_keys.ini");
    static const std::string imgui_ini = config_path("imgui.ini");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::GetIO().IniFilename = imgui_ini.c_str();
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    if (!theme().load(theme_ini.c_str())) {
#ifdef _WIN32
        // Half the display's scaling: sharp fonts read fine smaller than
        // the OS would blow them up to.
        const float f = 1.0f + (dpi_scale - 1.0f) * 0.5f;
        theme().font_size_ui = std::round(theme().font_size_ui * f);
        theme().font_size_pattern = std::round(theme().font_size_pattern * f);
#endif
    }
    actions_load(keys_ini.c_str());
    {
        char* base = SDL_GetBasePath();
        fonts_set_base_path(base ? base : "");
        fonts_load();
        logo_load(renderer, base ? base : "");
        if (base) SDL_free(base);
    }
    App app;
    app.update_title();
    if (crash::take_pending()) app.crash_notice = crash::log_path();
    if (std::string err = app.engine.init(); !err.empty()) {
        app.status = "Audio init failed: " + err;
        std::fprintf(stderr, "%s\n", app.status.c_str());
    }
    std::vector<int16_t> capture;
    if (!opt.record.empty()) app.engine.set_capture(&capture);
    if (!opt.file.empty()) app.open_any(opt.file);
    std::string applied_title;
    // The build that ran last time, with its '+' (local changes) marker dropped.
    std::string this_commit = build_info().commit;
    if (!this_commit.empty() && this_commit.back() == '+') this_commit.pop_back();
    const bool just_updated = *theme().last_seen_commit && this_commit != theme().last_seen_commit;
    if (!this_commit.empty()) std::snprintf(theme().last_seen_commit, sizeof theme().last_seen_commit, "%s", this_commit.c_str());
    bool update_announced = false;
    if (!headless) {
        if (theme().updates_at_startup || just_updated) { app.show_updates = true; app.updates_quiet = app.engine.loaded(); }
        if (theme().check_updates) update::check();
    }

    bool running = true;
    bool fullscreen = false;
    int frame = 0;
    std::vector<Step> script = parse_script(opt.script);
    size_t step = 0;
    int wait_frames = 0;
    Chord held;            // key pressed last frame, released this frame
    int mouse_down = 0;    // frames the button stays down
    int rmouse_down = 0;
    std::string pending_shot;
    // Settings go to disk as they change, so a crash loses nothing.
    std::string theme_written, keys_written;   // last contents written; empty = nothing yet
    Uint32 next_autosave = SDL_GetTicks() + 2000;
    auto autosave = [&] {
        if (std::string t = theme().text(); t != theme_written && theme().save(theme_ini.c_str())) theme_written = std::move(t);
        if (std::string t = actions_text(); t != keys_written && actions_save(keys_ini.c_str())) keys_written = std::move(t);
    };
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL2_ProcessEvent(&ev);
            switch (ev.type) {
                case SDL_QUIT: running = false; break;
                case SDL_DROPFILE:
                    app.open_any(ev.drop.file);
                    SDL_free(ev.drop.file);
                    break;
                default: break;
            }
        }
        if (app.window_title != applied_title) {
            applied_title = app.window_title;
            SDL_SetWindowTitle(window, applied_title.c_str());
        }
        if (app.restart) running = false;
        if (!update_announced && update::stage() == update::Stage::Available) {
            update_announced = true;
            if (!app.show_updates) { app.show_updates = true; app.updates_quiet = true; }
        }
        if (app.toggle_fullscreen) {
            app.toggle_fullscreen = false;
            fullscreen = !fullscreen;
            SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        }
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(10);
            continue;
        }
        if (SDL_TICKS_PASSED(SDL_GetTicks(), next_autosave)) { next_autosave = SDL_GetTicks() + 2000; autosave(); }

        // This bit of code should fix the scaling issues on MacOS!
        int win_w, win_h;
        int render_w, render_h;
        SDL_GetWindowSize(window, &win_w, &win_h);
        SDL_GetRendererOutputSize(renderer, &render_w, &render_h);

        if (win_w > 0 && win_h > 0) {
            float scale_x = static_cast<float>(render_w) / static_cast<float>(win_w);
            float scale_y = static_cast<float>(render_h) / static_cast<float>(win_h);
            SDL_RenderSetScale(renderer, scale_x, scale_y);
        }
        
        fonts_begin_frame();
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        {
            ImGuiIO& io = ImGui::GetIO();
            if (held.bound()) {
                io.AddKeyEvent(held.key, false);
                if (held.mods & MOD_CTRL) io.AddKeyEvent(ImGuiMod_Ctrl, false);
                if (held.mods & MOD_SHIFT) io.AddKeyEvent(ImGuiMod_Shift, false);
                if (held.mods & MOD_ALT) io.AddKeyEvent(ImGuiMod_Alt, false);
                held = Chord{};
            } else if (mouse_down > 0) {
                if (--mouse_down == 0) io.AddMouseButtonEvent(0, false);
            } else if (rmouse_down > 0) {
                if (--rmouse_down == 0) io.AddMouseButtonEvent(1, false);
            } else if (wait_frames > 0) {
                --wait_frames;
            } else if (step < script.size()) {
                const Step& st = script[step++];
                if (st.cmd == "wait") wait_frames = std::atoi(st.arg.c_str());
                else if (st.cmd == "key") {
                    if (chord_parse(st.arg.c_str(), held)) {
                        if (held.mods & MOD_CTRL) io.AddKeyEvent(ImGuiMod_Ctrl, true);
                        if (held.mods & MOD_SHIFT) io.AddKeyEvent(ImGuiMod_Shift, true);
                        if (held.mods & MOD_ALT) io.AddKeyEvent(ImGuiMod_Alt, true);
                        io.AddKeyEvent(held.key, true);
                    } else std::fprintf(stderr, "script: unknown key '%s'\n", st.arg.c_str());
                } else if (st.cmd == "hold" || st.cmd == "release") {
                    Chord c;
                    if (chord_parse(st.arg.c_str(), c)) {
                        const bool down = st.cmd == "hold";
                        io.AddKeyEvent(c.key, down);
                        if (c.key == ImGuiKey_LeftCtrl || c.key == ImGuiKey_RightCtrl) io.AddKeyEvent(ImGuiMod_Ctrl, down);
                        if (c.key == ImGuiKey_LeftShift || c.key == ImGuiKey_RightShift) io.AddKeyEvent(ImGuiMod_Shift, down);
                        if (c.key == ImGuiKey_LeftAlt || c.key == ImGuiKey_RightAlt) io.AddKeyEvent(ImGuiMod_Alt, down);
                    }
                } else if (st.cmd == "move") {
                    int x = 0, y = 0; std::sscanf(st.arg.c_str(), "%d %d", &x, &y);
                    io.AddMousePosEvent(float(x), float(y));
                } else if (st.cmd == "wheel") {
                    float x = 0, y = 0; std::sscanf(st.arg.c_str(), "%f %f", &x, &y);
                    io.AddMouseWheelEvent(x, y);
                } else if (st.cmd == "click") {
                    io.AddMousePosEvent(float(st.x), float(st.y));
                    io.AddMouseButtonEvent(0, true);
                    mouse_down = 2;
                } else if (st.cmd == "rclick") {
                    io.AddMousePosEvent(float(st.x), float(st.y));
                    io.AddMouseButtonEvent(1, true);
                    rmouse_down = 2;
                } else if (st.cmd == "mdown") {
                    io.AddMousePosEvent(float(st.x), float(st.y));
                    io.AddMouseButtonEvent(0, true);
                } else if (st.cmd == "mup") {
                    io.AddMouseButtonEvent(0, false);
                } else if (st.cmd == "rdown") {
                    io.AddMousePosEvent(float(st.x), float(st.y));
                    io.AddMouseButtonEvent(1, true);
                } else if (st.cmd == "rup") {
                    io.AddMouseButtonEvent(1, false);
                } else if (st.cmd == "run") {
                    int a = action_by_id(st.arg.c_str());
                    if (a >= 0) run_action(app, a); else std::fprintf(stderr, "script: unknown action '%s'\n", st.arg.c_str());
                } else if (st.cmd == "text") {
                    io.AddInputCharactersUTF8(st.arg.c_str());
                } else if (st.cmd == "shot") {
                    pending_shot = st.arg;
                } else std::fprintf(stderr, "script: unknown command '%s'\n", st.cmd.c_str());
            }
        }
        ImGui::NewFrame();

        ui_draw(app);
        if (!opt.script.empty()) {
            static std::string last_status;
            if (app.status != last_status) { last_status = app.status; std::fprintf(stderr, "[frame %d] status: %s\n", frame, app.status.c_str()); }
        }

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 18, 18, 22, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        if (!pending_shot.empty()) { save_screenshot(renderer, pending_shot); pending_shot.clear(); }
        if (!opt.screenshot.empty() || !opt.script.empty()) {
            if (++frame >= opt.frames && step >= script.size() && wait_frames == 0 && mouse_down == 0 && rmouse_down == 0 && !held.bound()) {
                if (!opt.screenshot.empty()) save_screenshot(renderer, opt.screenshot);
                running = false;
            }
            SDL_Delay(16);
        }
        SDL_RenderPresent(renderer);
    }

    autosave();
    if (!opt.record.empty()) { app.engine.set_capture(nullptr); write_wav_mono(opt.record, capture, Engine::kSampleRate); }
    app.engine.shutdown();
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (app.restart) update::restart();
    return 0;
}
