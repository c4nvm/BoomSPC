// Software Creations' SNES sound driver (Mike Webb & Paul Tonge; the engine
// behind Plok, Equinox, Spider-Man & X-Men, Rock 'n Roll Racing, Uniracers,
// Ken Griffey and more). Reverse-engineered from the Plok rips, verified on
// Equinox; see docs/ROADMAP.md.
//
// There is no order list. A song slot names one byte stream per voice and the
// streams are programs:
//   00..7F      note (00 = rest), followed by a duration byte unless a default
//               duration is active (`86 n`; `87` forces one explicit duration)
//   81 lo hi    jump          82 lo hi  call         83  return
//   84 n .. 85  repeat n times (nestable, shares the call stack)
//   A3 n / A4 n jump / call to a random one of the n pointers that follow
//   A7/A8 f lo hi  jump if flag f set / clear,  A9 f  wait for flag f
//   80, BA..FF  end of voice (Equinox and earlier builds end at B8: the
//               dispatcher's `cmp a,#$xx` sets the boundary)
//   89 n        instrument = sample number (SRCN) straight from the argument;
//               the instrument tables only add a transpose and a pitch multiplier
//   everything else: envelope, volume, pan, slides, vibrato, echo, noise, tempo
// One tick is one timer-2 period; at the usual tempo byte $C8 that is 80/s.
//
// The tracker shows each slot as one pattern per voice, expanded up to the
// backward jump that loops the voice. Streams jump around and share blocks,
// so edits are patched in place: notes, durations and arguments can change,
// nothing can be inserted.
#pragma once

#include <cstdint>
#include <memory>

#include "seq.hpp"

namespace follin {
struct Layout {
    uint16_t voice_ptr_base = 0x30;   // eight 16-bit stream pointers
    uint8_t  end_first = 0xBA;        // first opcode that ends a voice (Plok $BA, Equinox $B8)
    uint16_t song_lo[8] = {}, song_hi[8] = {};   // per-voice pointer tables, indexed by slot
    int      slots = 5;
    uint16_t pitch_lo = 0, pitch_hi = 0;         // 13*n-entry pitch table split in two
    uint16_t transpose_table = 0, mult_table = 0;  // per instrument (SRCN)
    int      instrument_count = 0;
    uint16_t tempo_addr = 0xFC;                  // timer 2 period
    uint16_t slot_addr = 0xE4;                   // currently playing slot
    uint16_t stack_base = 0;                     // call/repeat frames: lo, hi, count at stack_base + index
    uint8_t  stack_ptr_zp = 0x50;                // per-voice frame index ($50 + 2*voice)
    bool valid() const { return song_lo[0] != 0; }
};

Layout detect_layout(const uint8_t* ram);

class FollinDriver : public seq::Driver {
public:
    explicit FollinDriver(Layout layout) : L(layout) {}
    Layout L;

    std::string name() const override { return L.end_first >= 0xBA ? "Software Creations (Plok build)" : "Software Creations (Equinox build)"; }
    const char* id() const override { return "follin"; }
    uint8_t rest_byte() const override { return 0; }
    uint8_t tie_byte() const override { return 0; }
    bool    is_note_byte(uint8_t b) const override { return b >= 1 && b < 0x80; }
    int     note_semitone(uint8_t b) const override { return int(b) - 2; }
    uint8_t note_byte(int semitone_from_c0) const override;
    uint8_t note_min() const override { return 1; }
    uint8_t note_max() const override { return 0x7F; }
    bool        is_command(uint8_t b) const override { return b >= 0x80; }
    int         cmd_size(uint8_t op) const override;
    const char* cmd_name(uint8_t op) const override;
    const char* cmd_code(uint8_t op) const override;
    seq::FxClass cmd_class(uint8_t op) const override;
    bool        is_instrument_cmd(uint8_t op) const override { return op == 0x89; }
    uint8_t     instrument_opcode() const override { return 0x89; }
    uint8_t     first_command() const override { return 0x80; }
    int         command_count() const override { return L.end_first - 0x80; }
    std::string event_text(const seq::Event& e) const override;
    bool pitch_fx(const seq::Event& e, seq::PitchFx& out) const override;
    bool note_retriggers(const std::vector<seq::Event>& ev, int i) const override;
    bool can_slide() const override { return true; }
    bool set_slide(std::vector<seq::Event>& ev, int tick, int dur, int from, int to, int existing) const override;
    bool is_frame_command(const seq::Event& e) const override { return e.type == seq::EventType::Command && (e.b[0] == 0x82 || e.b[0] == 0x83 || e.b[0] == 0x84 || e.b[0] == 0x85 || e.b[0] == 0xA4); }
    int  call_count(const seq::Event& e) const override { return e.type == seq::EventType::Command && (e.b[0] == 0x82 || e.b[0] == 0xA4) ? 1 : 0; }
    bool       has_instruments() const override { return L.transpose_table != 0; }
    int        instrument_count(const uint8_t* ram) const override { (void)ram; return L.instrument_count; }
    seq::Instrument read_instrument(const uint8_t* ram, int index) const override;
    bool       preview_regs(const uint8_t* ram, uint8_t note_byte, int instrument, uint8_t regs[8]) const override;
    std::vector<seq::Song> find_songs(const uint8_t* ram, const uint8_t* dsp) const override;
    int  pick_current_song(const uint8_t* ram, const std::vector<seq::Song>& songs) const override;
    seq::Position locate(const uint8_t* ram, const seq::Song& song, const seq::Position* prev) const override;
    double ticks_per_second(const uint8_t* ram) const override;
    bool   tempo_writes(const uint8_t* ram, double tps, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    std::vector<uint8_t> serialize_track(const std::vector<seq::Event>& events) const override;
    void retime(std::vector<seq::Event>& ev) const override;
    bool in_place_only() const override { return true; }
    bool has_stream_edit() const override { return true; }
    bool end_bytes(uint8_t out[16], int& size) const override { out[0] = L.end_first; size = 1; return true; }
    bool set_duration(std::vector<seq::Event>& ev, int i, int dur) const override;
    int  jump_target(const seq::Event& e) const override { return e.type == seq::EventType::Command && e.b[0] == 0x81 ? (e.b[1] | (e.b[2] << 8)) : -1; }
    void set_jump_target(seq::Event& e, uint16_t addr) const override { e.b[1] = uint8_t(addr & 0xFF); e.b[2] = uint8_t(addr >> 8); }
    void track_pointer_writes(const seq::Song& song, int pattern_idx, int voice, uint16_t dest, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    void reclaimable_ranges(const uint8_t* ram, std::vector<std::pair<uint16_t, uint16_t>>& out) const override;
    void live_state_writes(const uint8_t* ram, const seq::Position& pos, int voice, uint16_t ptr, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    bool remaps_stack() const override { return L.stack_base != 0; }
    bool set_note_at(std::vector<seq::Event>& ev, int tick, uint8_t note_byte, int pattern_len) const override;
    bool insert_command_at(std::vector<seq::Event>& ev, int tick, const uint8_t* bytes, int size) const override;
    bool set_instrument(std::vector<seq::Event>& ev, int tick0, int tick1, uint8_t ins) const override;
    bool remove_span(std::vector<seq::Event>& ev, int tick, int ticks, bool keep_length) const override;
    bool insert_span(std::vector<seq::Event>& ev, int tick, int ticks, uint8_t byte) const override;

    void parse_slot(const uint8_t* ram, const uint16_t starts[8], seq::Track out[8]) const;
    seq::Track parse_stream(const uint8_t* ram, uint16_t start) const;
    int note_pitch(const uint8_t* ram, int note, int instrument) const;
};

std::unique_ptr<seq::Driver> detect(const uint8_t* ram);

}
