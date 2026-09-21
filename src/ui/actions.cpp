#include "actions.hpp"

#include <cstdio>
#include <cstring>

namespace {
constexpr Chord K(ImGuiKey k, int m = 0) { return Chord{k, m}; }
constexpr Binding B(Chord a, Chord b = Chord{}) { return Binding{{a, b}}; }

const ActionDef kDefs[A_COUNT] = {
    {"PLAY_TOGGLE",    "Play / stop",                    "Transport", SCOPE_GLOBAL,  B(K(ImGuiKey_Enter), K(ImGuiKey_KeypadEnter)), false},
    {"PLAY",           "Play",                           "Transport", SCOPE_GLOBAL,  B(K(ImGuiKey_None)), false},
    {"STOP",           "Stop",                           "Transport", SCOPE_GLOBAL,  B(K(ImGuiKey_None)), false},
    {"PLAY_START",     "Play from beginning (restart)",  "Transport", SCOPE_GLOBAL,  B(K(ImGuiKey_R, MOD_CTRL)), false},
    {"PLAY_CURSOR",    "Play from the cursor",           "Transport", SCOPE_GLOBAL,  B(K(ImGuiKey_Enter, MOD_SHIFT), K(ImGuiKey_KeypadEnter, MOD_SHIFT)), false},
    {"EDIT_TOGGLE",    "Toggle edit mode",               "Transport", SCOPE_GLOBAL,  B(K(ImGuiKey_Space)), false},
    {"FOLLOW_TOGGLE",  "Toggle follow playback",         "Transport", SCOPE_GLOBAL,  B(K(ImGuiKey_ScrollLock), K(ImGuiKey_F, MOD_CTRL)), false},
    {"METRONOME_TOGGLE", "Toggle metronome",             "Transport", SCOPE_GLOBAL,  B(K(ImGuiKey_M, MOD_CTRL)), false},

    {"OPEN",           "Open SPC or project...",         "File",      SCOPE_GLOBAL,  B(K(ImGuiKey_O, MOD_CTRL)), false},
    {"SAVE_PROJECT",   "Save project",                   "File",      SCOPE_GLOBAL,  B(K(ImGuiKey_S, MOD_CTRL)), false},
    {"SAVE_PROJECT_AS","Save project as...",             "File",      SCOPE_GLOBAL,  B(K(ImGuiKey_S, MOD_CTRL | MOD_SHIFT)), false},
    {"EXPORT_SPC",     "Export SPC...",                  "File",      SCOPE_GLOBAL,  B(K(ImGuiKey_E, MOD_CTRL)), false},
    {"EXPORT_WAV",     "Export WAV...",                  "File",      SCOPE_GLOBAL,  B(K(ImGuiKey_E, MOD_CTRL | MOD_SHIFT)), false},
    {"QUIT",           "Quit",                           "File",      SCOPE_GLOBAL,  B(K(ImGuiKey_Q, MOD_CTRL)), false},

    {"OCTAVE_UP",      "Octave up",                      "Entry",     SCOPE_GLOBAL,  B(K(ImGuiKey_KeypadMultiply), K(ImGuiKey_RightBracket)), false},
    {"OCTAVE_DOWN",    "Octave down",                    "Entry",     SCOPE_GLOBAL,  B(K(ImGuiKey_KeypadDivide), K(ImGuiKey_LeftBracket)), false},
    {"STEP_UP",        "Increase edit step",             "Entry",     SCOPE_GLOBAL,  B(K(ImGuiKey_KeypadMultiply, MOD_CTRL), K(ImGuiKey_RightBracket, MOD_CTRL)), false},
    {"STEP_DOWN",      "Decrease edit step",             "Entry",     SCOPE_GLOBAL,  B(K(ImGuiKey_KeypadDivide, MOD_CTRL), K(ImGuiKey_LeftBracket, MOD_CTRL)), false},

    {"MUTE_1",         "Mute / unmute voice 1",          "Channels",  SCOPE_GLOBAL,  B(K(ImGuiKey_F1)), false},
    {"MUTE_2",         "Mute / unmute voice 2",          "Channels",  SCOPE_GLOBAL,  B(K(ImGuiKey_F2)), false},
    {"MUTE_3",         "Mute / unmute voice 3",          "Channels",  SCOPE_GLOBAL,  B(K(ImGuiKey_F3)), false},
    {"MUTE_4",         "Mute / unmute voice 4",          "Channels",  SCOPE_GLOBAL,  B(K(ImGuiKey_F4)), false},
    {"MUTE_5",         "Mute / unmute voice 5",          "Channels",  SCOPE_GLOBAL,  B(K(ImGuiKey_F5)), false},
    {"MUTE_6",         "Mute / unmute voice 6",          "Channels",  SCOPE_GLOBAL,  B(K(ImGuiKey_F6)), false},
    {"MUTE_7",         "Mute / unmute voice 7",          "Channels",  SCOPE_GLOBAL,  B(K(ImGuiKey_F7)), false},
    {"MUTE_8",         "Mute / unmute voice 8",          "Channels",  SCOPE_GLOBAL,  B(K(ImGuiKey_F8)), false},
    {"MUTE_CURSOR",    "Mute channel at cursor",         "Channels",  SCOPE_PATTERN, B(K(ImGuiKey_F9, MOD_ALT)), false},
    {"SOLO_CURSOR",    "Solo channel at cursor",         "Channels",  SCOPE_PATTERN, B(K(ImGuiKey_F10, MOD_ALT)), false},
    {"UNMUTE_ALL",     "Unmute all channels",            "Channels",  SCOPE_GLOBAL,  B(K(ImGuiKey_F9, MOD_ALT | MOD_SHIFT)), false},

    {"NEXT_ORDER",     "Go to next order",               "Orders",    SCOPE_GLOBAL,  B(K(ImGuiKey_KeypadAdd), K(ImGuiKey_PageDown, MOD_CTRL)), true},
    {"PREV_ORDER",     "Go to previous order",           "Orders",    SCOPE_GLOBAL,  B(K(ImGuiKey_KeypadSubtract), K(ImGuiKey_PageUp, MOD_CTRL)), true},

    {"CUR_UP",         "Move cursor up",                 "Cursor",    SCOPE_PATTERN, B(K(ImGuiKey_UpArrow)), true},
    {"CUR_DOWN",       "Move cursor down",               "Cursor",    SCOPE_PATTERN, B(K(ImGuiKey_DownArrow)), true},
    {"CUR_LEFT",       "Move cursor left",               "Cursor",    SCOPE_PATTERN, B(K(ImGuiKey_LeftArrow)), true},
    {"CUR_RIGHT",      "Move cursor right",              "Cursor",    SCOPE_PATTERN, B(K(ImGuiKey_RightArrow)), true},
    {"CUR_UP_COARSE",  "Move cursor up (coarse)",        "Cursor",    SCOPE_PATTERN, B(K(ImGuiKey_PageUp)), true},
    {"CUR_DOWN_COARSE","Move cursor down (coarse)",      "Cursor",    SCOPE_PATTERN, B(K(ImGuiKey_PageDown)), true},
    {"CUR_BEGIN",      "Move cursor to beginning",       "Cursor",    SCOPE_PATTERN, B(K(ImGuiKey_Home)), false},
    {"CUR_END",        "Move cursor to end",             "Cursor",    SCOPE_PATTERN, B(K(ImGuiKey_End)), false},
    {"CUR_PREV_CH",    "Move cursor to previous channel","Cursor",    SCOPE_PATTERN, B(K(ImGuiKey_LeftArrow, MOD_CTRL), K(ImGuiKey_Tab, MOD_SHIFT)), true},
    {"CUR_NEXT_CH",    "Move cursor to next channel",    "Cursor",    SCOPE_PATTERN, B(K(ImGuiKey_RightArrow, MOD_CTRL), K(ImGuiKey_Tab)), true},

    {"SEL_UP",         "Expand selection up",            "Selection", SCOPE_PATTERN, B(K(ImGuiKey_UpArrow, MOD_SHIFT)), true},
    {"SEL_DOWN",       "Expand selection down",          "Selection", SCOPE_PATTERN, B(K(ImGuiKey_DownArrow, MOD_SHIFT)), true},
    {"SEL_LEFT",       "Expand selection left",          "Selection", SCOPE_PATTERN, B(K(ImGuiKey_LeftArrow, MOD_SHIFT)), true},
    {"SEL_RIGHT",      "Expand selection right",         "Selection", SCOPE_PATTERN, B(K(ImGuiKey_RightArrow, MOD_SHIFT)), true},
    {"SEL_UP_COARSE",  "Expand selection up (coarse)",   "Selection", SCOPE_PATTERN, B(K(ImGuiKey_PageUp, MOD_SHIFT)), true},
    {"SEL_DOWN_COARSE","Expand selection down (coarse)", "Selection", SCOPE_PATTERN, B(K(ImGuiKey_PageDown, MOD_SHIFT)), true},
    {"SEL_BEGIN",      "Expand selection to beginning",  "Selection", SCOPE_PATTERN, B(K(ImGuiKey_Home, MOD_CTRL | MOD_SHIFT), K(ImGuiKey_Home, MOD_SHIFT)), false},
    {"SEL_END",        "Expand selection to end",        "Selection", SCOPE_PATTERN, B(K(ImGuiKey_End, MOD_CTRL | MOD_SHIFT), K(ImGuiKey_End, MOD_SHIFT)), false},
    {"SEL_ALL",        "Select all (column, then pattern)","Selection",SCOPE_PATTERN, B(K(ImGuiKey_A, MOD_CTRL)), false},
    {"SEL_CLEAR",      "Clear selection",                "Selection", SCOPE_PATTERN, B(K(ImGuiKey_Escape)), false},

    {"UNDO",           "Undo",                           "Edit",      SCOPE_GLOBAL,  B(K(ImGuiKey_Z, MOD_CTRL)), true},
    {"REDO",           "Redo",                           "Edit",      SCOPE_GLOBAL,  B(K(ImGuiKey_Y, MOD_CTRL), K(ImGuiKey_Z, MOD_CTRL | MOD_SHIFT)), true},
    {"NOTE_OFF",       "Note off (rest)",                "Edit",      SCOPE_PATTERN, B(K(ImGuiKey_GraveAccent), K(ImGuiKey_CapsLock)), false},
    {"NOTE_TIE",       "Tie (hold previous note)",       "Edit",      SCOPE_PATTERN, B(K(ImGuiKey_1), K(ImGuiKey_Equal)), false},
    {"DELETE",         "Delete field / selection",       "Edit",      SCOPE_PATTERN, B(K(ImGuiKey_Delete)), false},
    {"PULL_DELETE",    "Pull delete (remove row, shift up)","Edit",   SCOPE_PATTERN, B(K(ImGuiKey_Backspace)), false},
    {"INSERT",         "Insert row (shift down)",        "Edit",      SCOPE_PATTERN, B(K(ImGuiKey_Insert)), false},
    {"TRANSPOSE_UP",   "Transpose +1",                   "Edit",      SCOPE_PATTERN, B(K(ImGuiKey_F2, MOD_CTRL), K(ImGuiKey_UpArrow, MOD_CTRL)), true},
    {"TRANSPOSE_DOWN", "Transpose -1",                   "Edit",      SCOPE_PATTERN, B(K(ImGuiKey_F1, MOD_CTRL), K(ImGuiKey_DownArrow, MOD_CTRL)), true},
    {"TRANSPOSE_OCT_UP","Transpose +1 octave",           "Edit",      SCOPE_PATTERN, B(K(ImGuiKey_F4, MOD_CTRL), K(ImGuiKey_UpArrow, MOD_CTRL | MOD_SHIFT)), true},
    {"TRANSPOSE_OCT_DOWN","Transpose -1 octave",         "Edit",      SCOPE_PATTERN, B(K(ImGuiKey_F3, MOD_CTRL), K(ImGuiKey_DownArrow, MOD_CTRL | MOD_SHIFT)), true},
    {"VALUE_UP",       "Increase value +1",              "Edit",      SCOPE_PATTERN, B(K(ImGuiKey_F2, MOD_SHIFT)), true},
    {"VALUE_DOWN",     "Decrease value -1",              "Edit",      SCOPE_PATTERN, B(K(ImGuiKey_F1, MOD_SHIFT)), true},
    {"VALUE_UP_COARSE","Increase value +16",             "Edit",      SCOPE_PATTERN, B(K(ImGuiKey_F4, MOD_SHIFT)), true},
    {"VALUE_DOWN_COARSE","Decrease value -16",           "Edit",      SCOPE_PATTERN, B(K(ImGuiKey_F3, MOD_SHIFT)), true},
    {"INTERPOLATE",    "Interpolate values over selection","Edit",    SCOPE_PATTERN, B(K(ImGuiKey_I, MOD_CTRL)), false},
    {"FADE",           "Fade values (from / to)...",     "Edit",      SCOPE_PATTERN, B(K(ImGuiKey_None)), false},
    {"SCALE",          "Scale values (percent)...",      "Edit",      SCOPE_PATTERN, B(K(ImGuiKey_None)), false},
    {"RANDOMIZE",      "Randomize values (min / max)...","Edit",      SCOPE_PATTERN, B(K(ImGuiKey_None)), false},
    {"INVERT",         "Invert values",                  "Edit",      SCOPE_PATTERN, B(K(ImGuiKey_None)), false},
    {"FLIP",           "Flip selection (reverse rows)",  "Edit",      SCOPE_PATTERN, B(K(ImGuiKey_None)), false},
    {"COLLAPSE",       "Collapse rows (x1/2)",           "Edit",      SCOPE_PATTERN, B(K(ImGuiKey_None)), false},
    {"EXPAND",         "Expand rows (x2)",               "Edit",      SCOPE_PATTERN, B(K(ImGuiKey_None)), false},
    {"INS_SET",        "Set instrument of selection to the current one", "Edit", SCOPE_PATTERN, B(K(ImGuiKey_None)), false},
    {"VOICE_FOLD",     "Fold / unfold the cursor voice's column (note + effect code only)", "View", SCOPE_PATTERN, B(K(ImGuiKey_Backslash, MOD_CTRL)), false},
    {"COPY",           "Copy",                           "Edit",      SCOPE_PATTERN, B(K(ImGuiKey_C, MOD_CTRL)), false},
    {"CUT",            "Cut",                            "Edit",      SCOPE_PATTERN, B(K(ImGuiKey_X, MOD_CTRL)), false},
    {"PASTE",          "Paste",                          "Edit",      SCOPE_PATTERN, B(K(ImGuiKey_V, MOD_CTRL)), false},
    {"PASTE_MIX",      "Paste mix (keep existing)",      "Edit",      SCOPE_PATTERN, B(K(ImGuiKey_V, MOD_CTRL | MOD_SHIFT)), false},
    {"PASTE_FLOOD",    "Paste flood (repeat to the end)","Edit",      SCOPE_PATTERN, B(K(ImGuiKey_V, MOD_CTRL | MOD_ALT)), false},

    {"ZOOM_IN",        "Pattern font bigger",            "View",      SCOPE_GLOBAL,  B(K(ImGuiKey_Equal, MOD_CTRL), K(ImGuiKey_KeypadAdd, MOD_CTRL)), true},
    {"ZOOM_OUT",       "Pattern font smaller",           "View",      SCOPE_GLOBAL,  B(K(ImGuiKey_Minus, MOD_CTRL), K(ImGuiKey_KeypadSubtract, MOD_CTRL)), true},
    {"ZOOM_RESET",     "Pattern font reset",             "View",      SCOPE_GLOBAL,  B(K(ImGuiKey_0, MOD_CTRL)), false},
    {"WIN_SEQUENCER",  "Window: Sequencer",              "Windows",   SCOPE_GLOBAL,  B(K(ImGuiKey_None)), false},
    {"WIN_INSTRUMENTS","Window: Instruments",            "Windows",   SCOPE_GLOBAL,  B(K(ImGuiKey_None)), false},
    {"WIN_EFFECTS",    "Window: Effects (command list)", "Windows",   SCOPE_GLOBAL,  B(K(ImGuiKey_None)), false},
    {"WIN_EVENT",      "Window: Event (cursor events)",  "Windows",   SCOPE_GLOBAL,  B(K(ImGuiKey_None)), false},
    {"WIN_SAMPLES",    "Window: Samples",                "Windows",   SCOPE_GLOBAL,  B(K(ImGuiKey_None)), false},
    {"WIN_PLAYER",     "Window: Player",                 "Windows",   SCOPE_GLOBAL,  B(K(ImGuiKey_None)), false},
    {"WIN_VOICES",     "Window: Voices",                 "Windows",   SCOPE_GLOBAL,  B(K(ImGuiKey_None)), false},
    {"WIN_DSP",        "Window: DSP",                    "Windows",   SCOPE_GLOBAL,  B(K(ImGuiKey_None)), false},
    {"WIN_MEMORY",     "Window: Memory",                 "Windows",   SCOPE_GLOBAL,  B(K(ImGuiKey_None)), false},
    {"WIN_DISASM",     "Window: Disassembly",            "Windows",   SCOPE_GLOBAL,  B(K(ImGuiKey_None)), false},
    {"WIN_SETTINGS",   "Window: Settings",               "Windows",   SCOPE_GLOBAL,  B(K(ImGuiKey_Comma, MOD_CTRL)), false},
    {"WIN_SHORTCUTS",  "Window: Keyboard shortcuts",     "Windows",   SCOPE_GLOBAL,  B(K(ImGuiKey_Slash, MOD_CTRL), K(ImGuiKey_F1, MOD_SHIFT | MOD_CTRL)), false},
    {"WIN_ABOUT",      "About BoomSPC",                  "Windows",   SCOPE_GLOBAL,  B(K(ImGuiKey_None)), false},
    {"WIN_UPDATES",    "Updates and changelog",          "Windows",   SCOPE_GLOBAL,  B(K(ImGuiKey_None)), false},
    {"COMMAND_PALETTE","Command palette",                "Windows",   SCOPE_GLOBAL,  B(K(ImGuiKey_P, MOD_CTRL)), false},
    {"FULLSCREEN",     "Toggle full screen",             "Windows",   SCOPE_GLOBAL,  B(K(ImGuiKey_F11)), false},
};

Binding g_bind[A_COUNT];
bool g_init = false;

void ensure_init() {
    if (g_init) return;
    actions_reset_defaults();
}

struct Pretty { const char* imgui; const char* label; };
const Pretty kPretty[] = {
    {"KeypadMultiply", "Num *"}, {"KeypadDivide", "Num /"}, {"KeypadAdd", "Num +"}, {"KeypadSubtract", "Num -"},
    {"KeypadEnter", "Num Enter"}, {"KeypadDecimal", "Num ."}, {"KeypadEqual", "Num ="},
    {"LeftBracket", "["}, {"RightBracket", "]"}, {"GraveAccent", "`"}, {"Equal", "="}, {"Minus", "-"},
    {"Comma", ","}, {"Period", "."}, {"Slash", "/"}, {"Backslash", "\\"}, {"Semicolon", ";"}, {"Apostrophe", "'"},
    {"UpArrow", "Up"}, {"DownArrow", "Down"}, {"LeftArrow", "Left"}, {"RightArrow", "Right"},
    {"PageUp", "PgUp"}, {"PageDown", "PgDn"}, {"Escape", "Esc"}, {"Backspace", "BkSp"}, {"Delete", "Del"}, {"Insert", "Ins"},
};

const char* key_label(ImGuiKey k, bool pretty) {
    const char* n = ImGui::GetKeyName(k);
    if (!pretty) return n;
    for (const Pretty& p : kPretty) if (!std::strcmp(p.imgui, n)) return p.label;
    if (!std::strncmp(n, "Keypad", 6)) { static char b[16]; std::snprintf(b, sizeof b, "Num %s", n + 6); return b; }
    return n;
}

int current_mods() {
    ImGuiIO& io = ImGui::GetIO();
    return (io.KeyCtrl ? MOD_CTRL : 0) | (io.KeyShift ? MOD_SHIFT : 0) | (io.KeyAlt ? MOD_ALT : 0);
}

bool is_modifier_key(ImGuiKey k) {
    return k == ImGuiKey_LeftCtrl || k == ImGuiKey_RightCtrl || k == ImGuiKey_LeftShift || k == ImGuiKey_RightShift ||
           k == ImGuiKey_LeftAlt || k == ImGuiKey_RightAlt || k == ImGuiKey_LeftSuper || k == ImGuiKey_RightSuper ||
           k == ImGuiMod_Ctrl || k == ImGuiMod_Shift || k == ImGuiMod_Alt || k == ImGuiMod_Super;
}

}

const ActionDef& action_def(Action a) { return kDefs[a]; }
Binding& action_binding(Action a) { ensure_init(); return g_bind[a]; }

void actions_reset_defaults() {
    for (int i = 0; i < A_COUNT; ++i) g_bind[i] = kDefs[i].def;
    g_init = true;
}

const char* chord_format(const Chord& c, char* buf, int n, bool pretty) {
    if (!c.bound()) { std::snprintf(buf, size_t(n), "%s", ""); return buf; }
    std::snprintf(buf, size_t(n), "%s%s%s%s", c.mods & MOD_CTRL ? "Ctrl+" : "", c.mods & MOD_SHIFT ? "Shift+" : "",
                  c.mods & MOD_ALT ? "Alt+" : "", key_label(c.key, pretty));
    return buf;
}

bool chord_parse(const char* s, Chord& out) {
    Chord c;
    const char* p = s;
    while (*p == ' ') ++p;
    for (;;) {
        if (!std::strncmp(p, "Ctrl+", 5)) { c.mods |= MOD_CTRL; p += 5; }
        else if (!std::strncmp(p, "Shift+", 6)) { c.mods |= MOD_SHIFT; p += 6; }
        else if (!std::strncmp(p, "Alt+", 4)) { c.mods |= MOD_ALT; p += 4; }
        else break;
    }
    char name[64];
    std::snprintf(name, sizeof name, "%s", p);
    for (char* e = name + std::strlen(name); e > name && (e[-1] == ' ' || e[-1] == '\n' || e[-1] == '\r'); --e) e[-1] = 0;
    if (!*name || !std::strcmp(name, "None")) { out = Chord{}; return true; }
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k)
        if (!std::strcmp(key_label(ImGuiKey(k), false), name) || !std::strcmp(key_label(ImGuiKey(k), true), name)) { c.key = ImGuiKey(k); out = c; return true; }
    return false;
}

bool chord_pressed(const Chord& c, bool repeat) {
    if (!c.bound()) return false;
    if (current_mods() != c.mods) return false;
    return ImGui::IsKeyPressed(c.key, repeat);
}

bool Binding::pressed(bool repeat) const { return chord_pressed(c[0], repeat) || chord_pressed(c[1], repeat); }

bool action_pressed(Action a) { ensure_init(); return g_bind[a].pressed(kDefs[a].repeat); }

const char* action_shortcut(Action a) {
    static char bufs[A_COUNT][40];
    ensure_init();
    return chord_format(g_bind[a].c[0], bufs[a], sizeof bufs[a]);
}

ImGuiKey chord_capture(Chord& out) {
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
        ImGuiKey key = ImGuiKey(k);
        if (is_modifier_key(key)) continue;
        if (key >= ImGuiKey_GamepadStart && key <= ImGuiKey_GamepadRStickDown) continue;
        if (key >= ImGuiKey_MouseLeft && key <= ImGuiKey_MouseWheelY) continue;
        if (ImGui::IsKeyPressed(key, false)) { out.key = key; out.mods = current_mods(); return key; }
    }
    return ImGuiKey_None;
}

int action_by_id(const char* id) {
    for (int i = 0; i < A_COUNT; ++i) if (!std::strcmp(kDefs[i].id, id)) return i;
    return -1;
}

bool actions_load(const char* path) {
    ensure_init();
    FILE* f = std::fopen(path, "r");
    if (!f) return false;
    char line[256];
    while (std::fgets(line, sizeof line, f)) {
        char* eq = std::strchr(line, '=');
        if (!eq || line[0] == '#') continue;
        *eq = 0;
        for (int i = 0; i < A_COUNT; ++i) {
            if (std::strcmp(line, kDefs[i].id)) continue;
            char* v = eq + 1;
            char* comma = std::strchr(v, ',');
            Chord a, b;
            if (comma) { *comma = 0; chord_parse(comma + 1, b); }
            chord_parse(v, a);
            g_bind[i].c[0] = a; g_bind[i].c[1] = b;
        }
    }
    std::fclose(f);
    return true;
}

std::string actions_text() {
    ensure_init();
    std::string out = "# BoomSPC key bindings: ACTION=chord[, chord]. Empty = unbound.\n";
    for (int i = 0; i < A_COUNT; ++i) {
        char a[40], b[40];
        chord_format(g_bind[i].c[0], a, sizeof a, false);
        chord_format(g_bind[i].c[1], b, sizeof b, false);
        out += kDefs[i].id; out += '='; out += a;
        if (*b) { out += ", "; out += b; }
        out += '\n';
    }
    return out;
}

bool actions_save(const char* path) {
    FILE* f = std::fopen(path, "w");
    if (!f) return false;
    const std::string t = actions_text();
    std::fwrite(t.data(), 1, t.size(), f);
    std::fclose(f);
    return true;
}
