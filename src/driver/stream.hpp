// Shared base for drivers whose songs are one byte program per voice with
// no order list (Konami, Hudson, Chunsoft, Mint, Compile, Pandora Box,
// Neverland, Prism, Graphic Research, ASCII, Falcom, Berlioz...). A format supplies
// `decode` (one event from its bytes, plus the control flow it causes) and
// a few tables; the parser, position tracking and the generic stream
// editing come from here. Formats with more exotic control flow keep their
// own driver (Follin, AKAO, Rare, Capcom).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "seq.hpp"

namespace stream {

struct CmdSpec {
    uint8_t     size;        // opcode + args, 0 = unknown opcode
    const char* code;        // three letters for the grid
    const char* name;
    seq::FxClass cls;
    uint8_t     addr_at = 0; // byte index of a 16-bit address operand, 0 = none
    bool        frame = false;   // opens or closes a call / repeat frame
};

// Per-voice state a format carries between events (what each field means
// is the format's business; only decode reads and writes them).
struct State {
    int len = 0, oct = 0, trans = 0, vel = 0, ratio = 0;
    int x[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    uint8_t flags = 0;
    int start = 0;     // the program's first address (set by the parser)
    int tick = 0;      // tick of the event being decoded (set by the parser)
    int voice = -1;    // voice being parsed, -1 when unknown (retime)
};

// What the parser does after an event.
struct Flow {
    enum Kind { Next, Jump, Call, Return, RepStart, RepEnd, RepBreak, End } kind = Next;
    int target = -1;   // Jump / Call / RepBreak destination (RepBreak: -1 = just past the repeat's end)
    int count = 0;     // Call: passes; RepStart / RepEnd: total passes (0 = forever); RepBreak: pass to break on (0 = last); Return: 1 = a no-op outside a call
    int slot = 0;      // repeat slot for formats with several counters
};

class Driver : public seq::Driver {
public:
    // Decodes the event at `p` (16 readable bytes, `pc` its address or 0),
    // updating the voice state. Fills type, size, b, duration and pitch.
    virtual void decode(const uint8_t* p, int pc, State& s, seq::Event& e, Flow& f) const = 0;
    virtual const CmdSpec& spec(uint8_t op) const = 0;
    // Start address of voice v's program from the song header, 0 = unused.
    virtual uint16_t track_start(const uint8_t* ram, uint16_t header, int v) const = 0;
    // Song headers to list, in order (the first is the usual pick).
    virtual std::vector<uint16_t> song_headers(const uint8_t* ram) const = 0;
    virtual uint16_t live_ptr_addr(int v) const = 0;   // where the live pointer word of voice v lives
    // Called before the live state of a song is read (formats whose voice
    // slots depend on the header).
    virtual void select_song(uint16_t header) const { (void)header; }
    virtual int  ptr_span() const { return 16; }
    // Fewer notes than this in a header and it is not a song.
    virtual int  min_notes() const { return 4; }
    // Extra live state to remap on relocation (repeat / call stack words).
    virtual void live_extra_writes(const uint8_t* ram, int voice, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const { (void)ram; (void)voice; (void)remap; (void)out; }
    // Initial voice state before a program runs (transpose in force etc.).
    virtual State initial_state(const uint8_t* ram, uint16_t header, int voice) const { (void)ram; (void)header; (void)voice; return State{}; }
    // State to re-run decode from over an event list being edited (formats
    // whose header state the events cannot reproduce recover it here).
    virtual State edit_state(const std::vector<seq::Event>& ev) const { (void)ev; return State{}; }
    // A jump command to `target`, for a program that loops by running into
    // bytes it already played (the header pointer starts mid-stream).
    virtual bool loop_jump(uint16_t target, seq::Event& e) const { (void)target; (void)e; return false; }
    // Whether the live pointer parks only after a note (never on the next event's first byte).
    virtual bool ptr_after_note() const { return false; }
    // Note byte that sounds `semitone` given the voice state (transpose, octave).
    virtual uint8_t note_byte_in(int semitone, const State& s) const { (void)s; return note_byte(semitone); }

    // seq::Driver
    int         cmd_size(uint8_t op) const override { return spec(op).size; }
    const char* cmd_name(uint8_t op) const override { return spec(op).name; }
    const char* cmd_code(uint8_t op) const override { return spec(op).code; }
    seq::FxClass cmd_class(uint8_t op) const override { return spec(op).cls; }
    std::vector<seq::Song> find_songs(const uint8_t* ram, const uint8_t* dsp) const override;
    int  pick_current_song(const uint8_t* ram, const std::vector<seq::Song>& songs) const override;
    seq::Position locate(const uint8_t* ram, const seq::Song& song, const seq::Position* prev) const override;
    uint16_t live_ptr(const uint8_t* ram, const seq::Position& pos, int v) const override;
    std::vector<uint8_t> serialize_track(const std::vector<seq::Event>& events) const override;
    void retime(std::vector<seq::Event>& ev) const override;
    bool in_place_only() const override { return true; }
    bool has_stream_edit() const override { return true; }
    bool is_frame_command(const seq::Event& e) const override { return e.type == seq::EventType::Command && spec(e.b[0]).frame; }
    bool is_return_command(const seq::Event& e) const override;
    int  jump_target(const seq::Event& e) const override;
    void set_jump_target(seq::Event& e, uint16_t addr) const override;
    void live_state_writes(const uint8_t* ram, const seq::Position& pos, int voice, uint16_t ptr, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const override;
    bool remaps_stack() const override { return true; }
    bool set_note_at(std::vector<seq::Event>& ev, int tick, uint8_t note_byte, int pattern_len) const override { return seq::stream_set_note_at(*this, ev, tick, note_byte, pattern_len); }
    bool enter_note(std::vector<seq::Event>& ev, int tick, int semitone, int pattern_len) const override;
    bool insert_command_at(std::vector<seq::Event>& ev, int tick, const uint8_t* bytes, int size) const override { return seq::stream_insert_command_at(*this, ev, tick, bytes, size); }
    bool set_instrument(std::vector<seq::Event>& ev, int tick0, int tick1, uint8_t ins) const override { return seq::stream_set_instrument(*this, ev, tick0, tick1, ins); }
    bool remove_span(std::vector<seq::Event>& ev, int tick, int ticks, bool keep_length) const override { (void)keep_length; return seq::stream_remove_span(*this, ev, tick, ticks); }
    bool insert_span(std::vector<seq::Event>& ev, int tick, int ticks, uint8_t byte) const override { return seq::stream_insert_span(*this, ev, tick, ticks, byte); }

    seq::Track parse_track(const uint8_t* ram, uint16_t start, const State& init, int budget = 60000) const;
    // Called after each voice of a header is parsed (formats where one
    // voice's timing depends on another's).
    virtual void track_parsed(int v, const seq::Track& t) const { (void)v; (void)t; }
    bool parse_header(const uint8_t* ram, uint16_t header, seq::Pattern& out, int budget = 60000) const;
    virtual void prune_idle_voices(const uint8_t* ram, seq::Pattern& p) const;
    void prune_by_live_pointers(const uint8_t* ram, seq::Pattern& p) const;
    // Voice state in force before event i (re-running decode over the list).
    State state_before(const std::vector<seq::Event>& ev, int i) const;
    // Semitones the parsed pitches sit above what decode gives from a fresh
    // State (the header's initial transpose / octave), so edits can use
    // state_before without the header.
    int pitch_base(const std::vector<seq::Event>& ev) const;
    // Label for the song picker; the default names the address and length.
    virtual std::string song_label(const uint8_t* ram, uint16_t header, const seq::Pattern& p, int index) const;
};

inline uint16_t rd16(const uint8_t* ram, uint32_t a) { return uint16_t(ram[a & 0xFFFF] | (ram[(a + 1) & 0xFFFF] << 8)); }
inline uint16_t rd16be(const uint8_t* ram, uint32_t a) { return uint16_t((ram[a & 0xFFFF] << 8) | ram[(a + 1) & 0xFFFF]); }
// First match of `pat` (0x100 = wildcard) in ram[lo, hi), or -1.
int find_pattern(const uint8_t* ram, int lo, int hi, const int* pat, int n);
// Event with the bytes at `addr` copied in.
seq::Event make_event(seq::EventType t, uint16_t addr, const uint8_t* ram, int size, int tick);
seq::Event cmd_event(uint8_t op, int a = -1, int b = -1);

}
