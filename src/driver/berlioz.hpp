// Sculptured Software's "Berlioz" SNES driver (Mortal Kombat II, Secret of
// Evermore). Read from the driver code in ARAM; nothing public describes it.
//
// A song is a track count and up to 20 absolute track pointers; the driver
// gives each note a DSP voice dynamically, so tracks past the eighth are
// left out here. Ticks come from timer 1 (20 ms), every tick when the
// tempo is 0, else tempo/256 of them. Track bytes:
//   00-7F        note: bits 7-3 = semitones from the last note - 8, bits
//                2-0 = ticks - 1
//   80-BF d      note 20-5F for d ticks (the last note is updated too)
//   C0-DF        rest 1-32     F8 / FD / FE / FF  rest 32 / 64 / 128 / 256
//   EF n d       note n for d ticks
//   E0 a b  voice ADSR?   E1-E4 x  voice params   E5 f v m  DSP voice choice
//   E6      default voice   E9 x  global   EA  key off   ED x  velocity scale
//   EB / EC bend items until 00: hd = pitch +/-h (1/20 semitone x8) for d
//           ticks, 01 d = wait, 02 = key off, 0x b (x >= 3) = pitch by b
//   F0      end   F1 a b  volume a*b/256   F2 v  volume   F3 p  priority
//   F4      the next note does not retrigger   F5 i  instrument (inside an
//           F6 block the block's pass table overrides it)
//   F6 body end n off skip: play [body, end) n times (0 = forever) with a
//           16-bit pitch offset in 1/20 semitones; afterwards continue at
//           the F6 + 11 + skip (skip bytes = the pass table)
//   F7 a b  detune (1/20 semitones)   F9  restart the track   FA t  tempo
//   FB x  param   FC  flag
// The end of an F6 block is an address, not a byte: the parser puts a
// zero-size event there that jumps back or out.
#pragma once

#include <algorithm>

#include "stream.hpp"

namespace berlioz {
struct Layout {
    uint8_t  ptr_lo = 0, ptr_hi = 0;    // zero-page track pointer bytes (+track)
    uint8_t  header_zp = 0;             // current song header
    uint8_t  ntracks_zp = 0;
    uint8_t  tempo_zp = 0;
    uint16_t song_table = 0;            // song pointers
    uint16_t depth = 0;                 // per-track F6 depth (5 = none) and the frame tables
    uint16_t frame_lo = 0, frame_hi = 0, frame_count = 0;
    uint16_t esa_addr = 0;              // byte holding the echo buffer page
    bool valid() const { return ptr_lo && header_zp && song_table; }
};

Layout detect_layout(const uint8_t* ram);

class BerliozDriver : public stream::Driver {
public:
    explicit BerliozDriver(Layout layout) : L(layout) {}
    Layout L;

    static const uint8_t kRest = 0xC0, kTie = 0xF4;   // editor tokens

    std::string name() const override { return "Sculptured (Berlioz)"; }
    const char* id() const override { return "berlioz"; }
    uint8_t rest_byte() const override { return kRest; }
    uint8_t tie_byte() const override { return kTie; }
    bool    is_note_byte(uint8_t b) const override { return b < 0xC0 || b == 0xEF; }
    // Editor note tokens are key + 60 (the 2-byte form's bytes for keys 20-5F).
    int     note_semitone(uint8_t b) const override { return (b >= 0x60 ? b - 0x60 : b) + 15; }
    uint8_t note_byte(int semitone_from_c0) const override { return uint8_t(std::clamp(semitone_from_c0 - 15, 0, 0x7F) + 0x60); }
    uint8_t note_min() const override { return 0x60; }
    uint8_t note_max() const override { return 0xDF; }
    bool    transpose_event(seq::Event& e, int semis) const override;
    bool        is_command(uint8_t b) const override { return b >= 0xE0; }
    bool        is_instrument_cmd(uint8_t op) const override { return op == 0xF5; }
    uint8_t     instrument_opcode() const override { return 0xF5; }
    uint8_t     first_command() const override { return 0xE0; }
    int         command_count() const override { return 0x20; }
    std::string event_text(const seq::Event& e) const override;
    bool note_retriggers(const std::vector<seq::Event>& ev, int i) const override { return ev[size_t(i)].type != seq::EventType::Tie; }
    bool       has_instruments() const override { return false; }
    int        instrument_count(const uint8_t* ram) const override { (void)ram; return 0; }
    seq::Instrument read_instrument(const uint8_t* ram, int index) const override { (void)ram; (void)index; return seq::Instrument{}; }
    bool       preview_regs(const uint8_t* ram, uint8_t note_byte, int instrument, uint8_t regs[8]) const override { (void)ram; (void)note_byte; (void)instrument; (void)regs; return false; }
    double ticks_per_second(const uint8_t* ram) const override;
    bool   tempo_writes(const uint8_t* ram, double tps, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    bool end_bytes(uint8_t out[16], int& size) const override { out[0] = 0xF0; size = 1; return true; }
    bool set_duration(std::vector<seq::Event>& ev, int i, int dur) const override;
    void apply_note_byte(seq::Event& e, uint8_t byte) const override;
    int  jump_target(const seq::Event& e) const override;
    std::vector<uint8_t> serialize_relocated(const std::vector<seq::Event>& ev, uint16_t dest, std::vector<int>* offsets) const override;
    uint16_t live_ptr(const uint8_t* ram, const seq::Position& pos, int v) const override;
    void live_state_writes(const uint8_t* ram, const seq::Position& pos, int voice, uint16_t ptr, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    void track_pointer_writes(const seq::Song& song, int pattern_idx, int voice, uint16_t dest, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    bool set_note_at(std::vector<seq::Event>& ev, int tick, uint8_t note_byte, int pattern_len) const override;
    int  default_ticks_per_beat() const override { return 24; }
    // The echo buffer is cleared from ESA up to FC00 whenever echo is set
    // up, and the songs pick the delay: that whole span is the buffer.
    int  echo_length(const uint8_t* ram, int dsp_edl) const override { return !L.esa_addr || ram[L.esa_addr] == 0xFF ? dsp_edl : 15; }

    void decode(const uint8_t* p, int pc, stream::State& s, seq::Event& e, stream::Flow& f) const override;
    const stream::CmdSpec& spec(uint8_t op) const override;
    uint16_t track_start(const uint8_t* ram, uint16_t header, int v) const override;
    std::vector<uint16_t> song_headers(const uint8_t* ram) const override;
    int  pick_current_song(const uint8_t* ram, const std::vector<seq::Song>& songs) const override;
    uint16_t live_ptr_addr(int v) const override { return uint16_t(L.ptr_lo + v); }
    int  ptr_span() const override { return 1; }
    int  min_notes() const override { return 1; }
    stream::State initial_state(const uint8_t* ram, uint16_t header, int voice) const override;
    bool ptr_after_note() const override { return true; }
    uint8_t note_byte_in(int semitone, const stream::State& s) const override { return note_byte(semitone - (s.trans + (s.trans >= 0 ? 10 : -10)) / 20); }
    // Songs sit above the code; the frame tables sit at the top of RAM.
    void free_space_bounds(int& lo, int& hi) const override { lo = std::max(0x200, int(L.song_table) + 0x80); hi = L.frame_count ? std::min({int(L.frame_count), int(L.frame_lo), int(L.frame_hi)}) & 0xFF00 : 0x10000; }
    void prune_idle_voices(const uint8_t* ram, seq::Pattern& p) const override;
    std::string song_label(const uint8_t* ram, uint16_t header, const seq::Pattern& p, int index) const override;

private:
    // A relative note carries its absolute note in b[1]; this spells the
    // next relative note out before the note it depends on changes.
    static bool is_rel(const seq::Event& e) { return e.size == 1 && e.b[0] < 0x80 && e.type != seq::EventType::Command; }
    static void absolutize(seq::Event& e);
    void fix_next(std::vector<seq::Event>& ev, int i) const;
    mutable std::vector<std::pair<uint16_t, uint16_t>> reloc_;   // old -> new address of every byte the last serialize_relocated wrote
};

std::unique_ptr<seq::Driver> detect(const uint8_t* ram);

}
