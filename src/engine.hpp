// Playback engine: owns the SPC700/S-DSP core and feeds SDL audio.
//
// Threading: the SDL audio thread renders in sdl_callback(); the UI thread
// calls the control methods. Every touch of `spc`/`filter` happens under
// `mtx`. snapshot() copies the emulator's visible state out under that lock
// so the UI never reads emulator memory directly.
#pragma once

#include <SDL.h>

#include <atomic>
#include <climits>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "SNES_SPC.h"
#include "SPC_Filter.h"
#include "snes.hpp"
#include "snsf.hpp"
#include "spc_file.hpp"

struct EngineSnapshot {
    uint8_t dsp[SPC_DSP::register_count]{};
    uint8_t ram[0x10000]{};
    struct { int pc = 0, a = 0, x = 0, y = 0, sp = 0, psw = 0; } cpu;
    int64_t sample_pairs = 0;   // pairs rendered since load/restart
    bool loaded = false;
    // SNSF sets: the live cartridge image (the driver parses from it) and
    // the ROM bank the synthesised `ram` window shows at $8000-$FFFF.
    std::shared_ptr<const std::vector<uint8_t>> snes_rom;
    int snes_bank = -1;
};

class Engine {
public:
    static constexpr int kSampleRate = SNES_SPC::sample_rate;  // 32000

    Engine() = default;
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Opens the audio device. Returns an error string on failure.
    std::string init();
    void shutdown();

    // Loads an SPC and leaves the engine paused at sample 0.
    std::string load(const SpcFile& file);
    // Loads an SNSF set: the SNES CPU runs the game's sound engine and
    // drives the SPC through the APU ports. Restart re-runs it from reset.
    std::string load_snsf(const SnsfFile& file);
    bool snsf() const { return snsf_ != nullptr; }
    // SNSF address window: the ROM bank shown at $8000-$FFFF of the
    // synthesised RAM (see snsf_read); set from the current song's bank.
    void set_bank_window(int bank);
    int  bank_window() const { return bank_window_; }
    // Writes the edited set as a self-contained .snsf (the patched image).
    std::string export_snsf(const std::string& path);
    // Runs the machine for `seconds` of song time without output (tests, tools).
    void run_silent(double seconds);
    bool loaded() const { return loaded_; }
    const SpcFile& file() const { return file_; }

    void play();
    void pause();
    void toggle();
    void restart();          // reload the current SPC from its snapshot
    // Start playing from a point in the song: the driver is run silently
    // (many times real time, in the audio thread) until `reached` says the
    // song is there, then playback goes on audibly. `reached` sees the RAM
    // and the song-time sample count after every 2 ms slice; it runs on
    // the audio thread. `from_start` reloads first. `limit_seconds` of song
    // time is the give-up point.
    void seek(std::function<bool(const uint8_t* ram, int64_t sample)> reached, bool from_start, double limit_seconds);
    bool seeking() const { return seeking_; }
    void cancel_seek();
    bool playing() const { return playing_; }

    // Voice mute mask, bit n = voice n.
    void set_mute_mask(int mask);
    int  mute_mask() const { return mute_mask_; }

    // 0x100 = normal speed.
    void set_tempo(int tempo);
    int  tempo() const { return tempo_; }

    // Output shaping. gain: 0x100 = unity. bass: SPC_Filter::bass_none..bass_max.
    void set_filter(bool enabled, int gain, int bass);
    bool filter_enabled() const { return filter_enabled_; }
    int  gain() const { return gain_; }
    int  bass() const { return bass_; }

    // Many dumps have garbage in the echo buffer; clearing it on load is the
    // usual player behaviour.
    void set_clear_echo_on_load(bool v) { clear_echo_on_load_ = v; }
    bool clear_echo_on_load() const { return clear_echo_on_load_; }

    // When the tagged length runs out: loop (restart) or stop.
    void set_loop(bool v) { loop_ = v; }
    bool loop() const { return loop_; }

    // Note preview, Furnace style: keys `voice` on with the given voice
    // registers (VOLL VOLR PITCHL PITCHH SRCN ADSR1 ADSR2 GAIN), bypassing the
    // driver. While paused the DSP alone is clocked so the note still sounds.
    void preview_on(int voice, const uint8_t regs[8]);
    void preview_off();
    int  preview_voice() const { return preview_voice_; }

    // Metronome: a noise click mixed into the output on every highlighted
    // row, louder on the strong highlight. The UI feeds the row clock it
    // derives from the driver's live position (row `ref_row` starts at
    // output sample `ref_samples`, rows are `samples_per_row` long), so the
    // clicks sit exactly where the grid claims the rows are.
    struct Metronome {
        bool    on = false;
        double  ref_row = 0;
        int64_t ref_samples = 0;
        double  samples_per_row = 0;
        int     hi1 = 4, hi2 = 16;
        float   volume = 1.0f;
    };
    void set_metronome(const Metronome& m);

    // Pointer watch. The audio thread renders in 2 ms slices while a watch is
    // set and records the output sample at which the watched bytes (the
    // driver's live track pointers) change. Snapshots only see the state at
    // the end of a 32 ms buffer; this places events to within a slice.
    // `regs` are the eight voices' DSP registers (VOLL VOLR PITCHL PITCHH
    // SRCN ADSR1 ADSR2 GAIN) as the change was confirmed: how the note that
    // just started actually sounds, effects included.
    struct WatchEvent { int64_t sample; uint8_t bytes[32]; uint8_t regs[8][8]; };
    void set_watch(uint16_t addr, int len);        // len 0..32, 0 = off
    void take_watch(std::vector<WatchEvent>& out);  // appends and drains, oldest first

    // Debug aid: every rendered stereo frame is also appended here (mono mix).
    void set_capture(std::vector<int16_t>* buf) { std::lock_guard<std::mutex> lock(mtx_); capture_ = buf; }

    double position_seconds() const { return double(sample_pairs_) / kSampleRate; }
    std::string last_cpu_error() const;

    void snapshot(EngineSnapshot& out);

    // Editing. Writes go to the live emulator RAM and to the file image, so
    // restart/export keep them. Every write is undoable; writes made between
    // begin_edit()/end_edit() undo as one step (a relocated track is two
    // writes: the bytes and the pattern's pointer).
    // Where a write goes: the running emulator and the image that restart
    // and export use (the default), or one of them. A voice pointer moved
    // to a rewritten stream is live state: the emulator's pointer and the
    // image's (where the rip was dumped) are remapped separately.
    enum WriteTarget { kBoth, kLiveOnly, kImageOnly };
    void write_ram(uint16_t addr, const uint8_t* data, size_t n, WriteTarget target = kBoth);
    // The image's RAM (the dump plus every edit), for computing image-side writes.
    const uint8_t* image_ram();
    void begin_edit();
    void end_edit();
    bool undo();
    bool redo();
    // Undoes the last step and forgets it (a block that could not be finished).
    void revert_edit();
    bool can_undo() const { return !undo_stack_.empty(); }
    bool can_redo() const { return !redo_stack_.empty(); }
    bool dirty() const { return dirty_; }

    // Writes the (edited) file image. The header, tags and initial state are
    // the ones from the loaded file.
    std::string export_spc(const std::string& path);
    // The edited file image (what export_spc would write).
    std::vector<uint8_t> file_image();
    void mark_clean() { dirty_ = false; }
    // Renders the edited file image to a 16-bit stereo WAV.
    std::string export_wav(const std::string& path, double seconds);

private:
    static void sdl_callback(void* userdata, Uint8* stream, int len);
    void render(int16_t* out, int frames);
    void apply_settings_locked();
    void reload_locked();

    std::mutex mtx_;
    SNES_SPC   spc_;
    SPC_Filter filter_;
    SpcFile    file_;
    std::unique_ptr<Snes> snes_;       // SNSF mode: the SNES side, driving spc_
    std::unique_ptr<SnsfFile> snsf_;   // ...and the set it plays (its rom = the image edits are kept in)
    int bank_window_ = 0;
    std::vector<uint8_t> image_view_;  // image_ram() in SNSF mode
    // Runs the machine for `frames`: the SPC alone, or the SNES with it.
    blargg_err_t run_locked(int frames, int16_t* out);
    // The SNSF address space the editor sees: $0000-$0FFF WRAM $7E:0000..,
    // $1000-$1FFF WRAM $7E:F000.. (voice state), $8000-$FFFF the bank window.
    // `live`: the running machine; else the image restart/export use.
    uint8_t  snsf_read(uint32_t addr, bool live) const;
    void     snsf_write(uint32_t addr, uint8_t v, WriteTarget target);
    void     live_bytes(uint16_t addr, int n, uint8_t* out);   // n bytes of live RAM / window

    SDL_AudioDeviceID dev_ = 0;
    std::atomic<bool>    playing_{false};
    std::atomic<bool>    seeking_{false};
    std::function<bool(const uint8_t*, int64_t)> seek_reached_;
    int64_t              seek_limit_ = 0;    // sample count to give up at
    std::atomic<bool>    loaded_{false};
    std::atomic<int64_t> sample_pairs_{0};
    // When the last audio callback finished and where the counter stood, so
    // the UI can interpolate between callbacks (some audio backends deliver
    // them in bursts, which would make the playhead stutter).
    std::atomic<int64_t> cb_time_us_{0};
    std::atomic<int64_t> cb_samples_{0};
    std::atomic<int>     cb_frames_{0};

    int  mute_mask_ = 0;
    int  tempo_     = SNES_SPC::tempo_unit;
    bool filter_enabled_ = true;
    int  gain_ = SPC_Filter::gain_unit;
    int  bass_ = SPC_Filter::bass_norm;
    bool clear_echo_on_load_ = true;
    bool loop_ = true;
    std::string cpu_error_;
    int  preview_voice_ = -1;     // voice keyed on by preview_on(), -1 = none
    int  preview_tail_  = 0;      // samples left to clock the DSP while paused (release)
    bool koff_pending_  = false;  // clear KOFF once the DSP has seen it
    std::vector<int16_t>* capture_ = nullptr;

    Metronome met_;
    int64_t met_last_row_ = INT64_MIN;
    int     met_left_ = 0;        // click samples still to mix
    float   met_gain_ = 0;        // click amplitude (decays)
    uint32_t met_rng_ = 0x9E3779B9u;
    void mix_metronome_locked(int16_t* out, int frames, int64_t first_sample);

    uint16_t watch_addr_ = 0;
    int      watch_len_ = 0;
    uint8_t  watch_last_[32] = {};
    WatchEvent watch_pending_{};  // a change seen once; reported when the next slice confirms it
    bool     watch_has_pending_ = false;
    std::vector<WatchEvent> watch_events_;
    void watch_check_locked(int64_t sample);
    bool seek_step_locked();

    struct Patch { uint16_t addr; std::vector<uint8_t> before, after; WriteTarget target = kBoth; };
    struct UndoEntry { std::vector<Patch> patches; };
    std::vector<UndoEntry> undo_stack_, redo_stack_;
    int  edit_depth_ = 0;         // nesting of begin_edit()
    bool undo_open_ = false;      // the top undo entry still collects writes
    bool dirty_ = false;
    void apply_locked(const Patch& p, bool forward);
};
