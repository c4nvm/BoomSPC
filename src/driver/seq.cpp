#include "seq.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdio>

#include "akao.hpp"
#include "capcom.hpp"
#include "rare.hpp"
#include "follin.hpp"
#include "chun.hpp"
#include "compile.hpp"
#include "hudson.hpp"
#include "konami.hpp"
#include "mint.hpp"
#include "pandora.hpp"
#include "prism.hpp"
#include "graphres.hpp"
#include "ascii.hpp"
#include "falcom.hpp"
#include "neverland.hpp"
#include "berlioz.hpp"
#include "slick.hpp"
#include "wolfteam.hpp"
#include "heartbeat.hpp"
#include "nspc.hpp"
#include "wario.hpp"

namespace seq {
int Song::pattern_index(uint16_t addr) const {
    for (size_t i = 0; i < patterns.size(); ++i)
        if (patterns[i].addr == addr) return int(i);
    return -1;
}

int Song::total_ticks() const {
    int t = 0;
    for (const Order& o : orders) {
        int pi = pattern_index(o.pattern_addr);
        if (pi >= 0) t += patterns[pi].length_ticks;
    }
    return t;
}

std::string note_name(int semitone) {
    static const char* names[] = {"C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"};
    if (semitone < 0) return "???";
    int oct = semitone / 12;
    return std::string(names[semitone % 12]) + char('0' + std::min(oct, 9));
}

bool Driver::transpose_event(Event& e, int semis) const {
    if (e.type != EventType::Note) return false;
    int b = int(e.b[0]) + semis;
    if (b < int(note_min()) || b > int(note_max())) return false;
    e.b[0] = uint8_t(b);
    return true;
}

std::string Driver::event_text(const Event& e) const {
    char b[64];
    switch (e.type) {
        case EventType::Length:
            if (e.size == 2) std::snprintf(b, sizeof b, "len %d  q%X v%X", e.b[0], e.b[1] >> 4, e.b[1] & 15);
            else std::snprintf(b, sizeof b, "len %d", e.b[0]);
            return b;
        case EventType::SubCall: std::snprintf(b, sizeof b, "Subroutine $%02X%02X x%d", e.b[2], e.b[1], e.b[3]); return b;
        case EventType::Command: {
            std::string s = cmd_name(e.b[0]);
            for (int i = 1; i < e.size; ++i) { std::snprintf(b, sizeof b, " %02X", e.b[i]); s += b; }
            return s;
        }
        case EventType::End: return "End";
        case EventType::Note: return note_name(e);
        case EventType::Tie: return "tie";
        case EventType::Rest: return "rest (note off)";
        case EventType::Percussion: std::snprintf(b, sizeof b, "percussion %d", percussion_index(e.b[0])); return b;
    }
    return "?";
}

bool Driver::pitch_fx(const Event& e, PitchFx& out) const {
    if (e.type != EventType::Command || cmd_class(e.b[0]) != FxClass::Pitch) return false;
    std::string n = cmd_name(e.b[0]);
    for (char& c : n) c = char(std::tolower(uint8_t(c)));
    const bool off = n.find("off") != std::string::npos;
    out = PitchFx{};
    if (n.find("vibrato") != std::string::npos) {
        out.kind = off ? PitchFx::VibratoOff : PitchFx::Vibrato;
        if (!off && e.size > 1) out.depth = e.b[e.size - 1];
    } else if (n.find("portamento") != std::string::npos) {
        out.kind = off ? PitchFx::SlideOff : PitchFx::Portamento;
    } else if (n.find("slide") != std::string::npos || n.find("bend") != std::string::npos || n.find("envelope") != std::string::npos) {
        out.kind = off ? PitchFx::SlideOff : PitchFx::Slide;
    } else return false;
    return true;
}

double Driver::pitch_units(int semitone) const {
    return 4096.0 * std::pow(2.0, (semitone - 60) / 12.0);
}

bool Driver::set_slide(std::vector<Event>& ev, int tick, int dur, int from, int to, int existing) const {
    uint8_t bytes[16]; int size = 0;
    if (!make_slide(from, to, dur, bytes, size)) return false;
    if (existing >= 0 && existing < int(ev.size()) && ev[size_t(existing)].type == EventType::Command && ev[size_t(existing)].size == size && !ev[size_t(existing)].in_sub) {
        std::memcpy(ev[size_t(existing)].b, bytes, size_t(size));
        return true;
    }
    return insert_command_at(ev, tick, bytes, size);
}

void Driver::unroll(std::vector<Event>& ev) const {
    std::vector<Event> out;
    out.reserve(ev.size());
    for (const Event& e : ev) {
        if (e.type == EventType::SubCall || is_frame_command(e)) continue;
        Event c = e;
        if (c.in_sub) c.addr = 0;
        c.in_sub = false; c.nest = 0; c.sub_iter = 0; c.in_call = false;
        out.push_back(c);
    }
    ev.swap(out);
    retime(ev);
}

bool Driver::unroll_at(std::vector<Event>& ev, int tick) const {
    retime(ev);
    int i = -1;
    for (size_t k = 0; k < ev.size(); ++k) {
        const Event& e = ev[k];
        if (!e.in_sub) continue;
        if ((e.duration > 0 && e.tick <= tick && tick < e.tick + e.duration) || (e.duration == 0 && e.tick == tick)) { i = int(k); break; }
    }
    if (i < 0) return false;
    int a = i, b = i;
    while (a > 0 && ev[size_t(a - 1)].in_sub) --a;
    while (b + 1 < int(ev.size()) && ev[size_t(b + 1)].in_sub) ++b;
    const bool call = a > 0 && !ev[size_t(a - 1)].in_sub && call_count(ev[size_t(a - 1)]) > 0;
    std::vector<Event> out;
    out.reserve(ev.size() + 8);
    if (call) {
        const int start = a - 1;
        const Event& cmd = ev[size_t(start)];
        const int n = call_count(cmd);
        const int level = ev[size_t(a)].nest;
        const uint16_t body = ev[size_t(a)].addr;
        for (int k = a + 1; k <= b; ++k) if (ev[size_t(k)].nest < level) { b = k - 1; break; }   // the caller's level resumes after the body
        std::vector<int> seg;   // first event index of each pass
        for (int k = a; k <= b; ++k) if (ev[size_t(k)].addr == body && ev[size_t(k)].nest == level && (seg.empty() || k > seg.back())) seg.push_back(k);
        if (seg.empty() || seg[0] != a) seg.insert(seg.begin(), a);
        int pass = 0;
        for (size_t k = 0; k < seg.size(); ++k) if (seg[k] <= i) pass = int(k);
        const int passes = int(seg.size());
        const int seg_lo = seg[size_t(pass)], seg_hi = pass + 1 < passes ? seg[size_t(pass) + 1] - 1 : b;
        for (int k = 0; k < start; ++k) out.push_back(ev[size_t(k)]);
        if (pass > 0) {
            Event c = cmd; set_call_count(c, pass); out.push_back(c);
            for (int k = a; k < seg_lo; ++k) out.push_back(ev[size_t(k)]);
        }
        for (int k = seg_lo; k <= seg_hi; ++k) {
            Event c = ev[size_t(k)];
            if (c.nest == level && is_return_command(c)) continue;
            if (c.nest >= level) { c.in_sub = c.sub_iter > 0; c.addr = 0; if (c.nest == level) c.in_call = false; }
            if (c.nest > 0) --c.nest;
            out.push_back(c);
        }
        if (passes - pass - 1 > 0 && n > pass + 1) {
            Event c = cmd; c.addr = 0; set_call_count(c, n - pass - 1); out.push_back(c);
            for (int k = seg_hi + 1; k <= b; ++k) out.push_back(ev[size_t(k)]);
        }
        for (int k = b + 1; k < int(ev.size()); ++k) out.push_back(ev[size_t(k)]);
    } else {
        int start = a;
        if (ev[size_t(a)].addr)
            for (int j = a - 1; j >= 0; --j) if (!ev[size_t(j)].in_sub && ev[size_t(j)].addr == ev[size_t(a)].addr) start = j;
        while (start > 0 && !ev[size_t(start - 1)].in_sub && ev[size_t(start - 1)].nest + 1 >= ev[size_t(a)].nest && (ev[size_t(start - 1)].type == EventType::SubCall || is_frame_command(ev[size_t(start - 1)]))) --start;   // the repeat's own start, not an enclosing one
        for (int k = 0; k < int(ev.size()); ++k) {
            const Event& e = ev[size_t(k)];
            if (k >= start && k <= b) {
                if (e.type == EventType::SubCall || is_frame_command(e)) continue;
                Event c = e;
                if (c.in_sub) c.addr = 0;
                c.in_sub = false; c.nest = 0; c.sub_iter = 0; c.in_call = false;
                out.push_back(c);
            } else out.push_back(e);
        }
    }
    ev.swap(out);
    retime(ev);
    return true;
}

bool Driver::extend(std::vector<Event>& ev, int ticks) const {
    retime(ev);
    const int chunk = 48;
    if (ev.empty()) {
        if (!has_stream_edit()) return false;
        Event r{};
        r.type = EventType::Rest; r.b[0] = rest_byte(); r.size = 1;
        ev.push_back(r);
        if (!set_duration(ev, 0, std::min(std::max(1, ticks), chunk))) { ev.clear(); return false; }
        uint8_t eb[16]; int es = 0;
        if (end_bytes(eb, es)) {
            Event e{};
            e.type = EventType::End; e.size = uint8_t(es);
            for (int i = 0; i < es; ++i) e.b[i] = eb[i];
            ev.push_back(e);
        }
        retime(ev);
    }
    for (int guard = 0; guard < 4096; ++guard) {
        int end = 0;
        for (const Event& e : ev) end = std::max(end, e.tick + e.duration);
        if (ticks <= end) return true;
        if (!insert_span(ev, end, std::min(chunk, ticks - end), rest_byte())) return end > 0;
        retime(ev);
    }
    return true;
}

std::unique_ptr<Driver> detect_snes_driver(std::shared_ptr<const std::vector<uint8_t>> rom) {
    if (!rom) return nullptr;
    return wario::detect(std::move(rom));
}

std::unique_ptr<Driver> detect_driver(const uint8_t* ram, const uint8_t* dsp) {
    nspc::Layout L = nspc::detect(ram);
    if (L.valid()) {
        nspc::refine_with_dsp(ram, dsp, L);
        return std::make_unique<nspc::NspcDriver>(L);
    }
    if (std::unique_ptr<Driver> d = follin::detect(ram)) return d;
    if (std::unique_ptr<Driver> d = akao::detect(ram)) return d;
    if (std::unique_ptr<Driver> d = rare::detect(ram)) return d;
    if (std::unique_ptr<Driver> d = capcom::detect(ram)) return d;
    if (std::unique_ptr<Driver> d = konami::detect(ram)) return d;
    if (std::unique_ptr<Driver> d = hudson::detect(ram)) return d;
    if (std::unique_ptr<Driver> d = chun::detect(ram)) return d;
    if (std::unique_ptr<Driver> d = mint::detect(ram)) return d;
    if (std::unique_ptr<Driver> d = compile::detect(ram)) return d;
    if (std::unique_ptr<Driver> d = pandora::detect(ram)) return d;
    if (std::unique_ptr<Driver> d = prism::detect(ram)) return d;
    if (std::unique_ptr<Driver> d = graphres::detect(ram)) return d;
    if (std::unique_ptr<Driver> d = ascii::detect(ram)) return d;
    if (std::unique_ptr<Driver> d = falcom::detect(ram)) return d;
    if (std::unique_ptr<Driver> d = heartbeat::detect(ram)) return d;
    if (std::unique_ptr<Driver> d = neverland::detect(ram)) return d;
    if (std::unique_ptr<Driver> d = berlioz::detect(ram)) return d;
    if (std::unique_ptr<Driver> d = slick::detect(ram)) return d;
    if (std::unique_ptr<Driver> d = wolfteam::detect(ram)) return d;
    return nullptr;
}

}
