// Bitmasters' SLICK/Audio driver (Earthworm Jim, NBA Jam Tournament
// Edition). Read from the driver code in ARAM; nothing public describes it.
//
// The SNES uploads chunks listed in an 8-word directory. A song chunk:
// size, song id, track count n, tempo, ?, echo flag (nonzero = 12 bytes
// of echo settings follow), then n 8-byte track entries (flags, ?,
// volume, pan, ?, ?, order-list offset from the chunk) and the data the
// pattern offsets count from. Each track has an order list of 16-bit
// pattern offsets; a pattern is one header byte, then a MIDI-style
// variable-length delay, then events each followed by such a delay:
//   00-7F [v] g   note (velocity byte unless flag bit 0), sounding g ticks
//   80-BF         (one byte)   C0-DF v   controller n & 1F = v
//   E0 E1 E4 E5 EA EB EF  no effect     E2 00  loop point   E2 x  loop
//   E3  end (no delay)   E6 x  program   E7 t  tempo: (t + 40) / 60 ticks
//   per timer tick   E8  next pattern (its delay follows)   E9 s x  DSP
//   EC/ED/EE x  voice mute state
// Ticks: 8000 / timer latch per second times the track's tempo factor.
// A note lasts until the next event here (its gate is shown); a delay of
// zero stacks notes into a chord.
//
// BoomSPC parses a track as its first pattern chained through E8 events
// that jump to the next order entry (target and entry kept in the event
// bytes). Editing rewrites only the pattern visits that changed and
// repoints their order entries.
#pragma once

#include <algorithm>

#include "stream.hpp"

namespace slick {
struct Layout {
    uint16_t dir = 0;                       // chunk directory (8 words)
    uint16_t ptr_lo = 0, ptr_hi = 0;        // per-slot stream pointer bytes
    uint16_t flags = 0, state = 0;          // per-slot track flags (030F) and state (02B7)
    uint16_t order_lo = 0, order_hi = 0;    // per-slot order pointer
    uint16_t base_lo = 0, base_hi = 0;      // per-slot pattern base
    uint16_t loop_lo = 0, loop_hi = 0, loop_order_lo = 0, loop_order_hi = 0;   // saved by E2 00
    uint16_t tempo_int = 0, tempo_frac = 0; // per-slot ticks per timer tick, 8.8
    bool valid() const { return dir && ptr_lo && order_lo; }
};

Layout detect_layout(const uint8_t* ram);

class SlickDriver : public stream::Driver {
public:
    explicit SlickDriver(Layout layout) : L(layout) {}
    Layout L;

    static const uint8_t kRest = 0xEA;   // a no-op with a delay

    std::string name() const override { return "Bitmasters (SLICK)"; }
    const char* id() const override { return "slick"; }
    uint8_t rest_byte() const override { return kRest; }
    uint8_t tie_byte() const override { return 0; }
    bool    is_note_byte(uint8_t b) const override { return b < 0x80; }
    int     note_semitone(uint8_t b) const override { return b & 0x7F; }
    uint8_t note_byte(int semitone_from_c0) const override { return uint8_t(std::clamp(semitone_from_c0, 0, 0x7F)); }
    uint8_t note_min() const override { return 0x00; }
    uint8_t note_max() const override { return 0x7F; }
    bool    transpose_event(seq::Event& e, int semis) const override;
    bool        is_command(uint8_t b) const override { return b >= 0x80; }
    bool        is_instrument_cmd(uint8_t op) const override { return op == 0xE6; }
    uint8_t     instrument_opcode() const override { return 0xE6; }
    uint8_t     first_command() const override { return 0xE0; }
    int         command_count() const override { return 0x10; }
    std::string event_text(const seq::Event& e) const override;
    bool       has_instruments() const override { return false; }
    int        instrument_count(const uint8_t* ram) const override { (void)ram; return 0; }
    seq::Instrument read_instrument(const uint8_t* ram, int index) const override { (void)ram; (void)index; return seq::Instrument{}; }
    bool       preview_regs(const uint8_t* ram, uint8_t note_byte, int instrument, uint8_t regs[8]) const override { (void)ram; (void)note_byte; (void)instrument; (void)regs; return false; }
    double ticks_per_second(const uint8_t* ram) const override;
    bool   tempo_writes(const uint8_t* ram, double tps, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    bool end_bytes(uint8_t out[16], int& size) const override { out[0] = 0xE3; size = 1; return true; }
    bool set_duration(std::vector<seq::Event>& ev, int i, int dur) const override;
    void apply_note_byte(seq::Event& e, uint8_t byte) const override;
    std::vector<uint8_t> serialize_relocated(const std::vector<seq::Event>& ev, uint16_t dest, std::vector<int>* offsets) const override;
    uint16_t live_ptr(const uint8_t* ram, const seq::Position& pos, int v) const override;
    void live_state_writes(const uint8_t* ram, const seq::Position& pos, int voice, uint16_t ptr, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    void track_pointer_writes(const seq::Song& song, int pattern_idx, int voice, uint16_t dest, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    bool set_note_at(std::vector<seq::Event>& ev, int tick, uint8_t note_byte, int pattern_len) const override;
    bool set_instrument(std::vector<seq::Event>& ev, int tick0, int tick1, uint8_t ins) const override;
    bool insert_command_at(std::vector<seq::Event>& ev, int tick, const uint8_t* bytes, int size) const override;
    bool unroll_at(std::vector<seq::Event>& ev, int tick) const override;
    bool piecewise_streams() const override { return true; }
    void replaced_ranges(std::vector<std::pair<uint16_t, uint16_t>>& out) const override { for (const Piece& p : pieces_) if (p.lo < p.hi) out.push_back({p.lo, p.hi}); }
    int  default_ticks_per_beat() const override { return 24; }
    int  min_notes() const override { return 1; }
    void free_space_bounds(int& lo, int& hi) const override { lo = 0x200; hi = 0xFF00; }   // per-slot tables live at FF12 and up

    void decode(const uint8_t* p, int pc, stream::State& s, seq::Event& e, stream::Flow& f) const override;
    const stream::CmdSpec& spec(uint8_t op) const override;
    uint16_t track_start(const uint8_t* ram, uint16_t header, int v) const override;
    std::vector<uint16_t> song_headers(const uint8_t* ram) const override;
    int  pick_current_song(const uint8_t* ram, const std::vector<seq::Song>& songs) const override;
    uint16_t live_ptr_addr(int v) const override { return uint16_t(L.ptr_lo + v); }
    int  ptr_span() const override { return 1; }
    stream::State initial_state(const uint8_t* ram, uint16_t header, int voice) const override;
    stream::State edit_state(const std::vector<seq::Event>& ev) const override;
    bool ptr_after_note() const override { return true; }
    void prune_idle_voices(const uint8_t* ram, seq::Pattern& p) const override;
    std::string song_label(const uint8_t* ram, uint16_t header, const seq::Pattern& p, int index) const override;

private:
    // The delay after an event is the last field of its bytes; the note
    // gate (a second such number) sits before it.
    static int  vlq(const uint8_t* p, int& n);
    static int  put_vlq(uint8_t* p, int v);
    static bool timed(const seq::Event& e) { return e.duration > 0 && (e.type == seq::EventType::Note || e.type == seq::EventType::Rest); }
    static bool is_start(const seq::Event& e) { return e.b[15] == 0xA1; }   // a pattern's header byte and first delay
    static int  delay_at(const seq::Event& e);
    // Moves a covering event's delay into a rest after it, so a note can go there.
    int  peel_delay(std::vector<seq::Event>& ev, int i) const;
    uint16_t order_start(const uint8_t* ram, uint16_t header, int v) const;
    int  data_base(const uint8_t* ram, uint16_t header) const;

    mutable const uint8_t* ram_ = nullptr;
    mutable int base_ = 0;                  // the pattern base of the song being parsed
    struct Piece { uint16_t entry, offset, lo, hi; };
    mutable std::vector<Piece> pieces_;
    mutable std::vector<std::pair<uint16_t, uint16_t>> reloc_;
};

std::unique_ptr<seq::Driver> detect(const uint8_t* ram);

}
