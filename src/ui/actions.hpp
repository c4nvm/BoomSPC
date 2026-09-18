// Every keyboard-driven operation is an Action with a name, a default key
// chord and a scope. The table drives the hotkey dispatch, the menus' shortcut
// labels, the Keyboard settings tab and the command palette, the way Furnace's
// guiActions table does. Bindings are saved to boomspc_keys.ini.
#pragma once

#include "imgui.h"

enum ActionScope { SCOPE_GLOBAL, SCOPE_PATTERN };

enum Action {
    A_PLAY_TOGGLE, A_PLAY, A_STOP, A_PLAY_START, A_PLAY_CURSOR, A_EDIT_TOGGLE, A_FOLLOW_TOGGLE, A_METRONOME_TOGGLE,
    A_OPEN, A_SAVE_PROJECT, A_SAVE_PROJECT_AS, A_EXPORT_SPC, A_EXPORT_WAV, A_UNDO, A_REDO, A_QUIT,
    A_OCTAVE_UP, A_OCTAVE_DOWN, A_STEP_UP, A_STEP_DOWN,
    A_MUTE_1, A_MUTE_2, A_MUTE_3, A_MUTE_4, A_MUTE_5, A_MUTE_6, A_MUTE_7, A_MUTE_8,
    A_MUTE_CURSOR, A_SOLO_CURSOR, A_UNMUTE_ALL,
    A_NEXT_ORDER, A_PREV_ORDER,
    A_CUR_UP, A_CUR_DOWN, A_CUR_LEFT, A_CUR_RIGHT, A_CUR_UP_COARSE, A_CUR_DOWN_COARSE,
    A_CUR_BEGIN, A_CUR_END, A_CUR_PREV_CH, A_CUR_NEXT_CH,
    A_SEL_UP, A_SEL_DOWN, A_SEL_LEFT, A_SEL_RIGHT, A_SEL_UP_COARSE, A_SEL_DOWN_COARSE,
    A_SEL_BEGIN, A_SEL_END, A_SEL_ALL, A_SEL_CLEAR,
    A_NOTE_OFF, A_NOTE_TIE, A_DELETE, A_PULL_DELETE, A_INSERT,
    A_TRANSPOSE_UP, A_TRANSPOSE_DOWN, A_TRANSPOSE_OCT_UP, A_TRANSPOSE_OCT_DOWN,
    A_VALUE_UP, A_VALUE_DOWN, A_VALUE_UP_COARSE, A_VALUE_DOWN_COARSE,
    A_INTERPOLATE, A_FADE, A_SCALE, A_RANDOMIZE, A_INVERT,
    A_FLIP, A_COLLAPSE, A_EXPAND, A_INS_SET, A_VOICE_FOLD,
    A_COPY, A_CUT, A_PASTE, A_PASTE_MIX, A_PASTE_FLOOD,
    A_ZOOM_IN, A_ZOOM_OUT, A_ZOOM_RESET,
    A_WIN_SEQUENCER, A_WIN_INSTRUMENTS, A_WIN_EFFECTS, A_WIN_EVENT, A_WIN_SAMPLES, A_WIN_PLAYER, A_WIN_VOICES, A_WIN_DSP,
    A_WIN_MEMORY, A_WIN_DISASM, A_WIN_SETTINGS, A_WIN_SHORTCUTS, A_WIN_ABOUT, A_WIN_UPDATES, A_COMMAND_PALETTE, A_FULLSCREEN,
    A_COUNT
};

enum { MOD_CTRL = 1, MOD_SHIFT = 2, MOD_ALT = 4 };

struct Chord {
    ImGuiKey key = ImGuiKey_None;
    int      mods = 0;
    bool bound() const { return key != ImGuiKey_None; }
    bool operator==(const Chord& o) const { return key == o.key && mods == o.mods; }
};

struct Binding {
    Chord c[2];
    bool pressed(bool repeat) const;
};

struct ActionDef {
    const char* id;       // stable name used in the ini
    const char* name;     // human readable
    const char* group;    // section in the Keyboard tab / palette
    ActionScope scope;
    Binding     def;      // default binding
    bool        repeat;   // fires on key repeat
};

const ActionDef& action_def(Action a);
Binding&         action_binding(Action a);
void             actions_reset_defaults();
const char*      action_shortcut(Action a);
bool             action_pressed(Action a);

const char* chord_format(const Chord& c, char* buf, int n, bool pretty = true);
bool        chord_parse(const char* s, Chord& out);

bool chord_pressed(const Chord& c, bool repeat);
ImGuiKey chord_capture(Chord& out);

int  action_by_id(const char* id);
bool actions_load(const char* path);
bool actions_save(const char* path);
