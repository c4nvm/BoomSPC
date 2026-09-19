// Compile's SNES sound driver (Kirby's Avalanche / Super Puyo Puyo, Jaki
// Crush, Super Aleste, Super Nazo Puyo...). Format after VGMTrans'
// CompileSnes; layout read from the driver code in ARAM.
//
// A song header is [track count] then 14 bytes per track (flags, volume,
// ..., transpose at +5, tempo at +6, stream pointer at +8). Streams:
//   00          rest   01-7F  note (key - 1, MIDI numbering)
//   C0-DD       percussion note
//   any of these may be followed by duration bytes: DE n (n ticks) or
//   DF-EE (a table: 1 2 3 4 6 8 12 16 24 32 48 9 18 30 36 42); the
//   duration bytes may also stand alone and set the length for later notes
//   80 addr     jump (backwards = the song loop)   82  end of track
//   8D n c      repeat counter n = c    81 n addr   count down, jump while not 0
//   AD n addr   count down, jump when 0
//   9A addr     call (one level)   9B  return
//   89 t  transpose (relative)   96 t  tempo   97  tuning   A0 i  instrument
//   91 f  OR into the global flags   92 m  voice mode bits (m & 3)
//   9D s  signed extra step for the note timer   99  toggles that step
// A quarter note is 12 ticks; ticks run at tempo * 60 / 256 per second.
#pragma once

#include <algorithm>
#include <map>

#include "stream.hpp"

namespace compile {
struct Layout {
    int      version = 0;          // 1 Aleste, 2 Jaki Crush, 3 Super Puyo Puyo, 4 later
    uint16_t song_list = 0;
    uint16_t field[14] = {};       // per-voice arrays the loader copies header bytes 1.. into (+voice): [7]/[8] = pointer, [5] = tempo
    bool valid() const { return song_list && field[7] && field[8]; }
};

Layout detect_layout(const uint8_t* ram);

class CompileDriver : public stream::Driver {
public:
    explicit CompileDriver(Layout layout) : L(layout) {}
    Layout L;

    std::string name() const override;
    const char* id() const override { return "compile"; }
    uint8_t rest_byte() const override { return 0x00; }
    uint8_t tie_byte() const override { return 0; }
    bool    is_note_byte(uint8_t b) const override { return b >= 1 && b < 0x80; }
    bool    is_percussion(uint8_t b) const override { return b >= 0xC0 && b <= 0xDD; }
    int     percussion_index(uint8_t b) const override { return b - 0xC0; }
    int     note_semitone(uint8_t b) const override { return b - 1 - 12; }
    uint8_t note_byte(int semitone_from_c0) const override { return uint8_t(std::clamp(semitone_from_c0 + 13, 1, 0x7F)); }
    uint8_t note_min() const override { return 0x01; }
    uint8_t note_max() const override { return 0x7F; }
    bool    transpose_event(seq::Event& e, int semis) const override;
    bool        is_command(uint8_t b) const override { return b >= 0x80 && b < 0xC0; }
    bool        is_instrument_cmd(uint8_t op) const override { return op == 0xA0; }
    uint8_t     instrument_opcode() const override { return 0xA0; }
    uint8_t     first_command() const override { return 0x80; }
    int         command_count() const override { return 0x30; }
    std::string event_text(const seq::Event& e) const override;
    bool pitch_fx(const seq::Event& e, seq::PitchFx& out) const override;
    bool       has_instruments() const override { return false; }
    int        instrument_count(const uint8_t* ram) const override { (void)ram; return 0; }
    seq::Instrument read_instrument(const uint8_t* ram, int index) const override { (void)ram; (void)index; return seq::Instrument{}; }
    bool       preview_regs(const uint8_t* ram, uint8_t note_byte, int instrument, uint8_t regs[8]) const override { (void)ram; (void)note_byte; (void)instrument; (void)regs; return false; }
    double ticks_per_second(const uint8_t* ram) const override;
    int    default_ticks_per_beat() const override { return 12; }
    bool   tempo_writes(const uint8_t* ram, double tps, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    bool end_bytes(uint8_t out[16], int& size) const override { out[0] = 0x82; size = 1; return true; }
    bool set_duration(std::vector<seq::Event>& ev, int i, int dur) const override;
    void apply_note_byte(seq::Event& e, uint8_t byte) const override;
    int  call_count(const seq::Event& e) const override { return e.type == seq::EventType::Command && e.b[0] == 0x9A ? 1 : 0; }
    bool remaps_stack() const override { return false; }
    uint16_t live_ptr(const uint8_t* ram, const seq::Position& pos, int v) const override;
    void live_state_writes(const uint8_t* ram, const seq::Position& pos, int voice, uint16_t ptr, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    void track_pointer_writes(const seq::Song& song, int pattern_idx, int voice, uint16_t dest, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    void free_space_bounds(int& lo, int& hi) const override { lo = data_lo_; hi = 0x10000; }

    void decode(const uint8_t* p, int pc, stream::State& s, seq::Event& e, stream::Flow& f) const override;
    const stream::CmdSpec& spec(uint8_t op) const override;
    uint16_t track_start(const uint8_t* ram, uint16_t header, int v) const override;
    std::vector<uint16_t> song_headers(const uint8_t* ram) const override;
    uint16_t live_ptr_addr(int v) const override { return uint16_t(L.field[7] + v); }
    stream::State initial_state(const uint8_t* ram, uint16_t header, int voice) const override;
    uint8_t note_byte_in(int semitone, const stream::State& s) const override { return uint8_t(std::clamp(semitone + 13 - s.trans, 1, 0x7F)); }
    bool ptr_after_note() const override { return true; }

    int entry_of(const uint8_t* ram, uint16_t header, int v) const;

private:
    mutable int data_lo_ = 0x200;
    mutable std::map<uint32_t, uint16_t> slots_;   // (header << 8 | voice) -> address of its pointer word
};

std::unique_ptr<seq::Driver> detect(const uint8_t* ram);

}
