// Driver-independent sequence model and the Driver interface.
//
// Every supported sound driver parses its own byte streams into this model:
// songs made of orders, patterns made of eight tracks, tracks made of events
// that each carry the raw bytes they came from. The UI, the tracker edit
// operations and the position tracking work on the model; what a byte means,
// how notes are named and how a track is serialised back is the Driver's job.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace seq {

enum class EventType { Length, Note, Tie, Rest, Percussion, Command, End, SubCall };

struct Event {
    EventType type;
    uint16_t  addr;        // absolute ARAM address of the first byte
    uint8_t   size;        // bytes occupied
    uint8_t   b[16];       // raw bytes (opcode + args); commands longer than this are rejected
    int       tick;        // start tick within the pattern (subroutine expansion applied)
    int       duration;    // ticks this event occupies (notes/ties/rests), else 0
    bool      in_sub;      // true if this event was reached through a subroutine call / loop repeat
    int       sub_iter;    // iteration index when in_sub
    int       pitch = -1;  // resolved semitone from C-0 for drivers whose note bytes depend on
                           // state (AKAO: octave), else -1 (name the byte with Driver::note_semitone)
    uint8_t   nest = 0;    // call/repeat frames open when this event runs (stream drivers)

    uint8_t   note() const { return b[0]; }
};

struct Track {
    uint16_t addr = 0;                 // 0 = unused voice
    std::vector<Event> events;         // expanded (subroutines inlined) event list
    int total_ticks = 0;               // ticks until this track's own end
    bool truncated = false;            // parse gave up (bad byte / runaway)

    // Set once the pattern length is known. Tracks are often packed back to
    // back with no terminator, relying on another voice ending the pattern
    // first, so the parsed stream can run into the next track.
    int      used_events = 0;          // events that play before the pattern ends
    uint16_t end_addr = 0;             // exclusive end of the bytes this track really uses
    bool     terminated = false;       // true if the byte at end_addr is this track's own terminator
    bool     loops = false;            // the stream jumps back on itself (drivers without an order list)
    int      loop_event = -1;          // ...to this event index (0 = the start), -1 = unknown
};

struct Pattern {
    uint16_t addr = 0;
    Track    tracks[8];
    int      length_ticks = 0;
};

struct Order {
    uint16_t entry_addr;               // address of this order-list word
    uint16_t pattern_addr;
};

struct Song {
    uint16_t order_addr = 0;           // start of the order list (identity of the song)
    uint16_t order_end  = 0;           // address just past the terminator
    std::vector<Order> orders;
    int  loop_count = 0;               // 0 = no loop (song stops), 0xFF = forever
    int  loop_to    = -1;              // order index the loop jumps to
    std::vector<Pattern> patterns;     // unique patterns, in order of first appearance
    std::string label;                 // driver-specific description for the song picker
    int  bank = -1;                    // ROM bank the song's data lives in (SNES-side engines), -1 = n/a
    int  pattern_index(uint16_t addr) const;
    int  total_ticks() const;
};

struct Instrument {
    uint8_t srcn = 0, adsr0 = 0, adsr1 = 0, gain = 0, pitch_hi = 0, pitch_lo = 0;
    uint8_t perc_note = 0;             // percussion entries only
    int8_t  transpose = 0;             // semitone offset applied to notes (drivers that have one)
};

// Live playback position derived from the driver's zero-page state.
struct Position {
    bool valid = false;
    int  order_index = -1;             // index into Song::orders, -1 unknown
    int  voice_event[8];               // index into the current pattern's track events, -1 unknown
    int  voice_tick[8];                // tick of that event, -1 unknown
    uint16_t voice_ptr[8];             // raw live pointers
    uint16_t track_ptr_base = 0;       // where the 8 pointers were found (0 = not found)
    uint8_t  track_ptr_span = 16;      // bytes of live pointer state from there (split lo/hi arrays need more)
    uint16_t order_ptr_addr = 0;       // where the order pointer was found (0 = not found)
};

// Colour class of a command, for the pattern view.
enum class FxClass { Invalid, Instrument, Pitch, Volume, Panning, Song, Time, Speed, Sys1, Sys2, Misc };

// What a pitch command does, for the piano roll's overlay. Fields the
// format does not carry stay at their defaults.
struct PitchFx {
    // Glide: every note change from here on glides from the previous pitch
    // to the new one at `units` per tick (Follin's $90).
    enum Kind { None, Vibrato, VibratoOff, Slide, SlideOff, Portamento, Glide } kind = None;
    int delay = 0;       // ticks before it takes effect
    int length = 0;      // ticks a slide takes (0 = the rest of the note / unknown)
    int target = -1;     // slide destination as a semitone from C-0, or -1
    int delta = 0;       // ...relative semitones when target < 0
    int units = 0;       // ...or DSP pitch units per tick (signed) when the format stores a rate, not a target
    int depth = 0, rate = 0;   // vibrato
    bool next_note = false;    // applies to the note that follows (N-SPC bends) rather than the sounding one
    // A voice state that shapes every note from here until switched off,
    // rather than the one note it sits on. With `units` and a `length`, a
    // Slide reverses every `length` ticks: a triangle wobble, not a ramp.
    bool sticky = false;
};

// "C-4" style name for a semitone counted from C-0.
std::string note_name(int semitone_from_c0);

// ---------------------------------------------------------------------------

struct Driver {
    virtual ~Driver() = default;
    virtual std::string name() const = 0;
    // Short tag for the file/test tools ("nspc", "follin").
    virtual const char* id() const = 0;

    // ---- notes ----
    virtual uint8_t rest_byte() const = 0;
    virtual uint8_t tie_byte() const = 0;                 // 0 = the driver has no tie
    virtual bool    is_note_byte(uint8_t b) const = 0;    // a pitched note
    virtual bool    is_percussion(uint8_t b) const { (void)b; return false; }
    virtual int     percussion_index(uint8_t b) const { (void)b; return 0; }
    virtual int     note_semitone(uint8_t b) const = 0;   // from C-0
    virtual uint8_t note_byte(int semitone_from_c0) const = 0;
    virtual uint8_t note_min() const = 0;                 // lowest / highest pitched note byte
    virtual uint8_t note_max() const = 0;
    std::string     note_name(uint8_t b) const { return seq::note_name(note_semitone(b)); }
    std::string     note_name(const Event& e) const { return seq::note_name(e.pitch >= 0 ? e.pitch : note_semitone(e.b[0])); }
    int             event_semitone(const Event& e) const { return e.pitch >= 0 ? e.pitch : note_semitone(e.b[0]); }
    // Shifts a note event by semitones; false when the encoding cannot express it.
    virtual bool    transpose_event(Event& e, int semis) const;

    // ---- commands ----
    virtual bool        is_command(uint8_t b) const = 0;
    virtual int         cmd_size(uint8_t op) const = 0;   // opcode + args, 0 = unknown
    virtual const char* cmd_name(uint8_t op) const = 0;
    virtual const char* cmd_code(uint8_t op) const = 0;   // three letters for the grid
    virtual FxClass     cmd_class(uint8_t op) const = 0;
    virtual bool        is_instrument_cmd(uint8_t op) const = 0;
    virtual uint8_t     instrument_opcode() const = 0;
    // Range of opcodes for the "add command" picker.
    virtual uint8_t     first_command() const = 0;
    virtual int         command_count() const = 0;
    // One-line description of an event for tooltips and the event editor.
    virtual std::string event_text(const Event& e) const;
    // Pitch modulation the command starts (vibrato, slides), or false. The
    // default classifies by the command's name; drivers that know their
    // argument layout fill in the numbers.
    virtual bool pitch_fx(const Event& e, PitchFx& out) const;
    // Command bytes that slide a note of `ticks` from `from` to `to`
    // (semitones from C-0), or false when the format has no such command.
    virtual bool make_slide(int from, int to, int ticks, uint8_t out[16], int& size) const { (void)from; (void)to; (void)ticks; (void)out; (void)size; return false; }
    // Whether set_slide can write anything for this format.
    virtual bool can_slide() const { uint8_t b[16]; int n = 0; return make_slide(60, 62, 24, b, n); }
    // Makes the note starting at `tick` (`dur` ticks, sounding `from`) slide
    // to `to`, or retargets the slide `existing` (an event index from
    // pitch_fx spans, -1 = none) that already shapes it. The default writes
    // make_slide's bytes; formats that slide by other means override.
    virtual bool set_slide(std::vector<Event>& ev, int tick, int dur, int from, int to, int existing) const;
    // DSP pitch value a note plays at, for converting rate-based bends to
    // semitones: nominal tuning, instrument fine-tune ignored.
    virtual double pitch_units(int semitone) const;
    // False when timed event i sounds without a key-on (a legato /
    // slurred continuation of the previous note).
    virtual bool note_retriggers(const std::vector<Event>& ev, int i) const { (void)ev; (void)i; return true; }

    // ---- instruments ----
    virtual bool       has_instruments() const = 0;
    virtual int        instrument_count(const uint8_t* ram) const = 0;
    virtual Instrument read_instrument(const uint8_t* ram, int index) const = 0;
    // False for table slots that hold nothing (sparse instrument maps).
    virtual bool       instrument_used(const uint8_t* ram, int index) const { (void)ram; (void)index; return true; }
    // Table index <-> the number written in the pattern (AddmusicK numbers
    // its custom instruments from 30 regardless of the global table's size).
    virtual int        instrument_number(const uint8_t* ram, int index) const { (void)ram; return index; }
    virtual int        instrument_index(const uint8_t* ram, int number) const { (void)ram; return number; }
    // Voice registers (VOLL VOLR PITCHL PITCHH SRCN ADSR1 ADSR2 GAIN) for a preview note.
    virtual bool       preview_regs(const uint8_t* ram, uint8_t note_byte, int instrument, uint8_t regs[8]) const = 0;

    // ---- songs and playback ----
    virtual std::vector<Song> find_songs(const uint8_t* ram, const uint8_t* dsp) const = 0;
    virtual int  pick_current_song(const uint8_t* ram, const std::vector<Song>& songs) const = 0;
    // `prev` is the last position: drivers with loops use it to keep the
    // playhead moving forward through repeated bytes.
    virtual Position locate(const uint8_t* ram, const Song& song, const Position* prev) const = 0;
    // Where an ended voice parks its pointer: after the end byte (1) or on it (0).
    virtual int      end_park_offset() const { return 1; }
    // The live pointer of voice v (drivers with split low/high arrays override).
    virtual uint16_t live_ptr(const uint8_t* ram, const Position& pos, int v) const {
        return uint16_t(ram[(pos.track_ptr_base + v * 2) & 0xFFFF] | (ram[(pos.track_ptr_base + v * 2 + 1) & 0xFFFF] << 8));
    }
    virtual double ticks_per_second(const uint8_t* ram) const { (void)ram; return 0; }
    // Ticks per beat the driver's music usually assumes (48 = a whole note of $C0).
    virtual int    default_ticks_per_beat() const { return 48; }
    // RAM writes that make the driver run at `tps` ticks per second from now
    // on (the song's own tempo commands still override later). False if the
    // driver's tempo state is unknown.
    virtual bool   tempo_writes(const uint8_t* ram, double tps, std::vector<std::pair<uint16_t, uint8_t>>& out) const { (void)ram; (void)tps; (void)out; return false; }

    // ---- editing ----
    // Byte stream of an event list; expanded subroutine / loop bodies are
    // skipped and the calls emitted instead.
    virtual std::vector<uint8_t> serialize_track(const std::vector<Event>& events) const = 0;
    // Re-derives tick/duration fields after editing.
    virtual void retime(std::vector<Event>& ev) const = 0;
    // Streams whose events are not contiguous (jumps) can only be patched in
    // place: every event keeps its address and size.
    virtual bool in_place_only() const { return false; }
    // Sets a note at `tick`, splitting whatever covers it when possible.
    virtual bool set_note_at(std::vector<Event>& ev, int tick, uint8_t note_byte, int pattern_len) const = 0;
    // Same, from a semitone (drivers with octave state pick the byte themselves).
    virtual bool enter_note(std::vector<Event>& ev, int tick, int semitone, int pattern_len) const {
        return set_note_at(ev, tick, note_byte(semitone), pattern_len);
    }
    // Inserts a command at `tick`.
    virtual bool insert_command_at(std::vector<Event>& ev, int tick, const uint8_t* bytes, int size) const = 0;
    // Sets (or inserts) the instrument for the row [tick0, tick1).
    virtual bool set_instrument(std::vector<Event>& ev, int tick0, int tick1, uint8_t ins) const = 0;
    // ---- stream editing (one program per voice: Follin, AKAO, Rare) ----
    // These drivers implement the pieces below and get set_note_at,
    // insert_command_at, remove_span and insert_span from the generic
    // stream_* helpers: events inside subroutine bodies (in_sub) are shared
    // bytes and may only change byte for byte; anything that changes the
    // main stream's length is rewritten and relocated by Tracker::write_track.
    virtual bool has_stream_edit() const { return false; }
    // Re-encodes timed event ev[i] to last `dur` ticks, inserting whatever
    // state commands the format needs around it. Touched events get addr 0.
    virtual bool set_duration(std::vector<Event>& ev, int i, int dur) const { (void)ev; (void)i; (void)dur; return false; }
    // Changes the note of a timed event, keeping its duration encoding.
    virtual void apply_note_byte(Event& e, uint8_t byte) const { e.b[0] = byte; e.type = byte == rest_byte() ? EventType::Rest : byte == tie_byte() && tie_byte() ? EventType::Tie : EventType::Note; }
    // A command that ends the playable stream without an address (AKAO's
    // loop-to-mark); End events and loop jumps are recognised anyway.
    virtual bool ends_stream(const Event& e) const { (void)e; return false; }
    // The ARAM address a jump-like command targets (-1 if none) and its rewrite.
    virtual int  jump_target(const Event& e) const { (void)e; return -1; }
    virtual void set_jump_target(Event& e, uint16_t addr) const { (void)e; (void)addr; }
    // Bytes of the main stream laid out at `dest`: subroutine bodies are
    // skipped (the calls stay), forward jumps that only chain the trace are
    // dropped and the loop jump is re-targeted. Default = serialize_track.
    virtual std::vector<uint8_t> serialize_relocated(const std::vector<Event>& ev, uint16_t dest, std::vector<int>* offsets = nullptr) const;
    // RAM writes that point voice `voice` of `song`'s pattern at `dest`.
    // Default: the N-SPC style pattern header (8 words).
    virtual void track_pointer_writes(const Song& song, int pattern_idx, int voice, uint16_t dest, std::vector<std::pair<uint16_t, uint8_t>>& out) const;
    // RAM writes that move the driver's live state for `voice` onto a
    // rewritten stream mid-play. `remap` maps old addresses (event starts
    // and the byte after each event) to their new ones; `ptr` is where the
    // pointer goes when its old value has no mapping. Drivers add whatever
    // else they keep by address (AKAO's loop mark and repeat stack, Rare's
    // call stack). Default: the pointer at pos.track_ptr_base + voice*2.
    struct Remap { std::vector<std::pair<uint16_t, uint16_t>> pairs; int find(uint16_t a) const { for (auto& p : pairs) if (p.first == a) return p.second; return -1; } };
    virtual void live_state_writes(const uint8_t* ram, const Position& pos, int voice, uint16_t ptr, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const;
    // True when live_state_writes also relocates the driver's call/repeat
    // stack, so playback can move onto a rewritten stream from inside a frame.
    virtual bool remaps_stack() const { return false; }
    // Byte ranges that may be handed out as free RAM when the user opts to
    // reclaim (sound effects, jingles the tracker does not list as songs).
    virtual void reclaimable_ranges(const uint8_t* ram, std::vector<std::pair<uint16_t, uint16_t>>& out) const { (void)ram; (void)out; }
    // False when the address space is not the SPC's (an SNES-side engine
    // edited through a synthesised window): the free-space search then
    // skips the SPC reservations (DSP directory, echo, samples).
    virtual bool spc_ram_space() const { return true; }
    // Address range the free-space search may hand out.
    virtual void free_space_bounds(int& lo, int& hi) const { lo = 0x200; hi = 0x10000; }
    // ROM bank whose window the engine must show for this song (-1 = not banked).
    virtual int  song_bank(const Song& song) const { (void)song; return -1; }

    // Commands that open or close a call / repeat frame (calls, loop starts
    // and ends, loop breaks). Removing them, together with marking every
    // expanded pass as plain data, flattens a voice.
    virtual bool is_frame_command(const Event& e) const { (void)e; return false; }
    // Repeat count of a call command (SubCall / a frame command that calls
    // a body n times), 0 when `e` is not a call; and its rewrite.
    virtual int  call_count(const Event& e) const { return e.type == EventType::SubCall ? (e.b[3] ? e.b[3] : 256) : 0; }
    virtual void set_call_count(Event& e, int n) const { if (e.type == EventType::SubCall) e.b[3] = uint8_t(n); }
    // Flattens the event list: subroutine bodies and repeat passes become
    // ordinary main-stream events (each pass its own copy) and the frame
    // commands go, so nothing is shared and every row can be edited. The
    // stream grows by the copies; write_track relocates it.
    virtual void unroll(std::vector<Event>& ev) const;
    // Unrolls only the call / repeat instance whose shared bytes cover
    // `tick` (its passes become plain copies, the frame commands go); the
    // rest of the voice keeps its structure. Returns false when nothing
    // shared covers the tick.
    bool unroll_at(std::vector<Event>& ev, int tick) const;
    // Lengthens the track to `ticks` with rests (nothing if already longer);
    // an empty list becomes a fresh stream: one rest and the terminator.
    virtual bool extend(std::vector<Event>& ev, int ticks) const;
    // The bytes that end a voice stream (stream drivers), or false.
    virtual bool end_bytes(uint8_t out[16], int& size) const { (void)out; (void)size; return false; }

    // Quantise/velocity per note; only N-SPC has it.
    virtual bool has_qv() const { return false; }
    virtual bool set_qv(std::vector<Event>& ev, int timed_idx, int q, int v) const { (void)ev; (void)timed_idx; (void)q; (void)v; return false; }
    virtual bool remove_span(std::vector<Event>& ev, int tick, int ticks, bool keep_length) const { (void)ev; (void)tick; (void)ticks; (void)keep_length; return false; }
    virtual bool insert_span(std::vector<Event>& ev, int tick, int ticks, uint8_t byte) const { (void)ev; (void)tick; (void)ticks; (void)byte; return false; }
};

// Recognises the driver in ARAM, or returns null.
std::unique_ptr<Driver> detect_driver(const uint8_t* ram, const uint8_t* dsp);
// SNES-side engines: detected from the cartridge ROM (SNSF sets). The
// driver keeps the shared image, so live edits and re-parses agree.
std::unique_ptr<Driver> detect_snes_driver(std::shared_ptr<const std::vector<uint8_t>> rom);

// Generic edits for stream drivers (see Driver::has_stream_edit). The event
// list is the expanded trace; only events outside subroutine bodies move.
// Makes `tick` editable: a voice whose bytes there are shared (subroutine
// body, repeat pass) is unrolled, and a track that ends before `tick` is
// extended with rests to `min_end` (at least tick + 1). The edit helpers
// below call it, so edits never fail on structure alone.
void stream_prepare(const Driver& d, std::vector<Event>& ev, int tick, int min_end = 0);
int  stream_timed_covering(const std::vector<Event>& ev, int tick, bool allow_sub);   // index or -1
bool stream_split_at(const Driver& d, std::vector<Event>& ev, int tick);              // splits the timed event covering tick
bool stream_set_note_at(const Driver& d, std::vector<Event>& ev, int tick, uint8_t byte, int min_end = 0);
bool stream_insert_command_at(const Driver& d, std::vector<Event>& ev, int tick, const uint8_t* bytes, int size, int min_end = 0);
bool stream_remove_span(const Driver& d, std::vector<Event>& ev, int tick, int ticks);
bool stream_insert_span(const Driver& d, std::vector<Event>& ev, int tick, int ticks, uint8_t byte);
bool stream_set_instrument(const Driver& d, std::vector<Event>& ev, int tick0, int tick1, uint8_t ins);
// True when the main stream's event structure (addresses and sizes) no
// longer matches `original`, i.e. it must be rewritten rather than patched.
bool stream_structure_changed(const std::vector<Event>& ev, const std::vector<Event>& original);
// Places the eight live pointers of a stream driver in the pattern. A
// pointer that sits inside bytes played more than once (a called
// subroutine, a repeat) matches several events; the song has one clock, so
// the pass is chosen as the tick the most voices can agree on, with the
// previous position breaking ties toward continuity. `after_only`: the
// driver parks a pointer only after a note (Capcom), never on the next
// event's first byte. Fills voice_event / voice_tick / valid of `pos`.
void resolve_stream_position(const Pattern& p, const uint16_t ptr[8], const Position* prev, bool after_only, Position& pos);
// Rewrites the word at `at` when it holds a mapped old address (driver state).
void remap_word(const uint8_t* ram, uint16_t at, const Driver::Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out);

}  // namespace seq
