// Wolf Team's SNES driver (Tales of Phantasia, Star Ocean). Read from the
// driver code in ARAM; nothing public describes it.
//
// The song lives on a page the driver names (4000 in Tales of Phantasia,
// 3800 in Star Ocean). Its header holds a tempo base at +22 and, from
// +23, fourteen 3-byte track entries: flags (bit 7 = used) and the
// offset of the track's order list from the page. An order list is
// 16-bit pattern offsets from the page, ended by FFFF. Patterns:
//   00-7F d g v  note for d ticks, sounding g (+1), velocity v; the same
//                note again while it still sounds does not retrigger
//   90 d      rest        91 / FD  pattern end: next order entry
//   92        repeat start (two slots; the order position is saved too)
//   93 n      repeat n times (0 = forever)
//   94 d x    pitch bend x-40 over d   95 d t  tempo t, wait d
//   96 i      instrument               97/98/99 d x  volume / pan / ? = x, wait d
//   9A        (3 bytes, nothing)       9B f  flag   9C a b c  envelope
//   A2 t      transpose t-40   A3 f  flag   AA f v  echo feedback, volume
//   AD f      noise/echo bits  AE f  flag   AF a b  ADSR   B0 v  echo volume
//   B2 f      humanize on/off (random +/-1 ticks)
// Ticks come from timer 0: 8000 / latch per second.
//
// DSP voices are handed out per note (zero page $01 + v*16 names the track
// a voice is playing), so a song can run more than eight tracks; the eight
// columns show the first eight entries that are in use (`col_`).
//
// BoomSPC parses a track as its first pattern chained through pattern-end
// events that jump to the next order entry (target and entry kept in the
// event bytes). Editing rewrites only the pattern visits that changed.
#pragma once

#include <algorithm>

#include "stream.hpp"

namespace wolfteam {
struct Layout {
    uint16_t tracks = 0;        // track structs, 32 bytes each
    uint8_t  page_zp = 0;       // zero-page byte added to the page constant
    uint8_t  page_add = 0;      // the song page constant
    uint16_t saves = 0;         // saved repeat pointers, 4 bytes per track
    bool valid() const { return tracks && page_add; }
};

Layout detect_layout(const uint8_t* ram);

class WolfteamDriver : public stream::Driver {
public:
    explicit WolfteamDriver(Layout layout) : L(layout) {}
    Layout L;

    static const uint8_t kRest = 0x90, kTie = 0x94;   // editor tokens

    std::string name() const override { return "Wolf Team"; }
    const char* id() const override { return "wolfteam"; }
    uint8_t rest_byte() const override { return kRest; }
    uint8_t tie_byte() const override { return 0; }
    bool    is_note_byte(uint8_t b) const override { return b < 0x80; }
    int     note_semitone(uint8_t b) const override { return (b & 0x7F) + 12; }
    uint8_t note_byte(int semitone_from_c0) const override { return uint8_t(std::clamp(semitone_from_c0 - 12, 0, 0x7F)); }
    uint8_t note_min() const override { return 0x00; }
    uint8_t note_max() const override { return 0x7F; }
    bool    transpose_event(seq::Event& e, int semis) const override;
    bool        is_command(uint8_t b) const override { return b >= 0x80; }
    bool        is_instrument_cmd(uint8_t op) const override { return op == 0x96; }
    uint8_t     instrument_opcode() const override { return 0x96; }
    uint8_t     first_command() const override { return 0x90; }
    int         command_count() const override { return 0x23; }
    std::string event_text(const seq::Event& e) const override;
    bool note_retriggers(const std::vector<seq::Event>& ev, int i) const override { return ev[size_t(i)].type != seq::EventType::Tie; }
    bool       has_instruments() const override { return false; }
    int        instrument_count(const uint8_t* ram) const override { (void)ram; return 0; }
    seq::Instrument read_instrument(const uint8_t* ram, int index) const override { (void)ram; (void)index; return seq::Instrument{}; }
    bool       preview_regs(const uint8_t* ram, uint8_t note_byte, int instrument, uint8_t regs[8]) const override { (void)ram; (void)note_byte; (void)instrument; (void)regs; return false; }
    double ticks_per_second(const uint8_t* ram) const override { return 8000.0 / (ram[0xFA] ? ram[0xFA] : 256); }
    bool   tempo_writes(const uint8_t* ram, double tps, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    bool end_bytes(uint8_t out[16], int& size) const override { out[0] = 0xFD; size = 1; return true; }
    bool set_duration(std::vector<seq::Event>& ev, int i, int dur) const override;
    void apply_note_byte(seq::Event& e, uint8_t byte) const override;
    std::vector<uint8_t> serialize_relocated(const std::vector<seq::Event>& ev, uint16_t dest, std::vector<int>* offsets) const override;
    uint16_t live_ptr(const uint8_t* ram, const seq::Position& pos, int v) const override { (void)pos; return stream::rd16(ram, live_ptr_addr(v)); }
    void live_state_writes(const uint8_t* ram, const seq::Position& pos, int voice, uint16_t ptr, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    void track_pointer_writes(const seq::Song& song, int pattern_idx, int voice, uint16_t dest, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    bool set_note_at(std::vector<seq::Event>& ev, int tick, uint8_t note_byte, int pattern_len) const override;
    bool unroll_at(std::vector<seq::Event>& ev, int tick) const override;
    bool piecewise_streams() const override { return true; }
    void replaced_ranges(std::vector<std::pair<uint16_t, uint16_t>>& out) const override { for (const Piece& p : pieces_) if (p.lo < p.hi) out.push_back({p.lo, p.hi}); }
    int  default_ticks_per_beat() const override { return 48; }
    int  end_park_offset() const override { return 0; }
    // Below the song page sit the driver and its sound effects; only the
    // filler just under the page is free.
    void free_space_bounds(int& lo, int& hi) const override { lo = std::max(0x201, int(free_lo_)); hi = 0x10000; }

    void decode(const uint8_t* p, int pc, stream::State& s, seq::Event& e, stream::Flow& f) const override;
    const stream::CmdSpec& spec(uint8_t op) const override;
    uint16_t track_start(const uint8_t* ram, uint16_t header, int v) const override;
    std::vector<uint16_t> song_headers(const uint8_t* ram) const override;
    uint16_t live_ptr_addr(int v) const override { return uint16_t(L.tracks + 32 * track_of(v) + 3); }
    int  ptr_span() const override { return 2; }
    stream::State initial_state(const uint8_t* ram, uint16_t header, int voice) const override;
    stream::State edit_state(const std::vector<seq::Event>& ev) const override;
    bool ptr_after_note() const override { return false; }
    void prune_idle_voices(const uint8_t* ram, seq::Pattern& p) const override;
    std::string song_label(const uint8_t* ram, uint16_t header, const seq::Pattern& p, int index) const override;

private:
    mutable int8_t col_[8] = {0, 1, 2, 3, 4, 5, 6, 7};   // track entry shown in each column
    int track_of(int v) const { return v >= 0 && v < 8 ? col_[v] : v; }

private:
    static bool timed(const seq::Event& e) { return e.duration > 0 && (e.type == seq::EventType::Note || e.type == seq::EventType::Rest || e.type == seq::EventType::Tie); }
    static bool is_end(const seq::Event& e) { return e.type == seq::EventType::Command && (e.b[0] == 0x91 || e.b[0] == 0xFD); }
    uint16_t order_start(const uint8_t* ram, uint16_t header, int v) const;

    mutable const uint8_t* ram_ = nullptr;
    mutable uint16_t header_ = 0;
    mutable uint16_t free_lo_ = 0;
    struct Piece { uint16_t entry, offset, lo, hi; };
    mutable std::vector<Piece> pieces_;
    mutable std::vector<std::pair<uint16_t, uint16_t>> reloc_;
};

std::unique_ptr<seq::Driver> detect(const uint8_t* ram);

}
