// Pandora Box's SNES sound driver (Jun Suzuki: Kishin Kourinden Oni,
// Arabian Nights, Traverse, Gakkou de atta Kowai Hanashi...). Format after
// VGMTrans' PandoraBoxSnes; layout read from the driver code in ARAM.
//
// One song header per slot, found through the zero-page word the loader
// reads it from: +7 timebase (a quarter note is timebase / 4 ticks), +10
// eight track offsets relative to the header (FFFF = unused). Streams:
//   00-3F       note: low 4 bits = key (0 = rest, 1..12 = C..B), bit 4
//               holds the note into the next one, bit 5 clear = a length
//               byte follows, set = the last length again
//   40-47 octave   48-4F gate (n/8)   50-5F volume from a table   60-DF instrument
//   E0 t  tempo (BPM)   E2 t  transpose   E4 / E5  octave up / down
//   EC n  repeat n times (FF = forever) ... ED   EE  leave the repeat on its last pass
//   F5  end of track
// One tick per timer-0 event; the driver programs the latch from the
// tempo, so ticks run at 8000 / latch per second.
#pragma once

#include <algorithm>

#include "stream.hpp"

namespace pandora {
struct Layout {
    uint8_t  header_zp[2] = {0, 0}; // zero-page words holding the slot headers (music, second slot)
    uint8_t  slot[8] = {};          // zero-page base of each voice's state (pointer word first, repeat stack top at +2)
    uint16_t stack_base = 0;        // repeat stacks: 5-byte entries (start, end, count) per voice at base + voice * stride
    uint8_t  stack_stride = 0;
    bool valid() const { return header_zp[0] && slot[0]; }
};

Layout detect_layout(const uint8_t* ram);

class PandoraDriver : public stream::Driver {
public:
    explicit PandoraDriver(Layout layout) : L(layout) {}
    Layout L;

    std::string name() const override { return "Pandora Box (Kishin Kourinden Oni / Arabian Nights / Traverse)"; }
    const char* id() const override { return "pandora"; }
    uint8_t rest_byte() const override { return 0x00; }
    uint8_t tie_byte() const override { return 0; }
    bool    is_note_byte(uint8_t b) const override { return b < 0x40 && (b & 15) != 0; }
    int     note_semitone(uint8_t b) const override { return 36 + (b & 15) - 1 - 12; }
    uint8_t note_byte(int semitone_from_c0) const override { return uint8_t((((semitone_from_c0 % 12) + 12) % 12) + 1); }
    uint8_t note_min() const override { return 0x01; }
    uint8_t note_max() const override { return 0x0C; }
    bool    transpose_event(seq::Event& e, int semis) const override;
    bool        is_command(uint8_t b) const override { return b >= 0x40; }
    bool        is_instrument_cmd(uint8_t op) const override { return op >= 0x60 && op <= 0xDF; }
    uint8_t     instrument_opcode() const override { return 0x60; }
    uint8_t     first_command() const override { return 0xE0; }
    int         command_count() const override { return 0x17; }
    std::string event_text(const seq::Event& e) const override;
    bool pitch_fx(const seq::Event& e, seq::PitchFx& out) const override;
    bool note_retriggers(const std::vector<seq::Event>& ev, int i) const override;
    bool       has_instruments() const override { return false; }
    int        instrument_count(const uint8_t* ram) const override { (void)ram; return 0; }
    seq::Instrument read_instrument(const uint8_t* ram, int index) const override { (void)ram; (void)index; return seq::Instrument{}; }
    bool       preview_regs(const uint8_t* ram, uint8_t note_byte, int instrument, uint8_t regs[8]) const override { (void)ram; (void)note_byte; (void)instrument; (void)regs; return false; }
    double ticks_per_second(const uint8_t* ram) const override { return ram[0xFA] ? 8000.0 / ram[0xFA] : 0; }
    int    default_ticks_per_beat() const override { return ppqn_; }
    bool   tempo_writes(const uint8_t* ram, double tps, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    bool end_bytes(uint8_t out[16], int& size) const override { out[0] = 0xF5; size = 1; return true; }
    bool set_duration(std::vector<seq::Event>& ev, int i, int dur) const override;
    void apply_note_byte(seq::Event& e, uint8_t byte) const override;
    bool remaps_stack() const override { return L.stack_base != 0; }
    void live_extra_writes(const uint8_t* ram, int voice, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    bool set_instrument(std::vector<seq::Event>& ev, int tick0, int tick1, uint8_t ins) const override;
    bool enter_note(std::vector<seq::Event>& ev, int tick, int semitone, int pattern_len) const override;
    uint16_t live_ptr(const uint8_t* ram, const seq::Position& pos, int v) const override { (void)pos; return stream::rd16(ram, L.slot[v]); }
    void live_state_writes(const uint8_t* ram, const seq::Position& pos, int voice, uint16_t ptr, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    void track_pointer_writes(const seq::Song& song, int pattern_idx, int voice, uint16_t dest, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    void free_space_bounds(int& lo, int& hi) const override { lo = data_lo_; hi = 0x10000; }

    void decode(const uint8_t* p, int pc, stream::State& s, seq::Event& e, stream::Flow& f) const override;
    const stream::CmdSpec& spec(uint8_t op) const override;
    uint16_t track_start(const uint8_t* ram, uint16_t header, int v) const override;
    std::vector<uint16_t> song_headers(const uint8_t* ram) const override;
    uint16_t live_ptr_addr(int v) const override { return L.slot[v]; }
    int  ptr_span() const override { return 2; }
    stream::State initial_state(const uint8_t* ram, uint16_t header, int voice) const override;
    bool ptr_after_note() const override { return true; }

private:
    mutable int data_lo_ = 0x200;
    mutable int ppqn_ = 48;
};

std::unique_ptr<seq::Driver> detect(const uint8_t* ram);

}
