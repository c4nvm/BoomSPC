// Capcom's SNES sound driver (the "Capcom v2" engine of Mega Man X/X2/X3,
// Mega Man 7, the Street Fighter II family, Breath of Fire, Final Fight
// and more). Reverse-engineered from the Mega Man X rips and cross-checked
// with loveemu's capspc; see docs/ROADMAP.md.
//
// No order list. A song header is eight big-endian stream pointers in
// reverse voice order (word 0 = voice 7); the driver copies them into its
// zero page ($00+v low bytes, $08+v high bytes). Each stream is a program:
//   20..FF      note: bits 5-7 pick the duration (index 0-6 in a table of
//               3 6 12 24 48 96 192 ticks; a pending "dotted" flag uses the
//               1.5x table, the triplet flag the 2/3 table), bits 0-4 the
//               key (0 = rest, 1..31 = semitones above the octave base)
//   00 / 01     toggle triplet / slur (a slurred note sounds its full length
//               and the note after it does not retrigger)
//   02          dotted (next note only)      03  toggle two octaves up
//   04 f        set the octave-up / triplet / slur bits directly
//   05 hh ll    tempo (16-bit, subtracted from the note timer every frame)
//   06 r        duration rate (n/256)       07 v  volume     08 i  instrument
//   09 o        octave (0-7, +8 = two up)   0A t / 0B t  global / voice transpose
//   0C t        tuning                      0D t  portamento time
//   0E..11 n hh ll   loop slot 0-3: first pass sets the counter to n and jumps
//               back; later passes count down, so the target plays n+1
//               times (n = 0: forever)
//   12..15 f hh ll   loop break: on the slot's last pass, set the note bits
//               to f and jump to hh ll
//   16 hh ll    jump                        17  end of voice
//   18 pan  19 master volume  1A t v  LFO  1B a b  echo  1C on/off  1D release
// Ticks: every other timer-0 frame the 16-bit note counter drops by the
// tempo, and a note adds its length to the counter's high byte, so the
// sequence runs at (8000 / latch) / 2 * tempo / 256 ticks per second (48
// per quarter).
//
// Editing: durations outside the tables are written as tied notes (slur on
// for every piece but the last).
#pragma once

#include <cstdint>
#include <memory>

#include "seq.hpp"

namespace capcom {
struct Layout {
    uint8_t  ptr_lo = 0x00;        // zero page: per-voice stream pointer low bytes
    uint8_t  ptr_hi = 0x08;        // ...high bytes
    uint8_t  ptr_stride = 1;       // 1 (v2/v3) or 2 (v1: interleaved pairs)
    uint8_t  ctl_zp = 0x10;        // per-voice note control bits
    uint8_t  gtrans_zp = 0xD1;     // global transpose (command 0A)
    uint8_t  tempo_zp = 0xCC;      // 16-bit tempo, low byte first
    uint16_t cmd_table = 0;        // handler addresses, 2 bytes per opcode
    uint16_t dur_normal = 0, dur_dotted = 0, dur_triplet = 0;   // 7 bytes each
    uint16_t octave_table = 0;     // 16 bytes: key offset per control nibble
    uint16_t pitch_table = 0;      // 13 little-endian words for one octave
    uint16_t ins_table = 0;        // 6 bytes per instrument: SRCN ADSR1 ADSR2 GAIN mult(8.8)
    uint16_t bgm_header = 0;       // fixed header (Mega Man X style), else 0
    uint16_t song_list = 0;        // list of header pointers (priority byte first), else 0
    bool valid() const { return cmd_table != 0 && (bgm_header || song_list); }
};

Layout detect_layout(const uint8_t* ram);
Layout layout_for_tests();

enum : uint8_t { kOctaveUp = 0x08, kDotted = 0x10, kTriplet = 0x20, kSlur = 0x40 };

struct State {
    uint8_t ctl = 0;               // note control bits
    int gtrans = 0, vtrans = 0;    // transposes from commands
    int base = 0;                  // transpose in force before the stream (live $D1)
};

class CapcomDriver : public seq::Driver {
public:
    explicit CapcomDriver(Layout layout) : L(layout) {}
    Layout L;

    std::string name() const override { return "Capcom"; }
    const char* id() const override { return "capcom"; }
    uint8_t rest_byte() const override { return 0x20; }
    uint8_t tie_byte() const override { return 0; }
    bool    is_note_byte(uint8_t b) const override { return b >= 0x20 && (b & 0x1F) != 0; }
    int     note_semitone(uint8_t b) const override { return 48 + int(b & 0x1F) - 1; }
    uint8_t note_byte(int semitone_from_c0) const override { return uint8_t(0x20 | (((semitone_from_c0 % 12) + 12) % 12 + 1)); }
    uint8_t note_min() const override { return 0x21; }
    uint8_t note_max() const override { return 0x3F; }
    bool    transpose_event(seq::Event& e, int semis) const override;
    bool        is_command(uint8_t b) const override { return b < 0x20; }
    int         cmd_size(uint8_t op) const override;
    const char* cmd_name(uint8_t op) const override;
    const char* cmd_code(uint8_t op) const override;
    seq::FxClass cmd_class(uint8_t op) const override;
    bool        is_instrument_cmd(uint8_t op) const override { return op == 0x08; }
    uint8_t     instrument_opcode() const override { return 0x08; }
    uint8_t     first_command() const override { return 0x00; }
    int         command_count() const override { return 0x20; }
    std::string event_text(const seq::Event& e) const override;
    bool pitch_fx(const seq::Event& e, seq::PitchFx& out) const override;
    bool note_retriggers(const std::vector<seq::Event>& ev, int i) const override;
    bool       has_instruments() const override { return L.ins_table != 0; }
    int        instrument_count(const uint8_t* ram) const override;
    seq::Instrument read_instrument(const uint8_t* ram, int index) const override;
    bool       preview_regs(const uint8_t* ram, uint8_t note_byte, int instrument, uint8_t regs[8]) const override;
    std::vector<seq::Song> find_songs(const uint8_t* ram, const uint8_t* dsp) const override;
    int  pick_current_song(const uint8_t* ram, const std::vector<seq::Song>& songs) const override;
    seq::Position locate(const uint8_t* ram, const seq::Song& song, const seq::Position* prev) const override;
    uint16_t live_ptr(const uint8_t* ram, const seq::Position& pos, int v) const override;
    double ticks_per_second(const uint8_t* ram) const override;
    bool   tempo_writes(const uint8_t* ram, double tps, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    std::vector<uint8_t> serialize_track(const std::vector<seq::Event>& events) const override;
    void retime(std::vector<seq::Event>& ev) const override;
    bool in_place_only() const override { return true; }
    bool has_stream_edit() const override { return true; }
    bool end_bytes(uint8_t out[16], int& size) const override { out[0] = 0x17; size = 1; return true; }
    bool set_duration(std::vector<seq::Event>& ev, int i, int dur) const override;
    void apply_note_byte(seq::Event& e, uint8_t byte) const override;
    bool is_frame_command(const seq::Event& e) const override { return e.type == seq::EventType::Command && e.b[0] >= 0x0E && e.b[0] <= 0x15; }
    int  jump_target(const seq::Event& e) const override;
    void set_jump_target(seq::Event& e, uint16_t addr) const override;
    void track_pointer_writes(const seq::Song& song, int pattern_idx, int voice, uint16_t dest, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    void live_state_writes(const uint8_t* ram, const seq::Position& pos, int voice, uint16_t ptr, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    bool remaps_stack() const override { return true; }
    bool set_note_at(std::vector<seq::Event>& ev, int tick, uint8_t note_byte, int pattern_len) const override;
    bool enter_note(std::vector<seq::Event>& ev, int tick, int semitone, int pattern_len) const override;
    bool insert_command_at(std::vector<seq::Event>& ev, int tick, const uint8_t* bytes, int size) const override;
    bool set_instrument(std::vector<seq::Event>& ev, int tick0, int tick1, uint8_t ins) const override;
    bool remove_span(std::vector<seq::Event>& ev, int tick, int ticks, bool keep_length) const override;
    bool insert_span(std::vector<seq::Event>& ev, int tick, int ticks, uint8_t byte) const override;

    seq::Track parse_track(const uint8_t* ram, uint16_t start, int base, int budget = 60000) const;
    bool parse_header(const uint8_t* ram, uint16_t header, int base, seq::Pattern& out, int budget = 60000) const;
    int  note_ticks(uint8_t byte, uint8_t ctl) const;
    int  note_pitch(uint8_t byte, const State& s) const;
    void apply_command(State& s, const seq::Event& e, bool jumped) const;
    int  pitch_of(const uint8_t* ram, int note, int instrument) const;
};

std::unique_ptr<seq::Driver> detect(const uint8_t* ram);

}
