#include "nspc.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <unordered_map>

namespace nspc {
namespace {
inline uint16_t rd16(const uint8_t* ram, uint32_t a) { return uint16_t(ram[a & 0xFFFF] | (ram[(a + 1) & 0xFFFF] << 8)); }

const CommandSpec kSmwCommands[] = {
    {"Instrument", 1},        // DA
    {"Pan", 1},               // DB
    {"Pan fade", 2},          // DC
    {"Pitch bend", 3},        // DD  delay, length, target note
    {"Vibrato", 3},           // DE  delay, rate, depth
    {"Vibrato off", 0},       // DF
    {"Global volume", 1},     // E0
    {"Global vol fade", 2},   // E1
    {"Tempo", 1},             // E2
    {"Tempo fade", 2},        // E3
    {"Global transpose", 1},  // E4
    {"Tremolo", 3},           // E5
    {"Tremolo off", 0},       // E6
    {"Volume", 1},            // E7
    {"Volume fade", 2},       // E8
    {"Subroutine", 3},        // E9  addr lo, hi, count
    {"Vibrato fade", 1},      // EA
    {"Pitch env release", 3}, // EB
    {"Pitch env attack", 3},  // EC
    {"Pitch env off", 0},     // ED
    {"Fine tune", 1},         // EE
    {"Echo on", 3},           // EF  voice mask, vol L, vol R
    {"Echo off", 0},          // F0
    {"Echo params", 3},       // F1  delay, feedback, FIR
    {"Echo vol fade", 3},     // F2
    {"AMK sample load", 2},   // F3
    {"AMK misc", 1},          // F4
    {"AMK FIR", 8},           // F5
    {"AMK DSP write", 2},     // F6
    {"AMK F7", 3},            // F7
    {"AMK noise", 1},         // F8
    {"AMK data send", 2},     // F9
    {"AMK misc 2", 2},        // FA
    {"AMK arpeggio", 2},      // FB  count, length, then `count` notes
    {"AMK remote", 4},        // FC
    {"AMK tremolo", 3},       // FD
    {"AMK FE", 0},            // FE
    {"AMK FF", 0},            // FF
};

const CommandSpec kEbCommands[] = {
    {"Instrument", 1},        // E0
    {"Pan", 1},               // E1
    {"Pan fade", 2},          // E2
    {"Vibrato", 3},           // E3
    {"Vibrato off", 0},       // E4
    {"Global volume", 1},     // E5
    {"Global vol fade", 2},   // E6
    {"Tempo", 1},             // E7
    {"Tempo fade", 2},        // E8
    {"Global transpose", 1},  // E9
    {"Transpose", 1},         // EA
    {"Tremolo", 3},           // EB
    {"Tremolo off", 0},       // EC
    {"Volume", 1},            // ED
    {"Volume fade", 2},       // EE
    {"Subroutine", 3},        // EF
    {"Vibrato fade", 1},      // F0
    {"Pitch env release", 3}, // F1
    {"Pitch env attack", 3},  // F2
    {"Pitch env off", 0},     // F3
    {"Fine tune", 1},         // F4
    {"Echo on", 3},           // F5
    {"Echo off", 0},          // F6
    {"Echo params", 3},       // F7
    {"Echo vol fade", 3},     // F8
    {"Pitch slide", 3},       // F9
    {"Percussion base", 1},   // FA
};

constexpr uint8_t kSmwSub = 0xE9;
constexpr uint8_t kEbSub  = 0xEF;

uint8_t sub_opcode(const Layout& L) { return L.variant == Variant::EB ? kEbSub : kSmwSub; }

int count_bytes(const uint8_t* ram, int lo, int hi, const uint8_t* needle, int n) {
    int c = 0;
    for (int a = lo; a + n <= hi; ++a)
        if (std::memcmp(ram + a, needle, size_t(n)) == 0) ++c;
    return c;
}

}

int Layout::cmd_size(uint8_t b) const {
    if (!is_command(b)) return 0;
    int i = b - cmd_base;
    if (i < 64 && len_from_ram[i]) return len_from_ram[i];
    if (i < command_count) return commands[i].argc + 1;
    return 0;
}

const char* Layout::cmd_name(uint8_t b) const {
    if (!is_command(b)) return "?";
    int i = b - cmd_base;
    return i < command_count ? commands[i].name : "Unknown";
}

int note_pitch(const uint8_t* ram, int semitone, int mult_hi, int mult_lo) {
    static const uint16_t kTable[13] = {0x085F, 0x08DE, 0x0965, 0x09F4, 0x0A8C, 0x0B2C, 0x0BD6,
                                        0x0C8B, 0x0D4A, 0x0E14, 0x0EEA, 0x0FCD, 0x10BE};
    int table_addr = -1;
    for (int a = 0x200; a < 0xFF00 && table_addr < 0; ++a)
        if (rd16(ram, a) == kTable[0] && rd16(ram, a + 2) == kTable[1] && rd16(ram, a + 4) == kTable[2] && rd16(ram, a + 6) == kTable[3]) table_addr = a;
    if (semitone < 0) semitone = 0;
    int n = semitone % 12, oct = semitone / 12;
    int base = table_addr >= 0 ? rd16(ram, table_addr + n * 2) : kTable[n];
    int shift = 5 - oct;
    if (shift > 0) base >>= shift; else if (shift < 0) base <<= -shift;
    int pitch = (base * ((mult_hi << 8) | mult_lo)) >> 8;
    return std::min(pitch, 0x3FFF);
}

const char* variant_name(Variant v) {
    switch (v) {
        case Variant::SMW: return "N-SPC (SMW type)";
        case Variant::EB:  return "N-SPC (EarthBound type)";
        default:           return "unknown";
    }
}

Layout layout_for(Variant v) {
    Layout L;
    L.variant = v;
    if (v == Variant::SMW) {
        L.cmd_base = 0xDA; L.tie = 0xC6; L.rest = 0xC7;
        L.perc_base = 0xD0; L.perc_end = 0xD9;
        L.commands = kSmwCommands; L.command_count = int(sizeof kSmwCommands / sizeof kSmwCommands[0]);
    } else if (v == Variant::EB) {
        L.cmd_base = 0xE0; L.tie = 0xC8; L.rest = 0xC9;
        L.perc_base = 0xCA; L.perc_end = 0xDF;
        L.commands = kEbCommands; L.command_count = int(sizeof kEbCommands / sizeof kEbCommands[0]);
    }
    return L;
}

Layout detect(const uint8_t* ram) {
    Layout L;
    constexpr int code_lo = 0x200, code_hi = 0x2000;

    const uint8_t cmp_da[] = {0x68, 0xDA};
    const uint8_t cmp_e0[] = {0x68, 0xE0};
    const uint8_t sbc_e0[] = {0xA8, 0xE0};
    const uint8_t cmp_c6[] = {0x68, 0xC6};
    const uint8_t cmp_c8[] = {0x68, 0xC8};
    int smw_score = count_bytes(ram, code_lo, code_hi, cmp_da, 2) * 2 + count_bytes(ram, code_lo, code_hi, cmp_c6, 2);
    int eb_score  = (count_bytes(ram, code_lo, code_hi, cmp_e0, 2) + count_bytes(ram, code_lo, code_hi, sbc_e0, 2)) * 2 +
                    count_bytes(ram, code_lo, code_hi, cmp_c8, 2);
    if (smw_score == 0 && eb_score == 0) return L;
    L = layout_for(smw_score >= eb_score ? Variant::SMW : Variant::EB);

    for (int a = code_lo; a < code_hi - 8; ++a)
        if (ram[a] == 0xE4 && ram[a + 2] == 0xEE && ram[a + 3] == 0xCF && ram[a + 4] == 0x60 && ram[a + 5] == 0x84) { L.tempo_addr = ram[a + 1]; break; }

    for (int a = code_lo; a < code_hi - 50 && !L.len_table; ++a) {
        if (ram[a] != 0x68 || ram[a + 1] != L.cmd_base || ram[a + 2] != 0x90) continue;
        for (int k = a + 3; k < a + 48; ++k) {
            if (ram[k] != 0x96 && ram[k] != 0xF6) continue;
            int table = (rd16(ram, k + 1) + L.cmd_base) & 0xFFFF;
            const uint8_t* t = ram + table;
            bool total = t[0] == 2 && t[1] == 2 && t[2] == 3 && t[3] == 4;
            bool args  = t[0] == 1 && t[1] == 1 && t[2] == 2 && t[3] == 3;
            if (!total && !args) continue;
            L.len_table = uint16_t(table);
            for (int i = 0; i < 64 && table + i < 0x10000; ++i) {
                uint8_t v = t[i];
                if (args) v = uint8_t(v + 1);
                if (v == 0 || v > 12) break;
                L.len_from_ram[i] = v;
            }
            break;
        }
    }
    if (!L.len_table) return Layout{};
    if (L.variant == Variant::SMW) {
        for (int i = 25; i < 64; ++i)
            if (L.len_from_ram[i]) { L.amk = true; break; }
    }

    for (int a = code_lo; a < code_hi - 8; ++a) {
        if (ram[a] != 0x8D || (ram[a + 1] != 5 && ram[a + 1] != 6)) continue;
        if (ram[a + 2] != 0x8F || ram[a + 5] != 0x8F) continue;
        if (ram[a + 7] != uint8_t(ram[a + 4] + 1)) continue;
        uint8_t stride = ram[a + 1];
        uint16_t addr = uint16_t(ram[a + 3] | (ram[a + 6] << 8));
        if (addr < 0x200) continue;
        if (L.variant == Variant::SMW) {
            if (stride == 5 && !L.inst_table) { L.inst_table = addr; L.inst_stride = 5; }
            else if (stride == 6 && !L.perc_table) L.perc_table = addr;
        } else {
            if (!L.inst_table) { L.inst_table = addr; L.inst_stride = stride; }
            else if (!L.perc_table && addr != L.inst_table) L.perc_table = addr;
        }
    }
    if (!L.inst_table) {
        auto looks_like_table = [&](int addr, int stride) {
            for (int i = 0; i < 2; ++i) {
                const uint8_t* e = ram + addr + i * stride;
                if (e[0] > 0x7F || (!(e[1] & 0x80) && e[3] == 0)) return false;
            }
            return true;
        };
        if (L.variant == Variant::EB && looks_like_table(0x6E00, 6)) { L.inst_table = 0x6E00; L.inst_stride = 6; }
        if (L.variant == Variant::SMW && !L.amk && looks_like_table(0x5F46, 5)) { L.inst_table = 0x5F46; L.inst_stride = 5; }
    }
    return L;
}

void refine_with_dsp(const uint8_t* ram, const uint8_t* dsp, Layout& L) {
    if (!L.valid() || L.inst_table) return;
    std::vector<int> hits;
    int live = 0;
    for (int v = 0; v < 8; ++v) {
        const uint8_t* r = dsp + v * 0x10 + 4;
        if (!(r[1] & 0x80) && r[3] == 0) continue;
        ++live;
        for (int a = 0x200; a < 0xFFF0; ++a)
            if (ram[a] == r[0] && ram[a + 1] == r[1] && ram[a + 2] == r[2] && ram[a + 3] == r[3]) hits.push_back(a);
    }
    if (live < 1 || hits.empty()) return;
    std::sort(hits.begin(), hits.end());
    hits.erase(std::unique(hits.begin(), hits.end()), hits.end());

    const int stride = 6;
    std::vector<std::pair<int, int>> grids;   // base, member count
    std::vector<bool> used(hits.size(), false);
    for (size_t i = 0; i < hits.size(); ++i) {
        if (used[i]) continue;
        int base = hits[i], n = 0;
        for (size_t j = i; j < hits.size(); ++j)
            if (!used[j] && hits[j] < base + 64 * stride && (hits[j] - base) % stride == 0) { used[j] = true; ++n; }
        while (base - stride >= 0x200 && ram[base - stride] < 0x40 && (ram[base - stride + 1] & 0x80)) base -= stride;
        grids.push_back({base, n});
    }
    if (grids.empty()) return;
    std::sort(grids.begin(), grids.end());
    L.inst_table = uint16_t(grids[0].first);
    L.inst_stride = stride;
    if (grids.size() > 1 && L.amk) {
        L.inst_table2 = uint16_t(grids[1].first);
        int n = 0;
        while (n < 64) {
            const uint8_t* e = ram + ((L.inst_table2 + n * stride) & 0xFFFF);
            if (e[0] > 0x7F || (!(e[1] & 0x80) && !e[3])) break;
            ++n;
        }
        L.inst2_count = n;
    }
}

namespace {
struct Parser {
    const uint8_t* ram;
    const Layout&  L;
    Track&         out;
    int            cur_len = 0;
    int            tick = 0;
    int            bytes_seen = 0;
    static constexpr int kMaxBytes  = 0x4000;
    static constexpr int kMaxEvents = 8192;

    bool push(Event e) {
        e.tick = tick;
        tick += e.duration;
        out.events.push_back(e);
        return out.events.size() < size_t(kMaxEvents);
    }

    int run(uint16_t addr, int sub_iter, bool in_sub) {
        uint32_t p = addr;
        while (bytes_seen < kMaxBytes) {
            uint8_t b = ram[p & 0xFFFF];
            Event e{};
            e.addr = uint16_t(p);
            e.in_sub = in_sub;
            e.sub_iter = sub_iter;
            e.b[0] = b;

            if (b == 0x00) {
                if (!in_sub) {
                    e.type = EventType::End; e.size = 1;
                    push(e);
                }
                return int(p & 0xFFFF);
            }
            if (b < 0x80) {
                e.type = EventType::Length; e.size = 1;
                cur_len = b;
                uint8_t n = ram[(p + 1) & 0xFFFF];
                if (n < 0x80 && n != 0) { e.size = 2; e.b[1] = n; }
                p += e.size; bytes_seen += e.size;
                if (!push(e)) return -1;
                continue;
            }
            if (b < L.tie) {
                e.type = EventType::Note; e.size = 1; e.duration = cur_len;
            } else if (b == L.tie) {
                e.type = EventType::Tie; e.size = 1; e.duration = cur_len;
            } else if (b == L.rest) {
                e.type = EventType::Rest; e.size = 1; e.duration = cur_len;
            } else if (L.perc_base && b >= L.perc_base && b <= L.perc_end) {
                e.type = EventType::Percussion; e.size = 1; e.duration = cur_len;
            } else if (L.is_command(b)) {
                int size = L.cmd_size(b);
                if (size <= 0) return -1;
                if (b == sub_opcode(L)) {
                    if (in_sub) return -1;
                    e.type = EventType::SubCall; e.size = 4;
                    for (int i = 1; i < 4; ++i) e.b[i] = ram[(p + i) & 0xFFFF];
                    uint16_t target = uint16_t(e.b[1] | (e.b[2] << 8));
                    int count = e.b[3];
                    p += 4; bytes_seen += 4;
                    if (!push(e)) return -1;
                    if (target < 0x100 || count == 0) return -1;
                    for (int it = 0; it < count; ++it)
                        if (run(target, it, true) < 0) return -1;
                    continue;
                }
                e.type = EventType::Command;
                if (L.amk && b == 0xFB) {
                    int n = ram[(p + 1) & 0xFFFF];
                    size += (n & 0x80) ? 1 : n;
                }
                if (size > int(sizeof e.b)) return -1;
                e.size = uint8_t(size);
                for (int i = 1; i < size; ++i) e.b[i] = ram[(p + i) & 0xFFFF];
            } else {
                return -1;
            }
            p += e.size; bytes_seen += e.size;
            if (!push(e)) return -1;
        }
        return -1;
    }
};

}

Track parse_track(const uint8_t* ram, const Layout& L, uint16_t addr) {
    Track t;
    t.addr = addr;
    if (!addr) return t;
    Parser P{ram, L, t};
    if (P.run(addr, 0, false) < 0) t.truncated = true;
    t.total_ticks = P.tick;
    return t;
}

Pattern parse_pattern(const uint8_t* ram, const Layout& L, uint16_t addr) {
    Pattern p;
    p.addr = addr;
    int len = -1;
    for (int v = 0; v < 8; ++v) {
        uint16_t ta = rd16(ram, addr + v * 2);
        p.tracks[v] = parse_track(ram, L, ta);
        const Track& t = p.tracks[v];
        if (t.addr && !t.truncated && (len < 0 || t.total_ticks < len)) len = t.total_ticks;
    }
    p.length_ticks = std::max(len, 0);

    for (int v = 0; v < 8; ++v) {
        Track& t = p.tracks[v];
        if (!t.addr) continue;
        size_t n = 0;
        while (n < t.events.size() && t.events[n].tick < p.length_ticks) ++n;
        if (n < t.events.size() && t.events[n].type == EventType::End) ++n;
        if (n == 0 && !t.events.empty()) n = 1;
        t.used_events = int(n);
        uint16_t end = t.addr;
        for (size_t i = 0; i < n; ++i)
            if (!t.events[i].in_sub) end = std::max<uint16_t>(end, uint16_t(t.events[i].addr + t.events[i].size));
        if (n > 0 && t.events[n - 1].type == EventType::End) { t.end_addr = end; t.terminated = true; }
        else { t.end_addr = end; t.terminated = ram[end] == 0x00 && n < t.events.size() && t.events[n].type == EventType::End; }
    }
    return p;
}

namespace {
struct Scanner {
    const uint8_t* ram;
    const Layout&  L;
    std::vector<int8_t> track_cache;    // 0 unknown, 1 ok, -1 bad
    std::vector<int8_t> pattern_cache;

    Scanner(const uint8_t* r, const Layout& l) : ram(r), L(l), track_cache(0x10000, 0), pattern_cache(0x10000, 0) {}

    bool track_ok(uint16_t a) {
        if (a < 0x400) return false;
        if (track_cache[a]) return track_cache[a] > 0;
        Track t = parse_track(ram, L, a);
        bool ok = !t.truncated && !t.events.empty() && t.events.back().type == EventType::End &&
                  t.events.size() > 1;
        track_cache[a] = ok ? 1 : -1;
        return ok;
    }

    bool pattern_ok(uint16_t a) {
        if (a < 0x400 || a > 0xFFF0) return false;
        if (pattern_cache[a]) return pattern_cache[a] > 0;
        int used = 0;
        bool ok = true;
        for (int v = 0; v < 8 && ok; ++v) {
            uint16_t t = rd16(ram, a + v * 2);
            if (t == 0) continue;
            ++used;
            if (!track_ok(t)) ok = false;
        }
        ok = ok && used >= 1;
        pattern_cache[a] = ok ? 1 : -1;
        return ok;
    }
};

}

std::vector<Song> find_songs(const uint8_t* ram, const Layout& L, const uint8_t* dsp) {
    std::vector<Song> songs;
    if (!L.valid()) return songs;
    Scanner S(ram, L);

    std::vector<bool> consumed(0x10000, false);
    auto skip = [&](int lo, int hi) { for (int k = std::max(0, lo); k < std::min(0x10000, hi); ++k) consumed[k] = true; };
    if (dsp) {
        int dir = dsp[0x5D] << 8;
        if (dir >= 0x200) {
            int n = 0;
            for (; n < 256 && dir + n * 4 + 4 <= 0x10000; ++n) {
                int start = rd16(ram, dir + n * 4), loop = rd16(ram, dir + n * 4 + 2);
                if (start == 0xFFFF || (start == 0 && loop == 0)) break;
                if (start < 0x200 || loop < start) continue;
                int e = start;
                while (e + 9 <= 0x10000 && !(ram[e] & 1)) e += 9;
                skip(start, std::min(0x10000, e + 9));
            }
            skip(dir, dir + n * 4);
        }
        int esa = dsp[0x6D] << 8, edl = dsp[0x7D] & 0x0F;
        if (edl) skip(esa, esa + edl * 2048);
    }

    constexpr int kDataStart = 0x400;
    for (int a = kDataStart; a < 0xFFF0; ++a) {
        if (consumed[a]) continue;
        if (!S.pattern_ok(rd16(ram, a))) continue;

        int p = a;
        std::vector<Order> orders;
        while (p < 0xFFF0 && S.pattern_ok(rd16(ram, p))) {
            orders.push_back({uint16_t(p), rd16(ram, p)});
            p += 2;
        }
        uint16_t term = rd16(ram, p);
        int loop_count = 0, loop_to = -1, end = p + 2;
        if (term == 0) {
        } else if (term < 0x100) {
            uint16_t target = rd16(ram, p + 2);
            if (target < a || target >= p || ((target - a) & 1)) continue;
            loop_count = term;
            loop_to = (target - a) / 2;
            end = p + 4;
        } else {
            continue;
        }
        Song s;
        s.order_addr = uint16_t(a);
        s.order_end  = uint16_t(end);
        s.orders     = std::move(orders);
        s.loop_count = loop_count;
        s.loop_to    = loop_to;
        for (const Order& o : s.orders)
            if (s.pattern_index(o.pattern_addr) < 0) s.patterns.push_back(parse_pattern(ram, L, o.pattern_addr));
        if (s.total_ticks() == 0) continue;
        songs.push_back(std::move(s));
        for (int k = a; k < end; ++k) consumed[k] = true;
    }
    return songs;
}

namespace {
struct Range { uint16_t lo, hi; };

void track_ranges(const Track& t, std::vector<Range>& out) {
    if (!t.addr || t.events.empty()) return;
    uint16_t lo = t.events.front().addr, hi = lo;
    for (const Event& e : t.events) {
        if (e.in_sub) continue;
        hi = std::max<uint16_t>(hi, uint16_t(e.addr + e.size));
    }
    out.push_back({lo, hi});
    for (size_t i = 0; i < t.events.size(); ++i) {
        if (!t.events[i].in_sub || t.events[i].sub_iter != 0) continue;
        uint16_t slo = t.events[i].addr, shi = slo;
        while (i < t.events.size() && t.events[i].in_sub && t.events[i].sub_iter == 0) {
            shi = std::max<uint16_t>(shi, uint16_t(t.events[i].addr + t.events[i].size));
            ++i;
        }
        out.push_back({slo, uint16_t(shi + 1)});
    }
}

bool in_ranges(const std::vector<Range>& rs, uint16_t p) {
    for (const Range& r : rs) if (p >= r.lo && p <= r.hi) return true;
    return false;
}

bool find_track_pointers(const uint8_t* ram, const Song& song, uint16_t& base_out, int& pattern_out, int& score_out) {
    int best_score = 0; uint16_t best_base = 0; int best_pat = -1;
    for (size_t pi = 0; pi < song.patterns.size(); ++pi) {
        const Pattern& pat = song.patterns[pi];
        std::vector<Range> ranges[8];
        int used = 0;
        for (int v = 0; v < 8; ++v) { track_ranges(pat.tracks[v], ranges[v]); if (pat.tracks[v].addr) ++used; }
        if (!used) continue;
        const int need = std::max(2, (used * 3 + 4) / 5);
        for (int base = 0; base + 16 <= 0x400; ++base) {
            int score = 0;
            for (int v = 0; v < 8; ++v) {
                uint16_t p = rd16(ram, base + v * 2);
                if (p && in_ranges(ranges[v], p)) ++score;
            }
            if (score < need) continue;
            int weighted = score * 4 + (base == 0x30 ? 3 : 0) + (base < 0x100 ? 1 : 0);
            if (weighted > best_score) { best_score = weighted; best_base = uint16_t(base); best_pat = int(pi); score_out = score; }
        }
    }
    if (best_pat < 0) return false;
    base_out = best_base; pattern_out = best_pat;
    return true;
}

int find_order_pointer(const uint8_t* ram, const Song& song, uint16_t& addr_out) {
    int found = -1;
    for (int a = 0; a + 2 <= 0x400; ++a) {
        uint16_t w = rd16(ram, a);
        if (w <= song.order_addr || w > song.order_end) continue;
        if ((w - song.order_addr) & 1) continue;
        int idx = (w - song.order_addr) / 2 - 1;
        if (idx >= int(song.orders.size())) idx = int(song.orders.size()) - 1;
        if (found < 0 || a == 0x40) { addr_out = uint16_t(a); found = idx; }
        if (a == 0x40) break;
    }
    return found;
}

}

int pick_current_song(const uint8_t* ram, const Layout& L, const std::vector<Song>& songs) {
    (void)L;
    if (songs.empty()) return -1;
    int best = -1, best_score = 0;
    for (size_t i = 0; i < songs.size(); ++i) {
        uint16_t base; int pat, score = 0;
        int s = 0;
        if (find_track_pointers(ram, songs[i], base, pat, score)) s += score * 2 + (base == 0x30 ? 3 : 0);
        uint16_t oa;
        if (find_order_pointer(ram, songs[i], oa) >= 0) s += oa == 0x40 ? 15 : 8;
        if (s > best_score) { best_score = s; best = int(i); }
    }
    if (best < 0)
        for (size_t i = 0; i < songs.size(); ++i)
            if (songs[i].orders.size() >= 2) return int(i);
    return best >= 0 ? best : 0;
}

Position locate(const uint8_t* ram, const Layout& L, const Song& song) {
    (void)L;
    Position pos;
    for (int v = 0; v < 8; ++v) { pos.voice_event[v] = -1; pos.voice_tick[v] = -1; pos.voice_ptr[v] = 0; }
    if (song.patterns.empty()) return pos;

    uint16_t base; int pat_idx, score;
    bool have_ptrs = find_track_pointers(ram, song, base, pat_idx, score);
    uint16_t oaddr;
    int order_idx = find_order_pointer(ram, song, oaddr);
    if (order_idx >= 0) {
        pos.order_ptr_addr = oaddr;
        int pi = song.pattern_index(song.orders[order_idx].pattern_addr);
        if (!have_ptrs || pi == pat_idx || score < 2) pat_idx = pi;
        else {
            int bestd = 1 << 30;
            for (size_t i = 0; i < song.orders.size(); ++i)
                if (song.pattern_index(song.orders[i].pattern_addr) == pat_idx && std::abs(int(i) - order_idx) < bestd) {
                    bestd = std::abs(int(i) - order_idx); order_idx = int(i);
                }
        }
    } else if (have_ptrs) {
        for (size_t i = 0; i < song.orders.size(); ++i)
            if (song.pattern_index(song.orders[i].pattern_addr) == pat_idx) { order_idx = int(i); break; }
    } else {
        return pos;
    }
    pos.valid = true;
    pos.order_index = order_idx;
    if (!have_ptrs || pat_idx < 0) return pos;
    pos.track_ptr_base = base;

    const Pattern& pat = song.patterns[pat_idx];
    for (int v = 0; v < 8; ++v) {
        uint16_t p = rd16(ram, base + v * 2);
        pos.voice_ptr[v] = p;
        const Track& t = pat.tracks[v];
        if (!t.addr || !p) continue;
        int best = -1;
        for (size_t i = 0; i < t.events.size(); ++i) {
            const Event& e = t.events[i];
            if (e.in_sub && e.sub_iter != 0) continue;
            uint16_t end = uint16_t(e.addr + e.size);
            if (end <= p && (best < 0 || end >= uint16_t(t.events[best].addr + t.events[best].size))) best = int(i);
        }
        if (best >= 0) { pos.voice_event[v] = best; pos.voice_tick[v] = t.events[best].tick; }
    }
    return pos;
}

int instrument_count(const uint8_t* ram, const Layout& L) {
    if (!L.inst_table || !L.inst_stride) return 0;
    int max = 64;
    if (L.perc_table > L.inst_table) max = std::min(max, (L.perc_table - L.inst_table) / L.inst_stride);
    int n = 0;
    for (; n < max; ++n) {
        const uint8_t* e = ram + ((L.inst_table + n * L.inst_stride) & 0xFFFF);
        if (e[0] > 0x7F) break;
        if (e[0] == 0 && e[1] == 0 && e[2] == 0 && e[3] == 0 && n > 0) break;
    }
    return n;
}

Instrument read_instrument(const uint8_t* ram, const Layout& L, int index) {
    Instrument in{};
    if (!L.inst_table) return in;
    const uint8_t* e = ram + ((L.inst_table + index * L.inst_stride) & 0xFFFF);
    in.srcn = e[0]; in.adsr0 = e[1]; in.adsr1 = e[2]; in.gain = e[3]; in.pitch_hi = e[4];
    in.pitch_lo = L.inst_stride >= 6 ? e[5] : 0;
    return in;
}

void write_instrument(uint8_t* ram, const Layout& L, int index, const Instrument& in) {
    if (!L.inst_table) return;
    uint8_t* e = ram + ((L.inst_table + index * L.inst_stride) & 0xFFFF);
    e[0] = in.srcn; e[1] = in.adsr0; e[2] = in.adsr1; e[3] = in.gain; e[4] = in.pitch_hi;
    if (L.inst_stride >= 6) e[5] = in.pitch_lo;
}

std::string note_name(int semitone) {
    static const char* names[] = {"C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"};
    if (semitone < 0) return "???";
    int oct = semitone / 12 + 1;
    return std::string(names[semitone % 12]) + char('0' + std::min(oct, 9));
}

int note_semitone(const Layout& L, uint8_t note_byte) {
    (void)L;
    return note_byte >= 0x80 ? note_byte - 0x80 : -1;
}

std::vector<uint8_t> serialize_track(const std::vector<Event>& events) {
    std::vector<uint8_t> out;
    for (const Event& e : events) {
        if (e.in_sub) continue;
        for (int i = 0; i < e.size; ++i) out.push_back(e.b[i]);
    }
    if (out.empty() || out.back() != 0) out.push_back(0);
    return out;
}

}
