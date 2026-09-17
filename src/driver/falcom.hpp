// Falcom's SNES sound driver (Ys V). Format after VGMTrans' FalcomSnes;
// layout read from the driver code in ARAM.
//
// The song header (its address in a zero-page word) holds 8 track offsets
// relative to the header (0 = unused) and, at +18, seven note lengths.
// Streams:
//   00-CF       note: high nibble = key (0-11 from C, 12 = rest), bit 3 =
//               slur (the next note does not retrigger when it has the same
//               key), low 3 bits = length: 1-7 from the header's table, 0 =
//               a length byte follows
//   D0-D6       octave 0-6   D7 t  tempo   D8 i  instrument   DD q  gate
//   ED n c      repeat n times; c is the live counter the driver keeps in the stream
//   EF rel      repeat end (rel points at the counter)   EE rel  leave the
//               repeat on its last pass (rel points past the EF)
//   FC rel      jump (0 = end of track)
// Ticks: tempo / 256 per timer-0 event, so (8000 / latch) * tempo / 256 per second.
#pragma once

#include <algorithm>

#include "stream.hpp"

namespace falcom {
struct Layout {
    uint8_t  header_zp = 0;        // zero-page word holding the song header address
    uint8_t  ptr_lo_zp = 0, ptr_hi_zp = 0;   // live pointer bytes (+voice)
    uint8_t  tempo_zp = 0;
    bool valid() const { return header_zp && ptr_lo_zp; }
};

Layout detect_layout(const uint8_t* ram);

class FalcomDriver : public stream::Driver {
public:
    explicit FalcomDriver(Layout layout) : L(layout) {}
    Layout L;

    std::string name() const override { return "Falcom (Ys V)"; }
    const char* id() const override { return "falcom"; }
    uint8_t rest_byte() const override { return 0xC0; }
    uint8_t tie_byte() const override { return 0; }
    bool    is_note_byte(uint8_t b) const override { return b < 0xC0; }
    int     note_semitone(uint8_t b) const override { return 4 * 12 + (b >> 4) + 12; }
    uint8_t note_byte(int semitone_from_c0) const override { return uint8_t((((semitone_from_c0 % 12) + 12) % 12) << 4); }
    uint8_t note_min() const override { return 0x00; }
    uint8_t note_max() const override { return 0xBF; }
    bool    transpose_event(seq::Event& e, int semis) const override;
    bool        is_command(uint8_t b) const override { return b >= 0xD0; }
    bool        is_instrument_cmd(uint8_t op) const override { return op == 0xD8; }
    uint8_t     instrument_opcode() const override { return 0xD8; }
    uint8_t     first_command() const override { return 0xD0; }
    int         command_count() const override { return 0x2D; }
    std::string event_text(const seq::Event& e) const override;
    bool pitch_fx(const seq::Event& e, seq::PitchFx& out) const override;
    bool note_retriggers(const std::vector<seq::Event>& ev, int i) const override;
    bool       has_instruments() const override { return false; }
    int        instrument_count(const uint8_t* ram) const override { (void)ram; return 0; }
    seq::Instrument read_instrument(const uint8_t* ram, int index) const override { (void)ram; (void)index; return seq::Instrument{}; }
    bool       preview_regs(const uint8_t* ram, uint8_t note_byte, int instrument, uint8_t regs[8]) const override { (void)ram; (void)note_byte; (void)instrument; (void)regs; return false; }
    double ticks_per_second(const uint8_t* ram) const override;
    bool   tempo_writes(const uint8_t* ram, double tps, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    bool end_bytes(uint8_t out[16], int& size) const override { out[0] = 0xFC; out[1] = 0; out[2] = 0; size = 3; return true; }
    bool set_duration(std::vector<seq::Event>& ev, int i, int dur) const override;
    void apply_note_byte(seq::Event& e, uint8_t byte) const override;
    int  jump_target(const seq::Event& e) const override;
    void set_jump_target(seq::Event& e, uint16_t addr) const override;
    std::vector<uint8_t> serialize_relocated(const std::vector<seq::Event>& ev, uint16_t dest, std::vector<int>* offsets) const override;
    bool remaps_stack() const override { return true; }   // the repeat counters travel with the bytes
    uint16_t live_ptr(const uint8_t* ram, const seq::Position& pos, int v) const override;
    void live_state_writes(const uint8_t* ram, const seq::Position& pos, int voice, uint16_t ptr, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    bool enter_note(std::vector<seq::Event>& ev, int tick, int semitone, int pattern_len) const override;
    void track_pointer_writes(const seq::Song& song, int pattern_idx, int voice, uint16_t dest, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    void free_space_bounds(int& lo, int& hi) const override { lo = data_lo_; hi = 0x10000; }

    void decode(const uint8_t* p, int pc, stream::State& s, seq::Event& e, stream::Flow& f) const override;
    const stream::CmdSpec& spec(uint8_t op) const override;
    uint16_t track_start(const uint8_t* ram, uint16_t header, int v) const override;
    std::vector<uint16_t> song_headers(const uint8_t* ram) const override;
    uint16_t live_ptr_addr(int v) const override { return uint16_t(L.ptr_lo_zp + v); }
    int  ptr_span() const override { return 1; }
    stream::State initial_state(const uint8_t* ram, uint16_t header, int voice) const override;
    bool ptr_after_note() const override { return true; }

private:
    mutable int data_lo_ = 0x200;
    mutable uint8_t lens_[7] = {0x30, 0x18, 0x0C, 0x06, 0x03, 0x24, 0x12};   // the current song's length table
};

std::unique_ptr<seq::Driver> detect(const uint8_t* ram);

}
