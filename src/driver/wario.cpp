#include "wario.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

using seq::Event;
using seq::EventType;
using seq::FxClass;

namespace wario {
namespace {
constexpr uint16_t kReq = 0x0100;        // requested song
constexpr uint16_t kCurrent = 0x0108;    // song playing (1-based)
constexpr uint16_t kActive = 0x0110;     // voice mask
constexpr uint16_t kTicksLeft = 0x1D08;  // WRAM $FD08 + v
constexpr uint16_t kStackDepth = 0x1D10; // $FD10 + v
constexpr uint16_t kPtr = 0x1D18;        // $FD18 + 2v
constexpr uint16_t kStack = 0x1E40;      // $FE40 + 16v

struct CmdSpec { uint8_t argc; const char* code; const char* name; FxClass cls; };
const CmdSpec kCmds[0x17] = {
    {2, "Jmp", "Jump", FxClass::Song},                                   // 5A
    {2, "Cal", "Call subroutine", FxClass::Song},                        // 5B
    {0, "Ret", "Return", FxClass::Song},                                 // 5C
    {1, "Rep", "Repeat start (count)", FxClass::Song},                   // 5D
    {0, "RpE", "Repeat end", FxClass::Song},                             // 5E
    {1, "Ins", "Sample (instrument)", FxClass::Instrument},              // 5F
    {1, "Vol", "Voice volume", FxClass::Volume},                         // 60
    {1, "MVl", "Master volume", FxClass::Volume},                        // 61
    {1, "Pan", "Pan (80 = centre)", FxClass::Panning},                   // 62
    {3, "Vib", "Vibrato (delay, rate, depth)", FxClass::Pitch},          // 63
    {0, "VbO", "Vibrato off", FxClass::Pitch},                           // 64
    {3, "Trm", "Tremolo (delay, rate, depth)", FxClass::Volume},         // 65
    {0, "TrO", "Tremolo off", FxClass::Volume},                          // 66
    {3, "PEn", "Pitch envelope up (delay, ticks, amount)", FxClass::Pitch},    // 67
    {3, "PEd", "Pitch envelope down (delay, ticks, amount)", FxClass::Pitch},  // 68
    {0, "PEO", "Pitch envelope off", FxClass::Pitch},                    // 69
    {2, "PnS", "Pan slide (ticks, target)", FxClass::Panning},           // 6A
    {2, "VlS", "Volume slide (ticks, target)", FxClass::Volume},         // 6B
    {4, "Env", "Envelope to SPC (ADSR1, ADSR2, GAIN, x)", FxClass::Volume},   // 6C
    {0, "S06", "SPC command 06", FxClass::Sys2},                         // 6D
    {1, "S07", "SPC command 07 (x)", FxClass::Sys2},                     // 6E
    {1, "Dtn", "Detune (1/256 semitone)", FxClass::Pitch},               // 6F
    {0, "End", "End of voice", FxClass::Song},                           // 70
};

uint32_t lorom(uint8_t bank, uint16_t addr) { return (uint32_t(bank & 0x7F) << 15) | (addr & 0x7FFF); }

}

Layout detect_layout(const std::vector<uint8_t>& rom) {
    Layout L;
    if (rom.size() < 0x10000) return L;
    const uint8_t disp[] = {0x38, 0xE9, 0x5A, 0x0A, 0xAA, 0x7C};
    const uint8_t look[] = {0x3A, 0x0A, 0x0A, 0xAA, 0xBF};
    size_t d = std::string::npos;
    for (size_t i = 0; i + sizeof disp <= rom.size(); ++i) if (std::memcmp(&rom[i], disp, sizeof disp) == 0) { d = i; break; }
    if (d == std::string::npos) return L;
    const size_t bank_start = d & ~size_t(0x7FFF);
    for (size_t i = bank_start; i + 12 < d; ++i) {
        if (std::memcmp(&rom[i], look, sizeof look) != 0 || rom[i + 8] != 0x48 || rom[i + 9] != 0xBF) continue;
        const uint32_t a = rom[i + 5] | rom[i + 6] << 8, bank = rom[i + 7];
        const uint32_t b = rom[i + 10] | rom[i + 11] << 8;
        if (b != a + 2 || a < 0x8000) continue;
        L.song_table = uint16_t(a);
        L.song_bank = uint8_t(bank);
        L.tick_entry = uint16_t(0x8000 + (d - bank_start));
        break;
    }
    if (!L.song_table) return L;
    int limit = 64;
    for (size_t i = bank_start; i + 12 < d; ++i) {
        if (std::memcmp(&rom[i], look, sizeof look) != 0 || rom[i + 8] != 0x48 || rom[i + 9] != 0xBF) continue;
        const uint32_t a = rom[i + 5] | rom[i + 6] << 8;
        if (a > L.song_table && a < L.song_table + 4 * 64) limit = int((a - L.song_table) / 4);
    }
    for (int i = 0; i < limit; ++i) {
        const uint32_t e = lorom(L.song_bank, uint16_t(L.song_table + 4 * i));
        if (e + 4 > rom.size()) break;
        const uint16_t ptr = uint16_t(rom[e] | rom[e + 1] << 8);
        const uint8_t bank = rom[e + 2];
        if (ptr < 0x8000) break;
        const uint32_t h = lorom(bank, ptr);
        if (h + 1 > rom.size()) break;
        const int n = rom[h];
        if (n < 1 || n > 8 || h + 1 + 5 * size_t(n) > rom.size()) break;
        bool ok = true;
        for (int k = 0; k < n && ok; ++k) {
            const uint32_t t = h + 1 + 5 * size_t(k);
            if (rom[t] > 7 || (rom[t + 1] | rom[t + 2] << 8) < 0x8000) ok = false;
        }
        if (!ok) break;
        L.songs = i + 1;
    }
    return L;
}

std::unique_ptr<seq::Driver> detect(std::shared_ptr<const std::vector<uint8_t>> rom) {
    Layout L = detect_layout(*rom);
    if (!L.valid()) return nullptr;
    return std::make_unique<WarioDriver>(L, std::move(rom));
}

uint8_t WarioDriver::rd(uint8_t bank, uint32_t addr) const {
    const uint32_t o = lorom(bank, uint16_t(addr));
    return o < rom_->size() ? (*rom_)[o] : 0;
}

uint8_t WarioDriver::note_byte(int semitone_from_c0) const { return uint8_t(std::clamp(semitone_from_c0 - 24, 0, 0x57)); }

bool WarioDriver::transpose_event(Event& e, int semis) const {
    if (e.type != EventType::Note) return false;
    int n = int(e.b[0] & 0x7F) + semis;
    if (n < 0 || n > 0x57) return false;
    e.b[0] = uint8_t((e.b[0] & 0x80) | n);
    return true;
}

void WarioDriver::apply_note_byte(Event& e, uint8_t byte) const {
    e.b[0] = uint8_t((e.b[0] & 0x80) | (byte & 0x7F));
    e.type = (byte & 0x7F) == 0x58 ? EventType::Rest : (byte & 0x7F) == 0x59 ? EventType::Tie : EventType::Note;
}

int WarioDriver::cmd_size(uint8_t op) const {
    const int i = int(op & 0x7F) - 0x5A;
    if (i < 0 || i >= 0x17) return 0;
    return 1 + kCmds[i].argc;
}
const char* WarioDriver::cmd_name(uint8_t op) const { const int i = int(op & 0x7F) - 0x5A; return i >= 0 && i < 0x17 ? kCmds[i].name : "?"; }
const char* WarioDriver::cmd_code(uint8_t op) const { const int i = int(op & 0x7F) - 0x5A; return i >= 0 && i < 0x17 ? kCmds[i].code : "???"; }
FxClass WarioDriver::cmd_class(uint8_t op) const { const int i = int(op & 0x7F) - 0x5A; return i >= 0 && i < 0x17 ? kCmds[i].cls : FxClass::Invalid; }

std::string WarioDriver::event_text(const Event& e) const {
    char b[96];
    if (e.type == EventType::Note || e.type == EventType::Rest || e.type == EventType::Tie) {
        int k = 2;
        std::string extra;
        if (e.b[0] & 0x80) { char g[24]; std::snprintf(g, sizeof g, "  gate %d/256", e.b[k]); extra += g; ++k; }
        if (e.b[1] & 0x80) { char v[24]; std::snprintf(v, sizeof v, "  velocity %d", e.b[k]); extra += v; }
        std::snprintf(b, sizeof b, "%s  %d ticks%s", e.type == EventType::Rest ? "rest" : e.type == EventType::Tie ? "tie" : note_name(e).c_str(), e.duration, extra.c_str());
        return b;
    }
    if (e.type == EventType::Command && (e.b[0] == 0x5A || e.b[0] == 0x5B)) {
        std::snprintf(b, sizeof b, "%s $%02X%02X", cmd_name(e.b[0]), e.b[2], e.b[1]);
        return b;
    }
    return seq::Driver::event_text(e);
}

bool WarioDriver::pitch_fx(const Event& e, seq::PitchFx& out) const {
    if (e.type != EventType::Command) return false;
    out = seq::PitchFx{};
    switch (e.b[0]) {
        case 0x63: out.kind = seq::PitchFx::Vibrato; out.delay = e.b[1]; out.rate = e.b[2]; out.depth = e.b[3]; return true;
        case 0x64: out.kind = seq::PitchFx::VibratoOff; return true;
        case 0x67: case 0x68: out.kind = seq::PitchFx::Slide; out.delay = e.b[1]; out.length = e.b[2]; out.delta = (e.b[0] == 0x67 ? 1 : -1) * e.b[3]; out.sticky = true; return true;
        case 0x69: out.kind = seq::PitchFx::SlideOff; out.sticky = true; return true;
        default: return false;
    }
}

seq::Instrument WarioDriver::read_instrument(const uint8_t* ram, int index) const {
    (void)ram;
    seq::Instrument in{};
    in.srcn = uint8_t(index);
    in.gain = 0x7F;
    return in;
}

bool WarioDriver::preview_regs(const uint8_t* ram, uint8_t note_byte, int instrument, uint8_t regs[8]) const {
    (void)ram;
    const int pitch = std::clamp(int(pitch_units(note_semitone(note_byte)) + 0.5), 1, 0x3FFF);
    regs[0] = 0x30; regs[1] = 0x30;
    regs[2] = uint8_t(pitch & 0xFF); regs[3] = uint8_t(pitch >> 8);
    regs[4] = uint8_t(instrument); regs[5] = 0x00; regs[6] = 0x00; regs[7] = 0x7F;
    return true;
}

seq::Track WarioDriver::parse_track(uint8_t bank, uint16_t start) const {
    seq::Track t;
    t.addr = start;
    struct Frame { int ret; bool loop; int count; };
    std::vector<Frame> stack;
    std::vector<uint8_t> iter(0x10000, 0);
    int pc = start, tick = 0;
    auto emit = [&](Event e) {
        bool called = false;
        for (const Frame& f : stack) if (!f.loop) called = true;
        e.in_sub = called || iter[e.addr] > 0;
        e.nest = uint8_t(std::min<size_t>(stack.size(), 255));
        e.sub_iter = iter[e.addr];
        if (iter[e.addr] < 255) ++iter[e.addr];
        t.events.push_back(e);
    };
    auto make = [&](EventType type, int addr, int size, int at) {
        Event e{};
        e.type = type; e.addr = uint16_t(addr); e.size = uint8_t(std::min(size, 16)); e.tick = at; e.duration = 0;
        for (int i = 0; i < e.size; ++i) e.b[i] = rd(bank, uint32_t(addr + i));
        return e;
    };
    auto loop_target = [&](int addr) { for (size_t i = 0; i < t.events.size(); ++i) if (t.events[i].addr == addr) return int(i); return 0; };
    for (int guard = 0; guard < 60000; ++guard) {
        if (pc < 0x8000 || pc > 0xFFFF) { t.truncated = true; break; }
        const uint8_t b = rd(bank, uint32_t(pc)), n = uint8_t(b & 0x7F);
        if (n < 0x5A) {
            const uint8_t d = rd(bank, uint32_t(pc + 1));
            int size = 2;
            if (b & 0x80) ++size;
            if (d & 0x80) ++size;
            Event e = make(n == 0x58 ? EventType::Rest : n == 0x59 ? EventType::Tie : EventType::Note, pc, size, tick);
            e.duration = std::max(1, int(d & 0x7F));
            emit(e);
            tick += e.duration;
            pc += size;
            continue;
        }
        const int size = cmd_size(n);
        if (size == 0) { t.truncated = true; break; }
        if (n == 0x70) { emit(make(EventType::End, pc, 1, tick)); t.terminated = true; break; }
        emit(make(EventType::Command, pc, size, tick));
        const uint8_t a1 = rd(bank, uint32_t(pc + 1)), a2 = rd(bank, uint32_t(pc + 2));
        int next = pc + size;
        bool stop = false;
        switch (n) {
            case 0x5A: {
                const int target = a1 | a2 << 8;
                if (iter[target & 0xFFFF] > 0) { t.loops = true; t.loop_event = loop_target(target); stop = true; }
                else next = target;
                break;
            }
            case 0x5B: if (stack.size() > 32) stop = true; else { stack.push_back({pc + 3, false, 0}); next = a1 | a2 << 8; } break;
            case 0x5C:
                while (!stack.empty() && stack.back().loop) stack.pop_back();
                if (stack.empty()) stop = true; else { next = stack.back().ret; stack.pop_back(); }
                break;
            case 0x5D: stack.push_back({pc + 2, true, a1}); break;
            case 0x5E: {
                if (stack.empty() || !stack.back().loop) { stop = true; break; }
                Frame& f = stack.back();
                if (--f.count <= 0) stack.pop_back(); else next = f.ret;
                break;
            }
            default: break;
        }
        if (stop) { t.terminated = true; break; }
        pc = next;
    }
    int end = 0;
    for (const Event& e : t.events) end = std::max(end, e.tick + e.duration);
    t.total_ticks = end;
    t.used_events = int(t.events.size());
    t.end_addr = t.addr;
    return t;
}

std::vector<seq::Song> WarioDriver::find_songs(const uint8_t* ram, const uint8_t* dsp) const {
    (void)ram; (void)dsp;
    std::vector<seq::Song> songs;
    for (int i = 0; i < L.songs; ++i) {
        const uint16_t entry = uint16_t(L.song_table + 4 * i);
        const uint16_t hdr = uint16_t(rd(L.song_bank, entry) | rd(L.song_bank, entry + 1) << 8);
        const uint8_t bank = rd(L.song_bank, entry + 2);
        const int n = rd(bank, hdr);
        seq::Pattern pat;
        pat.addr = hdr;
        bool loops = false, any = false;
        for (int k = 0; k < n && k < 8; ++k) {
            const uint16_t e = uint16_t(hdr + 1 + 5 * k);
            const int v = rd(bank, e);
            const uint16_t ptr = uint16_t(rd(bank, e + 1) | rd(bank, e + 2) << 8);
            if (v > 7) continue;
            pat.tracks[v] = parse_track(bank, ptr);
            if (pat.tracks[v].total_ticks > 0) any = true;
            if (pat.tracks[v].loops) loops = true;
            pat.length_ticks = std::max(pat.length_ticks, pat.tracks[v].total_ticks);
        }
        if (!any) continue;
        seq::Song sg;
        sg.bank = bank;
        sg.order_addr = hdr;
        sg.order_end = uint16_t(hdr + 1 + 5 * n);
        sg.orders.push_back({sg.order_addr, pat.addr});
        sg.patterns.push_back(std::move(pat));
        sg.loop_count = loops ? 0xFF : 0;
        sg.loop_to = loops ? 0 : -1;
        char b[64];
        std::snprintf(b, sizeof b, "song %d (%d ticks%s, bank $%02X)", i + 1, sg.patterns[0].length_ticks, loops ? ", loops" : "", bank);
        sg.label = b;
        songs.push_back(std::move(sg));
    }
    return songs;
}

int WarioDriver::pick_current_song(const uint8_t* ram, const std::vector<seq::Song>& songs) const {
    const int id = ram[kCurrent];   // 1-based; the label carries the number
    if (id >= 1) {
        for (size_t i = 0; i < songs.size(); ++i) {
            int n = 0;
            if (std::sscanf(songs[i].label.c_str(), "song %d", &n) == 1 && n == id) return int(i);
        }
    }
    return songs.empty() ? -1 : 0;
}

seq::Position WarioDriver::locate(const uint8_t* ram, const seq::Song& song, const seq::Position* prev) const {
    seq::Position pos;
    pos.track_ptr_base = kPtr;
    pos.track_ptr_span = 16;
    pos.order_index = 0;
    uint16_t ptr[8];
    const int active = ram[kActive];
    for (int v = 0; v < 8; ++v) ptr[v] = (active & (1 << v)) ? uint16_t(ram[kPtr + 2 * v] | ram[kPtr + 2 * v + 1] << 8) : 0;
    seq::resolve_stream_position(song.patterns[0], ptr, prev, false, pos);
    return pos;
}

std::vector<uint8_t> WarioDriver::serialize_track(const std::vector<Event>& events) const {
    std::vector<uint8_t> out;
    for (const Event& e : events) {
        if (e.in_sub) continue;
        for (int i = 0; i < e.size; ++i) out.push_back(e.b[i]);
    }
    return out;
}

void WarioDriver::retime(std::vector<Event>& ev) const {
    int tick = 0;
    for (Event& e : ev) {
        e.tick = tick;
        if (e.type == EventType::Note || e.type == EventType::Rest || e.type == EventType::Tie) {
            e.duration = std::max(1, int(e.b[1] & 0x7F));
            tick += e.duration;
        } else e.duration = 0;
    }
}

bool WarioDriver::set_duration(std::vector<Event>& ev, int i, int dur) const {
    if (i < 0 || i >= int(ev.size()) || dur < 1) return false;
    Event& e = ev[size_t(i)];
    if (dur <= 127) { e.b[1] = uint8_t((e.b[1] & 0x80) | dur); e.duration = dur; e.addr = 0; return true; }
    e.b[1] = uint8_t((e.b[1] & 0x80) | 127); e.duration = 127; e.addr = 0;
    int left = dur - 127, at = i + 1;
    while (left > 0) {
        Event tie{};
        tie.type = EventType::Tie; tie.addr = 0; tie.size = 2; tie.b[0] = 0x59; tie.b[1] = uint8_t(std::min(left, 127));
        tie.duration = tie.b[1]; tie.tick = 0;
        ev.insert(ev.begin() + at, tie);
        left -= tie.duration; ++at;
    }
    return true;
}

void WarioDriver::track_pointer_writes(const seq::Song& song, int pattern_idx, int voice, uint16_t dest, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    (void)pattern_idx;
    const uint8_t bank = uint8_t(song.bank);
    const int n = rd(bank, song.order_addr);
    for (int k = 0; k < n && k < 8; ++k) {
        const uint16_t e = uint16_t(song.order_addr + 1 + 5 * k);
        if (rd(bank, e) != voice) continue;
        out.push_back({uint16_t(e + 1), uint8_t(dest & 0xFF)});
        out.push_back({uint16_t(e + 2), uint8_t(dest >> 8)});
        return;
    }
}

void WarioDriver::live_state_writes(const uint8_t* ram, const seq::Position& pos, int voice, uint16_t ptr, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    (void)pos;
    const uint16_t at = uint16_t(kPtr + voice * 2);
    int mapped = remap.find(uint16_t(ram[at] | ram[at + 1] << 8));
    if (mapped >= 0) ptr = uint16_t(mapped);
    out.push_back({at, uint8_t(ptr & 0xFF)});
    out.push_back({uint16_t(at + 1), uint8_t(ptr >> 8)});
    const int depth = ram[kStackDepth + voice];
    const uint16_t base = uint16_t(kStack + voice * 16);
    for (int i = 0; i + 1 < depth && i < 15; ++i) {
        const uint16_t w = uint16_t(ram[base + i] | ram[base + i + 1] << 8);
        if (w < 0x8000) continue;
        const int m = remap.find(w);
        if (m < 0) continue;
        out.push_back({uint16_t(base + i), uint8_t(m & 0xFF)});
        out.push_back({uint16_t(base + i + 1), uint8_t(m >> 8)});
        ++i;
    }
}

bool WarioDriver::set_note_at(std::vector<Event>& ev, int tick, uint8_t note_byte, int pattern_len) const { return seq::stream_set_note_at(*this, ev, tick, note_byte, pattern_len); }
bool WarioDriver::insert_command_at(std::vector<Event>& ev, int tick, const uint8_t* bytes, int size) const { return seq::stream_insert_command_at(*this, ev, tick, bytes, size); }
bool WarioDriver::set_instrument(std::vector<Event>& ev, int tick0, int tick1, uint8_t ins) const { return seq::stream_set_instrument(*this, ev, tick0, tick1, ins); }
bool WarioDriver::remove_span(std::vector<Event>& ev, int tick, int ticks, bool keep_length) const { (void)keep_length; return seq::stream_remove_span(*this, ev, tick, ticks); }
bool WarioDriver::insert_span(std::vector<Event>& ev, int tick, int ticks, uint8_t byte) const { return seq::stream_insert_span(*this, ev, tick, ticks, byte); }

}
