// Hudson's "SFX SOUND DRIVER" (Super Bomberman 2-5, Tengai Makyou Zero,
// Hagane, Do-Re-Mi Fantasy, Super Genjin 2...). Format after VGMTrans'
// HudsonSnes; layout read from the driver code in ARAM.
//
// A song starts with a header: track pointers (a bit mask of the voices
// used, then one word per set bit) and tagged blocks (timebase, instrument
// and percussion tables, echo, note velocity flag) ending with 00. In the
// v2 engine the track pointers are block 01. Streams:
//   00-CF       note: high nibble = key (0 = rest, 1..12 = C..B in the
//               current octave), bit 3 holds the note into the next one,
//               low 3 bits = length index (C0 60 30 18 0C 06 03 01 from
//               index 1, shifted by the timebase; 0 = an explicit length
//               byte follows); a velocity byte follows when the song's
//               header enables it
//   D1 t  tempo (BPM)   D2 o  octave   D3 / D4  octave up / down
//   D5 q  gate   D6 i  instrument   D9 v  volume   DA p  pan
//   DD n ... DE   repeat n times    DF addr   call    E0 addr   jump
//   E7 t / E8 t   transpose absolute / relative
//   EB  loop point   EC  jump to the loop point   ED  loop point (first pass only)
//   v2: EF i d  pitch envelope table i after d ticks   F2 d  vibrato delay
//   (0 = off)   F3 s  signed volume slide per tick   F0 n  never read
//   FE xx ...  sub-commands (echo off, percussion on/off, vibrato type,
//              v2: registers, compares and conditional branches)
//   FF  end of track / return
// Repeats and calls share a per-voice stack in RAM. A quarter note is
// 48 >> timebase ticks; ticks run at tempo * (48 >> timebase) / 60 per second.
#pragma once

#include <map>

#include "stream.hpp"

namespace hudson {
struct Layout {
    int      version = -1;         // 0, 1, 2 as in VGMTrans
    uint16_t song_list = 0;        // words: song header addresses
    uint16_t ptr_lo = 0, ptr_hi = 0;       // live pointer byte arrays (+voice)
    uint16_t loop_lo = 0, loop_hi = 0;     // loop point byte arrays (+voice)
    uint16_t stack_lo = 0, stack_hi = 0;   // per-voice call stack base tables (+voice)
    uint16_t stack_sp = 0;         // per-voice stack depth (+voice)
    uint16_t tempo_addr = 0;
    uint16_t cmd_table = 0;
    bool valid() const { return version >= 0 && song_list && ptr_lo; }
};

Layout detect_layout(const uint8_t* ram);

// What a song header says about its streams.
struct SongInfo {
    uint16_t tracks[8] = {};
    int      shift = 2;            // timebase
    bool     velocity = false;     // notes carry a velocity byte
    uint16_t ins_table = 0;        // 4 bytes per instrument: SRCN ADSR1 ADSR2 GAIN
    int      ins_count = 0;
    uint16_t slots[8] = {};        // where each voice's pointer word sits in the header
    uint16_t end = 0;              // first byte after the header
};

class HudsonDriver : public stream::Driver {
public:
    explicit HudsonDriver(Layout layout) : L(layout) {}
    Layout L;

    std::string name() const override;
    const char* id() const override { return "hudson"; }
    uint8_t rest_byte() const override { return 0x00; }
    uint8_t tie_byte() const override { return 0; }
    bool    is_note_byte(uint8_t b) const override { return b < 0xD0 && (b >> 4) != 0; }
    int     note_semitone(uint8_t b) const override { return 36 + (b >> 4) - 1; }
    uint8_t note_byte(int semitone_from_c0) const override { return uint8_t((((semitone_from_c0 % 12) + 12) % 12 + 1) << 4); }
    uint8_t note_min() const override { return 0x10; }
    uint8_t note_max() const override { return 0xC0; }
    bool    transpose_event(seq::Event& e, int semis) const override;
    bool        is_command(uint8_t b) const override { return b >= 0xD0; }
    bool        is_instrument_cmd(uint8_t op) const override { return op == 0xD6; }
    uint8_t     instrument_opcode() const override { return 0xD6; }
    uint8_t     first_command() const override { return 0xD0; }
    int         command_count() const override { return 0x30; }
    std::string event_text(const seq::Event& e) const override;
    bool pitch_fx(const seq::Event& e, seq::PitchFx& out) const override;
    bool note_retriggers(const std::vector<seq::Event>& ev, int i) const override;
    bool       has_instruments() const override { return true; }
    int        instrument_count(const uint8_t* ram) const override;
    seq::Instrument read_instrument(const uint8_t* ram, int index) const override;
    bool       preview_regs(const uint8_t* ram, uint8_t note_byte, int instrument, uint8_t regs[8]) const override;
    double ticks_per_second(const uint8_t* ram) const override;
    int    default_ticks_per_beat() const override { return 48 >> shift_; }
    bool   tempo_writes(const uint8_t* ram, double tps, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    bool end_bytes(uint8_t out[16], int& size) const override { out[0] = 0xFF; size = 1; return true; }
    bool set_duration(std::vector<seq::Event>& ev, int i, int dur) const override;
    void apply_note_byte(seq::Event& e, uint8_t byte) const override;
    int  call_count(const seq::Event& e) const override { return e.type == seq::EventType::Command && e.b[0] == 0xDF ? 1 : 0; }
    uint16_t live_ptr(const uint8_t* ram, const seq::Position& pos, int v) const override;
    void live_state_writes(const uint8_t* ram, const seq::Position& pos, int voice, uint16_t ptr, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    void track_pointer_writes(const seq::Song& song, int pattern_idx, int voice, uint16_t dest, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    void free_space_bounds(int& lo, int& hi) const override { lo = data_lo_; hi = 0x10000; }

    void decode(const uint8_t* p, int pc, stream::State& s, seq::Event& e, stream::Flow& f) const override;
    const stream::CmdSpec& spec(uint8_t op) const override;
    uint16_t track_start(const uint8_t* ram, uint16_t header, int v) const override;
    std::vector<uint16_t> song_headers(const uint8_t* ram) const override;
    uint16_t live_ptr_addr(int v) const override { return uint16_t(L.ptr_lo + v); }
    int  ptr_span() const override { return 16; }
    stream::State initial_state(const uint8_t* ram, uint16_t header, int voice) const override;
    bool enter_note(std::vector<seq::Event>& ev, int tick, int semitone, int pattern_len) const override;
    std::string song_label(const uint8_t* ram, uint16_t header, const seq::Pattern& p, int index) const override;
    bool ptr_after_note() const override { return true; }

    bool parse_song_header(const uint8_t* ram, uint16_t addr, SongInfo& info) const;
    int  note_ticks(uint8_t byte, int shift) const;

private:
    mutable int shift_ = 2;        // timebase of the song being parsed
    mutable bool velocity_ = false;
    mutable uint16_t ins_table_ = 0;
    mutable int ins_count_ = 0;
    mutable int data_lo_ = 0x200;
    mutable std::map<uint16_t, SongInfo> infos_;   // parsed headers by address
};

std::unique_ptr<seq::Driver> detect(const uint8_t* ram);

}
