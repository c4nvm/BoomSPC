// Konami's SNES sound driver (Contra III, Axelay, Castlevania: Dracula X,
// Ganbare Goemon 2-4, Sparkster, Parodius, TMNT: Tournament Fighters,
// Animaniacs...). Format after VGMTrans' KonamiSnes and loveemu's konspc,
// layout read from the driver code in ARAM.
//
// A song header is eight little-endian voice program pointers. Streams:
//   00-5F k [len] d [vel]   note: key k (0 = C-1 + 12); bit 7 set reuses
//               the last length, else a length byte follows; then a byte
//               with bit 7 clear is the gate ratio (n/128, 127 = full,
//               tied into a following note of the same key) followed by
//               the velocity, else the byte is the velocity itself
//   60 / 61     percussion on / off       62.. version-specific
//   E0 len      rest                      E1 len ratio   tie (holds the note)
//   E2 i        instrument   E3 pan   E4 d r depth vibrato   EA t  tempo
//   E6 / E7 n dv dp   repeat: E7 jumps back to the byte after E6 until
//               it has run n times (0 = forever), adding dv to the
//               velocity and dp to the pitch on each pass; E8 / E9 is a
//               second, independent pair
//   F6 / F7     repeat with alternate ending: the section plays, then
//               replays and skips the first ending on the second pass
//   FD hh ll    jump (backwards = the song loop)   FE addr  call   FF  return / end
// The driver's length table only flags commands with arguments; sizes
// follow VGMTrans' per-version event maps. Ticks run at
// (8000 / timer-0 latch) * tempo / 256 per second, tempo per voice.
#pragma once

#include <algorithm>

#include "stream.hpp"

namespace konami {
struct Layout {
    int      version = 0;          // 1..6 as in VGMTrans
    uint8_t  ptr_zp = 0x30;        // zero page: voice pointer words (X = 2 * voice)
    uint16_t cmd_lo = 0, cmd_hi = 0, len_table = 0;   // handler tables (indexed by 2 * entry) and the length table
    uint8_t  first_6x = 0xE0;      // lowest 6x opcode in the tables (E0 = none)
    uint8_t  len_item = 2;         // bytes per length table entry
    uint16_t header = 0;           // fixed song header (Goemon 2 style), else 0
    uint16_t song_list = 0;        // 5-byte entries with the header pointer at +3, else 0
    uint16_t start_copy = 0;       // where the loader copies the current song's voice pointers (+X)
    uint8_t  song_zp = 0;          // zero page byte holding the current song number
    bool     song_one_based = false;
    uint16_t tempo_base = 0;       // per-voice tempo bytes (+X)
    uint16_t sub_ret = 0, rep1_ret = 0, rep2_ret = 0, volta_start = 0, volta_end = 0;   // per-voice return words (+X)
    uint8_t  rep1_count = 0, rep2_count = 0;   // zero page counters (+X)
    uint8_t  cond_zp = 0;          // v1: zero page flag (bit 0) that sends FC to its second address
    uint16_t data_lo = 0x200;      // lowest song byte: free RAM is handed out above it
    uint16_t ins_table = 0;        // common instrument table
    uint8_t  ins_size = 7, ins_count = 0;
    bool valid() const { return version != 0; }
};

Layout detect_layout(const uint8_t* ram);

class KonamiDriver : public stream::Driver {
public:
    explicit KonamiDriver(Layout layout) : L(layout) {}
    Layout L;

    std::string name() const override;
    const char* id() const override { return "konami"; }
    uint8_t rest_byte() const override { return 0xE0; }
    uint8_t tie_byte() const override { return 0xE1; }
    bool    is_note_byte(uint8_t b) const override { return (b & 0x7F) < 0x60; }
    int     note_semitone(uint8_t b) const override { return (b & 0x7F) + 12; }
    uint8_t note_byte(int semitone_from_c0) const override { return uint8_t(std::clamp(semitone_from_c0 - 12, 0, 0x5F)); }
    uint8_t note_min() const override { return 0x00; }
    uint8_t note_max() const override { return 0x5F; }
    bool    transpose_event(seq::Event& e, int semis) const override;
    bool        is_command(uint8_t b) const override { return b >= 0x60 && b < 0x80 ? true : b >= 0xE0; }
    bool        is_instrument_cmd(uint8_t op) const override { return op == 0xE2; }
    uint8_t     instrument_opcode() const override { return 0xE2; }
    uint8_t     first_command() const override { return 0xE0; }
    int         command_count() const override { return 0x20; }
    std::string event_text(const seq::Event& e) const override;
    bool pitch_fx(const seq::Event& e, seq::PitchFx& out) const override;
    bool note_retriggers(const std::vector<seq::Event>& ev, int i) const override;
    bool       has_instruments() const override { return L.ins_table != 0; }
    int        instrument_count(const uint8_t* ram) const override { (void)ram; return L.ins_count; }
    seq::Instrument read_instrument(const uint8_t* ram, int index) const override;
    bool       preview_regs(const uint8_t* ram, uint8_t note_byte, int instrument, uint8_t regs[8]) const override;
    double ticks_per_second(const uint8_t* ram) const override;
    bool   tempo_writes(const uint8_t* ram, double tps, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    bool end_bytes(uint8_t out[16], int& size) const override { out[0] = 0xFF; size = 1; return true; }
    bool set_duration(std::vector<seq::Event>& ev, int i, int dur) const override;
    void apply_note_byte(seq::Event& e, uint8_t byte) const override;
    int  call_count(const seq::Event& e) const override { return e.type == seq::EventType::Command && e.b[0] == 0xFE ? 1 : 0; }

    void decode(const uint8_t* p, int pc, stream::State& s, seq::Event& e, stream::Flow& f) const override;
    const stream::CmdSpec& spec(uint8_t op) const override;
    uint16_t track_start(const uint8_t* ram, uint16_t header, int v) const override;
    std::vector<uint16_t> song_headers(const uint8_t* ram) const override;
    uint16_t live_ptr_addr(int v) const override { return uint16_t(L.ptr_zp + v * 2); }
    void live_extra_writes(const uint8_t* ram, int voice, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    bool loop_jump(uint16_t target, seq::Event& e) const override { e.b[0] = 0xFD; e.b[1] = uint8_t(target); e.b[2] = uint8_t(target >> 8); e.size = 3; return true; }
    bool ptr_after_note() const override { return true; }
    void prune_idle_voices(const uint8_t* ram, seq::Pattern& p) const override { prune_by_live_pointers(ram, p); }
    stream::State initial_state(const uint8_t* ram, uint16_t header, int voice) const override;
    void free_space_bounds(int& lo, int& hi) const override { lo = L.data_lo; hi = 0x10000; }
    uint8_t note_byte_in(int semitone, const stream::State& s) const override { return uint8_t(std::clamp(semitone - 12 - s.trans, 0, 0x5F)); }

    int slide_size(const uint8_t* p) const;   // bytes of an F3 slide glued to a note
    int note_slide(const seq::Event& e) const; // offset of the slide inside a note event, 0 = none
};

std::unique_ptr<seq::Driver> detect(const uint8_t* ram);

}
