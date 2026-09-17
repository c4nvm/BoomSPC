// Heartbeat's SNES sound driver (Dragon Quest III and VI). An N-SPC
// descendant; format after VGMTrans' HeartBeatSnes, layout read from the
// driver code in ARAM.
//
// Songs are listed as split low / high byte arrays. A song header is
// [instrument table offset] then up to 8 track offsets (0 = end), all
// relative to the header. The driver runs up to 7 song slots; a table
// maps each slot's tracks to DSP voices. Streams:
//   00          end   01-7F  length, then an optional parameter byte (< 80:
//               gate high nibble, velocity low nibble)
//   80-CF       note   D0  tie   D1  rest   D2 / D3  slur on / off
//   D4 i  instrument   DD t  tempo   E3 v  volume   E0 t  transpose
//   F2 off  jump (relative to the header)   F3 off  call   F4  return
//   F9 00 n  loop count   F9 01 off  loop again while the count lasts
//   F9 03-07 x  one ADSR part   F9 09 l r  surround
// Ticks: (8000 / latch) * tempo / 256 per second (24 per quarter).
#pragma once

#include <algorithm>
#include <map>

#include "stream.hpp"

namespace heartbeat {
struct Layout {
    uint16_t song_lo = 0, song_hi = 0;   // song header address byte arrays; count = hi - lo
    uint16_t slot_song = 0;        // song index per slot (+slot, FF = none)
    uint16_t voice_table = 0;      // DSP voice per (track * 7 + slot), FF = unused
    uint16_t off_lo = 0, off_hi = 0;     // live stream offsets from the header (+voice)
    uint16_t tempo_base = 0;       // tempo per slot (+slot)
    uint16_t len_table = 0;        // argument counts for D2..
    bool valid() const { return song_lo && off_lo && len_table; }
};

Layout detect_layout(const uint8_t* ram);

class HeartbeatDriver : public stream::Driver {
public:
    explicit HeartbeatDriver(Layout layout) : L(layout) {}
    Layout L;

    std::string name() const override { return "Heartbeat (Dragon Quest III / VI)"; }
    const char* id() const override { return "heartbeat"; }
    uint8_t rest_byte() const override { return 0xD1; }
    uint8_t tie_byte() const override { return 0xD0; }
    bool    is_note_byte(uint8_t b) const override { return b >= 0x80 && b < 0xD0; }
    bool    is_command(uint8_t b) const override { return b >= 0xD2 || (b > 0 && b < 0x80); }
    int     note_semitone(uint8_t b) const override { return b - 0x80 + 12; }
    uint8_t note_byte(int semitone_from_c0) const override { return uint8_t(std::clamp(semitone_from_c0 - 12 + 0x80, 0x80, 0xCF)); }
    uint8_t note_min() const override { return 0x80; }
    uint8_t note_max() const override { return 0xCF; }
    bool    transpose_event(seq::Event& e, int semis) const override;
    bool        is_instrument_cmd(uint8_t op) const override { return op == 0xD4; }
    uint8_t     instrument_opcode() const override { return 0xD4; }
    uint8_t     first_command() const override { return 0xD2; }
    int         command_count() const override { return 0x28; }
    std::string event_text(const seq::Event& e) const override;
    bool pitch_fx(const seq::Event& e, seq::PitchFx& out) const override;
    bool       has_instruments() const override { return false; }
    int        instrument_count(const uint8_t* ram) const override { (void)ram; return 0; }
    seq::Instrument read_instrument(const uint8_t* ram, int index) const override { (void)ram; (void)index; return seq::Instrument{}; }
    bool       preview_regs(const uint8_t* ram, uint8_t note_byte, int instrument, uint8_t regs[8]) const override { (void)ram; (void)note_byte; (void)instrument; (void)regs; return false; }
    double ticks_per_second(const uint8_t* ram) const override;
    int    default_ticks_per_beat() const override { return 24; }
    bool   tempo_writes(const uint8_t* ram, double tps, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    bool end_bytes(uint8_t out[16], int& size) const override { out[0] = 0x00; size = 1; return true; }
    bool set_duration(std::vector<seq::Event>& ev, int i, int dur) const override;
    void apply_note_byte(seq::Event& e, uint8_t byte) const override;
    int  call_count(const seq::Event& e) const override { return e.type == seq::EventType::Command && e.b[0] == 0xF3 ? 1 : 0; }
    int  jump_target(const seq::Event& e) const override;
    void set_jump_target(seq::Event& e, uint16_t addr) const override;
    std::vector<uint8_t> serialize_relocated(const std::vector<seq::Event>& ev, uint16_t dest, std::vector<int>* offsets) const override;
    bool remaps_stack() const override { return false; }
    bool is_frame_command(const seq::Event& e) const override { return e.type == seq::EventType::Command && ((e.b[0] == 0xF9 && e.b[1] <= 1) || e.b[0] == 0xF3 || e.b[0] == 0xF4); }
    uint16_t live_ptr(const uint8_t* ram, const seq::Position& pos, int v) const override;
    void live_state_writes(const uint8_t* ram, const seq::Position& pos, int voice, uint16_t ptr, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    void track_pointer_writes(const seq::Song& song, int pattern_idx, int voice, uint16_t dest, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    void free_space_bounds(int& lo, int& hi) const override { lo = data_lo_; hi = 0x10000; }

    void decode(const uint8_t* p, int pc, stream::State& s, seq::Event& e, stream::Flow& f) const override;
    const stream::CmdSpec& spec(uint8_t op) const override;
    uint16_t track_start(const uint8_t* ram, uint16_t header, int v) const override;
    std::vector<uint16_t> song_headers(const uint8_t* ram) const override;
    uint16_t live_ptr_addr(int v) const override { return uint16_t(L.off_lo + v); }
    int  ptr_span() const override { return 1; }
    void select_song(uint16_t header) const override { cur_ = header; }
    stream::State initial_state(const uint8_t* ram, uint16_t header, int voice) const override;
    bool ptr_after_note() const override { return true; }
    std::string song_label(const uint8_t* ram, uint16_t header, const seq::Pattern& p, int index) const override;

private:
    int  slot_of(const uint8_t* ram, uint16_t header) const;   // slot the song is loaded in, -1 = none
    int  track_of(const uint8_t* ram, uint16_t header, int v) const;
    mutable int data_lo_ = 0x200;
    mutable uint16_t cur_ = 0;                          // header of the song whose live state is read
    mutable std::map<uint32_t, uint16_t> slots_;        // (header << 8 | voice) -> address of its offset word
    mutable uint8_t lens_[0x28] = {};
};

std::unique_ptr<seq::Driver> detect(const uint8_t* ram);

}
