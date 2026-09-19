// Namco's second SNES driver (Junko Ozawa): Wagyan Paradise, J.League Soccer
// Prime Goal 2 / 3, Yuu Yuu Hakusho Tokubetsu Hen. Reverse-engineered from
// the driver code in ARAM; nothing public describes it.
//
// Four sequencer tracks, not eight. Each keeps a byte program whose live
// pointer is a zero-page word at $00 + track*2, and drives up to eight DSP
// voices through a bitmask set by command 01, so one stream can sound a
// chord. Masked commands carry one value per set bit.
//
//   00 n        note length in ticks
//   01 m        DSP voice mask this track drives
//   02 lo hi    call
//   03          return, or end of track outside a call
//   04 n        length multiplier (a note lasts 00's value * this)
//   05 n        per-track parameter
//   06/07 n l h repeat slot 1: loop back to l/h until the counter reaches n
//   08 lo hi    jump
//   09 m ...    note: one value per set bit in m, then wait. A voice in the
//               track's mask but not in m keeps sounding. Values: 00-53 a
//               pitched note (C-1 up), 54 retrigger, 55-7F noise (clock in
//               the low 5 bits), 80+ a percussion record (5 bytes, table
//               pointer at song_table - 4: sample, pitch, two parameters and
//               the note value that finally plays), FF key off.
//   0A n        echo delay      0B m ...  parameter block $04E0
//   0C n        per-track       0D n      echo on/off
//   0E          rest: wait without changing the notes
//   0F/10 n l h repeat slot 2
//   11 n        echo feedback   12 n  echo FIR set
//   13 n        echo volume     14 n  echo buffer address
//   18-97       parameter write: the low nibble picks a 32-byte block at
//               $0360 + n*$20, then a slot mask and one value per set bit
//
// Songs are not grouped in ARAM: the table of 3-byte {track, lo, hi} records
// holds one entry per track and the SNES starts each one separately, so the
// set that is playing comes from the four live pointers.
//
// The tracker shows one column per DSP voice, not per track: column v is
// the program of the track whose 01 mask has bit v, decoded for that bit
// alone (a 09 without the bit is a tie). Columns of one track share bytes;
// an edit in one is written back as the whole program. The owning track is
// taken from the masks a program sets before it loops, so a track that
// re-masks mid-song shows in the columns of its first mask.
#pragma once

#include <algorithm>
#include <array>

#include "stream.hpp"

namespace ozawa {
struct Layout {
    uint16_t song_table = 0;   // 3-byte records: track, addr lo, addr hi
    uint16_t cmd_table = 0;    // jump table for commands $00-$14
    uint8_t  ptr_zp = 0;       // live pointer words at ptr_zp + track*2
    uint8_t  state_zp = 0;     // per-track run state at state_zp + track*2, 0 = stopped
    uint8_t  mask_zp = 0;      // live 01 mask at mask_zp + track*2, 0 = unknown
    int      entries = 0;      // records in the song table
    bool valid() const { return song_table && entries > 0; }
};

Layout detect_layout(const uint8_t* ram);

class OzawaDriver : public stream::Driver {
public:
    explicit OzawaDriver(Layout layout) : L(layout) {}
    Layout L;

    std::string name() const override { return "Namco (Junko Ozawa)"; }
    const char* id() const override { return "ozawa"; }

    // Notes live in command 09's operands, so the "note byte" is the value.
    uint8_t rest_byte() const override { return 0xFF; }
    uint8_t tie_byte() const override { return 0; }
    bool    is_note_byte(uint8_t b) const override { return b < 0xFF; }
    int     note_semitone(uint8_t b) const override { return b + 12; }
    uint8_t note_byte(int semitone_from_c0) const override { return uint8_t(std::clamp(semitone_from_c0 - 12, 0, 0xFE)); }
    uint8_t note_min() const override { return 0x00; }
    uint8_t note_max() const override { return 0x53; }
    int     event_percussion(const seq::Event& e) const override;
    bool    transpose_event(seq::Event& e, int semis) const override;
    void    apply_note_byte(seq::Event& e, uint8_t byte) const override;

    bool        is_command(uint8_t b) const override { return b <= 0x14 || (b >= 0x18 && b <= 0x97); }
    bool        is_instrument_cmd(uint8_t op) const override { return op == 0x20; }
    uint8_t     instrument_opcode() const override { return 0x20; }
    uint8_t     first_command() const override { return 0x00; }
    int         command_count() const override { return 0x98; }
    std::string event_text(const seq::Event& e) const override;

    bool       has_instruments() const override { return false; }
    int        instrument_count(const uint8_t* ram) const override { (void)ram; return 0; }
    seq::Instrument read_instrument(const uint8_t* ram, int index) const override { (void)ram; (void)index; return seq::Instrument{}; }
    bool       preview_regs(const uint8_t* ram, uint8_t nb, int instrument, uint8_t regs[8]) const override { (void)ram; (void)nb; (void)instrument; (void)regs; return false; }

    double ticks_per_second(const uint8_t* ram) const override;
    bool   in_place_only() const override { return true; }   // chords share a stream; no relocation yet

    void decode(const uint8_t* p, int pc, stream::State& s, seq::Event& e, stream::Flow& f) const override;
    const stream::CmdSpec& spec(uint8_t op) const override;
    uint16_t track_start(const uint8_t* ram, uint16_t header, int v) const override;
    std::vector<uint16_t> song_headers(const uint8_t* ram) const override;
    uint16_t live_ptr_addr(int v) const override { const int t = owner_[size_t(v & 7)]; return uint16_t(L.ptr_zp + (t < 0 ? v & 3 : t) * 2); }
    int  ptr_span() const override { return 8; }
    int  min_notes() const override { return 2; }
    int  jump_target(const seq::Event& e) const override;
    void select_song(uint16_t header) const override;
    seq::Position locate(const uint8_t* ram, const seq::Song& song, const seq::Position* prev) const override;
    stream::State edit_state(const std::vector<seq::Event>& ev) const override;

private:
    // Each pseudo-header is a table record address; the group it names holds
    // one start per track (0 = the track rests) and the track each DSP voice
    // column comes from (-1 = none).
    mutable std::vector<std::array<uint16_t, 4>> groups_;
    mutable std::vector<std::array<int8_t, 8>> owners_;
    mutable std::vector<uint16_t> headers_;
    mutable std::array<int8_t, 8> owner_ = {0, 1, 2, 3, -1, -1, -1, -1};   // of the selected song
    int group_of(uint16_t header) const;
};

std::unique_ptr<seq::Driver> detect(const uint8_t* ram);

}
