#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "engine.hpp"
#include "project.hpp"
#include "theme.hpp"
#include "tracker.hpp"

struct App {
    Engine engine;
    EngineSnapshot snap;     // refreshed once per frame in ui_draw()

    char path_buf[1024] = {};
    std::string status;      // last load result / error, shown in the status bar
    std::string source_path; // the .spc that was opened (or the project's source)
    std::string project_path;// current .boomspc, empty = unsaved
    std::string window_title;// main() applies changes to the SDL window
    std::string crash_notice;// crash log path when the last run died; a popup shows it once

    Tracker tracker;

    bool show_player = true;
    bool show_voices = true;
    bool show_dsp    = false;
    bool show_memory = false;
    bool show_demo   = false;
    bool show_sequencer   = true;
    bool show_instruments = true;
    bool show_effects     = true;    // the driver's command list
    bool show_event_editor = true;   // events under the cursor, with arguments
    bool focus_event_editor = false; // bring the Event tab to the front next frame
    bool focus_sequencer = false;    // same for the Sequencer (a loaded song over the Updates tab)
    bool show_samples     = false;
    bool show_disasm      = false;
    bool show_settings    = false;
    bool show_shortcuts   = false;
    bool show_palette     = false;   // command palette popup
    bool show_about       = false;
    bool show_updates     = false;
    bool updates_quiet    = false;   // open the Updates tab behind the Sequencer (a song is showing)
    bool restart          = false;   // quit and start the freshly built executable (main() does it)
    bool focus_path_box   = false;   // Ctrl+O: put the caret in the Player's path field
    bool toggle_fullscreen = false;  // handled by main()
    bool reset_layout = false;       // rebuild the default panel arrangement

    int  mem_goto = -1;      // memory panel scroll target, -1 = none pending

    enum Field { F_NOTE, F_INS, F_QUANT, F_VEL, F_FX, F_FXARG, F_COUNT };
    int  view_order = 0;         // order index being displayed
    bool follow = true;          // track the playback position
    bool metronome = false;      // click on highlighted rows (grid alignment check)
    int  metronome_rate = 1;     // 0 = half time, 1 = the highlight rows, 2 = double time
    bool edit_mode = false;      // Space toggles; keys only write when on
    int  ticks_per_row = 12;
    int  ticks_per_beat = 48;    // for the BPM readout; drivers suggest a default, songs may differ
    int  sel_voice = -1, sel_row = -1;   // cursor cell
    int  sel_field = F_NOTE;
    int  cur_nibble = 0;                 // hex entry progress in the current field
    int  sel_event = -1;
    bool sel_active = false;             // block selection, corners inclusive
    int  sel_v0 = 0, sel_f0 = 0, sel_r0 = 0, sel_v1 = 0, sel_f1 = 0, sel_r1 = 0;
    int  sel_all_stage = 0;              // Ctrl+A: 1 = column selected, 2 = pattern
    bool voice_collapsed[8] = {};        // tracker column shows only note + effect code
    int  octave = 4;
    int  preview_key = -1;               // ImGuiKey held for the sounding preview note, -1 = none
    int  pending_action = -1;            // pattern-scope action requested by a menu / the palette
    int  insert_fx_op = -1;              // command opcode the Effects panel asked to insert at the cursor
    int  value_dialog = -1;              // A_FADE / A_SCALE / A_RANDOMIZE popup to open, -1 = none

    int   seq_tab = 0;                   // 0 = tracker grid, 1 = piano roll, 2 = arrangement
    bool  arrangement_ack = false;       // the experimental notice was dismissed this run
    int   roll_ins = -1;                 // instrument for placed notes, -1 = whatever the track has there
    int   roll_len_rows = 1;             // length of a placed note in rows
    float roll_zoom = 1.0f;              // horizontal zoom (0.05 .. 32)
    float roll_zoom_y = 1.0f;            // vertical zoom: semitone row height (0.4 .. 4)
    float roll_anchor_tick = -1, roll_anchor_px = 0;
    float roll_anchor_semi = -1, roll_anchor_py = 0;
    bool  roll_ghost = true;             // show the other voices faintly
    int   roll_sel_tick = -1;            // selected bar (start tick on the cursor voice), -1 = none
    int   roll_sel_t0 = 0, roll_sel_t1 = 0;   // tick range selection (t1 > t0 = active), from a right-drag
    int   roll_snap = 1;                 // snap: 0 = off, 1 = row, 2 = half row, 4 = quarter row
    int   roll_fx_view = -1;             // fx lane: -1 = every command as tags, else this opcode as value bars
    int   roll_centre_frames = 3;        // frames left to centre the vertical scroll on C-4
    int   fitted_song = -1;              // song index the grid was last fitted for (drivers that switch songs after load)
    float voice_head[8] = {-1, -1, -1, -1, -1, -1, -1, -1};

    struct Heard { uint8_t regs[8]; int semitone; };
    std::unordered_map<uint32_t, Heard> heard;
    static uint32_t heard_key(int voice, uint16_t addr) { return uint32_t(voice) << 16 | addr; }
    bool preview_regs_heard(const seq::Track& track, int voice, int ev_index, int tick, int semitone, int ins, uint8_t regs[8]) const;

    struct MutedNote { int semitone, dur, ins; };
    std::unordered_map<uint32_t, MutedNote> muted;
    static uint32_t muted_key(int voice, int tick) { return uint32_t(voice) << 20 | uint32_t(tick & 0xFFFFF); }

    int  sel_instrument = -1;   // Instruments panel row; -1 = none, a typed note keeps the track's
    int  sel_sample = 0;

    char export_path[1024] = {};
    int  export_seconds = 0;
    bool export_wav_open = false, export_spc_open = false;
    bool import_wav_open = false;
    char import_path[1024] = {};
    int  import_rate = 0;
    int  import_loop = -1;

    void open_file(const std::string& path);
    void open_any(const std::string& path);
    void open_snsf(const std::string& path);
    bool open_project(const std::string& path);
    bool save_project_to(const std::string& path);
    void update_title();
    void dialog_open();
    void dialog_save_project(bool always_ask);
    void dialog_export_spc();
    void after_edit();
    double song_bpm() const;
    std::string fit_grid();
    // Outcome of the last play_from seek, filled by the audio thread.
    struct SeekResult { bool found = false; int order = 0, row = 0; };
    std::shared_ptr<SeekResult> seek_result;
    void play_from(int order, int tick);
    bool   set_song_bpm(double bpm);
};

void ui_draw(App& app);

void draw_player_panel(App& app);
void draw_voices_panel(App& app);
void draw_dsp_panel(App& app);
void draw_memory_panel(App& app);
void draw_sequencer_panel(App& app);
bool draw_piano_roll(App& app, int pat_idx, float head_tick);
void draw_arrangement(App& app, int pat_idx, float head_tick);
int  ruler_label_step(int rows, float px_per_row, float char_w);
int  preview_voice(const App& app);
void draw_instruments_panel(App& app);
void draw_effects_panel(App& app);
void draw_event_panel(App& app);
void draw_samples_panel(App& app);
void draw_disasm_panel(App& app);
void draw_export_dialogs(App& app);
void draw_status_bar(App& app);
void draw_about_window(App& app);
void draw_updates_panel(App& app);
void draw_welcome_banner();
void logo_load(void* sdl_renderer, const std::string& base_path);
void logo_refresh();
void draw_command_palette(App& app);
void run_action(App& app, int action);
