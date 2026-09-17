// N-SPC sequence format (Nintendo's in-house SNES sound driver).
//
// Song = order list of 16-bit pattern pointers, terminated by 0000 (stop) or
// by {count, addr} (loop `count` times to `addr`; 0xFF = forever).
// Pattern = 8 x 16-bit track pointers (0 = voice unused).
// Track = byte stream:
//   00          end of track
//   01..7F      note length in ticks; if the next byte is also < 0x80 it is a
//               "qv" byte: high nibble = duration ratio, low nibble = velocity
//   80..note_max  note (semitones from C-1)
//   tie / rest  (C6/C7 in the SMW variant, C8/C9 in the EB variant)
//   perc range  percussion notes (D0..D9 SMW, CA..DF EB)
//   cmd_base..  commands with fixed argument counts (see kCommands*)
//
// Two dialects are handled: the Super Mario World era driver (commands start
// at $DA, 5-byte instruments) and the EarthBound / Kirby era driver (commands
// start at $E0, 6-byte instruments), plus the licensee builds that keep the
// EarthBound dialect but move the command base, add commands or make the
// pointers relative (Intelligent Systems, Konami, Human, Tose, Falcom,
// Lemmings). Details are read from the driver code in ARAM.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "seq.hpp"

namespace nspc {
enum class Variant { Unknown, SMW, EB };
enum class Profile { Unknown, Earlier, Standard, IntelliFe3, IntelliTa, IntelliFe4, Konami, Human, Tose, FalcomYs4, Lemmings, Quintet };

struct CommandSpec {
    const char* name;
    uint8_t     argc;
};

struct Layout {
    Variant  variant   = Variant::Unknown;
    uint8_t  cmd_base  = 0;      // first command opcode
    uint8_t  tie       = 0;
    uint8_t  rest      = 0;
    uint8_t  perc_base = 0;      // first percussion note, 0 = none
    uint8_t  perc_end  = 0;      // last percussion note (inclusive)
    uint16_t len_table = 0;      // address of the command-length table in ARAM, 0 if not found
    uint16_t inst_table  = 0;    // instrument table address
    uint8_t  inst_stride = 0;    // 5 (SMW) or 6 (EB)
    uint16_t perc_table  = 0;    // percussion instrument table (6-byte entries), 0 if none
    uint16_t inst_table2 = 0;    // AddmusicK per-song custom instruments (6-byte entries)
    int      inst2_first = 30;   // instrument number of the first inst_table2 entry
    int      inst2_count = 0;
    uint8_t  len_from_ram[64]{};
    bool     amk = false;         // AddmusicK-extended SMW driver
    uint8_t  tempo_addr = 0;      // zero-page tempo byte (the tick accumulator adds it per timer-0 count), 0 = unknown
    Profile  profile = Profile::Unknown;
    uint8_t  order_zp = 0;        // zero-page word the driver reads the order list through, 0 = unknown
    uint16_t addr_base = 0;       // Konami / Falcom: song pointers are relative to this
    bool     relative = false;
    uint16_t resolve(uint16_t raw) const { return relative && raw ? uint16_t(raw + addr_base) : raw; }
    uint16_t unresolve(uint16_t addr) const { return relative && addr ? uint16_t(addr - addr_base) : addr; }

    const CommandSpec* commands = nullptr;  // indexed by opcode - cmd_base
    int command_count = 0;

    bool valid() const { return variant != Variant::Unknown; }
    bool is_command(uint8_t b) const { return cmd_base && b >= cmd_base; }
    int  cmd_size(uint8_t b) const;
    const char* cmd_name(uint8_t b) const;
};

using seq::EventType;
using seq::Event;
using seq::Track;
using seq::Pattern;
using seq::Order;
using seq::Song;
using seq::Instrument;
using seq::Position;

Layout detect(const uint8_t* ram);
Layout layout_for(Variant v);

void refine_with_dsp(const uint8_t* ram, const uint8_t* dsp, Layout& L);

Track parse_track(const uint8_t* ram, const Layout& L, uint16_t addr);

Pattern parse_pattern(const uint8_t* ram, const Layout& L, uint16_t addr);

std::vector<Song> find_songs(const uint8_t* ram, const Layout& L, const uint8_t* dsp = nullptr);
int  pick_current_song(const uint8_t* ram, const Layout& L, const std::vector<Song>& songs);

Position locate(const uint8_t* ram, const Layout& L, const Song& song);

int         instrument_count(const uint8_t* ram, const Layout& L);
Instrument  read_instrument(const uint8_t* ram, const Layout& L, int index);
void        write_instrument(uint8_t* ram, const Layout& L, int index, const Instrument& in);

std::string note_name(int semitone);
int         note_semitone(const Layout& L, uint8_t note_byte);
int         note_pitch(const uint8_t* ram, int semitone, int mult_hi, int mult_lo);

std::vector<uint8_t> serialize_track(const std::vector<Event>& events);

const char* variant_name(Variant v);
const char* profile_name(Profile p);

class NspcDriver : public seq::Driver {
public:
    explicit NspcDriver(Layout layout) : L(std::move(layout)) {}
    Layout L;

    std::string name() const override;
    const char* id() const override { return "nspc"; }
    uint8_t rest_byte() const override { return L.rest; }
    uint8_t tie_byte() const override { return L.tie; }
    bool    is_note_byte(uint8_t b) const override { return b >= 0x80 && b < L.tie; }
    bool    is_percussion(uint8_t b) const override { return L.perc_base && b >= L.perc_base && b <= L.perc_end; }
    int     percussion_index(uint8_t b) const override { return b - L.perc_base; }
    int     note_semitone(uint8_t b) const override { return nspc::note_semitone(L, b) + 12; }
    uint8_t note_byte(int semitone_from_c0) const override;
    uint8_t note_min() const override { return 0x80; }
    uint8_t note_max() const override { return uint8_t(L.tie - 1); }
    bool        is_command(uint8_t b) const override { return L.is_command(b); }
    int         cmd_size(uint8_t op) const override { return L.cmd_size(op); }
    const char* cmd_name(uint8_t op) const override { return L.cmd_name(op); }
    bool pitch_fx(const seq::Event& e, seq::PitchFx& out) const override;
    bool make_slide(int from, int to, int ticks, uint8_t out[16], int& size) const override;
    bool extend(std::vector<seq::Event>& ev, int ticks) const override;
    const char* cmd_code(uint8_t op) const override;
    seq::FxClass cmd_class(uint8_t op) const override;
    bool        is_instrument_cmd(uint8_t op) const override { return op == L.cmd_base; }
    uint8_t     instrument_opcode() const override { return L.cmd_base; }
    uint8_t     first_command() const override { return L.cmd_base; }
    int         command_count() const override { return L.command_count; }
    bool       has_instruments() const override { return L.inst_table != 0; }
    int        instrument_count(const uint8_t* ram) const override { return nspc::instrument_count(ram, L) + L.inst2_count; }
    Instrument read_instrument(const uint8_t* ram, int index) const override;
    int        instrument_number(const uint8_t* ram, int index) const override;
    int        instrument_index(const uint8_t* ram, int number) const override;
    bool       preview_regs(const uint8_t* ram, uint8_t note_byte, int instrument, uint8_t regs[8]) const override;
    std::vector<Song> find_songs(const uint8_t* ram, const uint8_t* dsp) const override;
    int      pick_current_song(const uint8_t* ram, const std::vector<Song>& songs) const override { return nspc::pick_current_song(ram, L, songs); }
    Position locate(const uint8_t* ram, const Song& song, const Position* prev) const override { (void)prev; return nspc::locate(ram, L, song); }
    double ticks_per_second(const uint8_t* ram) const override;
    bool   tempo_writes(const uint8_t* ram, double tps, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    std::vector<uint8_t> serialize_track(const std::vector<Event>& events) const override { return nspc::serialize_track(events); }
    void retime(std::vector<Event>& ev) const override;
    bool set_note_at(std::vector<Event>& ev, int tick, uint8_t note_byte, int pattern_len) const override;
    bool insert_command_at(std::vector<Event>& ev, int tick, const uint8_t* bytes, int size) const override;
    bool set_instrument(std::vector<Event>& ev, int tick0, int tick1, uint8_t ins) const override;
    bool has_qv() const override { return true; }
    bool set_qv(std::vector<Event>& ev, int timed_idx, int q, int v) const override;
    bool remove_span(std::vector<Event>& ev, int tick, int ticks, bool keep_length) const override;
    bool insert_span(std::vector<Event>& ev, int tick, int ticks, uint8_t byte) const override;

    void track_pointer_writes(const Song& song, int pattern_idx, int voice, uint16_t dest, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;

    static int ensure_own_length(std::vector<Event>& ev, int timed_idx);
};

}
