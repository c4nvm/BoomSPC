// Chunsoft's SNES sound driver (Dragon Quest V, Torneko no Daibouken,
// Kamaitachi no Yoru = "winter"; Otogirisou = "summer"). Format after
// VGMTrans' ChunSnes; layout read from the driver code in ARAM.
//
// A song header is [tempo][track count][track offsets], offsets relative
// to the header (absolute in the summer build). Streams:
//   00-4F       note with the last length: 00 = rest, 4F = tie, else key + 1
//   50-9F       the same with a length byte following
//   A0-B5       (winter) gate ratio from a table
//   E0 rel16 n  jump when the song's condition variable equals n
//   EA rel16    jump (backwards = the song loop)   EB t  tempo   EC r  gate ratio
//   ED v  volume   EE p  pan   EF a b  ADSR   F0 i  instrument
//   F4 rel16    play the section again once (jump back on the first pass)
//   F5 n rel16  play the section n times      DB / DC rel16  the same, second counter
//   F8 rel16    call   F9  return   FA t  transpose   FB s l  pitch slide   FF  end / return
// Relative targets count from the byte after the operand. Ticks run at
// tempo * 48 / 60 per second.
#pragma once

#include <algorithm>

#include "stream.hpp"

namespace chun {
struct Layout {
    bool     summer = false;
    uint16_t song_list = 0;
    uint8_t  entry_size = 6, entry_header = 1;   // song list entry layout
    uint8_t  ptr_zp = 0x00;        // zero page: voice pointer words
    uint8_t  first_cmd = 0xDB;
    uint16_t cmd_table = 0;
    uint16_t slot_table = 0;       // song slot of each voice (+2 * voice)
    uint16_t tempo_base = 0;       // per-slot tempo, 0 = unknown
    uint16_t cond_base = 0;        // per-slot condition variable (E0 compares against it)
    uint16_t stack_base = 0;       // per-voice call stack (3 words), 0 = unknown
    uint16_t stack_depth = 0;      // bytes used per voice (+2 * voice)
    bool valid() const { return song_list != 0; }
};

Layout detect_layout(const uint8_t* ram);

class ChunDriver : public stream::Driver {
public:
    explicit ChunDriver(Layout layout) : L(layout) {}
    Layout L;

    std::string name() const override { return L.summer ? "Chunsoft (summer: Otogirisou)" : "Chunsoft (winter: Dragon Quest V / Torneko / Kamaitachi)"; }
    const char* id() const override { return "chun"; }
    uint8_t rest_byte() const override { return 0x00; }
    uint8_t tie_byte() const override { return 0x4F; }
    bool    is_note_byte(uint8_t b) const override { return b < 0xA0 && (b & 0x7F) != 0 && (b % 0x50) != 0 && (b % 0x50) != 0x4F; }
    int     note_semitone(uint8_t b) const override { return (b % 0x50) - 1 - 12; }
    uint8_t note_byte(int semitone_from_c0) const override { return uint8_t(std::clamp(semitone_from_c0 + 12, 0, 0x4D) + 1); }
    uint8_t note_min() const override { return 0x01; }
    uint8_t note_max() const override { return 0x4E; }
    bool    transpose_event(seq::Event& e, int semis) const override;
    bool        is_command(uint8_t b) const override { return b >= 0xA0; }
    bool        is_instrument_cmd(uint8_t op) const override { return op == 0xF0; }
    uint8_t     instrument_opcode() const override { return 0xF0; }
    uint8_t     first_command() const override { return 0xDB; }
    int         command_count() const override { return 0x25; }
    std::string event_text(const seq::Event& e) const override;
    bool pitch_fx(const seq::Event& e, seq::PitchFx& out) const override;
    bool note_retriggers(const std::vector<seq::Event>& ev, int i) const override;
    bool       has_instruments() const override { return false; }
    int        instrument_count(const uint8_t* ram) const override { (void)ram; return 0; }
    seq::Instrument read_instrument(const uint8_t* ram, int index) const override { (void)ram; (void)index; return seq::Instrument{}; }
    bool       preview_regs(const uint8_t* ram, uint8_t note_byte, int instrument, uint8_t regs[8]) const override { (void)ram; (void)note_byte; (void)instrument; (void)regs; return false; }
    double ticks_per_second(const uint8_t* ram) const override;
    bool   tempo_writes(const uint8_t* ram, double tps, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    bool end_bytes(uint8_t out[16], int& size) const override { out[0] = 0xFF; size = 1; return true; }
    bool set_duration(std::vector<seq::Event>& ev, int i, int dur) const override;
    void apply_note_byte(seq::Event& e, uint8_t byte) const override;
    int  call_count(const seq::Event& e) const override { return e.type == seq::EventType::Command && e.b[0] == 0xF8 ? 1 : 0; }
    int  jump_target(const seq::Event& e) const override;
    void set_jump_target(seq::Event& e, uint16_t addr) const override;
    std::vector<uint8_t> serialize_relocated(const std::vector<seq::Event>& ev, uint16_t dest, std::vector<int>* offsets) const override;
    void track_pointer_writes(const seq::Song& song, int pattern_idx, int voice, uint16_t dest, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    void free_space_bounds(int& lo, int& hi) const override { lo = data_lo_; hi = 0x10000; }

    void decode(const uint8_t* p, int pc, stream::State& s, seq::Event& e, stream::Flow& f) const override;
    const stream::CmdSpec& spec(uint8_t op) const override;
    uint16_t track_start(const uint8_t* ram, uint16_t header, int v) const override;
    std::vector<uint16_t> song_headers(const uint8_t* ram) const override;
    uint16_t live_ptr_addr(int v) const override { return uint16_t(L.ptr_zp + v * 2); }
    void live_extra_writes(const uint8_t* ram, int voice, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    stream::State initial_state(const uint8_t* ram, uint16_t header, int voice) const override;
    uint8_t note_byte_in(int semitone, const stream::State& s) const override { return uint8_t(std::clamp(semitone + 12 - s.trans, 0, 0x4D) + 1); }
    bool ptr_after_note() const override { return true; }

    void track_parsed(int v, const seq::Track& t) const override;
    uint16_t tempo_addr(const uint8_t* ram) const;

private:
    mutable int data_lo_ = 0x200;
    mutable std::vector<std::pair<int, int>> lens_[8];   // (tick, note length) per voice, for synced voices
};

std::unique_ptr<seq::Driver> detect(const uint8_t* ram);

}
