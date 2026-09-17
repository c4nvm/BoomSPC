// Mint's SNES sound driver (Michiya Hirasawa: Gokinjo Boukentai, Shien's
// Revenge, The Combatribes, Lennus II...; VGMTrans calls it "Mori").
// Format after VGMTrans' MoriSnes. The command numbering differs between
// builds, so the opcodes are classified from the driver's own handler
// table: how many bytes a handler reads and what it does with them.
//
// A song header lists [voice][rel16] pairs (offset from the byte after
// the word) ended by FF. Streams are MIDI-like: an event may be preceded
// by up to three parameter bytes < 80: delta time (ticks to the next
// event), gate length (0 = tie into the next note of the same key) and
// velocity, each remembered for later notes. Then:
//   80-9F       note: key = note base + low 5 bits
//   A0-BF       note with one more byte: gate (< 80) or velocity (>= 80)
//   C0-FF       commands: instrument (rel16 to its data), volume, pan,
//               tempo, transpose, note base / octave up / down, jump
//               (rel16), call (rel16) / return, repeat start (count) /
//               end, wait (the current delta without a note), end
// Ticks run at (8000 / timer 0 latch) * tempo / 256 per second.
#pragma once

#include <algorithm>
#include <map>

#include "stream.hpp"

namespace mint {
enum class Kind : uint8_t { Nop, Args, End, Jump, Call, Ret, RepStart, RepEnd, Wait, NoteBase, OctUp, OctDown, Transpose, TransposeRel, Tempo, Instrument, Volume, Pan };

struct Layout {
    uint16_t song_list = 0;
    uint16_t cmd_table = 0;
    uint8_t  ptr_lo_zp = 0x3C, ptr_hi_zp = 0x4E;   // live pointer byte arrays (+voice)
    uint8_t  stack_zp = 0;         // per-voice index into the call stack, 0 = unknown
    uint16_t stack_base = 0;
    uint8_t  tempo_zp = 0;         // 16-bit tempo (8.8), 0 = unknown
    struct Op { Kind kind = Kind::Nop; uint8_t args = 0; bool rel = true; } ops[0x40];
    bool valid() const { return song_list && cmd_table; }
};

Layout detect_layout(const uint8_t* ram);

class MintDriver : public stream::Driver {
public:
    explicit MintDriver(Layout layout) : L(layout) {}
    Layout L;

    std::string name() const override { return "Mint (Gokinjo Boukentai / Shien's Revenge / Combatribes)"; }
    const char* id() const override { return "mint"; }
    uint8_t rest_byte() const override { return wait_op_; }
    uint8_t tie_byte() const override { return 0; }
    bool    is_note_byte(uint8_t b) const override { return b >= 0x80 && b < 0xC0; }
    int     note_semitone(uint8_t b) const override { return 48 + (b & 0x1F); }
    uint8_t note_byte(int semitone_from_c0) const override { return uint8_t(0x80 | std::clamp(semitone_from_c0 - 48, 0, 31)); }
    uint8_t note_min() const override { return 0x80; }
    uint8_t note_max() const override { return 0x9F; }
    bool    transpose_event(seq::Event& e, int semis) const override;
    bool        is_command(uint8_t b) const override { return b >= 0xC0; }
    bool        is_instrument_cmd(uint8_t op) const override { return op == ins_op_; }
    uint8_t     instrument_opcode() const override { return ins_op_; }
    uint8_t     first_command() const override { return 0xC0; }
    int         command_count() const override { return 0x40; }
    std::string event_text(const seq::Event& e) const override;
    bool       has_instruments() const override { return false; }
    int        instrument_count(const uint8_t* ram) const override { (void)ram; return 0; }
    seq::Instrument read_instrument(const uint8_t* ram, int index) const override { (void)ram; (void)index; return seq::Instrument{}; }
    bool       preview_regs(const uint8_t* ram, uint8_t note_byte, int instrument, uint8_t regs[8]) const override { (void)ram; (void)note_byte; (void)instrument; (void)regs; return false; }
    double ticks_per_second(const uint8_t* ram) const override;
    bool   tempo_writes(const uint8_t* ram, double tps, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    bool end_bytes(uint8_t out[16], int& size) const override { out[0] = end_op_; size = 1; return end_op_ != 0; }
    bool set_duration(std::vector<seq::Event>& ev, int i, int dur) const override;
    void apply_note_byte(seq::Event& e, uint8_t byte) const override;
    int  call_count(const seq::Event& e) const override { return e.type == seq::EventType::Command && L.ops[std::max(0, e.b[0] - 0xC0)].kind == Kind::Call && e.b[0] >= 0xC0 ? 1 : 0; }
    int  jump_target(const seq::Event& e) const override;
    void set_jump_target(seq::Event& e, uint16_t addr) const override;
    std::vector<uint8_t> serialize_relocated(const std::vector<seq::Event>& ev, uint16_t dest, std::vector<int>* offsets) const override;
    uint16_t live_ptr(const uint8_t* ram, const seq::Position& pos, int v) const override;
    void live_state_writes(const uint8_t* ram, const seq::Position& pos, int voice, uint16_t ptr, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    void track_pointer_writes(const seq::Song& song, int pattern_idx, int voice, uint16_t dest, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    void free_space_bounds(int& lo, int& hi) const override { lo = data_lo_; hi = 0x10000; }

    void decode(const uint8_t* p, int pc, stream::State& s, seq::Event& e, stream::Flow& f) const override;
    const stream::CmdSpec& spec(uint8_t op) const override;
    uint16_t track_start(const uint8_t* ram, uint16_t header, int v) const override;
    std::vector<uint16_t> song_headers(const uint8_t* ram) const override;
    uint16_t live_ptr_addr(int v) const override { return uint16_t(L.ptr_lo_zp + v); }
    stream::State initial_state(const uint8_t* ram, uint16_t header, int voice) const override;
    uint8_t note_byte_in(int semitone, const stream::State& s) const override;
    bool ptr_after_note() const override { return true; }

    void classify();   // fills the opcode helpers below from L.ops
private:
    uint8_t wait_op_ = 0, end_op_ = 0, ins_op_ = 0;
    mutable int data_lo_ = 0x200;
    mutable std::map<uint32_t, uint16_t> slots_;   // (header << 8 | voice) -> address of its header word
};

std::unique_ptr<seq::Driver> detect(const uint8_t* ram);

}
