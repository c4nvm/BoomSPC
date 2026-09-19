// N-SPC driver: seq::Driver over a Layout, plus the tracker-style edit
// operations that know about N-SPC's sticky length bytes.
#include "nspc.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace nspc {
namespace {
const char* kSmwCodes[] = {"Ins", "Pan", "PnF", "Bnd", "Vib", "VbO", "GVl", "GVF", "Tmp", "TmF", "GTr", "Trm", "TrO",
                           "Vol", "VlF", "Sub", "VbF", "PEr", "PEa", "PEo", "Tun", "Ech", "EcO", "EcP", "EcF",
                           "Smp", "Msc", "FIR", "DSP", "F7?", "Noi", "Snd", "Ms2", "Arp", "Rmt", "Tr2", "FE?", "FF?"};
const char* kEbCodes[]  = {"Ins", "Pan", "PnF", "Vib", "VbO", "GVl", "GVF", "Tmp", "TmF", "GTr", "Trn", "Trm", "TrO",
                           "Vol", "VlF", "Sub", "VbF", "PEr", "PEa", "PEo", "Tun", "Ech", "EcO", "EcP", "EcF", "Sld", "Prc"};

void retime_events(std::vector<Event>& ev) {
    int tick = 0, cur_len = 0;
    for (Event& e : ev) {
        e.tick = tick;
        switch (e.type) {
            case EventType::Length: cur_len = e.b[0]; e.duration = 0; break;
            case EventType::Note: case EventType::Tie: case EventType::Rest: case EventType::Percussion:
                e.duration = cur_len; break;
            default: e.duration = 0; break;
        }
        tick += e.duration;
    }
}

Event make_length(int ticks, int qv = -1) {
    Event e{};
    e.type = EventType::Length;
    e.b[0] = uint8_t(std::clamp(ticks, 1, 0x7F));
    e.size = 1;
    if (qv >= 0) { e.b[1] = uint8_t(qv); e.size = 2; }
    return e;
}

Event make_note(uint8_t byte) {
    Event e{};
    e.type = byte >= 0x80 ? EventType::Note : EventType::Rest;
    e.b[0] = byte;
    e.size = 1;
    return e;
}

void emit_span(std::vector<Event>& out, int ticks, uint8_t first, uint8_t cont, int& cur_len) {
    bool first_done = false;
    while (ticks > 0) {
        int n = std::min(ticks, 0x7F);
        if (n != cur_len) { out.push_back(make_length(n)); cur_len = n; }
        Event e = make_note(first_done ? cont : first);
        out.push_back(e);
        first_done = true;
        ticks -= n;
    }
}

}

bool NspcDriver::set_note_at(std::vector<Event>& ev, int tick, uint8_t note_byte, int pattern_len) const {
    retime_events(ev);
    while (!ev.empty() && ev.back().type == EventType::End) ev.pop_back();

    int idx = -1;
    for (size_t i = 0; i < ev.size(); ++i)
        if (ev[i].duration > 0 && ev[i].tick <= tick && tick < ev[i].tick + ev[i].duration) { idx = int(i); break; }
    if (idx >= 0 && ev[size_t(idx)].in_sub) {
        unroll_at(ev, tick);
        idx = -1;
        for (size_t i = 0; i < ev.size(); ++i)
            if (ev[i].duration > 0 && ev[i].tick <= tick && tick < ev[i].tick + ev[i].duration) { idx = int(i); break; }
    }

    if (idx < 0) {
        int end = 0, cur_len = 0;
        for (const Event& e : ev) { end = std::max(end, e.tick + e.duration); if (e.type == EventType::Length) cur_len = e.b[0]; }
        if (tick < end) return false;
        std::vector<Event> tail;
        emit_span(tail, tick - end, L.rest, L.rest, cur_len);
        int remain = std::max(1, std::min(pattern_len - tick, 0x7F));
        if (remain != cur_len) tail.push_back(make_length(remain));
        tail.push_back(make_note(note_byte));
        ev.insert(ev.end(), tail.begin(), tail.end());
        retime_events(ev);
        return true;
    }

    Event& cur = ev[idx];
    if (cur.tick == tick) {
        cur.b[0] = note_byte;
        cur.type = note_byte == L.tie ? EventType::Tie : note_byte == L.rest ? EventType::Rest :
                   (L.perc_base && note_byte >= L.perc_base && note_byte <= L.perc_end) ? EventType::Percussion : EventType::Note;
        return true;
    }

    const int total = cur.duration;
    const int before = tick - cur.tick;
    const int after  = total - before;
    int cur_len_after = total;
    uint8_t qv = 0; bool has_qv = false;
    for (int i = idx - 1; i >= 0; --i)
        if (ev[i].type == EventType::Length) { if (ev[i].size == 2) { qv = ev[i].b[1]; has_qv = true; } break; }

    std::vector<Event> repl;
    repl.push_back(make_length(before, has_qv ? qv : -1));
    Event head = cur; head.duration = 0;
    repl.push_back(head);
    repl.push_back(make_length(after));
    repl.push_back(make_note(note_byte));
    int erase_from = idx;
    if (idx > 0 && ev[idx - 1].type == EventType::Length) erase_from = idx - 1;
    ev.erase(ev.begin() + erase_from, ev.begin() + idx + 1);
    ev.insert(ev.begin() + erase_from, repl.begin(), repl.end());
    idx = erase_from;
    size_t next = idx + repl.size();
    bool next_has_len = false;
    for (size_t i = next; i < ev.size(); ++i) {
        if (ev[i].type == EventType::Length) { next_has_len = true; break; }
        if (ev[i].duration > 0 || (ev[i].type != EventType::Command && ev[i].type != EventType::SubCall)) break;
    }
    if (!next_has_len && next < ev.size() && after != cur_len_after) ev.insert(ev.begin() + next, make_length(cur_len_after));
    retime_events(ev);
    return true;
}

bool NspcDriver::extend(std::vector<Event>& ev, int ticks) const {
    retime_events(ev);
    int end = 0, cur_len = 0;
    for (const Event& e : ev) { end = std::max(end, e.tick + e.duration); if (e.type == EventType::Length) cur_len = e.b[0]; }
    if (ticks <= end) return true;
    while (!ev.empty() && ev.back().type == EventType::End) ev.pop_back();
    std::vector<Event> tail;
    emit_span(tail, ticks - end, L.rest, L.rest, cur_len);
    ev.insert(ev.end(), tail.begin(), tail.end());
    retime_events(ev);
    return true;
}

bool NspcDriver::insert_command_at(std::vector<Event>& ev, int tick, const uint8_t* bytes, int size) const {
    retime_events(ev);
    unroll_at(ev, tick);
    for (const Event& e : ev)
        if (e.duration > 0 && e.tick < tick && tick < e.tick + e.duration) {
            uint8_t cont = e.type == EventType::Rest ? L.rest : L.tie;
            if (!set_note_at(ev, tick, cont, tick + 1)) return false;
            break;
        }
    size_t at = ev.size();
    for (size_t i = 0; i < ev.size(); ++i)
        if (ev[i].tick >= tick && ev[i].type != EventType::Length) { at = i; break; }
    while (at > 0 && at == ev.size() && ev[at - 1].type == EventType::End) --at;
    Event e{};
    e.type = EventType::Command;
    e.size = uint8_t(size);
    std::memcpy(e.b, bytes, size_t(size));
    ev.insert(ev.begin() + long(at), e);
    retime_events(ev);
    return true;
}

int NspcDriver::ensure_own_length(std::vector<Event>& ev, int idx) {
    retime_events(ev);
    if (idx < 0 || idx >= int(ev.size()) || ev[size_t(idx)].duration <= 0) return -1;
    const int len = ev[size_t(idx)].duration;
    int qv = -1;
    for (int i = idx - 1; i >= 0; --i)
        if (ev[size_t(i)].type == EventType::Length && ev[size_t(i)].size == 2) { qv = ev[size_t(i)].b[1]; break; }

    bool timed_follows = false, len_between = false, qv_between = false;
    for (int i = idx + 1; i < int(ev.size()); ++i) {
        const Event& e = ev[size_t(i)];
        if (e.type == EventType::Length) { len_between = true; if (e.size == 2) qv_between = true; }
        else if (e.duration > 0) { timed_follows = true; break; }
        else if (e.type == EventType::End) break;
    }
    if (timed_follows && (!len_between || (qv >= 0 && !qv_between))) {
        Event r{};
        r.type = EventType::Length;
        r.b[0] = uint8_t(len);
        r.size = 1;
        if (qv >= 0) { r.b[1] = uint8_t(qv); r.size = 2; }
        ev.insert(ev.begin() + idx + 1, r);
    }
    if (idx > 0 && ev[size_t(idx - 1)].type == EventType::Length && !ev[size_t(idx - 1)].in_sub) {
        Event& l = ev[size_t(idx - 1)];
        if (l.size == 1 && qv >= 0) { l.b[1] = uint8_t(qv); l.size = 2; }
        retime_events(ev);
        return idx - 1;
    }
    Event l{};
    l.type = EventType::Length;
    l.b[0] = uint8_t(len);
    l.size = 1;
    if (qv >= 0) { l.b[1] = uint8_t(qv); l.size = 2; }
    ev.insert(ev.begin() + idx, l);
    retime_events(ev);
    return idx;
}

bool NspcDriver::set_qv(std::vector<Event>& ev, int timed_idx, int q, int v) const {
    int li = ensure_own_length(ev, timed_idx);
    if (li < 0) return false;
    Event& l = ev[size_t(li)];
    l.size = 2;
    l.b[1] = uint8_t(((q & 7) << 4) | (v & 15));
    return true;
}

bool NspcDriver::set_instrument(std::vector<Event>& ev, int tick0, int tick1, uint8_t ins) const {
    retime_events(ev);
    for (Event& e : ev)
        if (!e.in_sub && e.type == EventType::Command && e.b[0] == L.cmd_base && e.tick >= tick0 && e.tick < tick1) {
            e.b[1] = ins;
            return true;
        }
    uint8_t bytes[2] = {L.cmd_base, ins};
    return insert_command_at(ev, tick0, bytes, 2);
}

bool NspcDriver::remove_span(std::vector<Event>& ev, int tick, int ticks, bool keep_length) const {
    retime_events(ev);
    if (ticks <= 0) return false;
    const int t1 = tick + ticks;
    for (int guard = 0; guard < 64; ++guard) {
        int hit = -1;
        for (const Event& e : ev) if (e.in_sub && e.duration > 0 && e.tick < t1 && e.tick + e.duration > tick) { hit = e.tick; break; }
        if (hit < 0 || !unroll_at(ev, std::max(hit, tick))) break;
    }
    int total = 0;
    for (const Event& e : ev) total = std::max(total, e.tick + e.duration);
    for (int i = int(ev.size()) - 1; i >= 0; --i) {
        Event& e = ev[size_t(i)];
        if (e.duration <= 0) continue;
        int s = e.tick, d = e.tick + e.duration;
        int overlap = std::min(d, t1) - std::max(s, tick);
        if (overlap <= 0) continue;
        if (overlap >= e.duration) { ev.erase(ev.begin() + i); continue; }
        int nd = e.duration - overlap;
        int li = ensure_own_length(ev, i);
        if (li < 0) return false;
        ev[size_t(li)].b[0] = uint8_t(nd);
        retime_events(ev);
    }
    retime_events(ev);
    if (keep_length) {
        int last = -1;
        for (int i = 0; i < int(ev.size()); ++i) if (ev[size_t(i)].duration > 0 && !ev[size_t(i)].in_sub) last = i;
        int now = 0;
        for (const Event& e : ev) now = std::max(now, e.tick + e.duration);
        int missing = total - now;
        if (last >= 0 && missing > 0 && ev[size_t(last)].duration + missing <= 0x7F) {
            int li = ensure_own_length(ev, last);
            ev[size_t(li)].b[0] = uint8_t(ev[size_t(li + 1)].duration + missing);
            retime_events(ev);
        } else if (missing > 0) {
            int cur_len = 0;
            for (const Event& e : ev) if (e.type == EventType::Length) cur_len = e.b[0];
            const uint8_t tie = L.tie;
            while (!ev.empty() && ev.back().type == EventType::End) ev.pop_back();
            std::vector<Event> tail;
            emit_span(tail, missing, tie, tie, cur_len);
            ev.insert(ev.end(), tail.begin(), tail.end());
            retime_events(ev);
        }
    }
    return true;
}

bool NspcDriver::insert_span(std::vector<Event>& ev, int tick, int ticks, uint8_t byte) const {
    retime_events(ev);
    if (ticks <= 0) return false;
    int total = 0;
    for (const Event& e : ev) total = std::max(total, e.tick + e.duration);
    if (tick >= total) return false;
    unroll_at(ev, tick);
    int idx = -1;
    for (int i = 0; i < int(ev.size()); ++i) if (ev[size_t(i)].duration > 0 && ev[size_t(i)].tick == tick) { idx = i; break; }
    if (idx < 0) {
        uint8_t cont = L.tie;
        for (const Event& e : ev)
            if (e.duration > 0 && e.tick < tick && tick < e.tick + e.duration) { cont = e.type == EventType::Rest ? L.rest : L.tie; break; }
        if (!set_note_at(ev, tick, cont, total)) return false;
        for (int i = 0; i < int(ev.size()); ++i) if (ev[size_t(i)].duration > 0 && ev[size_t(i)].tick == tick) { idx = i; break; }
        if (idx < 0) return false;
    }
    int li = ensure_own_length(ev, idx);
    if (li < 0) return false;
    std::vector<Event> ins;
    int cur_len = 0;
    emit_span(ins, ticks, byte, byte, cur_len);
    ev.insert(ev.begin() + li, ins.begin(), ins.end());
    retime_events(ev);
    return remove_span(ev, total, ticks, false);
}

void NspcDriver::track_pointer_writes(const Song& song, int pattern_idx, int voice, uint16_t dest, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    if (pattern_idx < 0 || pattern_idx >= int(song.patterns.size())) return;
    const uint16_t at = uint16_t(song.patterns[size_t(pattern_idx)].addr + voice * 2), raw = L.unresolve(dest);
    out.push_back({at, uint8_t(raw & 0xFF)});
    out.push_back({uint16_t(at + 1), uint8_t(raw >> 8)});
}

std::string NspcDriver::name() const {
    std::string n = variant_name(L.variant);
    if (L.profile != Profile::Unknown && L.profile != Profile::Standard && L.profile != Profile::Earlier) n += std::string(", ") + profile_name(L.profile);
    if (L.amk) n += " + AddmusicK";
    return n;
}

uint8_t NspcDriver::note_byte(int semitone_from_c0) const {
    return uint8_t(std::clamp(0x80 + semitone_from_c0 - 12, 0x80, int(L.tie) - 1));
}

// Codes for the commands past the standard set (see the profile tables in nspc.cpp).
const char* kFe3ExtraCodes[] = {"EcO", "EcF", "LgO", "LgF", "Mod", "Prt", "JmC", "Jmp", "VTb", "VpD", "VpL", "ADS", "GnS"};
const char* kTaExtraCodes[]  = {"EcO", "EcF", "ADS", "GnS", "GnT", "VpD", "VpL", "PcT", "Sub"};
const char* kFe4ExtraCodes[] = {"EcO", "EcF", "Gn1", "Gn2", "F9?", "VpD", "VpL", "PcT", "Sub"};
const char* kSt3ExtraCodes[] = {"Lgt", "Noi", "ADS", "Smp", "Nop"};
const char* kHumanExtraCodes[] = {"Skp", "Nop", "Rst", "VMd", "Nop"};

const char* NspcDriver::cmd_code(uint8_t op) const {
    int i = op - L.cmd_base;
    if (L.variant == Variant::SMW && i >= 0 && i < int(sizeof kSmwCodes / sizeof *kSmwCodes)) return kSmwCodes[i];
    if (L.variant == Variant::EB && i >= 0 && i < int(sizeof kEbCodes / sizeof *kEbCodes)) return kEbCodes[i];
    const int x = i - L.command_count;
    if (x >= 0) {
        if (L.profile == Profile::IntelliFe3 && x < int(sizeof kFe3ExtraCodes / sizeof *kFe3ExtraCodes)) return kFe3ExtraCodes[x];
        if (L.profile == Profile::IntelliTa && x < int(sizeof kTaExtraCodes / sizeof *kTaExtraCodes)) return kTaExtraCodes[x];
        if (L.profile == Profile::IntelliFe4 && x < int(sizeof kFe4ExtraCodes / sizeof *kFe4ExtraCodes)) return kFe4ExtraCodes[x];
        if (L.profile == Profile::SuperTetris3 && x < int(sizeof kSt3ExtraCodes / sizeof *kSt3ExtraCodes)) return kSt3ExtraCodes[x];
        if (L.profile == Profile::Human && x < int(sizeof kHumanExtraCodes / sizeof *kHumanExtraCodes)) return kHumanExtraCodes[x];
    }
    if (L.profile == Profile::Konami && op == 0xE5) return "Lp[";
    if (L.profile == Profile::Konami && op == 0xE6) return "Lp]";
    if (L.profile == Profile::Konami && op == 0xFB) return "ADS";
    if (L.profile == Profile::Quintet && op == 0xFF) return "ADS";
    if (L.profile == Profile::Quintet && op == 0xF4) return "Tun";
    return "???";
}

bool NspcDriver::pitch_fx(const seq::Event& e, seq::PitchFx& out) const {
    if (e.type != EventType::Command) return false;
    const char* n = L.cmd_name(e.b[0]);
    auto has = [&](const char* s) { return std::strstr(n, s) != nullptr; };
    out = seq::PitchFx{};
    if (has("Vibrato off")) { out.kind = seq::PitchFx::VibratoOff; return true; }
    if (has("Vibrato fade")) return false;
    if (has("Vibrato")) { out.kind = seq::PitchFx::Vibrato; out.delay = e.b[1]; out.rate = e.b[2]; out.depth = e.b[3]; return true; }
    if (has("Pitch env off")) { out.kind = seq::PitchFx::SlideOff; return true; }
    if (has("Pitch env")) { out.kind = seq::PitchFx::Slide; out.delay = e.b[1]; out.length = e.b[2]; out.delta = has("release") ? int8_t(e.b[3]) : -int8_t(e.b[3]); out.next_note = true; return true; }
    if (has("Pitch bend") || has("Pitch slide")) {
        out.kind = seq::PitchFx::Slide; out.delay = e.b[1]; out.length = e.b[2];
        if (is_note_byte(e.b[3])) out.target = note_semitone(e.b[3]);
        out.next_note = true;
        return true;
    }
    return false;
}

bool NspcDriver::make_slide(int from, int to, int ticks, uint8_t out[16], int& size) const {
    (void)from;
    for (int op = L.cmd_base; op < 0x100; ++op) {
        const char* n = L.cmd_name(uint8_t(op));
        if (std::strstr(n, "Pitch bend") || std::strstr(n, "Pitch slide")) {
            if (L.cmd_size(uint8_t(op)) != 4) return false;
            out[0] = uint8_t(op); out[1] = 0; out[2] = uint8_t(std::clamp(ticks, 1, 255)); out[3] = note_byte(to);
            size = 4;
            return true;
        }
    }
    return false;
}

seq::FxClass NspcDriver::cmd_class(uint8_t op) const {
    using seq::FxClass;
    if (op == L.cmd_base) return FxClass::Instrument;
    const char* n = L.cmd_name(op);
    auto has = [&](const char* s) { return std::strstr(n, s) != nullptr; };
    if (has("Unknown") || has("?")) return FxClass::Invalid;
    if (has("AMK")) return FxClass::Sys2;
    if (has("Echo")) return FxClass::Sys1;
    if (has("Pan")) return FxClass::Panning;
    if (has("Tempo")) return FxClass::Speed;
    if (has("Subroutine") || has("Percussion")) return FxClass::Song;
    if (has("Vibrato") || has("Pitch") || has("tune") || has("ranspose") || has("slide") || has("bend")) return FxClass::Pitch;
    if (has("vol") || has("Vol") || has("Tremolo")) return FxClass::Volume;
    return FxClass::Misc;
}

Instrument NspcDriver::read_instrument(const uint8_t* ram, int index) const {
    int base = nspc::instrument_count(ram, L);
    if (index >= base && L.inst2_count) {
        const uint8_t* e = ram + ((L.inst_table2 + (index - base) * 6) & 0xFFFF);
        Instrument in{};
        in.srcn = e[0]; in.adsr0 = e[1]; in.adsr1 = e[2]; in.gain = e[3]; in.pitch_hi = e[4]; in.pitch_lo = e[5];
        return in;
    }
    return nspc::read_instrument(ram, L, index);
}

int NspcDriver::instrument_number(const uint8_t* ram, int index) const {
    int base = nspc::instrument_count(ram, L);
    return index >= base && L.inst2_count ? L.inst2_first + (index - base) : index;
}

int NspcDriver::instrument_index(const uint8_t* ram, int number) const {
    int base = nspc::instrument_count(ram, L);
    return number >= L.inst2_first && L.inst2_count ? base + (number - L.inst2_first) : number;
}

bool NspcDriver::preview_regs(const uint8_t* ram, uint8_t note_byte, int instrument, uint8_t regs[8]) const {
    if (!L.inst_table) return false;
    int count = nspc::instrument_count(ram, L);
    int index = instrument_index(ram, instrument);
    if (index >= count + L.inst2_count) index = std::max(0, count - 1);
    Instrument in = read_instrument(ram, index);
    int pitch = nspc::note_pitch(ram, nspc::note_semitone(L, note_byte), in.pitch_hi, in.pitch_lo);
    regs[0] = 0x30; regs[1] = 0x30;
    regs[2] = uint8_t(pitch & 0xFF); regs[3] = uint8_t(pitch >> 8);
    regs[4] = in.srcn; regs[5] = in.adsr0; regs[6] = in.adsr1; regs[7] = in.gain;
    return true;
}

std::vector<Song> NspcDriver::find_songs(const uint8_t* ram, const uint8_t* dsp) const {
    std::vector<Song> songs = nspc::find_songs(ram, L, dsp);
    for (Song& sg : songs) {
        char b[64];
        std::snprintf(b, sizeof b, "song @%04X (%zu orders, %d ticks)", sg.order_addr, sg.orders.size(), sg.total_ticks());
        sg.label = b;
    }
    return songs;
}

void NspcDriver::retime(std::vector<Event>& ev) const { retime_events(ev); }

double NspcDriver::ticks_per_second(const uint8_t* ram) const {
    if (!L.tempo_addr) return 0;
    int latch = ram[0xFA] ? ram[0xFA] : 0x10;
    return (8000.0 / latch) * ram[L.tempo_addr] / 256.0;
}

bool NspcDriver::tempo_writes(const uint8_t* ram, double tps, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    if (!L.tempo_addr) return false;
    int latch = ram[0xFA] ? ram[0xFA] : 0x10;
    int v = int(tps * 256.0 * latch / 8000.0 + 0.5);
    out.push_back({L.tempo_addr, uint8_t(std::clamp(v, 1, 255))});
    return true;
}

}
