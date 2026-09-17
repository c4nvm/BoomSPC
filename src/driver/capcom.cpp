#include "capcom.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

using seq::Event;
using seq::EventType;

namespace capcom {
namespace {
inline uint16_t rd16(const uint8_t* ram, uint32_t a) { return uint16_t(ram[a & 0xFFFF] | (ram[(a + 1) & 0xFFFF] << 8)); }
inline uint16_t rd16be(const uint8_t* ram, uint32_t a) { return uint16_t((ram[a & 0xFFFF] << 8) | ram[(a + 1) & 0xFFFF]); }

int find_pattern(const uint8_t* ram, int lo, int hi, const int* pat, int n) {
    for (int a = lo; a + n <= hi; ++a) {
        bool ok = true;
        for (int i = 0; i < n && ok; ++i) ok = pat[i] == 0x100 || ram[a + i] == pat[i];
        if (ok) return a;
    }
    return -1;
}

using seq::FxClass;
struct CmdSpec { uint8_t size; const char* code; const char* name; FxClass cls; };
const CmdSpec kCmds[0x20] = {
    {1, "Tri", "Toggle triplet", FxClass::Time},                                // 00
    {1, "Slr", "Toggle slur (tie into the next note)", FxClass::Time},          // 01
    {1, "Dot", "Dotted (next note)", FxClass::Time},                            // 02
    {1, "Oc+", "Toggle two octaves up", FxClass::Pitch},                        // 03
    {2, "NtP", "Note bits (8 = up, 20 = triplet, 40 = slur)", FxClass::Time},   // 04
    {3, "Tmp", "Tempo (16-bit)", FxClass::Speed},                               // 05
    {2, "DrR", "Duration rate (n/256)", FxClass::Time},                         // 06
    {2, "Vol", "Volume", FxClass::Volume},                                      // 07
    {2, "Ins", "Instrument", FxClass::Instrument},                              // 08
    {2, "Oct", "Octave", FxClass::Pitch},                                       // 09
    {2, "GTr", "Global transpose", FxClass::Pitch},                             // 0A
    {2, "Trn", "Voice transpose", FxClass::Pitch},                              // 0B
    {2, "Tun", "Tuning", FxClass::Pitch},                                       // 0C
    {2, "Por", "Portamento time", FxClass::Pitch},                              // 0D
    {4, "Lp0", "Loop slot 0: count, address", FxClass::Song},                   // 0E
    {4, "Lp1", "Loop slot 1: count, address", FxClass::Song},                   // 0F
    {4, "Lp2", "Loop slot 2: count, address", FxClass::Song},                   // 10
    {4, "Lp3", "Loop slot 3: count, address", FxClass::Song},                   // 11
    {4, "Br0", "Loop break slot 0: note bits, address", FxClass::Song},         // 12
    {4, "Br1", "Loop break slot 1: note bits, address", FxClass::Song},         // 13
    {4, "Br2", "Loop break slot 2: note bits, address", FxClass::Song},         // 14
    {4, "Br3", "Loop break slot 3: note bits, address", FxClass::Song},         // 15
    {3, "Jmp", "Jump", FxClass::Song},                                          // 16
    {1, "End", "End of voice", FxClass::Song},                                  // 17
    {2, "Pan", "Panning", FxClass::Panning},                                    // 18
    {2, "MVl", "Master volume", FxClass::Volume},                               // 19
    {3, "LFO", "LFO parameter (type, value)", FxClass::Pitch},                  // 1A
    {3, "Ech", "Echo parameters", FxClass::Sys1},                               // 1B
    {2, "EcO", "Echo on/off", FxClass::Sys1},                                   // 1C
    {2, "Rel", "Release rate", FxClass::Volume},                                // 1D
    {2, "Nop", "(no effect)", FxClass::Misc},                                   // 1E
    {2, "Nop", "(no effect)", FxClass::Misc},                                   // 1F
};

const uint8_t kOctaveTable[16] = {0x00, 0x0C, 0x18, 0x24, 0x30, 0x3C, 0x48, 0x54, 0x18, 0x24, 0x30, 0x3C, 0x48, 0x54, 0x60, 0x6C};
const uint8_t kDurNormal[7]  = {3, 6, 12, 24, 48, 96, 192};
const uint8_t kDurDotted[7]  = {0, 9, 18, 36, 72, 144, 0};
const uint8_t kDurTriplet[7] = {2, 4, 8, 16, 32, 64, 128};

Event make_event(EventType t, uint16_t addr, const uint8_t* ram, int size, int tick) {
    Event e{};
    e.type = t;
    e.addr = addr;
    e.size = uint8_t(std::min(size, 16));
    for (int i = 0; i < e.size; ++i) e.b[i] = ram[(addr + i) & 0xFFFF];
    e.tick = tick;
    return e;
}

Event cmd_event(uint8_t op, int a = -1) {
    Event c{};
    c.type = EventType::Command;
    c.addr = 0;
    c.b[0] = op;
    c.size = 1;
    if (a >= 0) { c.b[1] = uint8_t(a); c.size = 2; }
    return c;
}

bool is_loop(uint8_t op) { return op >= 0x0E && op <= 0x11; }
bool is_break(uint8_t op) { return op >= 0x12 && op <= 0x15; }

bool took_jump(const std::vector<Event>& ev, size_t i) {
    const Event& e = ev[i];
    if (e.type != EventType::Command || !is_break(e.b[0]) || !e.addr || i + 1 >= ev.size() || !ev[i + 1].addr) return false;
    return ev[i + 1].addr != uint16_t(e.addr + e.size);
}

}

Layout detect_layout(const uint8_t* ram) {
    Layout L;
    const int W = 0x100;
    const int note[] = {0x9F, 0x5C, 0x28, 0x07, 0x9C, 0xFD, 0xF4, W, 0x28, 0x30, 0xD0, 0x05, 0xF6, W, W, 0x2F, W,
                        0x28, 0x10, 0xD0, 0x05, 0xF6, W, W, 0x2F, W, 0xF4, W, 0x28, 0xEF, 0xD4, W, 0xF6, W, W};
    int n = find_pattern(ram, 0x200, 0x2000, note, 35);
    if (n < 0) return Layout{};
    L.ctl_zp = ram[n + 7];
    L.dur_normal = rd16(ram, n + 13);
    L.dur_triplet = rd16(ram, n + 22);
    L.dur_dotted = rd16(ram, n + 33);
    const int key[] = {0x28, 0x1F, 0xD0, 0x01, 0x6F, 0x2D, 0xF4, W, 0x28, 0x0F, 0xFD, 0xAE, 0x60, 0x96, W, W, 0xE3, W, W, 0x60, 0x84, W};
    if (int p = find_pattern(ram, 0x200, 0x2000, key, 22); p >= 0) { L.octave_table = rd16(ram, p + 14); L.gtrans_zp = ram[p + 21]; }
    else return Layout{};
    const int disp[] = {0x68, 0x20, 0xB0, W, 0xC4, W, 0x8D, W, 0x6D, 0x8D, W, 0x6D, 0x1C, 0xFD, 0xF6, W, W, 0x2D, 0xF6, W, W, 0x2D};
    if (int p = find_pattern(ram, 0x200, 0x2000, disp, 22); p >= 0) L.cmd_table = rd16(ram, p + 19);
    else return Layout{};
    const int fetch[] = {0xFB, W, 0xF4, W, 0xDA, W, 0x8D, 0x00, 0xF7, W, 0xBB, W, 0xD0, 0x02, 0xBB, W};
    if (int p = find_pattern(ram, 0x200, 0x2000, fetch, 16); p >= 0) { L.ptr_hi = ram[p + 1]; L.ptr_lo = ram[p + 3]; }
    else return Layout{};
    const int tempo[] = {0x2D, 0x3F, W, W, 0xEE, 0xF3, W, W, 0xD5, W, W, 0xDD, 0xD5, W, W, 0x6F, 0xDA, W};
    if (int p = find_pattern(ram, 0x200, 0x2000, tempo, 18); p >= 0) L.tempo_zp = ram[p + 17];
    const int pitch[] = {0xF6, W, W, 0xC4, W, 0xF6, W, W, 0xC4, W, 0xF6, W, W, 0x2D, 0xF6, W, W, 0xEE, 0x9A};
    if (int p = find_pattern(ram, 0x200, 0x2000, pitch, 19); p >= 0) L.pitch_table = rd16(ram, p + 6);
    const int ins[] = {0x8D, 0x06, 0xCF, 0xDA, W, 0x60, 0x98, W, W, 0x98, W, W};
    if (int p = find_pattern(ram, 0x200, 0x2000, ins, 12); p >= 0) L.ins_table = uint16_t(ram[p + 7] | (ram[p + 10] << 8));
    const int bgm[] = {0x6F, 0x3F, W, W, 0x8F, W, W, 0x8F, W, W, 0x3F, W, W, 0x8D, 0x00, 0xDD};
    if (int p = find_pattern(ram, 0x200, 0x2000, bgm, 16); p >= 0) L.bgm_header = uint16_t(((ram[p + 5] << 8) | ram[p + 8]) + 1);
    const int list[] = {0x1C, 0x5D, 0xF5, W, W, 0xC4, W, 0xF5, W, W, 0xC4, W, 0x04, W, 0xF0, W};
    if (int p = find_pattern(ram, 0x200, 0x2000, list, 16); p >= 0) L.song_list = std::min(rd16(ram, p + 3), rd16(ram, p + 8));
    if (L.bgm_header) {
        for (int v = 0; v < 8; ++v) if (rd16be(ram, L.bgm_header + v * 2) < 0x100) { L.bgm_header = 0; break; }
    }
    return L;
}

Layout layout_for_tests() {
    Layout L;
    L.cmd_table = 0x08E9; L.dur_normal = 0x089D; L.dur_dotted = 0x08A4; L.dur_triplet = 0x0896;
    L.octave_table = 0x08AB; L.pitch_table = 0x0D6B; L.ins_table = 0x47AC; L.bgm_header = 0x0DB0;
    return L;
}

std::unique_ptr<seq::Driver> detect(const uint8_t* ram) {
    Layout L = detect_layout(ram);
    if (!L.valid()) return nullptr;
    return std::make_unique<CapcomDriver>(L);
}

int CapcomDriver::cmd_size(uint8_t op) const { return op < 0x20 ? kCmds[op].size : 0; }
const char* CapcomDriver::cmd_name(uint8_t op) const { return op < 0x20 ? kCmds[op].name : "?"; }
const char* CapcomDriver::cmd_code(uint8_t op) const { return op < 0x20 ? kCmds[op].code : "???"; }
seq::FxClass CapcomDriver::cmd_class(uint8_t op) const { return op < 0x20 ? kCmds[op].cls : seq::FxClass::Invalid; }

bool CapcomDriver::transpose_event(Event& e, int semis) const {
    if (e.type != EventType::Note) return false;
    int key = (e.b[0] & 0x1F) + semis;
    if (key < 1 || key > 31) return false;
    e.b[0] = uint8_t((e.b[0] & 0xE0) | key);
    if (e.pitch >= 0) e.pitch += semis;
    return true;
}

void CapcomDriver::apply_note_byte(Event& e, uint8_t byte) const {
    int key = byte & 0x1F, old_key = e.b[0] & 0x1F;
    e.b[0] = uint8_t((e.b[0] & 0xE0) | key);
    e.type = key ? EventType::Note : EventType::Rest;
    if (e.type == EventType::Note) {
        if (e.pitch >= 0 && old_key) e.pitch += key - old_key;
        else if (e.pitch < 0) e.pitch = note_semitone(byte);
    } else e.pitch = -1;
}

int CapcomDriver::note_ticks(uint8_t byte, uint8_t ctl) const {
    int idx = (byte >> 5) - 1;
    if (idx < 0) return 0;
    if (ctl & kDotted) return kDurDotted[idx];
    if (ctl & kTriplet) return kDurTriplet[idx];
    return kDurNormal[idx];
}

int CapcomDriver::note_pitch(uint8_t byte, const State& s) const {
    int key = byte & 0x1F;
    if (!key) return -1;
    return key - 1 + kOctaveTable[s.ctl & 0x0F] + s.gtrans + s.vtrans + s.base;
}

void CapcomDriver::apply_command(State& s, const Event& e, bool jumped) const {
    if (e.type != EventType::Command) return;
    switch (e.b[0]) {
        case 0x00: s.ctl ^= kTriplet; break;
        case 0x01: s.ctl ^= kSlur; break;
        case 0x02: s.ctl |= kDotted; break;
        case 0x03: s.ctl ^= kOctaveUp; break;
        case 0x04: s.ctl = uint8_t((s.ctl & 0x97) | (e.b[1] & 0x68)); break;
        case 0x09: s.ctl = uint8_t((s.ctl & 0xF8) | e.b[1]); break;
        case 0x0A: s.gtrans = int8_t(e.b[1]); break;
        case 0x0B: s.vtrans = int8_t(e.b[1]); break;
        case 0x12: case 0x13: case 0x14: case 0x15:
            if (jumped) s.ctl = uint8_t((s.ctl & 0x97) | (e.b[1] & 0x68));
            break;
        default: break;
    }
}

bool CapcomDriver::pitch_fx(const Event& e, seq::PitchFx& out) const {
    if (e.type != EventType::Command) return false;
    out = seq::PitchFx{};
    if (e.b[0] == 0x1A && e.b[1] == 0x00) { out.kind = e.b[2] ? seq::PitchFx::Vibrato : seq::PitchFx::VibratoOff; out.depth = e.b[2]; return true; }
    if (e.b[0] == 0x0D) { out.kind = e.b[1] ? seq::PitchFx::Portamento : seq::PitchFx::SlideOff; out.length = e.b[1]; return true; }
    return false;
}

std::string CapcomDriver::event_text(const Event& e) const {
    char b[96];
    if (e.type == EventType::Note || e.type == EventType::Rest) {
        std::snprintf(b, sizeof b, "%s  %d ticks", e.type == EventType::Rest ? "rest" : note_name(e).c_str(), e.duration);
        return b;
    }
    if (e.type == EventType::Command) {
        switch (e.b[0]) {
            case 0x05: std::snprintf(b, sizeof b, "Tempo %d", (e.b[1] << 8) | e.b[2]); return b;
            case 0x09: std::snprintf(b, sizeof b, "Octave %d%s", e.b[1] & 7, e.b[1] & 8 ? " (+2)" : ""); return b;
            case 0x0A: std::snprintf(b, sizeof b, "Global transpose %+d", int8_t(e.b[1])); return b;
            case 0x0B: std::snprintf(b, sizeof b, "Voice transpose %+d", int8_t(e.b[1])); return b;
            case 0x0E: case 0x0F: case 0x10: case 0x11:
                if (e.b[1]) std::snprintf(b, sizeof b, "Loop slot %d  $%02X%02X  x%d more", e.b[0] - 0x0E, e.b[2], e.b[3], e.b[1]);
                else std::snprintf(b, sizeof b, "Loop slot %d  $%02X%02X  forever", e.b[0] - 0x0E, e.b[2], e.b[3]);
                return b;
            case 0x12: case 0x13: case 0x14: case 0x15:
                std::snprintf(b, sizeof b, "Loop break slot %d  $%02X%02X  note bits %02X", e.b[0] - 0x12, e.b[2], e.b[3], e.b[1]); return b;
            case 0x16: std::snprintf(b, sizeof b, "Jump $%02X%02X", e.b[1], e.b[2]); return b;
            default: break;
        }
    }
    return seq::Driver::event_text(e);
}

int CapcomDriver::instrument_count(const uint8_t* ram) const {
    if (!L.ins_table) return 0;
    int n = 0;
    for (int i = 0; i < 128; ++i) {
        const uint8_t* e = ram + ((L.ins_table + i * 6) & 0xFFFF);
        if (e[0] >= 0x80 || (e[4] == 0 && e[5] == 0) || (e[4] == 0xFF && e[5] == 0xFF)) break;
        n = i + 1;
    }
    return n;
}

seq::Instrument CapcomDriver::read_instrument(const uint8_t* ram, int index) const {
    seq::Instrument in{};
    const uint8_t* e = ram + ((L.ins_table + index * 6) & 0xFFFF);
    in.srcn = e[0]; in.adsr0 = e[1]; in.adsr1 = e[2]; in.gain = e[3];
    in.pitch_hi = e[4]; in.pitch_lo = e[5];
    return in;
}

int CapcomDriver::pitch_of(const uint8_t* ram, int note, int instrument) const {
    if (!L.pitch_table || note < 1) return 0;
    int oct = (note - 1) / 12, idx = (note - 1) % 12;
    if (oct > 8) oct = 8;
    int base = rd16(ram, L.pitch_table + idx * 2) << 1;
    base >>= (8 - oct);
    const uint8_t* e = ram + ((L.ins_table + instrument * 6) & 0xFFFF);
    int mult = L.ins_table ? ((e[4] << 8) | e[5]) : 0x0100;
    int pitch = int((long(base) * mult) >> 8);
    return std::min(pitch, 0x3FFF);
}

bool CapcomDriver::preview_regs(const uint8_t* ram, uint8_t note_byte, int instrument, uint8_t regs[8]) const {
    if (!L.ins_table) return false;
    int pitch = pitch_of(ram, note_semitone(note_byte) + 1, instrument);
    const uint8_t* e = ram + ((L.ins_table + instrument * 6) & 0xFFFF);
    regs[0] = 0x40; regs[1] = 0x40;
    regs[2] = uint8_t(pitch & 0xFF); regs[3] = uint8_t(pitch >> 8);
    regs[4] = e[0]; regs[5] = e[1]; regs[6] = e[2]; regs[7] = e[3];
    return true;
}

seq::Track CapcomDriver::parse_track(const uint8_t* ram, uint16_t start, int base, int budget) const {
    seq::Track t;
    t.addr = start;
    if (!start) return t;
    std::vector<uint8_t> iter(0x10000, 0);
    State s;
    s.base = base;
    int counter[4] = {0, 0, 0, 0};
    int pc = start, tick = 0;
    auto loop_target = [&](int addr) {
        for (size_t i = 0; i < t.events.size(); ++i) if (t.events[i].addr == addr) return int(i);
        return 0;
    };
    auto emit = [&](Event e) {
        e.in_sub = iter[e.addr] > 0;
        int open = 0;
        for (int k = 0; k < 4; ++k) if (counter[k]) ++open;
        e.nest = uint8_t(open);
        e.sub_iter = iter[e.addr];
        if (iter[e.addr] < 255) ++iter[e.addr];
        t.events.push_back(e);
    };
    for (;;) {
        if (int(t.events.size()) >= budget) break;
        const uint8_t b = ram[pc & 0xFFFF];
        if (b >= 0x20) {
            Event e = make_event((b & 0x1F) ? EventType::Note : EventType::Rest, uint16_t(pc), ram, 1, tick);
            e.duration = note_ticks(b, s.ctl);
            e.pitch = note_pitch(b, s);
            s.ctl &= uint8_t(~kDotted);
            emit(e);
            tick += e.duration;
            pc += 1;
            continue;
        }
        const int size = kCmds[b].size;
        Event e = make_event(b == 0x17 ? EventType::End : EventType::Command, uint16_t(pc), ram, size, tick);
        const int next = pc + size;
        if (b == 0x17) { emit(e); t.terminated = true; break; }
        if (b == 0x16) {
            int target = rd16be(ram, pc + 1);
            emit(e);
            if (iter[target & 0xFFFF] > 0) { t.loops = true; t.loop_event = loop_target(target); t.terminated = true; break; }
            pc = target;
            continue;
        }
        if (is_loop(b)) {
            int slot = b - 0x0E, count = e.b[1], target = rd16be(ram, pc + 2);
            emit(e);
            if (counter[slot] == 0) {
                if (count == 0) { t.loops = true; t.loop_event = loop_target(target); t.terminated = true; break; }
                counter[slot] = count;
                pc = target;
            } else if (--counter[slot] > 0) pc = target;
            else pc = next;
            continue;
        }
        if (is_break(b)) {
            int slot = b - 0x12, target = rd16be(ram, pc + 2);
            bool jump = counter[slot] == 1;
            emit(e);
            if (jump) {
                counter[slot] = 0;
                apply_command(s, e, true);
                pc = target;
            } else pc = next;
            continue;
        }
        emit(e);
        apply_command(s, e, false);
        pc = next;
    }
    if (!t.terminated) t.truncated = true;
    t.total_ticks = tick;
    t.used_events = int(t.events.size());
    t.end_addr = t.addr;
    return t;
}

bool CapcomDriver::parse_header(const uint8_t* ram, uint16_t header, int base, seq::Pattern& out, int budget) const {
    out = seq::Pattern{};
    out.addr = header;
    bool any = false;
    for (int v = 0; v < 8; ++v) {
        uint16_t start = rd16be(ram, header + (7 - v) * 2);
        if (start < 0x100) continue;
        if (ram[start] == 0x17) continue;
        out.tracks[v] = parse_track(ram, start, base, budget);
        if (out.tracks[v].truncated) return false;
        if (out.tracks[v].total_ticks > 0) any = true;
        out.length_ticks = std::max(out.length_ticks, out.tracks[v].total_ticks);
    }
    return any;
}

std::vector<seq::Song> CapcomDriver::find_songs(const uint8_t* ram, const uint8_t* dsp) const {
    (void)dsp;
    std::vector<seq::Song> songs;
    const int base = int8_t(ram[L.gtrans_zp]);
    auto add_song = [&](int header, seq::Pattern&& pat, int number) {
        bool loops = false;
        for (int v = 0; v < 8; ++v) if (pat.tracks[v].loops) loops = true;
        seq::Song sg;
        sg.order_addr = uint16_t(header);
        sg.order_end = uint16_t(header + 16);
        sg.orders.push_back({sg.order_addr, pat.addr});
        sg.patterns.push_back(std::move(pat));
        sg.loop_count = loops ? 0xFF : 0;
        sg.loop_to = loops ? 0 : -1;
        char b[64];
        if (number >= 0) std::snprintf(b, sizeof b, "song %d @%04X (%d ticks%s)", number, header, sg.patterns[0].length_ticks, loops ? ", loops" : "");
        else std::snprintf(b, sizeof b, "song @%04X (%d ticks%s)", header, sg.patterns[0].length_ticks, loops ? ", loops" : "");
        sg.label = b;
        songs.push_back(std::move(sg));
    };
    if (L.bgm_header) {
        seq::Pattern pat;
        if (parse_header(ram, L.bgm_header, base, pat)) add_song(L.bgm_header, std::move(pat), -1);
    }
    if (L.song_list) {
        for (int i = 1; i < 0x80 && L.song_list + i * 2 + 2 <= 0x10000; ++i) {
            int entry = rd16be(ram, L.song_list + i * 2);
            if (!entry) continue;
            int header = entry + 1;
            if (header + 16 > 0x10000) break;
            bool ok = true;
            for (int v = 0; v < 8 && ok; ++v) if (rd16be(ram, header + v * 2) < 0x100) ok = false;
            if (!ok) break;
            seq::Pattern pat;
            if (!parse_header(ram, uint16_t(header), base, pat)) continue;
            int notes = 0;
            for (int v = 0; v < 8; ++v) for (const Event& e : pat.tracks[v].events) if (e.type == EventType::Note) ++notes;
            if (notes < 16) continue;
            add_song(header, std::move(pat), i);
        }
    }
    return songs;
}

uint16_t CapcomDriver::live_ptr(const uint8_t* ram, const seq::Position& pos, int v) const {
    (void)pos;
    return uint16_t(ram[L.ptr_lo + v * L.ptr_stride] | (ram[L.ptr_hi + v * L.ptr_stride] << 8));
}

int CapcomDriver::pick_current_song(const uint8_t* ram, const std::vector<seq::Song>& songs) const {
    int best = -1, best_hits = 0;
    seq::Position none;
    for (size_t i = 0; i < songs.size(); ++i) {
        const seq::Pattern& p = songs[i].patterns[0];
        int hits = 0;
        for (int v = 0; v < 8; ++v) {
            uint16_t ptr = live_ptr(ram, none, v);
            if (!ptr) continue;
            for (const Event& e : p.tracks[v].events)
                if (e.addr == ptr || uint16_t(e.addr + e.size) == ptr) { ++hits; break; }
        }
        if (hits > best_hits) { best_hits = hits; best = int(i); }
    }
    return best >= 0 ? best : songs.empty() ? -1 : 0;
}

seq::Position CapcomDriver::locate(const uint8_t* ram, const seq::Song& song, const seq::Position* prev) const {
    seq::Position pos;
    pos.track_ptr_base = L.ptr_lo;
    pos.track_ptr_span = uint8_t(L.ptr_hi + 8 * L.ptr_stride - L.ptr_lo);
    pos.order_index = 0;
    uint16_t ptr[8];
    for (int v = 0; v < 8; ++v) ptr[v] = live_ptr(ram, pos, v);
    seq::resolve_stream_position(song.patterns[0], ptr, prev, true, pos);
    return pos;
}

double CapcomDriver::ticks_per_second(const uint8_t* ram) const {
    int latch = ram[0xFA], tempo = rd16(ram, L.tempo_zp);
    if (!latch || !tempo) return 0;
    return 8000.0 / latch / 2 * tempo / 256.0;
}

bool CapcomDriver::tempo_writes(const uint8_t* ram, double tps, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    int latch = ram[0xFA];
    if (!latch || tps <= 0) return false;
    int tempo = std::clamp(int(tps * latch * 2 / 8000.0 * 256.0 + 0.5), 1, 0xFFFF);
    out.push_back({L.tempo_zp, uint8_t(tempo & 0xFF)});
    out.push_back({uint16_t(L.tempo_zp + 1), uint8_t(tempo >> 8)});
    return true;
}

std::vector<uint8_t> CapcomDriver::serialize_track(const std::vector<Event>& events) const {
    std::vector<uint8_t> out;
    for (const Event& e : events) {
        if (e.in_sub) continue;
        for (int i = 0; i < e.size; ++i) out.push_back(e.b[i]);
    }
    return out;
}

void CapcomDriver::retime(std::vector<Event>& ev) const {
    State s;
    {
        State probe;
        for (size_t i = 0; i < ev.size(); ++i) {
            const Event& e = ev[i];
            if (e.type == EventType::Note && e.pitch >= 0) {
                int key = e.b[0] & 0x1F;
                s.base = e.pitch - (key - 1 + kOctaveTable[probe.ctl & 0x0F] + probe.gtrans + probe.vtrans);
                break;
            }
            if (e.type == EventType::Note || e.type == EventType::Rest) probe.ctl &= uint8_t(~kDotted);
            else apply_command(probe, e, took_jump(ev, i));
        }
    }
    int tick = 0;
    for (size_t i = 0; i < ev.size(); ++i) {
        Event& e = ev[i];
        e.tick = tick;
        if (e.type == EventType::Note || e.type == EventType::Rest) {
            e.duration = note_ticks(e.b[0], s.ctl);
            e.pitch = e.type == EventType::Note ? note_pitch(e.b[0], s) : -1;
            s.ctl &= uint8_t(~kDotted);
            tick += e.duration;
        } else {
            e.duration = 0;
            if (e.type == EventType::Command) apply_command(s, e, took_jump(ev, i));
        }
    }
}

namespace {
State state_before(const CapcomDriver& d, const std::vector<Event>& ev, int i) {
    State s;
    for (int k = 0; k < i; ++k) {
        const Event& e = ev[size_t(k)];
        if (e.type == EventType::Note || e.type == EventType::Rest) s.ctl &= uint8_t(~kDotted);
        else if (e.type == EventType::Command) d.apply_command(s, e, took_jump(ev, size_t(k)));
    }
    return s;
}

struct Piece { int idx; bool dotted, triplet; int ticks; };

bool decompose(int dur, bool triplet, std::vector<Piece>& out) {
    std::vector<Piece> opts;
    for (int i = 0; i < 7; ++i) {
        opts.push_back({i, false, false, kDurNormal[i]});
        if (kDurDotted[i]) opts.push_back({i, true, false, kDurDotted[i]});
        opts.push_back({i, false, true, kDurTriplet[i]});
    }
    const int INF = 1 << 20;
    std::vector<int> cost(size_t(dur) + 1, INF), from(size_t(dur) + 1, -1);
    cost[0] = 0;
    for (int d = 1; d <= dur; ++d)
        for (size_t o = 0; o < opts.size(); ++o) {
            const Piece& p = opts[o];
            if (p.ticks > d || cost[size_t(d - p.ticks)] >= INF) continue;
            int c = cost[size_t(d - p.ticks)] + 16 + (p.dotted ? 1 : 0) + (p.triplet != triplet ? 2 : 0);
            if (c < cost[size_t(d)]) { cost[size_t(d)] = c; from[size_t(d)] = int(o); }
        }
    if (cost[size_t(dur)] >= INF) return false;
    out.clear();
    for (int d = dur; d > 0; d -= opts[size_t(from[size_t(d)])].ticks) out.push_back(opts[size_t(from[size_t(d)])]);
    std::sort(out.begin(), out.end(), [](const Piece& a, const Piece& b) { return a.ticks > b.ticks; });
    return true;
}
}

bool CapcomDriver::note_retriggers(const std::vector<Event>& ev, int i) const {
    if (i < 0 || i >= int(ev.size()) || ev[size_t(i)].type != EventType::Note) return true;
    for (int k = i - 1; k >= 0; --k)
        if (ev[size_t(k)].duration > 0) return ev[size_t(k)].type != EventType::Note || !(state_before(*this, ev, k).ctl & kSlur);
    return true;
}

bool CapcomDriver::set_duration(std::vector<Event>& ev, int i, int dur) const {
    if (i < 0 || i >= int(ev.size()) || dur < 1 || dur > 4096) return false;
    State s = state_before(*this, ev, i);
    if (s.ctl & kDotted) {
        int k = i - 1;
        while (k >= 0 && !(ev[size_t(k)].type == EventType::Command && ev[size_t(k)].b[0] == 0x02)) --k;
        if (k < 0 || ev[size_t(k)].in_sub) return false;
        ev.erase(ev.begin() + k);
        --i;
        s.ctl &= uint8_t(~kDotted);
    }
    const Event orig = ev[size_t(i)];
    const bool rest = orig.type == EventType::Rest;
    const bool orig_slur = (s.ctl & kSlur) != 0;
    std::vector<Piece> pieces;
    if (!decompose(dur, (s.ctl & kTriplet) != 0, pieces)) return false;
    std::vector<Event> out;
    bool triplet = (s.ctl & kTriplet) != 0, slur = orig_slur;
    for (size_t k = 0; k < pieces.size(); ++k) {
        const Piece& p = pieces[k];
        const bool last = k + 1 == pieces.size();
        if (p.triplet != triplet) { out.push_back(cmd_event(0x00)); triplet = p.triplet; }
        if (p.dotted) out.push_back(cmd_event(0x02));
        if (!rest) {
            bool want = last ? orig_slur : true;
            if (want != slur) { out.push_back(cmd_event(0x01)); slur = want; }
        }
        Event n = orig;
        n.addr = 0;
        n.b[0] = uint8_t(((p.idx + 1) << 5) | (orig.b[0] & 0x1F));
        n.duration = p.ticks;
        out.push_back(n);
    }
    if (triplet != ((s.ctl & kTriplet) != 0)) out.push_back(cmd_event(0x00));
    ev.erase(ev.begin() + i);
    ev.insert(ev.begin() + i, out.begin(), out.end());
    return true;
}

int CapcomDriver::jump_target(const Event& e) const {
    if (e.type != EventType::Command) return -1;
    if (e.b[0] == 0x16 && e.size >= 3) return (e.b[1] << 8) | e.b[2];
    if ((is_loop(e.b[0]) || is_break(e.b[0])) && e.size >= 4) return (e.b[2] << 8) | e.b[3];
    return -1;
}

void CapcomDriver::set_jump_target(Event& e, uint16_t addr) const {
    if (e.b[0] == 0x16) { e.b[1] = uint8_t(addr >> 8); e.b[2] = uint8_t(addr & 0xFF); }
    else { e.b[2] = uint8_t(addr >> 8); e.b[3] = uint8_t(addr & 0xFF); }
}

void CapcomDriver::track_pointer_writes(const seq::Song& song, int pattern_idx, int voice, uint16_t dest, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    (void)pattern_idx;
    uint16_t at = uint16_t(song.order_addr + (7 - voice) * 2);
    out.push_back({at, uint8_t(dest >> 8)});
    out.push_back({uint16_t(at + 1), uint8_t(dest & 0xFF)});
}

void CapcomDriver::live_state_writes(const uint8_t* ram, const seq::Position& pos, int voice, uint16_t ptr, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    (void)pos;
    int mapped = remap.find(live_ptr(ram, pos, voice));
    if (mapped >= 0) ptr = uint16_t(mapped);
    out.push_back({uint16_t(L.ptr_lo + voice * L.ptr_stride), uint8_t(ptr & 0xFF)});
    out.push_back({uint16_t(L.ptr_hi + voice * L.ptr_stride), uint8_t(ptr >> 8)});
}

bool CapcomDriver::set_note_at(std::vector<Event>& ev, int tick, uint8_t note_byte, int pattern_len) const {
    (void)pattern_len;
    return seq::stream_set_note_at(*this, ev, tick, note_byte, pattern_len);
}

bool CapcomDriver::enter_note(std::vector<Event>& ev, int tick, int semitone, int pattern_len) const {
    seq::stream_prepare(*this, ev, tick, pattern_len);
    int idx = seq::stream_timed_covering(ev, tick, true);
    if (idx < 0) return false;
    if (ev[size_t(idx)].tick != tick) {
        if (ev[size_t(idx)].in_sub || !seq::stream_split_at(*this, ev, tick)) return false;
        idx = -1;
        for (size_t i = 0; i < ev.size(); ++i) if (ev[i].duration > 0 && !ev[i].in_sub && ev[i].tick == tick) { idx = int(i); break; }
        if (idx < 0) return false;
    }
    if (ev[size_t(idx)].in_sub) return false;
    State s = state_before(*this, ev, idx);
    std::vector<Event> probe = ev;
    retime(probe);
    for (size_t i = 0; i < probe.size(); ++i)
        if (probe[i].type == EventType::Note && probe[i].pitch >= 0) {
            State t = state_before(*this, probe, int(i));
            s.base = probe[i].pitch - ((probe[i].b[0] & 0x1F) - 1 + kOctaveTable[t.ctl & 0x0F] + t.gtrans + t.vtrans);
            break;
        }
    const int cur = s.ctl & 0x0F;
    int key = semitone + 1 - kOctaveTable[cur] - s.gtrans - s.vtrans - s.base;
    int oct = cur;
    if (key < 1 || key > 31) {
        int best = -1;
        for (int o = 0; o < 16; ++o) {
            int k = semitone + 1 - kOctaveTable[o] - s.gtrans - s.vtrans - s.base;
            if (k < 1 || k > 31) continue;
            if (best < 0 || std::abs(o - cur) < std::abs(best - cur)) best = o;
        }
        if (best < 0) return false;
        oct = best;
        key = semitone + 1 - kOctaveTable[oct] - s.gtrans - s.vtrans - s.base;
    }
    if (!set_note_at(ev, tick, uint8_t(0x20 | key), pattern_len)) return false;
    idx = -1;
    for (size_t i = 0; i < ev.size(); ++i) if (ev[i].duration > 0 && !ev[i].in_sub && ev[i].tick == tick) { idx = int(i); break; }
    if (idx < 0) return false;
    if (oct != cur) {
        std::vector<Event> set, back;
        if ((oct & 8) != (cur & 8)) {
            set.push_back(cmd_event(0x04, (s.ctl & 0x60) | (oct & 8)));
            back.push_back(cmd_event(0x04, (s.ctl & 0x60) | (cur & 8)));
        }
        set.push_back(cmd_event(0x09, oct & 7));
        back.push_back(cmd_event(0x09, cur & 7));
        bool later = false;
        for (size_t i = size_t(idx) + 1; i < ev.size(); ++i) if (ev[i].type == EventType::Note && !ev[i].in_sub) { later = true; break; }
        int end = idx;
        for (size_t i = size_t(idx) + 1; i < ev.size(); ++i) {
            if (ev[i].in_sub) break;
            if (ev[i].duration <= 0) continue;
            if ((ev[i].b[0] & 0x1F) != key || !(state_before(*this, ev, int(i)).ctl & kSlur)) break;
            end = int(i);
        }
        if (later) ev.insert(ev.begin() + end + 1, back.begin(), back.end());
        ev.insert(ev.begin() + idx, set.begin(), set.end());
    }
    retime(ev);
    return true;
}

bool CapcomDriver::insert_command_at(std::vector<Event>& ev, int tick, const uint8_t* bytes, int size) const {
    return seq::stream_insert_command_at(*this, ev, tick, bytes, size);
}

bool CapcomDriver::set_instrument(std::vector<Event>& ev, int tick0, int tick1, uint8_t ins) const {
    return seq::stream_set_instrument(*this, ev, tick0, tick1, ins);
}

bool CapcomDriver::remove_span(std::vector<Event>& ev, int tick, int ticks, bool keep_length) const {
    (void)keep_length;
    for (bool again = true; again && ticks > 0;) {
        again = false;
        retime(ev);
        for (int i = 0; i + 1 < int(ev.size()); ++i) {
            const Event& e = ev[size_t(i)];
            if (e.type != EventType::Command || e.b[0] != 0x02 || e.in_sub) continue;
            int j = i + 1;
            while (j < int(ev.size()) && ev[size_t(j)].duration <= 0) ++j;
            if (j >= int(ev.size()) || ev[size_t(j)].in_sub) continue;
            const Event& n = ev[size_t(j)];
            if (n.tick < tick || n.tick + n.duration > tick + ticks) continue;
            ticks -= n.duration;
            ev.erase(ev.begin() + j);
            ev.erase(ev.begin() + i);
            again = true;
            break;
        }
    }
    if (ticks <= 0) { retime(ev); return true; }
    return seq::stream_remove_span(*this, ev, tick, ticks);
}

bool CapcomDriver::insert_span(std::vector<Event>& ev, int tick, int ticks, uint8_t byte) const {
    retime(ev);
    int idx = -1;
    for (size_t i = 0; i < ev.size(); ++i) if (ev[i].duration > 0 && !ev[i].in_sub && ev[i].tick == tick) { idx = int(i); break; }
    const bool dotted = idx >= 0 && (state_before(*this, ev, idx).ctl & kDotted);
    if (!seq::stream_insert_span(*this, ev, tick, ticks, byte)) return false;
    if (dotted) {
        retime(ev);
        for (size_t i = 0; i < ev.size(); ++i)
            if (ev[i].duration > 0 && !ev[i].in_sub && ev[i].tick == tick + ticks) {
                if (!(state_before(*this, ev, int(i)).ctl & kDotted)) { ev.insert(ev.begin() + int(i), cmd_event(0x02)); retime(ev); }
                break;
            }
    }
    return true;
}

}
