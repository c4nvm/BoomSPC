// Rare's SNES sound driver: the Donkey Kong Country build and the later
// DKC2/DKC3 build (Phillip Wattis), which keeps the engine but renumbers
// its state, extends the pitch table three octaves down (note bytes gain a
// fixed offset so the same byte still plays the same pitch), drops the
// variant jump and adds a call-once, note variables (E0/E1 play the note
// last set by 1C/1D) and global volume pairs. Killer Instinct is the DKC2
// engine with a trimmed command set (call-once is 1F, 20/21 are ADSR
// presets, 22 is instrument + transpose + detune) and a song table of
// header pointers picked by $055C; the header carries the tempo after its
// eight stream pointers. Battletoads is not fingerprinted yet. See
// docs/ROADMAP.md.
//
// No order list: the SNES uploads a song block whose header is eight stream
// pointers and writes them straight into the driver's zero page ($4C+v low
// bytes, $5C+v high bytes). Each stream is a program:
//   80          rest (key off)      81..FF  note (n - $80 + transpose) into a
//                                           61-entry pitch table
//   after a note: one duration byte, or two (high, low) after command 2B,
//   or none while a fixed duration (06) is active. A duration of 0 is 1 tick.
//   00 end       03 lo hi  jump      04 n lo hi  call n times   05 return
//   06 d[d2]     fixed duration (two bytes in two-byte mode)   07 back to explicit
//   01 i         instrument (through the sample map)           28 i l r  instrument + volume
//   13 n / 14 n  transpose set / add     12 n detune            0B t / 0C t  tempo set / add
//   2B / 2C      two-byte durations on / off   2D  jump through a table picked by $ED
//   the rest: volume, envelope presets, vibrato, echo, FIR, noise, pitch mod
// Ticks: timer 0 at 8000/$EC Hz, each tick adding $27/256 to a sequencer
// accumulator, so the sequence runs at (8000/$EC) * $27/256 ticks per second.
//
// Voices 8-15 are sound effects on the same engine (table at $2380).
#pragma once

#include <cstdint>
#include <memory>

#include "seq.hpp"

namespace rare {
enum class Variant { Unknown, DKC1, DKC2, KI };

struct CmdSpec { uint8_t size; const char* code; const char* name; seq::FxClass cls; };

struct Layout {
    Variant  variant = Variant::Unknown;
    uint16_t cmd_table = 0;       // jump table, 2 bytes per opcode
    const CmdSpec* cmds = nullptr;
    int      cmd_count = 0;
    uint8_t  note_offset = 0;     // added to the note byte before the pitch table (DKC2: $24)
    uint8_t  note_var_base = 0;   // zero page of the E0 note variable (E1 is +8), 0 = none
    uint8_t  call_once = 0;       // opcode of the 3-byte call (DKC2: $21, KI: $1F), 0 = none
    uint16_t stack_lo = 0x0334;   // call stack: return address low bytes, v*8 + depth
    uint16_t stack_hi = 0x03B4;   // ...high bytes
    uint8_t  header_size = 16;    // song header bytes (KI: + tempo, + $23)
    uint8_t  ptr_lo = 0x4C;       // zero page: per-voice stream pointer low bytes
    uint8_t  ptr_hi = 0x5C;       // ...high bytes
    uint8_t  tempo_addr = 0x27;   // sequencer add per timer tick (0..255 / 256)
    uint8_t  timer_addr = 0xEC;   // timer 0 latch the driver copies to $FA
    uint8_t  variant_addr = 0xED; // index used by command 2D
    uint16_t pitch_table = 0;     // 61 words, index 0 unused
    uint16_t sample_map = 0;      // instrument number -> sample (SRCN)
    uint16_t transpose_base = 0;  // per-voice transpose ($0140)
    uint16_t sfx_table = 0;       // sound effect pointers (voices 8-15)
    uint16_t song_table = 0;      // DKC2/3: header pointers per song number, 0 = scan for headers
    uint16_t song_num_addr = 0;   // DKC2/3: the song number the SNES requested
    bool valid() const { return variant != Variant::Unknown; }
};

Layout detect_layout(const uint8_t* ram);
Layout layout_for_tests(Variant variant);

class RareDriver : public seq::Driver {
public:
    explicit RareDriver(Layout layout) : L(layout) {}
    Layout L;

    std::string name() const override {
        return L.variant == Variant::KI ? "Rare (Killer Instinct)" : L.variant == Variant::DKC2 ? "Rare (Donkey Kong Country 2/3)" : "Rare (Donkey Kong Country)";
    }
    const char* id() const override { return "rare"; }
    uint8_t rest_byte() const override { return 0x80; }
    uint8_t tie_byte() const override { return 0; }
    bool    is_note_byte(uint8_t b) const override { return b > 0x80; }
    int     note_semitone(uint8_t b) const override { return int(b & 0x7F) + 11; }
    uint8_t note_byte(int semitone_from_c0) const override;
    uint8_t note_min() const override { return 0x81; }
    uint8_t note_max() const override { return 0xBC; }
    bool    transpose_event(seq::Event& e, int semis) const override;
    bool        is_command(uint8_t b) const override { return b < 0x80; }
    int         cmd_size(uint8_t op) const override;
    const char* cmd_name(uint8_t op) const override;
    const char* cmd_code(uint8_t op) const override;
    seq::FxClass cmd_class(uint8_t op) const override;
    bool        is_instrument_cmd(uint8_t op) const override { return op == 0x01 || op == (L.variant == Variant::DKC1 ? 0x28 : 0x22); }
    uint8_t     instrument_opcode() const override { return 0x01; }
    uint8_t     first_command() const override { return 0x00; }
    int         command_count() const override { return L.cmd_count; }
    std::string event_text(const seq::Event& e) const override;
    bool       has_instruments() const override { return L.sample_map != 0; }
    int        instrument_count(const uint8_t* ram) const override;
    seq::Instrument read_instrument(const uint8_t* ram, int index) const override;
    bool       preview_regs(const uint8_t* ram, uint8_t note_byte, int instrument, uint8_t regs[8]) const override;
    std::vector<seq::Song> find_songs(const uint8_t* ram, const uint8_t* dsp) const override;
    int  pick_current_song(const uint8_t* ram, const std::vector<seq::Song>& songs) const override;
    seq::Position locate(const uint8_t* ram, const seq::Song& song, const seq::Position* prev) const override;
    uint16_t live_ptr(const uint8_t* ram, const seq::Position& pos, int v) const override;
    int      end_park_offset() const override { return 0; }
    double ticks_per_second(const uint8_t* ram) const override;
    int    default_ticks_per_beat() const override { return 16; }
    bool   tempo_writes(const uint8_t* ram, double tps, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    std::vector<uint8_t> serialize_track(const std::vector<seq::Event>& events) const override;
    void retime(std::vector<seq::Event>& ev) const override;
    bool in_place_only() const override { return true; }
    bool has_stream_edit() const override { return true; }
    bool end_bytes(uint8_t out[16], int& size) const override { out[0] = 0x00; size = 1; return true; }
    bool set_duration(std::vector<seq::Event>& ev, int i, int dur) const override;
    void apply_note_byte(seq::Event& e, uint8_t byte) const override;
    bool is_frame_command(const seq::Event& e) const override { return e.type == seq::EventType::Command && (e.b[0] == 0x04 || (L.call_once && e.b[0] == L.call_once)); }
    int  call_count(const seq::Event& e) const override { return e.type != seq::EventType::Command ? 0 : e.b[0] == 0x04 ? (e.b[1] ? e.b[1] : 256) : (L.call_once && e.b[0] == L.call_once) ? 1 : 0; }
    void set_call_count(seq::Event& e, int n) const override { if (e.type == seq::EventType::Command && e.b[0] == 0x04) e.b[1] = uint8_t(n); }
    int  jump_target(const seq::Event& e) const override { return e.type == seq::EventType::Command && e.b[0] == 0x03 ? (e.b[1] | (e.b[2] << 8)) : -1; }
    void set_jump_target(seq::Event& e, uint16_t addr) const override { e.b[1] = uint8_t(addr & 0xFF); e.b[2] = uint8_t(addr >> 8); }
    void track_pointer_writes(const seq::Song& song, int pattern_idx, int voice, uint16_t dest, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    void live_state_writes(const uint8_t* ram, const seq::Position& pos, int voice, uint16_t ptr, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    bool remaps_stack() const override { return true; }
    void reclaimable_ranges(const uint8_t* ram, std::vector<std::pair<uint16_t, uint16_t>>& out) const override;
    bool set_note_at(std::vector<seq::Event>& ev, int tick, uint8_t note_byte, int pattern_len) const override;
    bool enter_note(std::vector<seq::Event>& ev, int tick, int semitone, int pattern_len) const override;
    bool insert_command_at(std::vector<seq::Event>& ev, int tick, const uint8_t* bytes, int size) const override;
    bool set_instrument(std::vector<seq::Event>& ev, int tick0, int tick1, uint8_t ins) const override;
    bool remove_span(std::vector<seq::Event>& ev, int tick, int ticks, bool keep_length) const override;
    bool insert_span(std::vector<seq::Event>& ev, int tick, int ticks, uint8_t byte) const override;

    seq::Track parse_track(const uint8_t* ram, uint16_t start, int variant, int voice = -1, int budget = 60000) const;
    bool parse_header(const uint8_t* ram, uint16_t header, int variant, seq::Pattern& out, int budget = 60000) const;
};

std::unique_ptr<seq::Driver> detect(const uint8_t* ram);

}
