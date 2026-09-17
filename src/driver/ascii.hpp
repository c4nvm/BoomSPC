// ASCII's SNES sound driver by Shuichi Ukai (Ardy Lightfoot, Wizardry VI,
// Down the World...). Command set after VGMTrans' AsciiShuichiSnes with
// the Ardy Lightfoot build's opcodes read from its dispatcher; layout read
// from the driver code in ARAM.
//
// One song header at a fixed address: 8 track pointers stored as a low
// byte array then a high byte array (only the first N voices load).
// Streams (Ardy build; the Wizardry build puts the notes 12 higher):
//   A0-FE       note: key = byte - A0; then an optional length byte (< 80,
//               else the last length), an optional FF (slur: the next note
//               does not retrigger) and an optional 9A a b c (skipped)
//   9B [len]    rest
//   81 ... 82   song loop   83 ... 84 n   repeat n times (0 = 256)
//   86 addr     call (four levels)   87  return   80  end
//   89 i  instrument   8A t  tempo   8B t  transpose
// Timer 0 counts tempo events per tick: ticks per second = (8000 / latch) / tempo.
#pragma once

#include <algorithm>

#include "stream.hpp"

namespace ascii {
struct Layout {
    int      version = 1;          // 1 Ardy Lightfoot (notes from A0), 2 Wizardry VI (notes from AC)
    uint16_t header_lo = 0, header_hi = 0;   // song header byte arrays
    uint16_t ptr_lo = 0;           // live pointer low bytes (+voice)
    uint8_t  ptr_hi_zp = 0;        // live pointer high bytes (zero page +voice)
    uint8_t  voices = 8;
    uint8_t  note_base = 0xA0;
    uint8_t  tempo_zp = 0, tick_zp = 0;      // tempo argument, the divider it derives
    uint16_t call_sp = 0, call_lo = 0, call_hi = 0;      // call stack: index (+voice), entries (+index)
    uint16_t rep_sp = 0, rep_lo = 0, rep_hi = 0;         // repeat starts
    uint16_t end_sp = 0, end_lo = 0, end_hi = 0, end_count = 0;   // repeat ends and counters
    uint16_t loop_lo = 0; uint8_t loop_hi_zp = 0;        // song loop point (81)
    bool valid() const { return header_lo && ptr_lo; }
};

Layout detect_layout(const uint8_t* ram);

class AsciiDriver : public stream::Driver {
public:
    explicit AsciiDriver(Layout layout) : L(layout) {}
    Layout L;

    std::string name() const override { return L.version == 1 ? "ASCII / Shuichi Ukai (Ardy Lightfoot)" : "ASCII / Shuichi Ukai (Wizardry VI) - untested"; }
    const char* id() const override { return "ascii"; }
    uint8_t rest_byte() const override { return 0x9B; }
    uint8_t tie_byte() const override { return 0; }
    bool    is_note_byte(uint8_t b) const override { return b >= L.note_base && b < 0xFF; }
    int     note_semitone(uint8_t b) const override { return b - L.note_base + 12; }
    uint8_t note_byte(int semitone_from_c0) const override { return uint8_t(std::clamp(semitone_from_c0 - 12 + L.note_base, int(L.note_base), 0xFE)); }
    uint8_t note_min() const override { return L.note_base; }
    uint8_t note_max() const override { return 0xFE; }
    bool    transpose_event(seq::Event& e, int semis) const override;
    bool        is_command(uint8_t b) const override { return (b >= 0x80 && b < L.note_base) || b == 0xFF; }
    bool        is_instrument_cmd(uint8_t op) const override { return op == 0x89; }
    uint8_t     instrument_opcode() const override { return 0x89; }
    uint8_t     first_command() const override { return 0x80; }
    int         command_count() const override { return L.note_base - 0x80; }
    std::string event_text(const seq::Event& e) const override;
    bool pitch_fx(const seq::Event& e, seq::PitchFx& out) const override { (void)e; (void)out; return false; }
    bool note_retriggers(const std::vector<seq::Event>& ev, int i) const override;
    bool       has_instruments() const override { return false; }
    int        instrument_count(const uint8_t* ram) const override { (void)ram; return 0; }
    seq::Instrument read_instrument(const uint8_t* ram, int index) const override { (void)ram; (void)index; return seq::Instrument{}; }
    bool       preview_regs(const uint8_t* ram, uint8_t note_byte, int instrument, uint8_t regs[8]) const override { (void)ram; (void)note_byte; (void)instrument; (void)regs; return false; }
    double ticks_per_second(const uint8_t* ram) const override;
    bool   tempo_writes(const uint8_t* ram, double tps, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    bool end_bytes(uint8_t out[16], int& size) const override { out[0] = 0x80; size = 1; return true; }
    int  end_park_offset() const override { return 0; }
    bool set_duration(std::vector<seq::Event>& ev, int i, int dur) const override;
    void apply_note_byte(seq::Event& e, uint8_t byte) const override;
    int  call_count(const seq::Event& e) const override { return e.type == seq::EventType::Command && e.b[0] == 0x86 ? 1 : 0; }
    uint16_t live_ptr(const uint8_t* ram, const seq::Position& pos, int v) const override;
    void live_state_writes(const uint8_t* ram, const seq::Position& pos, int voice, uint16_t ptr, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    void live_extra_writes(const uint8_t* ram, int voice, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    void track_pointer_writes(const seq::Song& song, int pattern_idx, int voice, uint16_t dest, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    void free_space_bounds(int& lo, int& hi) const override { lo = data_lo_; hi = 0x10000; }

    void decode(const uint8_t* p, int pc, stream::State& s, seq::Event& e, stream::Flow& f) const override;
    const stream::CmdSpec& spec(uint8_t op) const override;
    uint16_t track_start(const uint8_t* ram, uint16_t header, int v) const override;
    std::vector<uint16_t> song_headers(const uint8_t* ram) const override { (void)ram; return {L.header_lo}; }
    uint16_t live_ptr_addr(int v) const override { return uint16_t(L.ptr_lo + v); }
    int  ptr_span() const override { return 1; }
    stream::State initial_state(const uint8_t* ram, uint16_t header, int voice) const override;
    uint8_t note_byte_in(int semitone, const stream::State& s) const override { return note_byte(semitone - s.trans); }
    bool ptr_after_note() const override { return true; }

private:
    mutable int data_lo_ = 0x200;
};

std::unique_ptr<seq::Driver> detect(const uint8_t* ram);

}
