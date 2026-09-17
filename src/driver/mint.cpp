#include "mint.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>

#include "spc700.hpp"

using seq::Event;
using seq::EventType;
using seq::FxClass;
using stream::CmdSpec;
using stream::Flow;
using stream::State;
using stream::find_pattern;
using stream::rd16;

namespace mint {
namespace {
const int W = 0x100;

bool has(const uint8_t* ram, int at, int len, const int* pat, int n) { return find_pattern(ram, at, at + len, pat, n) >= 0; }

// Prefix parameters of an event: delta, gate, velocity (each < 80).
int prefix_len(const uint8_t* p) {
    int n = 0;
    while (n < 3 && p[n] < 0x80) ++n;
    return n;
}
}

Layout detect_layout(const uint8_t* ram) {
    Layout L;
    const int load[] = {0x1C, 0xFD, 0xF6, W, W, 0xC4, W, 0xF6, W, W, 0xC4, W, 0x8D, 0x00, 0xF7, W, 0x10, W, 0x68, 0xFF};
    int p = find_pattern(ram, 0x200, 0x4000, load, 20);
    if (p < 0) return Layout{};
    L.song_list = rd16(ram, p + 3);
    const int disp[] = {0x80, 0xA8, 0xC0, 0x1C, 0xFD, 0xF6, W, W, 0x2D, 0xF6, W, W, 0x2D, 0x6F};
    int d = find_pattern(ram, 0x200, 0x4000, disp, 14);
    if (d < 0) return Layout{};
    L.cmd_table = rd16(ram, d + 10);
    const int fetch[] = {0xF4, W, 0xC4, W, 0xF4, W, 0xC4, W, 0x8D, 0x00, 0xF7, W, 0x3A, W, 0xFD, 0x30};
    int q = find_pattern(ram, 0x200, 0x4000, fetch, 16);
    if (q < 0) return Layout{};
    L.ptr_lo_zp = ram[q + 1]; L.ptr_hi_zp = ram[q + 5];
    const uint8_t wz = ram[q + 11];
    uint16_t delta_addr = 0; uint8_t counter_zp = 0;
    const int dl[] = {0xD5, W, W, 0xD4, W};
    if (int r = find_pattern(ram, q + 16, q + 40, dl, 5); r >= 0) { delta_addr = rd16(ram, r + 1); counter_zp = ram[r + 4]; }
    uint16_t trans_addr = 0, base_addr = 0;
    const int key[] = {0x60, 0x95, W, W, 0x60, 0x95, W, W, 0x28, 0x7F};
    if (int r = find_pattern(ram, 0x200, 0x4000, key, 10); r >= 0) { trans_addr = rd16(ram, r + 2); base_addr = rd16(ram, r + 6); }
    for (int i = 0; i < 0x40; ++i) {
        const int h = rd16(ram, L.cmd_table + i * 2);
        Layout::Op& op = L.ops[i];
        if (h < 0x200 || h >= 0xFFC0) continue;
        // Reads before the first branch; the handler's extent runs to its RET / JMP.
        int reads = 0, at = h, extent = 0;
        bool branched = false;
        for (int guard = 0; guard < 40; ++guard) {
            spc700::Insn in = spc700::disassemble(ram, uint16_t(at));
            const uint8_t o = in.bytes[0];
            if (o == 0xF7 && in.bytes[1] == wz && !branched) ++reads;
            if (o == 0x2F || o == 0xD0 || o == 0xF0 || o == 0x10 || o == 0x30 || o == 0x90 || o == 0xB0 || o == 0x2E || o == 0xDE || o == 0x6E || o == 0x0F) branched = true;
            at += in.len;
            if (o == 0x6F || o == 0x5F) break;
        }
        extent = std::min(64, at - h);
        const int push[] = {0xE4, wz, 0xD6, W, W};
        const int store_ptr[] = {0xDA, wz};
        const int add_ptr[] = {0x7A, wz};
        const int load_lo[] = {0xC4, wz};
        const int load_hi[] = {0xC4, uint8_t(wz + 1)};
        const int end[] = {0xE8, 0xFF, 0xD5};
        const int octup[] = {0x60, 0x88, 0x0C, 0xD5};
        const int octdn[] = {0x80, 0xA8, 0x0C, 0xD5};
        const int tempo[] = {0xE8, 0x00, 0xDA, W};
        const int ins[] = {0x7A, wz, 0xDA, W};
        const int wait[] = {0xF5, delta_addr & 0xFF, delta_addr >> 8, 0xD4, counter_zp};
        bool pushes = false;   // the pointer stored through a stack: MOV A,$wz then MOV !abs+Y / [dp]+Y, A
        for (int k = 0; k + 4 < extent; ++k)
            if (ram[h + k] == 0xE4 && ram[h + k + 1] == wz && (ram[h + k + 2] == 0xD6 || ram[h + k + 2] == 0xD7 || ram[h + k + 3] == 0xD6 || ram[h + k + 3] == 0xD7)) pushes = true;
        (void)push;
        const bool rel = has(ram, h, extent, add_ptr, 2);
        op.args = uint8_t(reads);
        op.rel = rel;
        if (find_pattern(ram, h, h + 3, end, 3) == h) op.kind = Kind::End;
        else if (reads == 2 && pushes) op.kind = Kind::Call;
        else if (reads == 2 && has(ram, h, extent, store_ptr, 2)) op.kind = Kind::Jump;
        else if (reads == 2 && has(ram, h, extent, ins, 4)) op.kind = Kind::Instrument;
        else if (reads == 1 && pushes) op.kind = Kind::RepStart;
        else if (reads == 1 && has(ram, h, 24, tempo, 4)) { op.kind = Kind::Tempo; int r = find_pattern(ram, h, h + 24, tempo, 4); L.tempo_zp = ram[r + 3]; }
        else if (reads == 1 && base_addr && ram[h + 7] == 0xD5 && rd16(ram, h + 8) == base_addr) op.kind = Kind::NoteBase;
        else if (reads == 1 && trans_addr && ram[h + 7] == 0x95 && rd16(ram, h + 8) == trans_addr) op.kind = Kind::TransposeRel;
        else if (reads == 1 && trans_addr && ram[h + 7] == 0xD5 && rd16(ram, h + 8) == trans_addr) op.kind = Kind::Transpose;
        else if (reads == 0 && has(ram, h, 12, octup, 4)) op.kind = Kind::OctUp;
        else if (reads == 0 && has(ram, h, 12, octdn, 4)) op.kind = Kind::OctDown;
        else if (reads == 0 && delta_addr && find_pattern(ram, h, h + 5, wait, 5) == h) op.kind = Kind::Wait;
        else if (reads == 0 && has(ram, h, extent, load_lo, 2) && has(ram, h, extent, load_hi, 2)) { const int dec[] = {0x9C, 0xF0}; op.kind = has(ram, h, extent, dec, 2) ? Kind::RepEnd : Kind::Ret; }
        else if (reads == 0 && ram[h] == 0x6F) op.kind = Kind::Nop;
        else op.kind = Kind::Args;
        if (i >= 0x27 && op.kind != Kind::Nop) { op.kind = Kind::Args; op.args = uint8_t(std::min(reads, 3)); }   // past the real table: sizes only
        if (op.kind == Kind::Call || op.kind == Kind::RepStart) {
            const int sp[] = {0xFB, W, 0xE4, wz, 0xD6, W, W};
            if (int r = find_pattern(ram, h, h + 32, sp, 7); r >= 0) { L.stack_zp = ram[r + 1]; L.stack_base = rd16(ram, r + 5); }
        }
    }
    return L;
}

std::unique_ptr<seq::Driver> detect(const uint8_t* ram) {
    Layout L = detect_layout(ram);
    if (!L.valid()) return nullptr;
    auto d = std::make_unique<MintDriver>(L);
    d->classify();
    return d;
}

void MintDriver::classify() {
    for (int i = 0; i < 0x40; ++i) {
        if (L.ops[i].kind == Kind::Wait && !wait_op_) wait_op_ = uint8_t(0xC0 + i);
        if (L.ops[i].kind == Kind::End && !end_op_) end_op_ = uint8_t(0xC0 + i);
        if (L.ops[i].kind == Kind::Instrument && !ins_op_) ins_op_ = uint8_t(0xC0 + i);
    }
}

const CmdSpec& MintDriver::spec(uint8_t op) const {
    static const CmdSpec kNote = {1, "Nte", "Note", FxClass::Misc};
    static CmdSpec out;
    if (op < 0xC0) return kNote;
    const int i = op - 0xC0;
    if (i >= 0x40) { out = {1, "???", "Unknown", FxClass::Misc}; return out; }
    const Layout::Op& o = L.ops[i];
    out = {uint8_t(1 + o.args), "Cmd", "Command", FxClass::Misc};
    switch (o.kind) {
        case Kind::Nop: out.code = "Nop"; out.name = "(no effect)"; break;
        case Kind::Args: break;
        case Kind::End: out.code = "End"; out.name = "End of track"; out.cls = FxClass::Song; break;
        case Kind::Jump: out.code = "Jmp"; out.name = "Jump"; out.cls = FxClass::Song; out.addr_at = 1; break;
        case Kind::Call: out.code = "Cal"; out.name = "Call subroutine"; out.cls = FxClass::Song; out.addr_at = 1; out.frame = true; break;
        case Kind::Ret: out.code = "Ret"; out.name = "Return"; out.cls = FxClass::Song; break;
        case Kind::RepStart: out.code = "Lp["; out.name = "Repeat start (count)"; out.cls = FxClass::Song; out.frame = true; break;
        case Kind::RepEnd: out.code = "Lp]"; out.name = "Repeat end"; out.cls = FxClass::Song; out.frame = true; break;
        case Kind::Wait: out.code = "Wai"; out.name = "Wait (the current delta)"; out.cls = FxClass::Time; break;
        case Kind::NoteBase: out.code = "NtB"; out.name = "Note number base"; out.cls = FxClass::Pitch; break;
        case Kind::OctUp: out.code = "Oc+"; out.name = "Octave up"; out.cls = FxClass::Pitch; break;
        case Kind::OctDown: out.code = "Oc-"; out.name = "Octave down"; out.cls = FxClass::Pitch; break;
        case Kind::Transpose: out.code = "Trn"; out.name = "Transpose"; out.cls = FxClass::Pitch; break;
        case Kind::TransposeRel: out.code = "Tr+"; out.name = "Transpose relative"; out.cls = FxClass::Pitch; break;
        case Kind::Tempo: out.code = "Tmp"; out.name = "Tempo"; out.cls = FxClass::Speed; break;
        case Kind::Instrument: out.code = "Ins"; out.name = "Instrument (data address)"; out.cls = FxClass::Instrument; break;
        case Kind::Volume: out.code = "Vol"; out.name = "Volume"; out.cls = FxClass::Volume; break;
        case Kind::Pan: out.code = "Pan"; out.name = "Panning"; out.cls = FxClass::Panning; break;
    }
    return out;
}

std::vector<uint16_t> MintDriver::song_headers(const uint8_t* ram) const {
    std::vector<uint16_t> out;
    for (int i = 0, misses = 0; i < 0x40 && misses < 6; ++i) {
        const uint16_t h = rd16(ram, L.song_list + i * 2);
        if (h < 0x200 || h >= 0xFF00 || ram[h] > 7) { ++misses; continue; }
        bool any = false;
        for (int v = 0; v < 8; ++v) if (track_start(ram, h, v)) any = true;
        if (!any) { ++misses; continue; }
        misses = 0;
        out.push_back(h);
    }
    return out;
}

uint16_t MintDriver::track_start(const uint8_t* ram, uint16_t header, int v) const {
    int p = header;
    for (int guard = 0; guard < 16; ++guard) {
        const uint8_t b = ram[p & 0xFFFF];
        if (b == 0xFF) return 0;
        if (b > 7) return 0;
        const uint16_t start = uint16_t(p + 3 + int16_t(rd16(ram, p + 1)));
        if (b == v) { slots_[uint32_t(header) << 8 | uint32_t(v)] = uint16_t(p + 1); return start >= 0x200 ? start : 0; }
        p += 3;
    }
    return 0;
}

State MintDriver::initial_state(const uint8_t* ram, uint16_t header, int voice) const {
    (void)voice;
    State s;
    s.len = 0;
    data_lo_ = std::min(data_lo_ == 0x200 ? int(header) : data_lo_, int(header));
    for (int v = 0; v < 8; ++v) if (uint16_t a = track_start(ram, header, v)) data_lo_ = std::min(data_lo_, int(a));
    return s;
}

void MintDriver::decode(const uint8_t* p, int pc, State& s, Event& e, Flow& f) const {
    const int n = prefix_len(p);
    int delta = s.len;
    if (n >= 1) { delta = p[0]; if (p[0]) s.len = p[0]; }
    if (n >= 2) s.ratio = p[1];
    if (n >= 3) s.vel = p[2];
    const uint8_t b = p[n];
    for (int i = 0; i <= n; ++i) e.b[i] = p[i];
    e.size = uint8_t(n + 1);
    e.pitch = -1;
    e.type = EventType::Command;
    if (b < 0xC0) {
        if (b >= 0xA0) { e.b[n + 1] = p[n + 1]; e.size = uint8_t(n + 2); if (p[n + 1] < 0x80) s.ratio = p[n + 1]; else s.vel = p[n + 1] & 0x7F; }
        e.type = EventType::Note;
        e.duration = delta;
        e.pitch = ((s.oct + (b & 0x1F) + s.trans) & 0x7F) - 12;
        return;
    }
    const int i = b - 0xC0;
    const Layout::Op& op = i < 0x40 ? L.ops[i] : L.ops[0];
    const int args = i < 0x40 ? op.args : 0;
    e.size = uint8_t(n + 1 + args);
    for (int k = n + 1; k < e.size; ++k) e.b[k] = p[k];
    const int operand = pc + n + 1;
    auto target16 = [&]() { const int raw = p[n + 1] | (p[n + 2] << 8); return op.rel ? (operand + 2 + int16_t(raw)) & 0xFFFF : raw; };
    switch (op.kind) {
        case Kind::End: f.kind = Flow::End; e.type = EventType::End; break;
        case Kind::Jump: f.kind = Flow::Jump; f.target = target16(); break;
        case Kind::Call: f.kind = Flow::Call; f.target = target16(); f.count = 1; break;
        case Kind::Ret: f.kind = Flow::Return; break;
        case Kind::RepStart: f.kind = Flow::RepStart; f.count = p[n + 1] ? p[n + 1] : 256; break;
        case Kind::RepEnd: f.kind = Flow::RepEnd; break;
        case Kind::Wait: e.type = EventType::Rest; e.duration = s.len; break;
        case Kind::NoteBase: s.oct = int8_t(p[n + 1]); break;
        case Kind::OctUp: s.oct += 12; break;
        case Kind::OctDown: s.oct -= 12; break;
        case Kind::Transpose: s.trans = int8_t(p[n + 1]); break;
        case Kind::TransposeRel: s.trans += int8_t(p[n + 1]); break;
        default: break;
    }
}

int MintDriver::jump_target(const Event& e) const {
    if (e.type != EventType::Command || !e.addr) return -1;
    const int n = prefix_len(e.b);
    const int i = e.b[n] - 0xC0;
    if (e.b[n] < 0xC0 || i >= 0x40) return -1;
    const Layout::Op& op = L.ops[i];
    if (op.kind != Kind::Jump && op.kind != Kind::Call) return -1;
    const int raw = e.b[n + 1] | (e.b[n + 2] << 8);
    return op.rel ? (e.addr + n + 3 + int16_t(raw)) & 0xFFFF : raw;
}

void MintDriver::set_jump_target(Event& e, uint16_t addr) const {
    const int n = prefix_len(e.b);
    const int i = e.b[n] - 0xC0;
    if (e.b[n] < 0xC0 || i >= 0x40) return;
    const int raw = L.ops[i].rel ? int(addr) - int(e.addr + n + 3) : int(addr);
    e.b[n + 1] = uint8_t(raw & 0xFF);
    e.b[n + 2] = uint8_t((raw >> 8) & 0xFF);
}

std::vector<uint8_t> MintDriver::serialize_relocated(const std::vector<Event>& ev, uint16_t dest, std::vector<int>* offsets) const {
    std::vector<int> off;
    std::vector<uint8_t> out = stream::Driver::serialize_relocated(ev, dest, &off);
    for (size_t i = 0; i < ev.size(); ++i) {
        if (ev[i].in_sub || off[i] < 0 || ev[i].type != EventType::Command) continue;
        const int n = prefix_len(ev[i].b);
        const int k = ev[i].b[n] - 0xC0;
        if (ev[i].b[n] < 0xC0 || k >= 0x40 || !L.ops[k].rel) continue;
        const int t = jump_target(ev[i]);
        if (t < 0) continue;
        const int j = relocated_target(ev, int(i), off);
        const int target = j >= 0 ? dest + off[size_t(j)] : t;
        const int rel = target - int(dest + off[i] + n + 3);
        out[size_t(off[i] + n + 1)] = uint8_t(rel & 0xFF);
        out[size_t(off[i] + n + 2)] = uint8_t((rel >> 8) & 0xFF);
    }
    if (offsets) *offsets = off;
    return out;
}

uint16_t MintDriver::live_ptr(const uint8_t* ram, const seq::Position& pos, int v) const {
    (void)pos;
    return uint16_t(ram[(L.ptr_lo_zp + v) & 0xFF] | (ram[(L.ptr_hi_zp + v) & 0xFF] << 8));
}

void MintDriver::live_state_writes(const uint8_t* ram, const seq::Position& pos, int voice, uint16_t ptr, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    (void)pos;
    int mapped = remap.find(live_ptr(ram, pos, voice));
    if (mapped >= 0) ptr = uint16_t(mapped);
    out.push_back({uint16_t(L.ptr_lo_zp + voice), uint8_t(ptr & 0xFF)});
    out.push_back({uint16_t(L.ptr_hi_zp + voice), uint8_t(ptr >> 8)});
    if (L.stack_base && L.stack_zp) {
        const int sp = ram[(L.stack_zp + voice) & 0xFF];
        for (int k = std::max(0, sp - 8); k + 1 < sp; ++k) seq::remap_word(ram, uint16_t(L.stack_base + k), remap, out);
    }
}

void MintDriver::track_pointer_writes(const seq::Song& song, int pattern_idx, int voice, uint16_t dest, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    (void)pattern_idx;
    auto it = slots_.find(uint32_t(song.order_addr) << 8 | uint32_t(voice));
    if (it == slots_.end()) return;
    const int rel = int(dest) - (it->second + 2);
    out.push_back({it->second, uint8_t(rel & 0xFF)});
    out.push_back({uint16_t(it->second + 1), uint8_t((rel >> 8) & 0xFF)});
}

bool MintDriver::transpose_event(Event& e, int semis) const {
    if (e.type != EventType::Note) return false;
    const int n = prefix_len(e.b);
    const int key = (e.b[n] & 0x1F) + semis;
    if (key < 0 || key > 31) return false;
    e.b[n] = uint8_t((e.b[n] & 0xE0) | key);
    if (e.pitch >= 0) e.pitch += semis;
    return true;
}

void MintDriver::apply_note_byte(Event& e, uint8_t byte) const {
    const int n = prefix_len(e.b);
    if (byte == wait_op_ && wait_op_) {
        if (e.type == EventType::Note) { e.b[n] = wait_op_; e.size = uint8_t(n + 1); }
        e.type = EventType::Rest; e.pitch = -1;
        return;
    }
    if (e.type != EventType::Note) {
        e.type = EventType::Note;
        e.b[n] = uint8_t(0x80 | (byte & 0x1F)); e.size = uint8_t(n + 1);
        e.pitch = note_semitone(byte);
        return;
    }
    const int old = e.b[n] & 0x1F;
    e.b[n] = uint8_t((e.b[n] & 0xE0) | (byte & 0x1F));
    if (e.pitch >= 0) e.pitch += (byte & 0x1F) - old;
}

uint8_t MintDriver::note_byte_in(int semitone, const State& s) const {
    const int key = semitone + 12 - s.oct - s.trans;
    return uint8_t(0x80 | std::clamp(key, 0, 31));
}

bool MintDriver::set_duration(std::vector<Event>& ev, int i, int dur) const {
    if (i < 0 || i >= int(ev.size()) || dur < 1 || dur > 4096) return false;
    Event& e = ev[size_t(i)];
    if (e.duration < 0 || (e.type != EventType::Note && e.type != EventType::Rest)) return false;
    // A later event without a delta of its own keeps the old value.
    for (size_t k = size_t(i) + 1; k < ev.size(); ++k) {
        Event& x = ev[k];
        if (x.duration <= 0 && x.type == EventType::Command) continue;
        if (x.b[0] < 0x80) break;
        if (x.in_sub) return false;
        std::memmove(x.b + 1, x.b, 15); x.b[0] = uint8_t(std::min(x.duration, 127)); x.size = uint8_t(x.size + 1); x.addr = 0;
        break;
    }
    if (e.b[0] >= 0x80) { std::memmove(e.b + 1, e.b, 15); e.size = uint8_t(e.size + 1); }
    const int first = std::min(dur, 127);
    e.b[0] = uint8_t(first);
    e.duration = first;
    e.addr = 0;
    std::vector<Event> tail;
    for (int left = dur - first; left > 0 && wait_op_;) {
        const int n = std::min(left, 127);
        Event t{};
        t.type = EventType::Rest; t.b[0] = uint8_t(n); t.b[1] = wait_op_; t.size = 2; t.duration = n;
        tail.push_back(t);
        left -= n;
    }
    ev.insert(ev.begin() + i + 1, tail.begin(), tail.end());
    return true;
}

std::string MintDriver::event_text(const Event& e) const {
    char b[96];
    const int n = prefix_len(e.b);
    std::string pre;
    if (n >= 2) { std::snprintf(b, sizeof b, "  gate %d", e.b[1]); pre += b; }
    if (n >= 3) { std::snprintf(b, sizeof b, "  vel %d", e.b[2]); pre += b; }
    if (e.type == EventType::Note) {
        std::snprintf(b, sizeof b, "%s  %d ticks%s%s", note_name(e).c_str(), e.duration, n ? "" : " (last delta)", e.b[n] >= 0xA0 ? (e.b[n + 1] < 0x80 ? "  gate*" : "  vel*") : "");
        return b + pre;
    }
    if (e.type == EventType::Rest) { std::snprintf(b, sizeof b, "wait  %d ticks", e.duration); return b + pre; }
    if (e.type == EventType::Command || e.type == EventType::End) {
        const int t = jump_target(e);
        std::string s = cmd_name(e.b[n]);
        if (t >= 0) { std::snprintf(b, sizeof b, " $%04X", t); s += b; }
        else for (int k = n + 1; k < e.size; ++k) { std::snprintf(b, sizeof b, " %02X", e.b[k]); s += b; }
        return s + pre;
    }
    return seq::Driver::event_text(e);
}

double MintDriver::ticks_per_second(const uint8_t* ram) const {
    const int latch = ram[0xFA];
    if (!latch || !L.tempo_zp) return 0;
    return 8000.0 / latch * rd16(ram, L.tempo_zp) / 256.0;
}

bool MintDriver::tempo_writes(const uint8_t* ram, double tps, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    const int latch = ram[0xFA];
    if (!latch || !L.tempo_zp || tps <= 0) return false;
    const int t = std::clamp(int(tps * latch / 8000.0 * 256.0 + 0.5), 1, 0xFFFF);
    out.push_back({L.tempo_zp, uint8_t(t & 0xFF)});
    out.push_back({uint16_t(L.tempo_zp + 1), uint8_t(t >> 8)});
    return true;
}

}
