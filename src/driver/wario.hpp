// Wario's Woods' sound engine: the sequencer runs on the SNES CPU (bank $0B
// of the ROM, reverse-engineered from the snsflib), the SPC700 is only a
// slave that takes DSP writes and sample uploads over the APU ports. So
// the "RAM" this driver sees is the SNSF address space BoomSPC synthesises
// (see Engine::snsf_read): $0000-$0FFF = WRAM $7E:0000.., $1000-$1FFF =
// WRAM $7E:F000.. (the engine's voice state), $8000-$FFFF = the current
// song's ROM bank. Song data comes from the whole ROM image, which the
// driver shares with the emulator, so edits show up everywhere.
//
// Song table (`BF xx xx yy` in the start routine): four bytes per song,
// pointer + bank, to a header of  count, then count x (voice, ptr16, bank,
// pad). Streams: 60.1 ticks per second (one per NMI), bytes
//   00..57 (+80)  note; bit 7 = a gate byte follows the duration
//   58            rest,   59  tie (the note carries on)
//   then the duration byte (1..127); bit 7 = a velocity byte follows
//   5A lo hi jump    5B lo hi call    5C return    5D n loop    5E loop end
//   5F x sample      60 x volume      61 x master volume   62 x pan
//   63 a b c vibrato / 64 off     65 a b c tremolo / 66 off
//   67/68 a b c pitch envelope / 69 off   6A a b pan slide   6B a b volume slide
//   6C a b c d SPC command 05   6D SPC 06   6E x SPC 07   6F x detune   70 end
// Per-voice live state in WRAM: pointers $FD18 (2 x 8), stack depth $FD10,
// call/loop stacks at $FE40 + 16 x voice, remaining ticks $FD08.
#pragma once

#include <memory>
#include <vector>

#include "seq.hpp"

namespace wario {
struct Layout {
    uint16_t song_table = 0;      // bank-relative
    uint8_t  song_bank = 0;
    int      songs = 0;
    uint16_t tick_entry = 0;      // for the fingerprint report
    bool valid() const { return song_table != 0 && songs > 0; }
};

Layout detect_layout(const std::vector<uint8_t>& rom);

class WarioDriver : public seq::Driver {
public:
    WarioDriver(Layout layout, std::shared_ptr<const std::vector<uint8_t>> rom) : L(layout), rom_(std::move(rom)) {}
    Layout L;

    std::string name() const override { return "Nintendo SNES-side engine (Wario's Woods)"; }
    const char* id() const override { return "wario"; }
    uint8_t rest_byte() const override { return 0x58; }
    uint8_t tie_byte() const override { return 0x59; }
    bool    is_note_byte(uint8_t b) const override { return (b & 0x7F) < 0x58; }
    int     note_semitone(uint8_t b) const override { return int(b & 0x7F) + 24; }
    uint8_t note_byte(int semitone_from_c0) const override;
    uint8_t note_min() const override { return 0; }
    uint8_t note_max() const override { return 0x57; }
    bool    transpose_event(seq::Event& e, int semis) const override;
    void    apply_note_byte(seq::Event& e, uint8_t byte) const override;
    bool        is_command(uint8_t b) const override { return (b & 0x7F) >= 0x5A; }
    int         cmd_size(uint8_t op) const override;
    const char* cmd_name(uint8_t op) const override;
    const char* cmd_code(uint8_t op) const override;
    seq::FxClass cmd_class(uint8_t op) const override;
    bool        is_instrument_cmd(uint8_t op) const override { return op == 0x5F; }
    uint8_t     instrument_opcode() const override { return 0x5F; }
    uint8_t     first_command() const override { return 0x5A; }
    int         command_count() const override { return 0x71 - 0x5A; }
    std::string event_text(const seq::Event& e) const override;
    bool pitch_fx(const seq::Event& e, seq::PitchFx& out) const override;
    bool is_frame_command(const seq::Event& e) const override { return e.type == seq::EventType::Command && (e.b[0] == 0x5B || e.b[0] == 0x5C || e.b[0] == 0x5D || e.b[0] == 0x5E); }
    int  call_count(const seq::Event& e) const override { return e.type == seq::EventType::Command && e.b[0] == 0x5B ? 1 : 0; }
    bool       has_instruments() const override { return true; }
    int        instrument_count(const uint8_t* ram) const override { (void)ram; return 64; }
    seq::Instrument read_instrument(const uint8_t* ram, int index) const override;
    bool       preview_regs(const uint8_t* ram, uint8_t note_byte, int instrument, uint8_t regs[8]) const override;
    std::vector<seq::Song> find_songs(const uint8_t* ram, const uint8_t* dsp) const override;
    int  pick_current_song(const uint8_t* ram, const std::vector<seq::Song>& songs) const override;
    seq::Position locate(const uint8_t* ram, const seq::Song& song, const seq::Position* prev) const override;
    double ticks_per_second(const uint8_t* ram) const override { (void)ram; return 21477272.0 / (262.0 * 1364.0); }
    int    default_ticks_per_beat() const override { return 24; }
    std::vector<uint8_t> serialize_track(const std::vector<seq::Event>& events) const override;
    void retime(std::vector<seq::Event>& ev) const override;
    bool in_place_only() const override { return true; }
    bool has_stream_edit() const override { return true; }
    bool end_bytes(uint8_t out[16], int& size) const override { out[0] = 0x70; size = 1; return true; }
    bool set_duration(std::vector<seq::Event>& ev, int i, int dur) const override;
    int  jump_target(const seq::Event& e) const override { return e.type == seq::EventType::Command && e.b[0] == 0x5A ? (e.b[1] | (e.b[2] << 8)) : -1; }
    void set_jump_target(seq::Event& e, uint16_t addr) const override { e.b[1] = uint8_t(addr & 0xFF); e.b[2] = uint8_t(addr >> 8); }
    void track_pointer_writes(const seq::Song& song, int pattern_idx, int voice, uint16_t dest, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    void live_state_writes(const uint8_t* ram, const seq::Position& pos, int voice, uint16_t ptr, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    bool remaps_stack() const override { return true; }
    bool set_note_at(std::vector<seq::Event>& ev, int tick, uint8_t note_byte, int pattern_len) const override;
    bool insert_command_at(std::vector<seq::Event>& ev, int tick, const uint8_t* bytes, int size) const override;
    bool set_instrument(std::vector<seq::Event>& ev, int tick0, int tick1, uint8_t ins) const override;
    bool remove_span(std::vector<seq::Event>& ev, int tick, int ticks, bool keep_commands) const override;
    bool insert_span(std::vector<seq::Event>& ev, int tick, int ticks, uint8_t byte) const override;
    bool spc_ram_space() const override { return false; }
    void free_space_bounds(int& lo, int& hi) const override { lo = 0x8000; hi = 0x10000; }
    int  song_bank(const seq::Song& song) const override { return song.bank; }

    seq::Track parse_track(uint8_t bank, uint16_t start) const;

private:
    std::shared_ptr<const std::vector<uint8_t>> rom_;
    uint8_t rd(uint8_t bank, uint32_t addr) const;
};

std::unique_ptr<seq::Driver> detect(std::shared_ptr<const std::vector<uint8_t>> rom);

}
