// Prism Kikaku's SNES sound driver (Nobuyuki Hara: Dual Orb, Dual Orb II,
// Cosmo Gang: The Video, King of Dragons...). Format after VGMTrans'
// PrismSnes (the Dual Orb II command set); layout read from the driver
// code in ARAM.
//
// A song header is a list of 4-byte entries [logical voice][DSP voice +
// flags][stream pointer] ended by a byte >= 80. Streams:
//   00-7F key / 80-9F noise note, then a length byte unless DD n set a
//   default length (DC turns it off), then a gate byte when F2 manual
//   gates are on (F1 off; otherwise the gate is half the length, capped
//   by F3's threshold)
//   EE  rest (with the length byte)   F4  tie with length   F5  tie
//   C0-C4 t  tempo   C5 addr  jump when C6 set the condition
//   DE n addr / DF n addr  repeat: jump back until played n + 1 times
//   E0  return   E1 addr  call   E2 addr  jump   E3 t  transpose   D8 t  relative
//   FE i  instrument   FF  end of track
// One tick per timer-0 event; the tempo byte is the latch, so ticks run
// at 8000 / tempo per second (48 per quarter).
#pragma once

#include <algorithm>
#include <map>

#include "stream.hpp"

namespace prism {
struct Layout {
    int      version = 3;          // 1 Cosmo Gang, 2 Dual Orb, 3 Dual Orb II
    uint16_t song_list = 0;
    uint16_t ptr_lo = 0, ptr_hi = 0;   // live pointer byte arrays (+logical voice)
    uint16_t sub_ret = 0;          // return address words (+2 * logical voice), 0 = unknown
    uint16_t code_end = 0;         // first unused byte after the command handlers
    bool valid() const { return song_list && ptr_lo; }
};

Layout detect_layout(const uint8_t* ram);

class PrismDriver : public stream::Driver {
public:
    explicit PrismDriver(Layout layout) : L(layout) {}
    Layout L;

    std::string name() const override;
    const char* id() const override { return "prism"; }
    uint8_t rest_byte() const override { return 0xEE; }
    uint8_t tie_byte() const override { return 0xF4; }
    bool    is_note_byte(uint8_t b) const override { return b < 0xA0; }
    int     note_semitone(uint8_t b) const override { return (b & 0x7F) - 12; }
    uint8_t note_byte(int semitone_from_c0) const override { return uint8_t(std::clamp(semitone_from_c0 + 12, 0, 0x7F)); }
    uint8_t note_min() const override { return 0x00; }
    uint8_t note_max() const override { return 0x7F; }
    bool    transpose_event(seq::Event& e, int semis) const override;
    bool        is_command(uint8_t b) const override { return b >= 0xA0; }
    bool        is_instrument_cmd(uint8_t op) const override { return op == 0xFE; }
    uint8_t     instrument_opcode() const override { return 0xFE; }
    uint8_t     first_command() const override { return 0xC0; }
    int         command_count() const override { return 0x40; }
    std::string event_text(const seq::Event& e) const override;
    bool pitch_fx(const seq::Event& e, seq::PitchFx& out) const override;
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
    int  call_count(const seq::Event& e) const override { return e.type == seq::EventType::Command && e.b[0] == 0xE1 ? 1 : 0; }
    uint16_t live_ptr(const uint8_t* ram, const seq::Position& pos, int v) const override;
    void live_state_writes(const uint8_t* ram, const seq::Position& pos, int voice, uint16_t ptr, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    void track_pointer_writes(const seq::Song& song, int pattern_idx, int voice, uint16_t dest, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    void free_space_bounds(int& lo, int& hi) const override { lo = L.code_end ? L.code_end : data_lo_; hi = 0x10000; }

    void decode(const uint8_t* p, int pc, stream::State& s, seq::Event& e, stream::Flow& f) const override;
    const stream::CmdSpec& spec(uint8_t op) const override;
    uint16_t track_start(const uint8_t* ram, uint16_t header, int v) const override;
    std::vector<uint16_t> song_headers(const uint8_t* ram) const override;
    uint16_t live_ptr_addr(int v) const override { return uint16_t(L.ptr_lo + logical(v)); }
    void select_song(uint16_t header) const override { cur_ = header; }
    stream::State initial_state(const uint8_t* ram, uint16_t header, int voice) const override;
    uint8_t note_byte_in(int semitone, const stream::State& s) const override { return uint8_t(std::clamp(semitone + 12 - s.trans, 0, 0x7F)); }
    bool ptr_after_note() const override { return true; }

private:
    mutable int data_lo_ = 0x200;
    mutable uint16_t cur_ = 0;                                // header of the song whose live state is read
    mutable std::map<uint32_t, uint8_t>  logical_;            // (header << 8 | voice) -> logical voice
    mutable std::map<uint32_t, uint16_t> slots_;              // (header << 8 | voice) -> address of its pointer word
    int logical(int v) const { auto it = logical_.find(uint32_t(cur_) << 8 | uint32_t(v)); return it == logical_.end() ? v : it->second; }
};

std::unique_ptr<seq::Driver> detect(const uint8_t* ram);

}
