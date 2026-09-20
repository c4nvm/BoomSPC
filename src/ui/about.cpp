// About panel. The logo (assets/logo.png) is read as a 4-tone bitmap and
// painted with the palette: greys by brightness, pure red = the secondary colour.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"
#include "theme.hpp"
#include "ui.hpp"
#include "update.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "stb_image.h"
#pragma GCC diagnostic pop

namespace {
SDL_Texture* g_logo = nullptr;
SDL_Window* g_window = nullptr;
int g_logo_w = 0, g_logo_h = 0;
std::string g_logo_path;   // where we looked, for the placeholder hint
constexpr int kTones = 6;        // 0..3 dark to light, 4 shirt shade, 5 shirt
std::vector<uint8_t> g_levels;   // per pixel: tone index, 254 = own colour, 255 = transparent
std::vector<uint32_t> g_own;     // the art's own pixels (colour art keeps them)
ImU32 g_painted[kTones] = {};    // palette the texture was last painted with

void logo_palette(ImU32 out[kTones]) {
    const Theme& th = theme();
    auto mix = [](ImVec4 a, ImVec4 b, float t) { return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, 1.0f); };
    const ImVec4 dark = mix(th.colors[TC_PATTERN_BG], ImVec4(0, 0, 0, 1), 0.6f);
    ImVec4 tones[kTones] = {
        dark,
        th.colors[TC_CHANNEL_CURSOR],
        th.colors[TC_ROW_INDEX_HI1],
        th.colors[TC_NOTE],
        mix(th.colors[TC_INS], dark, 0.35f),
        th.colors[TC_INS],
    };
    for (int i = 0; i < kTones; ++i) { tones[i].w = 1.0f; out[i] = ImGui::ColorConvertFloat4ToU32(tones[i]); }
}

void logo_paint() {
    if (!g_logo) return;
    ImU32 pal[kTones];
    logo_palette(pal);
    if (std::memcmp(pal, g_painted, sizeof pal) == 0) return;
    std::memcpy(g_painted, pal, sizeof pal);
    std::vector<uint32_t> px(g_levels.size());
    for (size_t i = 0; i < g_levels.size(); ++i) px[i] = g_levels[i] < kTones ? pal[g_levels[i]] : g_levels[i] == 254 ? g_own[i] : 0u;
    SDL_UpdateTexture(g_logo, nullptr, px.data(), g_logo_w * 4);
    if (g_window)
        if (SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(px.data(), g_logo_w, g_logo_h, 32, g_logo_w * 4, SDL_PIXELFORMAT_ABGR8888)) {
            SDL_SetWindowIcon(g_window, s);
            SDL_FreeSurface(s);
        }
}

// assets/logo.png: grey pixels are quantised to four tones and painted with
// the palette (dark to light), pure red takes the secondary colour, anything
// else is shown as it is.
bool try_load(SDL_Renderer* r, SDL_Window* w, const std::string& path) {
    int x, y, n;
    unsigned char* px = stbi_load(path.c_str(), &x, &y, &n, 4);
    if (!px) return false;
    SDL_Texture* tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, x, y);
    if (tex) {
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        g_logo = tex; g_logo_w = x; g_logo_h = y; g_window = w;
        g_levels.assign(size_t(x) * y, 255);
        g_own.resize(g_levels.size());
        std::memcpy(g_own.data(), px, g_own.size() * 4);
        bool tone_bitmap = true;
        for (size_t i = 0; i < g_levels.size(); ++i) {
            const unsigned char* p = px + i * 4;
            if (p[3] < 128) continue;
            const int spread = std::max({p[0], p[1], p[2]}) - std::min({p[0], p[1], p[2]});
            float lum = 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2];
            const bool red = p[0] > 100 && p[0] > p[1] + 80 && p[0] > p[2] + 80;
            if (spread <= 48) g_levels[i] = uint8_t(std::min(3, int(lum / 255.0f * 3.0f + 0.5f)));
            else if (red) g_levels[i] = uint8_t(lum < 110 ? 4 : 5);
            else { g_levels[i] = 254; tone_bitmap = false; }
        }
        if (!tone_bitmap)
            for (uint8_t& l : g_levels) if (l < 4) l = 254;
        std::memset(g_painted, 0xFF, sizeof g_painted);
        logo_paint();
    }
    stbi_image_free(px);
    return tex != nullptr;
}

void draw_logo(float max_w, float max_h) {
    if (g_logo) {
        logo_paint();
        float k = std::min(max_w / float(g_logo_w), max_h / float(g_logo_h));
        if (k > 1) k = 1;
        ImGui::Image((ImTextureID)(intptr_t)g_logo, ImVec2(g_logo_w * k, g_logo_h * k));
        return;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 sz(max_w, max_h);
    const Theme& th = theme();
    dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), th.u32(TC_CHANNEL_HEADER_BG, 0.35f), 6.0f);
    dl->AddRect(p, ImVec2(p.x + sz.x, p.y + sz.y), th.u32(TC_CHANNEL_HEADER_BG), 6.0f, 0, 2.0f);
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 2.2f);
    ImVec2 ts = ImGui::CalcTextSize("BoomSPC");
    dl->AddText(ImVec2(p.x + (sz.x - ts.x) * 0.5f, p.y + sz.y * 0.5f - ts.y * 0.8f), th.u32(TC_NOTE), "BoomSPC");
    ImGui::PopFont();
    const char* hint = "logo goes here: assets/logo.png";
    ImVec2 hs = ImGui::CalcTextSize(hint);
    dl->AddText(ImVec2(p.x + (sz.x - hs.x) * 0.5f, p.y + sz.y * 0.5f + ts.y * 0.5f), th.u32(TC_BLANK), hint);
    ImGui::Dummy(sz);
}

}

void logo_refresh() { logo_paint(); }

void logo_load(void* sdl_renderer, const std::string& base_path) {
    auto* r = static_cast<SDL_Renderer*>(sdl_renderer);
#if SDL_VERSION_ATLEAST(2, 0, 22)
    SDL_Window* w = SDL_RenderGetWindow(r);
#else
    SDL_Window* w = nullptr;   // no window icon on old SDL2
#endif
    const char* names[] = {"assets/logo.png", "assets/logo.bmp", "logo.png"};
    for (const char* n : names) {
        for (const std::string& base : {base_path, std::string()}) {
            std::string path = base + n;
            if (try_load(r, w, path)) { g_logo_path = path; return; }
        }
    }
    g_logo_path = base_path + "assets/logo.png";
}

void draw_welcome_banner() {
    float w = ImGui::GetContentRegionAvail().x;
    draw_logo(w, 110);
    ImGui::Spacing();
}

void draw_about_window(App& app) {
    ImGui::SetNextWindowSize(ImVec2(520, 540), ImGuiCond_FirstUseEver);
    if (!panel_begin("About BoomSPC", &app.show_about)) { panel_end(); return; }
    draw_logo(ImGui::GetContentRegionAvail().x, 160);
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.6f);
    ImGui::TextUnformatted("BoomSPC");
    ImGui::PopFont();
    ImGui::Text("version %s", update::version_label().c_str());
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    text_wrapped("SPC700 player and tracker-style editor for SNES music rips.");
    ImGui::PopStyleColor();
    if (!g_logo) text_wrapped("No logo found (looked for %s).", g_logo_path.c_str());
    ImGui::Separator();

    ImGui::SeparatorText("Credits");
    ImGui::Bullet(); text_wrapped("BoomSPC by c4nvm");
    ImGui::Bullet(); text_wrapped("Sound core: snes_spc 0.9.0 by Shay Green (blargg), LGPL 2.1");
    ImGui::Bullet(); text_wrapped("UI: Dear ImGui (docking branch) by Omar Cornut, MIT; SDL2 (zlib licence); zlib by Jean-loup Gailly and Mark Adler");
    ImGui::Bullet(); text_wrapped("stb_image by Sean Barrett, public domain");
    ImGui::Bullet(); text_wrapped("SPC700 opcode table from the SNESdev wiki, CC0; the SPC700 IPL boot ROM (64 bytes) is Sony's");
    ImGui::Bullet(); text_wrapped("Bundled pixel fonts: Mega Man X and Quake PC by Patrick H. Lauke (CC BY 3.0), Plok! fonts by ParadigmTheGreat (CC BY-SA 3.0), from FontStruct");
    ImGui::Bullet(); text_wrapped("Editing model and colours inspired by Furnace (tildearrow) and OpenMPT; piano roll gestures inspired by FamiStudio (BleuBleu)");
    ImGui::Bullet(); text_wrapped("N-SPC knowledge: the SMW / AddmusicK / EarthBound community disassemblies");
    ImGui::Bullet(); text_wrapped("AKAO and Capcom format references: loveemu's akaospc and capspc (MIT)");
    ImGui::Bullet(); text_wrapped("Software Creations, Rare, Capcom and Wario's Woods drivers reverse-engineered for BoomSPC from the game rips\n(Plok! music by Tim and Geoff Follin, driver by Software Creations)");
    ImGui::Separator();
    text_wrapped("Music in .spc rips belongs to its composers and publishers. BoomSPC only edits copies you load.");
    panel_end();
}
