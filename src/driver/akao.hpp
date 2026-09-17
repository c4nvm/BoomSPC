// Square's AKAO sound driver (SNES). Super Mario RPG is the reference and
// the only variant verified here; the other revisions (FF4 = rev.1,
// Romancing SaGa = rev.2, FF5 / Seiken Densetsu 2 / Mystic Quest = rev.3,
// RS2 / FF6 / Chrono Trigger / Live A Live / Front Mission / RS3 / Gun
// Hazard = rev.4) use the fingerprints and opcode maps that loveemu's
// akaospc documents, and are marked untested until someone runs them.
//
// Stream format (all revisions):
//   note byte n < first_command: key = n / lengths, len = n % lengths, with
//   12 keys per octave, key 12 = tie, key 13 = rest (rev.1 swaps those). The
//   length index reads a table of tick counts (whole note = $C0, 48 per
//   quarter); Super Mario RPG's index 13 means "explicit length byte next".
//   Pitch = current octave * 12 + key: the octave is state set by commands.
//   Commands first_command..FF have fixed argument counts from a table in
//   the driver. Repeats: loop start n ... loop end; a conditional jump exits
//   the repeat on a given pass; a jump backwards is the song loop.
//   Super Mario RPG instead marks the loop point (D7) and jumps back to it
//   (D0), and breaks out of a repeat on its last pass (D6).
//
// The song header lists the eight track start addresses (rev.3/4 store ROM
// addresses plus a base to relocate them; Super Mario RPG prefixes the
// header with 5-byte instrument records). Tracks are contiguous byte runs,
// so edits are patched in place for now.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "seq.hpp"

namespace akao {
enum class Variant { Unknown, Rev1, Rev2, Rev3, Rev4, SMRPG };

enum class Kind {
    Effect, Nop, OctaveSet, OctaveUp, OctaveDown, TransposeAbs, TransposeRel, Instrument, Tempo,
    LoopStart, LoopEnd, CondJump, Jump, End, ForceLength, Mark, LoopToMark, BreakLast, LoopEndCpu, SubseqJump,
};

struct Cmd {
    Kind        kind = Kind::Effect;
    int8_t      argc = 0;         // from the driver's table
    const char* name = "?";
    const char* code = "???";
    seq::FxClass cls = seq::FxClass::Misc;
};

struct Layout {
    Variant  variant = Variant::Unknown;
    uint8_t  first_cmd = 0;          // $C4 or $D2
    int      lengths = 14;           // note byte divisor (first_cmd / 14)
    uint16_t cmd_table = 0, len_table = 0;   // jump table and argument-count table
    bool     len_is_total = false;   // SMRPG: low 3 bits = opcode + args
    bool     key_is_remainder = false; // SMRPG: key = byte % 14, length = byte / 14 (others: the reverse)
    uint16_t note_len_table = 0;     // tick counts per length index
    int      note_len_count = 14;    // 13 for SMRPG (index 13 = explicit byte)
    uint8_t  note_lens[16] = {};     // copy of the table, so retime() needs no RAM
    uint16_t subseq_table = 0;       // SMRPG: 4-byte sequence entries CD/CE jump through
    int      timer0 = 0;             // rev.1-4: timer 0 latch (tempo is a fraction of it)
    bool     rev1_tie_rest_swap = false;
    uint16_t header = 0;             // song header (track pointer list) address
    bool     rom_addresses = false;  // header/jump targets are ROM addresses
    uint16_t apu_base = 0;           // ...relocated by apu_base - header word
    uint16_t track_ptr_base = 0;     // live pointers, 2 bytes per voice
    uint16_t octave_base = 0;        // live per-voice octave (SMRPG $183C), 0 = unknown
    int      octave_init = 4;        // octave every voice starts a song with (SMRPG writes 6)
    uint8_t  bgm_slot_var = 0;
    uint16_t tempo_addr = 0;         // timer 0 latch or tempo variable
    uint16_t inst_map = 0, sample_pairs = 0, adsr_pairs = 0, tune_pairs = 0;
    uint16_t mark_base = 0;          // SMRPG: per-voice loop mark addresses (D7 stores, D0 reads), 2 bytes per logical voice
    uint16_t rep_start_base = 0, rep_end_base = 0;   // SMRPG: repeat stack addresses, 6 bytes per logical voice (3 levels)
    Cmd      cmds[64];               // indexed by opcode - first_cmd
    bool     tested = false;         // verified against real rips in this project
    bool valid() const { return variant != Variant::Unknown; }
};

Layout detect_layout(const uint8_t* ram);
Layout smrpg_layout_for_tests();

class AkaoDriver : public seq::Driver {
public:
    explicit AkaoDriver(Layout layout) : L(layout) {}
    Layout L;

    std::string name() const override;
    const char* id() const override { return "akao"; }
    int     key_of(uint8_t b) const { return L.key_is_remainder ? b % 14 : b / L.lengths; }
    int     len_of(uint8_t b) const { return L.key_is_remainder ? b / 14 : b % L.lengths; }
    uint8_t pack(int key, int len) const { return uint8_t(L.key_is_remainder ? len * 14 + key : key * L.lengths + len); }
    uint8_t rest_byte() const override { return pack(13, 0); }
    uint8_t tie_byte() const override { return pack(12, 0); }
    bool    is_note_byte(uint8_t b) const override { return b < L.first_cmd && key_of(b) < 12; }
    int     note_semitone(uint8_t b) const override { return 48 + key_of(b); }
    uint8_t note_byte(int semitone_from_c0) const override { return pack((semitone_from_c0 % 12 + 12) % 12, 0); }
    uint8_t note_min() const override { return 0; }
    uint8_t note_max() const override { return uint8_t(L.first_cmd - 1); }
    bool    transpose_event(seq::Event& e, int semis) const override;
    bool        is_command(uint8_t b) const override { return b >= L.first_cmd; }
    int         cmd_size(uint8_t op) const override;
    const char* cmd_name(uint8_t op) const override;
    const char* cmd_code(uint8_t op) const override;
    seq::FxClass cmd_class(uint8_t op) const override;
    bool        is_instrument_cmd(uint8_t op) const override;
    uint8_t     instrument_opcode() const override;
    uint8_t     first_command() const override { return L.first_cmd; }
    int         command_count() const override { return 0x100 - L.first_cmd; }
    std::string event_text(const seq::Event& e) const override;
    bool       has_instruments() const override { return L.inst_map != 0; }
    int        instrument_count(const uint8_t* ram) const override;
    seq::Instrument read_instrument(const uint8_t* ram, int index) const override;
    bool       instrument_used(const uint8_t* ram, int index) const override;
    bool       preview_regs(const uint8_t* ram, uint8_t note_byte, int instrument, uint8_t regs[8]) const override;
    std::vector<seq::Song> find_songs(const uint8_t* ram, const uint8_t* dsp) const override;
    int  pick_current_song(const uint8_t* ram, const std::vector<seq::Song>& songs) const override { (void)ram; return songs.empty() ? -1 : 0; }
    seq::Position locate(const uint8_t* ram, const seq::Song& song, const seq::Position* prev) const override;
    double ticks_per_second(const uint8_t* ram) const override;
    bool   tempo_writes(const uint8_t* ram, double tps, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    std::vector<uint8_t> serialize_track(const std::vector<seq::Event>& events) const override;
    void retime(std::vector<seq::Event>& ev) const override;
    bool in_place_only() const override { return true; }
    bool has_stream_edit() const override { return true; }
    bool end_bytes(uint8_t out[16], int& size) const override {
        for (int o = L.first_cmd; o < 0x100; ++o) if (cmd(uint8_t(o))->kind == Kind::End) { out[0] = uint8_t(o); size = 1; return true; }
        return false;
    }
    bool set_duration(std::vector<seq::Event>& ev, int i, int dur) const override;
    void apply_note_byte(seq::Event& e, uint8_t byte) const override;
    bool ends_stream(const seq::Event& e) const override;
    int  jump_target(const seq::Event& e) const override;
    void set_jump_target(seq::Event& e, uint16_t addr) const override;
    void track_pointer_writes(const seq::Song& song, int pattern_idx, int voice, uint16_t dest, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    void live_state_writes(const uint8_t* ram, const seq::Position& pos, int voice, uint16_t ptr, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    bool remaps_stack() const override { return L.rep_start_base != 0; }
    uint16_t from_aram(const uint8_t* ram, uint16_t addr) const;
    bool set_note_at(std::vector<seq::Event>& ev, int tick, uint8_t note_byte, int pattern_len) const override;
    bool enter_note(std::vector<seq::Event>& ev, int tick, int semitone, int pattern_len) const override;
    bool is_frame_command(const seq::Event& e) const override {
        if (e.type != seq::EventType::Command) return false;
        const Cmd* c = cmd(e.b[0]);
        return c && (c->kind == Kind::LoopStart || c->kind == Kind::LoopEnd || c->kind == Kind::LoopEndCpu || c->kind == Kind::BreakLast || c->kind == Kind::CondJump);
    }
    bool insert_command_at(std::vector<seq::Event>& ev, int tick, const uint8_t* bytes, int size) const override;
    bool set_instrument(std::vector<seq::Event>& ev, int tick0, int tick1, uint8_t ins) const override;
    bool remove_span(std::vector<seq::Event>& ev, int tick, int ticks, bool keep_length) const override;
    bool insert_span(std::vector<seq::Event>& ev, int tick, int ticks, uint8_t byte) const override;

    seq::Track parse_track(const uint8_t* ram, uint16_t start, int octave) const;
    uint16_t to_aram(const uint8_t* ram, uint16_t addr) const;
    int bgm_slot(const uint8_t* ram) const;
    const Cmd* cmd(uint8_t op) const { return op >= L.first_cmd ? &L.cmds[op - L.first_cmd] : nullptr; }
};

std::unique_ptr<seq::Driver> detect(const uint8_t* ram);

}
