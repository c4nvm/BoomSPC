// Neverland's SNES sound driver (Lufia, Lufia II, Energy Breaker). Read
// from the driver code in ARAM; nothing public describes it.
//
// The song block sits at a fixed address (2800 in Lufia, 4400 in Lufia II,
// 2E00 in Energy Breaker): "SFC" / "S2C", the timer-0 latch, a 12-character
// title, 8 voice flags (FF = unused), and at +20 eight little-endian
// pointers to one section list per voice. A list is big-endian section
// addresses with 8x bytes in between (transpose for the next section) and
// ends with FF. The Lufia II build takes the pointers and addresses
// relative to the block; Lufia's are absolute, its F6 has an extra byte
// and its F3/F4/F5/F8-FA are two-byte no-ops. Sections:
//   00-7F l g v  note: key (+ transpose), length, gate (ticks until key
//               off; 0 = no key on, the previous note keeps ringing or
//               the rest stays silent; > length = held into the next
//               note, which then does not retrigger the same key), velocity
//   80-EF       note with the previous note's length, gate and velocity
//   F0 d x  LFO depth   F1 d x  volume   F2 d x  pan (40 = centre)
//   F3 d    wait        F4 d x  tempo (timer latch)   F6 d x  pitch scale (80 = x1)
//   F7 d x  instrument (d = ticks to wait after the command)
//   FB      repeat start (saves the list position too)   FC n  repeat n
//           times (0 = forever, 1 = no repeat), two nested slots
//   FD      section end: next list entry    FE  voice end
//   FF s x  extended: echo (00-04), ADSR bits (08-0C), master volume (0D-0F),
//           noise (10-12), pitch mod (13-14), LFO rate/delay (15-18), flags
//
// BoomSPC parses a voice as its first section chained through FD events
// that jump to the next section (the target, transpose and list entry are
// kept in the event bytes). Editing rewrites only the section visits that
// changed: each gets its own copy and its list entry is repointed, so
// sections other voices share stay as they are.
#pragma once

#include <algorithm>

#include "stream.hpp"

namespace neverland {
struct Layout {
    uint16_t header = 0;                    // song block
    uint16_t base = 0;                      // what list pointers and section addresses are relative to
    bool     v1 = false;                    // the Lufia build
    uint16_t ptr_lo = 0, ptr_hi = 0;        // live stream pointer bytes (+voice)
    uint16_t list_lo = 0, list_hi = 0;      // live list pointer bytes
    uint16_t trans = 0;                     // current transpose
    uint16_t slot_lp_lo = 0, slot_lp_hi = 0, slot_sp_lo = 0, slot_sp_hi = 0, slot_count = 0, slot_trans = 0;   // repeat slot 0 (+voice)
    uint16_t slot_stride = 0x10;            // slot 1 is this much further
    uint16_t fast_flag = 0;                 // nonzero = two ticks per timer count
    uint16_t pitch_lo = 0, pitch_hi = 0;    // 12-entry pitch table (top octave)
    uint16_t ins_table = 0;                 // 4 bytes per instrument: ADSR1, ADSR2, tuning hi, lo
    bool valid() const { return header && ptr_lo && list_lo && slot_lp_lo; }
};

Layout detect_layout(const uint8_t* ram);

class NeverlandDriver : public stream::Driver {
public:
    explicit NeverlandDriver(Layout layout);
    Layout L;

    static const uint8_t kRest = 0xFE, kTie = 0xFD;   // editor tokens (a note with gate 0, or a wait)

    std::string name() const override { return L.v1 ? "Neverland (Lufia)" : "Neverland (Lufia II)"; }
    const char* id() const override { return "neverland"; }
    uint8_t rest_byte() const override { return kRest; }
    uint8_t tie_byte() const override { return kTie; }
    bool    is_note_byte(uint8_t b) const override { return b < 0xF0; }
    int     note_semitone(uint8_t b) const override { return (b & 0x7F) + 12; }
    uint8_t note_byte(int semitone_from_c0) const override { return uint8_t(std::clamp(semitone_from_c0 - 12, 0, 0x7F)); }
    uint8_t note_min() const override { return 0x00; }
    uint8_t note_max() const override { return 0x5F; }
    bool    transpose_event(seq::Event& e, int semis) const override;
    bool        is_command(uint8_t b) const override { return b >= 0xF0; }
    bool        is_instrument_cmd(uint8_t op) const override { return op == 0xF7; }
    uint8_t     instrument_opcode() const override { return 0xF7; }
    int         instrument_arg(const seq::Event& e) const override { return e.b[2]; }
    uint8_t     first_command() const override { return 0xF0; }
    int         command_count() const override { return 0x10; }
    std::string event_text(const seq::Event& e) const override;
    bool pitch_fx(const seq::Event& e, seq::PitchFx& out) const override;
    bool note_retriggers(const std::vector<seq::Event>& ev, int i) const override;
    bool       has_instruments() const override { return L.ins_table != 0; }
    int        instrument_count(const uint8_t* ram) const override;
    bool       instrument_used(const uint8_t* ram, int index) const override;
    seq::Instrument read_instrument(const uint8_t* ram, int index) const override;
    bool       preview_regs(const uint8_t* ram, uint8_t note_byte, int instrument, uint8_t regs[8]) const override;
    double ticks_per_second(const uint8_t* ram) const override;
    bool   tempo_writes(const uint8_t* ram, double tps, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    bool end_bytes(uint8_t out[16], int& size) const override { out[0] = 0xFE; size = 1; return true; }
    bool set_duration(std::vector<seq::Event>& ev, int i, int dur) const override;
    void apply_note_byte(seq::Event& e, uint8_t byte) const override;
    std::vector<uint8_t> serialize_relocated(const std::vector<seq::Event>& ev, uint16_t dest, std::vector<int>* offsets) const override;
    uint16_t live_ptr(const uint8_t* ram, const seq::Position& pos, int v) const override;
    void live_state_writes(const uint8_t* ram, const seq::Position& pos, int voice, uint16_t ptr, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    void track_pointer_writes(const seq::Song& song, int pattern_idx, int voice, uint16_t dest, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    void free_space_bounds(int& lo, int& hi) const override { lo = std::max(int(L.base), 0x200); hi = std::min(L.base + 0x8000, 0x10000); }
    bool set_note_at(std::vector<seq::Event>& ev, int tick, uint8_t note_byte, int pattern_len) const override;
    bool set_instrument(std::vector<seq::Event>& ev, int tick0, int tick1, uint8_t ins) const override;
    bool remove_span(std::vector<seq::Event>& ev, int tick, int ticks, bool keep_length) const override;
    bool unroll_at(std::vector<seq::Event>& ev, int tick) const override;
    bool piecewise_streams() const override { return true; }
    void replaced_ranges(std::vector<std::pair<uint16_t, uint16_t>>& out) const override { for (const Piece& p : pieces_) if (p.lo < p.hi) out.push_back({p.lo, p.hi}); }
    int  default_ticks_per_beat() const override { return 48; }
    int  end_park_offset() const override { return 0; }

    void decode(const uint8_t* p, int pc, stream::State& s, seq::Event& e, stream::Flow& f) const override;
    const stream::CmdSpec& spec(uint8_t op) const override;
    uint16_t track_start(const uint8_t* ram, uint16_t header, int v) const override;
    std::vector<uint16_t> song_headers(const uint8_t* ram) const override;
    uint16_t live_ptr_addr(int v) const override { return uint16_t(L.ptr_lo + v); }
    int  ptr_span() const override { return 1; }
    stream::State initial_state(const uint8_t* ram, uint16_t header, int voice) const override;
    stream::State edit_state(const std::vector<seq::Event>& ev) const override;
    bool ptr_after_note() const override { return true; }
    uint8_t note_byte_in(int semitone, const stream::State& s) const override { return uint8_t(std::clamp(semitone - 12 - s.trans, 0, 0x7F)); }
    void prune_idle_voices(const uint8_t* ram, seq::Pattern& p) const override;
    std::string song_label(const uint8_t* ram, uint16_t header, const seq::Pattern& p, int index) const override;

private:
    // Short notes carry the length/gate/velocity they inherit in b[1..3];
    // this makes them explicit before those values change.
    static void longify(seq::Event& e);
    static bool is_short(const seq::Event& e) { return e.size == 1 && e.b[0] >= 0x80 && e.b[0] < 0xF0; }
    static bool timed(const seq::Event& e) { return e.duration > 0 && (e.type == seq::EventType::Note || e.type == seq::EventType::Rest || e.type == seq::EventType::Tie); }
    void longify_next(std::vector<seq::Event>& ev, int i) const;
    uint16_t list_start(const uint8_t* ram, int v) const;

    stream::CmdSpec cmds_[0x10];
    mutable const uint8_t* ram_ = nullptr;
    mutable uint16_t entry_[8] = {0, 0, 0, 0, 0, 0, 0, 0};   // each voice's first list entry (after its transpose bytes)
    // What the last serialize_relocated wrote: the section visits in the
    // block (list entry to repoint, 0 = the voice's first; block offset;
    // old byte range) and where every old byte went.
    struct Piece { uint16_t entry, offset, lo, hi; };
    mutable std::vector<Piece> pieces_;
    mutable std::vector<std::pair<uint16_t, uint16_t>> reloc_;
};

std::unique_ptr<seq::Driver> detect(const uint8_t* ram);

}
