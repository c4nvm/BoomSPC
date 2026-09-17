// Graphic Research's SNES sound driver (Derby Jockey 2, Mickey no Tokyo
// Disneyland Daibouken, Ganso Pachi-Slot Nippon'ichi...). Format after
// VGMTrans' GraphResSnes; layout read from the driver code in ARAM.
//
// One song header at a fixed address: 8 x [enabled][ROM address]; a
// track sits at header + 24 + (its ROM address - the first entry's).
// Streams:
//   00-7F       note: low 4 bits = key (0-6 C D E F G A B, 8-14 the same
//               a semitone up, 7 = rest, 15 = wait), bit 4 = a length
//               byte follows (else FD's default)
//   80-8F vol   90-9F octave   E7 / E8  octave down / up   E4 t  transpose
//   EA ... EB n rel   repeat n times (rel = offset back from the EB)
//   E9  leave the repeat on its last pass   EE n rel  one-level repeat
//   F9 rel  call (two levels)   F8  return   FA rel  jump   FF  end
//   FE  slur: the next note does not retrigger when it has the same key
//   EC r  gate (r/8)   F1 v  volume   F4 p  pan   F7 a d  ADSR   FC i  instrument
// One tick per timer-0 event (latch $85: about 60 per second).
#pragma once

#include <algorithm>
#include <map>

#include "stream.hpp"

namespace graphres {
struct Layout {
    uint16_t header = 0;           // song header
    uint8_t  ptr_zp = 0;           // zero page: voice pointer words (+2 * voice)
    uint8_t  enable_zp = 0;        // per-voice enable byte
    uint8_t  len_zp = 0;           // per-voice default note length
    uint8_t  call_sp_zp = 0;       // per-voice call stack index
    uint16_t call_base = 0;        // call stacks: 4 bytes per voice
    uint16_t loop_sp = 0;          // per-voice repeat stack index (+voice)
    uint16_t loop_count = 0;       // repeat counters (+voice +index)
    uint16_t loop_lo = 0, loop_hi = 0;   // address of the repeat's EB (+voice +index)
    bool valid() const { return header && ptr_zp; }
};

Layout detect_layout(const uint8_t* ram);

class GraphResDriver : public stream::Driver {
public:
    explicit GraphResDriver(Layout layout) : L(layout) {}
    Layout L;

    std::string name() const override { return "Graphic Research (Derby Jockey 2 / Mickey no Tokyo Disneyland)"; }
    const char* id() const override { return "graphres"; }
    uint8_t rest_byte() const override { return 0x07; }
    uint8_t tie_byte() const override { return 0x0F; }
    bool    is_note_byte(uint8_t b) const override { return b < 0x80 && (b & 7) != 7; }
    int     note_semitone(uint8_t b) const override;
    uint8_t note_byte(int semitone_from_c0) const override;
    uint8_t note_min() const override { return 0x00; }
    uint8_t note_max() const override { return 0x0E; }
    bool    transpose_event(seq::Event& e, int semis) const override;
    bool        is_command(uint8_t b) const override { return b >= 0x80; }
    bool        is_instrument_cmd(uint8_t op) const override { return op == 0xFC; }
    uint8_t     instrument_opcode() const override { return 0xFC; }
    uint8_t     first_command() const override { return 0xE0; }
    int         command_count() const override { return 0x20; }
    std::string event_text(const seq::Event& e) const override;
    bool pitch_fx(const seq::Event& e, seq::PitchFx& out) const override { (void)e; (void)out; return false; }
    bool note_retriggers(const std::vector<seq::Event>& ev, int i) const override;
    bool       has_instruments() const override { return false; }
    int        instrument_count(const uint8_t* ram) const override { (void)ram; return 0; }
    seq::Instrument read_instrument(const uint8_t* ram, int index) const override { (void)ram; (void)index; return seq::Instrument{}; }
    bool       preview_regs(const uint8_t* ram, uint8_t note_byte, int instrument, uint8_t regs[8]) const override { (void)ram; (void)note_byte; (void)instrument; (void)regs; return false; }
    double ticks_per_second(const uint8_t* ram) const override { return ram[0xFA] ? 8000.0 / ram[0xFA] : 0; }
    bool   tempo_writes(const uint8_t* ram, double tps, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    bool end_bytes(uint8_t out[16], int& size) const override { out[0] = 0xFF; size = 1; return true; }
    bool set_duration(std::vector<seq::Event>& ev, int i, int dur) const override;
    void apply_note_byte(seq::Event& e, uint8_t byte) const override;
    int  call_count(const seq::Event& e) const override { return e.type == seq::EventType::Command && e.b[0] == 0xF9 ? 1 : 0; }
    int  jump_target(const seq::Event& e) const override;
    void set_jump_target(seq::Event& e, uint16_t addr) const override;
    std::vector<uint8_t> serialize_relocated(const std::vector<seq::Event>& ev, uint16_t dest, std::vector<int>* offsets) const override;
    void live_extra_writes(const uint8_t* ram, int voice, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    bool enter_note(std::vector<seq::Event>& ev, int tick, int semitone, int pattern_len) const override;
    void track_pointer_writes(const seq::Song& song, int pattern_idx, int voice, uint16_t dest, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    void free_space_bounds(int& lo, int& hi) const override { lo = data_lo_; hi = 0x10000; }
    int  echo_length(const uint8_t* ram, int dsp_edl) const override;

    void decode(const uint8_t* p, int pc, stream::State& s, seq::Event& e, stream::Flow& f) const override;
    const stream::CmdSpec& spec(uint8_t op) const override;
    uint16_t track_start(const uint8_t* ram, uint16_t header, int v) const override;
    std::vector<uint16_t> song_headers(const uint8_t* ram) const override { (void)ram; return {L.header}; }
    uint16_t live_ptr_addr(int v) const override { return uint16_t(L.ptr_zp + v * 2); }
    int  ptr_span() const override { return 2; }
    stream::State initial_state(const uint8_t* ram, uint16_t header, int voice) const override;
    bool ptr_after_note() const override { return true; }

private:
    mutable int data_lo_ = 0x200;
    mutable std::map<uint16_t, uint16_t> base_;   // header -> ROM address of its first entry
};

std::unique_ptr<seq::Driver> detect(const uint8_t* ram);

}
