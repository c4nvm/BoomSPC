#include "akao.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using seq::Event;
using seq::EventType;
using seq::FxClass;

namespace akao {
namespace {
inline uint16_t rd16(const uint8_t* ram, uint32_t a) { return uint16_t(ram[a & 0xFFFF] | (ram[(a + 1) & 0xFFFF] << 8)); }

int find_pattern(const uint8_t* ram, int lo, int hi, const int* pat, int n) {
    for (int a = lo; a + n <= hi; ++a) {
        bool ok = true;
        for (int i = 0; i < n && ok; ++i) ok = pat[i] == 0x100 || ram[a + i] == pat[i];
        if (ok) return a;
    }
    return -1;
}

void set(Cmd& c, Kind k, const char* code, const char* name, FxClass cls) { c.kind = k; c.code = code; c.name = name; c.cls = cls; }

void fill_smrpg(Layout& L) {
    struct D { uint8_t op; Kind k; const char* code; const char* name; FxClass cls; };
    static const D d[] = {
        {0xC4, Kind::OctaveUp, "Oc+", "Octave up", FxClass::Pitch},
        {0xC5, Kind::OctaveDown, "Oc-", "Octave down", FxClass::Pitch},
        {0xC6, Kind::OctaveSet, "Oct", "Octave", FxClass::Pitch},
        {0xC7, Kind::Nop, "Nop", "No operation", FxClass::Misc},
        {0xC8, Kind::Effect, "Noi", "Noise clock", FxClass::Sys1},
        {0xC9, Kind::Effect, "NzO", "Noise on", FxClass::Sys1},
        {0xCA, Kind::Effect, "NzF", "Noise off", FxClass::Sys1},
        {0xCB, Kind::Effect, "PMO", "Pitch modulation on", FxClass::Sys1},
        {0xCC, Kind::Effect, "PMF", "Pitch modulation off", FxClass::Sys1},
        {0xCD, Kind::SubseqJump, "JsA", "Jump to sequence entry (first pointer)", FxClass::Song},
        {0xCE, Kind::SubseqJump, "JsB", "Jump to sequence entry (second pointer)", FxClass::Song},
        {0xCF, Kind::Effect, "Tun", "Fine tune", FxClass::Pitch},
        {0xD0, Kind::LoopToMark, "Lp!", "Loop to mark (or end of track)", FxClass::Song},
        {0xD1, Kind::Tempo, "Tmp", "Tempo (timer period)", FxClass::Speed},
        {0xD2, Kind::TransposeAbs, "Trn", "Transpose (global)", FxClass::Pitch},
        {0xD3, Kind::TransposeRel, "Tr+", "Transpose relative (global)", FxClass::Pitch},
        {0xD4, Kind::LoopStart, "Rep", "Repeat start (count)", FxClass::Song},
        {0xD5, Kind::LoopEnd, "RpE", "Repeat end", FxClass::Song},
        {0xD6, Kind::BreakLast, "Brk", "Leave repeat on its last pass", FxClass::Song},
        {0xD7, Kind::Mark, "Mrk", "Set loop mark", FxClass::Song},
        {0xD8, Kind::Effect, "EnR", "Reload envelope from instrument", FxClass::Volume},
        {0xD9, Kind::Effect, "Atk", "ADSR attack", FxClass::Volume},
        {0xDA, Kind::Effect, "Dec", "ADSR decay", FxClass::Volume},
        {0xDB, Kind::Effect, "SuR", "ADSR sustain rate", FxClass::Volume},
        {0xDC, Kind::Effect, "SuL", "ADSR sustain level", FxClass::Volume},
        {0xDD, Kind::Effect, "Gat", "Gate time", FxClass::Volume},
        {0xDE, Kind::Instrument, "Ins", "Instrument", FxClass::Instrument},
        {0xDF, Kind::Effect, "No+", "Noise clock relative", FxClass::Sys1},
        {0xE0, Kind::Effect, "Rel", "Release rate", FxClass::Volume},
        {0xE1, Kind::OctaveUp, "Oc+", "Octave up (duplicate)", FxClass::Pitch},
        {0xE2, Kind::Effect, "Vol", "Volume", FxClass::Volume},
        {0xE3, Kind::Effect, "Vo+", "Volume relative", FxClass::Volume},
        {0xE4, Kind::Effect, "VoF", "Volume fade (length, target)", FxClass::Volume},
        {0xE5, Kind::Effect, "VLF", "Volume LFO", FxClass::Volume},
        {0xE6, Kind::Effect, "Tg5", "Toggle voice flag", FxClass::Misc},
        {0xE7, Kind::Effect, "Pan", "Pan", FxClass::Panning},
        {0xE8, Kind::Effect, "PnF", "Pan fade (length, target)", FxClass::Panning},
        {0xE9, Kind::Effect, "PLF", "Pan LFO (rate, depth)", FxClass::Panning},
        {0xEA, Kind::OctaveUp, "Oc+", "Octave up (duplicate)", FxClass::Pitch},
        {0xEB, Kind::OctaveUp, "Oc+", "Octave up (duplicate)", FxClass::Pitch},
        {0xEC, Kind::Effect, "Bnd", "Pitch bend", FxClass::Pitch},
        {0xED, Kind::Effect, "Bn+", "Pitch bend relative", FxClass::Pitch},
        {0xEE, Kind::Effect, "SlO", "Slur on", FxClass::Misc},
        {0xEF, Kind::Effect, "SlF", "Slur off", FxClass::Misc},
        {0xF0, Kind::Effect, "PSl", "Pitch slide (length, semitones)", FxClass::Pitch},
        {0xF1, Kind::Effect, "PSd", "Pitch slide with delay", FxClass::Pitch},
        {0xF2, Kind::Effect, "Tm+", "Tempo relative", FxClass::Speed},
        {0xF3, Kind::Effect, "VbF", "Vibrato off", FxClass::Pitch},
        {0xF4, Kind::Effect, "Vib", "Vibrato (rate, depth)", FxClass::Pitch},
        {0xF5, Kind::Effect, "VbD", "Vibrato with delay", FxClass::Pitch},
        {0xF6, Kind::Effect, "Por", "Portamento time", FxClass::Pitch},
        {0xF7, Kind::Effect, "VbO", "Vibrato off (restore)", FxClass::Pitch},
        {0xF8, Kind::Effect, "Leg", "Legato on", FxClass::Misc},
        {0xF9, Kind::Effect, "LgF", "Legato off", FxClass::Misc},
        {0xFA, Kind::Effect, "EcO", "Echo on", FxClass::Sys1},
        {0xFB, Kind::Effect, "EcF", "Echo off", FxClass::Sys1},
        {0xFC, Kind::Effect, "Ech", "Echo volume L, R, feedback", FxClass::Sys1},
        {0xFD, Kind::OctaveUp, "Oc+", "Octave up (duplicate)", FxClass::Pitch},
        {0xFE, Kind::LoopEndCpu, "RpC", "Repeat end (CPU count)", FxClass::Song},
        {0xFF, Kind::OctaveUp, "Oc+", "Octave up (duplicate)", FxClass::Pitch},
    };
    for (const D& e : d) set(L.cmds[e.op - L.first_cmd], e.k, e.code, e.name, e.cls);
}

void fill_rev34_common(Layout& L) {
    struct D { int ofs; Kind k; const char* code; const char* name; FxClass cls; };
    static const D d[] = {
        {0x00, Kind::Effect, "Vol", "Volume", FxClass::Volume},
        {0x01, Kind::Effect, "VoF", "Volume fade", FxClass::Volume},
        {0x02, Kind::Effect, "Pan", "Pan", FxClass::Panning},
        {0x03, Kind::Effect, "PnF", "Pan fade", FxClass::Panning},
        {0x04, Kind::Effect, "PSl", "Pitch slide", FxClass::Pitch},
        {0x05, Kind::Effect, "Vib", "Vibrato on", FxClass::Pitch},
        {0x06, Kind::Effect, "VbF", "Vibrato off", FxClass::Pitch},
        {0x07, Kind::Effect, "Trm", "Tremolo on", FxClass::Volume},
        {0x08, Kind::Effect, "TrF", "Tremolo off", FxClass::Volume},
        {0x09, Kind::Effect, "PLF", "Pan LFO on", FxClass::Panning},
        {0x0A, Kind::Effect, "PLo", "Pan LFO off", FxClass::Panning},
        {0x0B, Kind::Effect, "Noi", "Noise clock", FxClass::Sys1},
        {0x0C, Kind::Effect, "NzO", "Noise on", FxClass::Sys1},
        {0x0D, Kind::Effect, "NzF", "Noise off", FxClass::Sys1},
        {0x0E, Kind::Effect, "PMO", "Pitch modulation on", FxClass::Sys1},
        {0x0F, Kind::Effect, "PMF", "Pitch modulation off", FxClass::Sys1},
        {0x10, Kind::Effect, "EcO", "Echo on", FxClass::Sys1},
        {0x11, Kind::Effect, "EcF", "Echo off", FxClass::Sys1},
        {0x12, Kind::OctaveSet, "Oct", "Octave", FxClass::Pitch},
        {0x13, Kind::OctaveUp, "Oc+", "Octave up", FxClass::Pitch},
        {0x14, Kind::OctaveDown, "Oc-", "Octave down", FxClass::Pitch},
        {0x15, Kind::TransposeAbs, "Trn", "Transpose", FxClass::Pitch},
        {0x16, Kind::TransposeRel, "Tr+", "Transpose relative", FxClass::Pitch},
        {0x17, Kind::Effect, "Tun", "Detune", FxClass::Pitch},
        {0x18, Kind::Instrument, "Ins", "Instrument", FxClass::Instrument},
        {0x19, Kind::Effect, "Atk", "ADSR attack", FxClass::Volume},
        {0x1A, Kind::Effect, "Dec", "ADSR decay", FxClass::Volume},
        {0x1B, Kind::Effect, "SuL", "ADSR sustain level", FxClass::Volume},
        {0x1C, Kind::Effect, "SuR", "ADSR sustain rate", FxClass::Volume},
        {0x1D, Kind::Effect, "EnR", "Default ADSR", FxClass::Volume},
        {0x1E, Kind::LoopStart, "Rep", "Repeat start (count)", FxClass::Song},
        {0x1F, Kind::LoopEnd, "RpE", "Repeat end", FxClass::Song},
    };
    for (const D& e : d) set(L.cmds[e.ofs], e.k, e.code, e.name, e.cls);
}

void fill_rev3(Layout& L) {
    fill_rev34_common(L);
    struct D { int ofs; Kind k; const char* code; const char* name; FxClass cls; };
    static const D d[] = {
        {0x20, Kind::End, "End", "End of track", FxClass::Song},
        {0x21, Kind::Tempo, "Tmp", "Tempo", FxClass::Speed},
        {0x22, Kind::Effect, "TmF", "Tempo fade", FxClass::Speed},
        {0x23, Kind::Effect, "EVl", "Echo volume", FxClass::Sys1},
        {0x24, Kind::Effect, "EVF", "Echo volume fade", FxClass::Sys1},
        {0x25, Kind::Effect, "EFB", "Echo feedback / FIR", FxClass::Sys1},
        {0x26, Kind::Effect, "MVl", "Master volume", FxClass::Volume},
        {0x27, Kind::CondJump, "CJp", "Conditional jump (pass, address)", FxClass::Song},
        {0x28, Kind::Jump, "Jmp", "Jump", FxClass::Song},
        {0x29, Kind::Effect, "CJc", "CPU-controlled jump", FxClass::Song},
    };
    for (const D& e : d) set(L.cmds[e.ofs], e.k, e.code, e.name, e.cls);
}

void fill_rev4(Layout& L, const uint8_t* ram) {
    fill_rev34_common(L);
    struct D { int ofs; Kind k; const char* code; const char* name; FxClass cls; };
    static const D d[] = {
        {0x20, Kind::Effect, "SlO", "Slur on", FxClass::Misc},
        {0x21, Kind::Effect, "SlF", "Slur off", FxClass::Misc},
        {0x22, Kind::Effect, "Leg", "Legato on", FxClass::Misc},
        {0x23, Kind::Effect, "LgF", "Legato off", FxClass::Misc},
        {0x24, Kind::ForceLength, "Len", "Force next note length", FxClass::Time},
        {0x25, Kind::Effect, "SF1", "Play sound effect 1", FxClass::Sys2},
        {0x26, Kind::Effect, "SF2", "Play sound effect 2", FxClass::Sys2},
        {0x2C, Kind::Tempo, "Tmp", "Tempo", FxClass::Speed},
        {0x2D, Kind::Effect, "TmF", "Tempo fade", FxClass::Speed},
        {0x2E, Kind::Effect, "EVl", "Echo volume", FxClass::Sys1},
        {0x2F, Kind::Effect, "EVF", "Echo volume fade", FxClass::Sys1},
    };
    for (const D& e : d) set(L.cmds[e.ofs], e.k, e.code, e.name, e.cls);
    auto entry = [&](int i) { return rd16(ram, L.cmd_table + i * 2); };
    if (entry(0x27) == entry(0x28)) set(L.cmds[0x27], Kind::End, "End", "End of track", FxClass::Song);
    else if (entry(0x28) == entry(0x29)) set(L.cmds[0x28], Kind::End, "End", "End of track", FxClass::Song);
    for (int i = 0x27; i < 0x100 - L.first_cmd; ++i) {
        if (L.cmds[i].kind != Kind::Effect) continue;
        uint16_t h = entry(i);
        int argc = L.cmds[i].argc;
        if (argc == 2) {
            const int jp[] = {0xFD, 0x3F, 0x100, 0x100};
            if (find_pattern(ram, h, h + 4, jp, 4) == h) {
                bool addw = false, ret = false;
                for (int k = 4; k < 20; ++k) { if (ram[h + k] == 0x7A) addw = true; if (ram[h + k] == 0x6F) { ret = true; break; } }
                if (addw && ret) {
                    set(L.cmds[i], Kind::Jump, "Jmp", "Jump", FxClass::Song);
                    for (int k = 4; k < 20; ++k) if (ram[h + k] == 0xD4 || ram[h + k] == 0xDB) { if (!L.track_ptr_base || ram[h + k + 1] < L.track_ptr_base) L.track_ptr_base = ram[h + k + 1]; }
                }
            }
        } else if (argc == 3) {
            const int cj[] = {0xC4, 0x100, 0x3F, 0x100, 0x100, 0xC4, 0x100, 0x3F, 0x100, 0x100, 0xC4, 0x100};
            if (find_pattern(ram, h, h + 12, cj, 12) == h) {
                for (int k = 12; k < 40; ++k) if (ram[h + k] == 0x2E) { set(L.cmds[i], Kind::CondJump, "CJp", "Conditional jump (pass, address)", FxClass::Song); break; }
            }
        }
        if (L.cmds[i].kind == Kind::Effect && L.cmds[i].name[0] == '?') {
            static char names[64][24], codes[64][8];
            std::snprintf(names[i], sizeof names[i], "Command %02X", L.first_cmd + i);
            std::snprintf(codes[i], sizeof codes[i], "%02X ", L.first_cmd + i);
            L.cmds[i].name = names[i]; L.cmds[i].code = codes[i];
        }
    }
}

void fill_rev1(Layout& L) {
    struct D { uint8_t op; Kind k; const char* code; const char* name; FxClass cls; };
    static const D d[] = {
        {0xD2, Kind::Tempo, "Tmp", "Tempo (fade)", FxClass::Speed}, {0xD3, Kind::Nop, "Nop", "No operation", FxClass::Misc},
        {0xD4, Kind::Effect, "EVl", "Echo volume", FxClass::Sys1}, {0xD5, Kind::Effect, "EFB", "Echo feedback / FIR", FxClass::Sys1},
        {0xD6, Kind::Effect, "PEn", "Pitch envelope on", FxClass::Pitch}, {0xD7, Kind::Effect, "Trm", "Tremolo on", FxClass::Volume},
        {0xD8, Kind::Effect, "Vib", "Vibrato on", FxClass::Pitch}, {0xD9, Kind::Effect, "PLF", "Pan LFO on", FxClass::Panning},
        {0xDA, Kind::OctaveSet, "Oct", "Octave", FxClass::Pitch}, {0xDB, Kind::Instrument, "Ins", "Instrument", FxClass::Instrument},
        {0xDC, Kind::Effect, "Env", "Envelope pattern", FxClass::Volume}, {0xDD, Kind::Effect, "Rel", "GAIN release rate", FxClass::Volume},
        {0xDE, Kind::Effect, "SuR", "GAIN sustain rate", FxClass::Volume}, {0xDF, Kind::Effect, "Noi", "Noise clock", FxClass::Sys1},
        {0xE0, Kind::LoopStart, "Rep", "Repeat start (count)", FxClass::Song}, {0xE1, Kind::OctaveUp, "Oc+", "Octave up", FxClass::Pitch},
        {0xE2, Kind::OctaveDown, "Oc-", "Octave down", FxClass::Pitch}, {0xE3, Kind::Nop, "Nop", "No operation", FxClass::Misc},
        {0xE4, Kind::Nop, "Nop", "No operation", FxClass::Misc}, {0xE5, Kind::Nop, "Nop", "No operation", FxClass::Misc},
        {0xE6, Kind::Effect, "PEf", "Pitch envelope off", FxClass::Pitch}, {0xE7, Kind::Effect, "TrF", "Tremolo off", FxClass::Volume},
        {0xE8, Kind::Effect, "VbF", "Vibrato off", FxClass::Pitch}, {0xE9, Kind::Effect, "PLo", "Pan LFO off", FxClass::Panning},
        {0xEA, Kind::Effect, "EcO", "Echo on", FxClass::Sys1}, {0xEB, Kind::Effect, "EcF", "Echo off", FxClass::Sys1},
        {0xEC, Kind::Effect, "NzO", "Noise on", FxClass::Sys1}, {0xED, Kind::Effect, "NzF", "Noise off", FxClass::Sys1},
        {0xEE, Kind::Effect, "PMO", "Pitch modulation on", FxClass::Sys1}, {0xEF, Kind::Effect, "PMF", "Pitch modulation off", FxClass::Sys1},
        {0xF0, Kind::LoopEnd, "RpE", "Repeat end", FxClass::Song}, {0xF1, Kind::End, "End", "End of track", FxClass::Song},
        {0xF2, Kind::Effect, "VoF", "Volume fade", FxClass::Volume}, {0xF3, Kind::Effect, "PnF", "Pan fade", FxClass::Panning},
        {0xF4, Kind::Jump, "Jmp", "Jump", FxClass::Song}, {0xF5, Kind::CondJump, "CJp", "Conditional jump", FxClass::Song},
        {0xF6, Kind::Effect, "CJc", "CPU-controlled jump", FxClass::Song},
    };
    for (const D& e : d) set(L.cmds[e.op - L.first_cmd], e.k, e.code, e.name, e.cls);
    for (int op = 0xF7; op <= 0xFF; ++op) set(L.cmds[op - L.first_cmd], Kind::End, "End", "End of track (duplicate)", FxClass::Song);
}

void fill_rev2(Layout& L) {
    struct D { uint8_t op; Kind k; const char* code; const char* name; FxClass cls; };
    static const D d[] = {
        {0xD2, Kind::Tempo, "Tmp", "Tempo", FxClass::Speed}, {0xD3, Kind::Effect, "TmF", "Tempo fade", FxClass::Speed},
        {0xD4, Kind::Effect, "Vol", "Volume", FxClass::Volume}, {0xD5, Kind::Effect, "VoF", "Volume fade", FxClass::Volume},
        {0xD6, Kind::Effect, "Pan", "Pan", FxClass::Panning}, {0xD7, Kind::Effect, "PnF", "Pan fade", FxClass::Panning},
        {0xD8, Kind::Effect, "EVl", "Echo volume", FxClass::Sys1}, {0xD9, Kind::Effect, "EVF", "Echo volume fade", FxClass::Sys1},
        {0xDA, Kind::TransposeAbs, "Trn", "Transpose", FxClass::Pitch}, {0xDB, Kind::Effect, "PEn", "Pitch envelope on", FxClass::Pitch},
        {0xDC, Kind::Effect, "PEf", "Pitch envelope off", FxClass::Pitch}, {0xDD, Kind::Effect, "Trm", "Tremolo on", FxClass::Volume},
        {0xDE, Kind::Effect, "TrF", "Tremolo off", FxClass::Volume}, {0xDF, Kind::Effect, "Vib", "Vibrato on", FxClass::Pitch},
        {0xE0, Kind::Effect, "VbF", "Vibrato off", FxClass::Pitch}, {0xE1, Kind::Effect, "Noi", "Noise clock", FxClass::Sys1},
        {0xE2, Kind::Effect, "NzO", "Noise on", FxClass::Sys1}, {0xE3, Kind::Effect, "NzF", "Noise off", FxClass::Sys1},
        {0xE4, Kind::Effect, "PMO", "Pitch modulation on", FxClass::Sys1}, {0xE5, Kind::Effect, "PMF", "Pitch modulation off", FxClass::Sys1},
        {0xE6, Kind::Effect, "EFB", "Echo feedback / FIR", FxClass::Sys1}, {0xE7, Kind::Effect, "EcO", "Echo on", FxClass::Sys1},
        {0xE8, Kind::Effect, "EcF", "Echo off", FxClass::Sys1}, {0xE9, Kind::Effect, "PLF", "Pan LFO on", FxClass::Panning},
        {0xEA, Kind::Effect, "PLo", "Pan LFO off", FxClass::Panning}, {0xEB, Kind::OctaveSet, "Oct", "Octave", FxClass::Pitch},
        {0xEC, Kind::OctaveUp, "Oc+", "Octave up", FxClass::Pitch}, {0xED, Kind::OctaveDown, "Oc-", "Octave down", FxClass::Pitch},
        {0xEE, Kind::LoopStart, "Rep", "Repeat start (count)", FxClass::Song}, {0xEF, Kind::LoopEnd, "RpE", "Repeat end", FxClass::Song},
        {0xF0, Kind::CondJump, "CJp", "Conditional jump", FxClass::Song}, {0xF1, Kind::Jump, "Jmp", "Jump", FxClass::Song},
        {0xF2, Kind::Effect, "SlO", "Slur on", FxClass::Misc}, {0xF3, Kind::Instrument, "Ins", "Instrument", FxClass::Instrument},
        {0xF4, Kind::Effect, "Env", "Envelope pattern", FxClass::Volume}, {0xF5, Kind::Effect, "SlF", "Slur off", FxClass::Misc},
        {0xF6, Kind::Effect, "CJc", "CPU-controlled jump", FxClass::Song}, {0xF7, Kind::Effect, "Tun", "Detune", FxClass::Pitch},
        {0xF8, Kind::End, "End", "End of track", FxClass::Song},
    };
    for (const D& e : d) set(L.cmds[e.op - L.first_cmd], e.k, e.code, e.name, e.cls);
    for (int op = 0xF9; op <= 0xFF; ++op) set(L.cmds[op - L.first_cmd], Kind::End, "End", "End of track (duplicate)", FxClass::Song);
}

}

Layout detect_layout(const uint8_t* ram) {
    Layout L;
    const int W = 0x100;

    {
        const int vl[] = {0xE7, W, 0x3A, W, 0x68, W, 0x90};
        int p = find_pattern(ram, 0x200, 0x2000, vl, 7);
        const int disp[] = {0x80, 0xA8, W, 0x2D, 0x5D, 0xF5, W, W, 0x28, 0x07};
        const int disp3[] = {0x80, 0xA8, W, 0x1C, 0x5D, 0x60, 0xE8, 0x00, 0x1F, W, W};
        int q = p >= 0 ? find_pattern(ram, 0x200, 0x2000, disp, 10) : -1;
        int q3 = p >= 0 && q < 0 ? find_pattern(ram, 0x200, 0x2000, disp3, 11) : -1;
        if (p >= 0 && (q >= 0 || q3 >= 0) && ram[p + 1] == ram[p + 3] && ram[p + 5] == ram[(q >= 0 ? q : q3) + 2]) {
            const uint8_t zp = ram[p + 1];
            const bool sd3 = q < 0;
            L.variant = Variant::SMRPG;
            L.tested = true;
            L.first_cmd = ram[p + 5];
            L.lengths = L.first_cmd / 14;
            L.key_is_remainder = true;
            if (!sd3) {
                L.len_table = rd16(ram, q + 6);
                L.len_is_total = true;
                L.header_prefix = true;
                const int jt[] = {0xEB, 0x1E, 0x1F, W, W};
                if (int r = find_pattern(ram, q, q + 64, jt, 5); r >= 0) L.cmd_table = rd16(ram, r + 3);
                const int nl[] = {0xEB, 0x06, 0xF6, W, W, 0xFD, 0xDB, W};
                if (int r = find_pattern(ram, 0x200, 0x2000, nl, 8); r >= 0) L.note_len_table = rd16(ram, r + 3);
                const int hd[] = {0x8D, 0x00, 0xF6, W, W, 0x30};
                if (int r = find_pattern(ram, 0x200, 0x2000, hd, 6); r >= 0) L.header = rd16(ram, r + 3);
            } else {
                L.cmd_table = rd16(ram, q3 + 9);
                const int lt[] = {0x80, 0xA8, L.first_cmd, 0x5D, 0xF5, W, W, 0x30};
                if (int r = find_pattern(ram, 0x200, 0x2000, lt, 8); r >= 0) L.len_table = rd16(ram, r + 5);
                const int nl[] = {0xEB, 0x06, 0xF6, W, W, 0xC4, 0x06, 0xD4};
                if (int r = find_pattern(ram, 0x200, 0x2000, nl, 8); r >= 0) L.note_len_table = rd16(ram, r + 3);
                const int hd[] = {0xCD, 0x00, 0xE4, W, 0x1C, 0xFD, 0xF5, W, W, 0xD6, W, W, 0xF5, W, W, 0xD6};
                if (int r = find_pattern(ram, 0x200, 0x2000, hd, 16); r >= 0 && rd16(ram, r + 7) + 1 == rd16(ram, r + 13)) L.header = rd16(ram, r + 7);
                L.len_plus = 1;
            }
            L.note_len_count = 13;
            const int tp[] = {0xE4, zp, 0xD5, W, W, 0xE4, zp + 1, 0xD5};
            if (int r = find_pattern(ram, 0x200, 0x2000, tp, 8); r >= 0) L.track_ptr_base = rd16(ram, r + 3);
            const int bs[] = {0xE4, W, 0x1C, 0x5D, 0xF6, W, W, 0xD5, W, W};
            if (int r = find_pattern(ram, 0x200, 0x2000, bs, 10); r >= 0 && rd16(ram, r + 8) == L.track_ptr_base) L.bgm_slot_var = ram[r + 1];
            const int oc[] = {0xF6, W, W, 0xBC, 0x2F, W, 0xF6, W, W, 0x9C};
            if (int r = find_pattern(ram, 0x200, 0x2000, oc, 10); r >= 0) L.octave_base = rd16(ram, r + 1);
            if (L.octave_base) {
                const int oi[] = {0xE8, W, 0xD6, L.octave_base & 0xFF, L.octave_base >> 8};
                if (int r = find_pattern(ram, 0x200, 0x2000, oi, 5); r >= 0) L.octave_init = ram[r + 1];
            }
            const int ins[] = {0x5D, 0xF5, W, W, 0x1C, 0x5D, 0xF5, W, W, 0xD6, W, W};
            if (int r = find_pattern(ram, 0x200, 0x2000, ins, 12); r >= 0) {
                L.inst_map = rd16(ram, r + 2); L.sample_pairs = uint16_t(rd16(ram, r + 7) - 1);
                const int adsr[] = {0xF5, W, W, 0xD6, W, W, 0xF5, W, W, 0xD6, W, W, 0xF5, W, W, 0xD6};
                for (int a = r + 12; a < r + 48; ++a)
                    if (find_pattern(ram, a, a + 16, adsr, 16) == a && rd16(ram, a + 1) + 1 == rd16(ram, a + 7)) { L.adsr_pairs = rd16(ram, a + 1); L.tune_pairs = rd16(ram, a + 13); break; }
            }
            auto handler_store = [&](uint8_t op) -> uint16_t {
                if (!L.cmd_table) return 0;
                int h = rd16(ram, L.cmd_table + (op - L.first_cmd) * 2);
                for (int i = 0; i < 24; ++i) if (ram[(h + i) & 0xFFFF] == 0xD5) return rd16(ram, h + i + 1);
                return 0;
            };
            L.mark_base = handler_store(0xD7);
            L.rep_start_base = handler_store(0xD4);
            L.rep_end_base = handler_store(0xD5);
            const int sq[] = {0x8D, 0x04, 0xCF, 0x2D, 0xDD, 0x60, 0x88, W, 0xFD, 0xAE, 0x6F};
            if (int r = find_pattern(ram, 0x200, 0x2000, sq, 11); r >= 0) L.subseq_table = uint16_t(ram[r + 7] << 8);
            L.tempo_addr = 0xFA;
            if (!L.cmd_table || !L.note_len_table || !L.header || !L.track_ptr_base) return Layout{};
            for (int i = 0; i < 64; ++i) {
                const uint8_t v = ram[(L.len_table + i) & 0xFFFF];
                L.cmds[i].argc = int8_t(sd3 ? std::max(0, (v < 0x80 ? v : 1) - 1) : std::max(0, (v & 7) - 1));
            }
            fill_smrpg(L);
            for (int i = 0; i < 16; ++i) L.note_lens[i] = uint8_t(ram[(L.note_len_table + i) & 0xFFFF] + (i < 13 ? L.len_plus : 0));
            return L;
        }
    }

    int notelen_ver = 0;
    {
        const int r4[] = {0xCD, 0x0E, 0x9E, 0xF8, W, 0xF6, W, W};
        const int r2[] = {0x8D, 0x00, 0xCD, 0x0F, 0x9E, 0xF8, W, 0xF6, W, W};
        const int r1[] = {0xCD, 0x0F, 0x8D, 0x00, 0x9E, 0xF8, W, 0xF6, W, W};
        if (int p = find_pattern(ram, 0x200, 0x10000, r4, 8); p >= 0) { L.note_len_table = rd16(ram, p + 6); notelen_ver = 4; }
        else if (int p2 = find_pattern(ram, 0x200, 0x10000, r2, 10); p2 >= 0) { L.note_len_table = rd16(ram, p2 + 8); notelen_ver = 2; }
        else if (int p1 = find_pattern(ram, 0x200, 0x10000, r1, 10); p1 >= 0) { L.note_len_table = rd16(ram, p1 + 8); notelen_ver = 1; }
    }
    if (!notelen_ver) return Layout{};
    int vcmd_ver = 0;
    {
        const int v4[] = {0xA8, W, 0xC4, W, 0x1C, 0xFD, 0xF6, W, W, 0x2D, 0xF6, W, W, 0x2D, 0xEB, W, 0xF6, W, W, W, W};
        const int v1[] = {0xA8, W, 0x1C, 0xFD, 0xF6, W, W, 0x2D, 0xF6, W, W, 0x2D, 0xDD, 0x5C, 0xFD, 0xF6, W, W, W, W};
        if (int p = find_pattern(ram, 0x200, 0x10000, v4, 21); p >= 0 && ram[p + 3] == ram[p + 15] && rd16(ram, p + 7) == rd16(ram, p + 11) + 1) {
            L.first_cmd = ram[p + 1]; L.cmd_table = rd16(ram, p + 11); L.len_table = rd16(ram, p + 17); vcmd_ver = 4;
        } else if (int p1 = find_pattern(ram, 0x200, 0x10000, v1, 20); p1 >= 0 && rd16(ram, p1 + 5) == rd16(ram, p1 + 9) + 1) {
            L.first_cmd = ram[p1 + 1]; L.cmd_table = rd16(ram, p1 + 9); L.len_table = rd16(ram, p1 + 16); vcmd_ver = 1;
        }
    }
    if (!vcmd_ver || L.first_cmd % 14 != 0) return Layout{};
    L.lengths = L.first_cmd / 14;
    L.note_len_count = L.lengths;
    {
        const int h4[] = {0xE5, W, W, 0xC4, W, 0xE5, W, W, 0xC4, W, 0xE8, W, 0x8D, W, 0x9A, W, 0xDA, W};
        const int hmq[] = {0xCD, 0x10, 0xF5, W, W, 0xD4, W, 0x1D, 0xD0, 0xF8, 0xE8, W, 0x8D, W, 0x9A, W, 0xDA, W, 0xCD, 0x0E, 0x8F, 0x80, W, 0xE5, W, W, 0xEC, W, W, 0xDA, W};
        const int h2[] = {0xCD, 0x00, 0x8D, 0x00, 0x8F, 0x01, W, 0xF5, W, W, 0xF0, W, 0x09, W, W, 0xD4, W, 0xF5, W, W, 0xD4, W};
        const int h1[] = {0x8D, 0x01, 0xCB, W, 0xCD, 0x00, 0xF5, W, W, 0xD4, W, 0xF5, W, W, 0xD4, W, 0xF0, W, 0xDB, W};
        if (int p = find_pattern(ram, 0x200, 0x10000, h4, 18); p >= 0 && rd16(ram, p + 1) + 1 == rd16(ram, p + 6)) {
            L.header = rd16(ram, p + 1); L.rom_addresses = true; L.apu_base = uint16_t((ram[p + 13] << 8) | ram[p + 11]);
        } else if (int pm = find_pattern(ram, 0x200, 0x10000, hmq, 31); pm >= 0) {
            L.header = uint16_t(rd16(ram, pm + 3) + 1); L.rom_addresses = true; L.apu_base = uint16_t((ram[pm + 13] << 8) | ram[pm + 11]);
        } else if (int p2 = find_pattern(ram, 0x200, 0x10000, h2, 22); p2 >= 0 && rd16(ram, p2 + 8) == rd16(ram, p2 + 18) + 1) {
            L.header = rd16(ram, p2 + 18);
        } else if (int p1 = find_pattern(ram, 0x200, 0x10000, h1, 20); p1 >= 0 && rd16(ram, p1 + 7) + 1 == rd16(ram, p1 + 12)) {
            L.header = rd16(ram, p1 + 7);
        }
    }
    if (!L.header) return Layout{};
    {
        const int t5[] = {0x8F, W, 0xF1, 0x8F, W, 0xFA, 0x8F, W, 0xFB, 0x8F, 0x03, 0xF1};
        const int tl[] = {0x8F, W, 0xF1, 0x8F, W, 0xFA, 0x8F, W, 0xFB, 0x8F, W, 0xFC, 0x8F, 0x07, 0xF1};
        const int t2[] = {0x8F, W, 0xF1, 0x8F, W, 0xFA, 0x8F, 0x01, 0xF1};
        const int t1[] = {0xE8, W, 0xC4, 0xF1, 0xE8, W, 0xC4, 0xFA, 0xE8, W, 0xC4, 0xFB, 0xE8, 0x03, 0xC4, 0xF1};
        if (int p = find_pattern(ram, 0x200, 0x10000, t5, 12); p >= 0) L.timer0 = ram[p + 4];
        else if (int pl = find_pattern(ram, 0x200, 0x10000, tl, 15); pl >= 0) L.timer0 = ram[pl + 4];
        else if (int p2 = find_pattern(ram, 0x200, 0x10000, t2, 9); p2 >= 0) L.timer0 = ram[p2 + 4];
        else if (int p1 = find_pattern(ram, 0x200, 0x10000, t1, 16); p1 >= 0) L.timer0 = ram[p1 + 5];
    }
    if (notelen_ver == 1 && L.first_cmd == 0xD2 && !L.rom_addresses) L.variant = Variant::Rev1;
    else if (notelen_ver == 2 && L.first_cmd == 0xD2 && !L.rom_addresses) L.variant = Variant::Rev2;
    else if (notelen_ver == 2 && L.first_cmd == 0xD2 && L.rom_addresses) L.variant = Variant::Rev3;
    else if (notelen_ver == 4 && L.first_cmd == 0xC4 && L.rom_addresses) L.variant = Variant::Rev4;
    else return Layout{};
    L.rev1_tie_rest_swap = L.variant == Variant::Rev1;
    for (int i = 0; i < 0x100 - L.first_cmd; ++i) L.cmds[i].argc = int8_t(ram[(L.len_table + i) & 0xFFFF] & 7);
    for (int i = 0; i < 16; ++i) L.note_lens[i] = ram[(L.note_len_table + i) & 0xFFFF];
    switch (L.variant) {
        case Variant::Rev1: fill_rev1(L); break;
        case Variant::Rev2: fill_rev2(L); break;
        case Variant::Rev3: fill_rev3(L); break;
        default: fill_rev4(L, ram); break;
    }
    if (!L.track_ptr_base) {
        for (int i = 0; i < 0x100 - L.first_cmd; ++i) {
            if (L.cmds[i].kind != Kind::Jump) continue;
            uint16_t h = rd16(ram, L.cmd_table + i * 2);
            for (int k = 0; k < 24; ++k) if (ram[h + k] == 0xD4 || ram[h + k] == 0xDB) { if (!L.track_ptr_base || ram[h + k + 1] < L.track_ptr_base) L.track_ptr_base = ram[h + k + 1]; }
            break;
        }
    }
    if (!L.track_ptr_base) L.track_ptr_base = 0x02;
    return L;
}

Layout smrpg_layout_for_tests() {
    Layout L;
    L.variant = Variant::SMRPG; L.tested = true; L.header_prefix = true;
    L.first_cmd = 0xC4; L.lengths = 14; L.len_is_total = true; L.key_is_remainder = true;
    L.note_len_count = 13;
    static const uint8_t lens[14] = {0xC0, 0x90, 0x60, 0x48, 0x30, 0x24, 0x20, 0x18, 0x10, 0x0C, 0x08, 0x06, 0x03, 0x0E};
    static const uint8_t argl[60] = {0x01, 0x01, 0x02, 0x11, 0x02, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x31, 0x02, 0x02, 0x02,
                                     0x02, 0x41, 0x51, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
                                     0x03, 0x03, 0x01, 0x02, 0x03, 0x03, 0x01, 0x01, 0x02, 0x02, 0x01, 0x01, 0x03, 0x04, 0x02, 0x01,
                                     0x03, 0x04, 0x02, 0x01, 0x01, 0x21, 0x01, 0x01, 0x04, 0x01, 0x01, 0x01};
    for (int i = 0; i < 14; ++i) L.note_lens[i] = lens[i];
    for (int i = 0; i < 60; ++i) L.cmds[i].argc = int8_t(std::max(0, (argl[i] & 7) - 1));
    fill_smrpg(L);
    L.track_ptr_base = 0x1BFC; L.tempo_addr = 0xFA;
    return L;
}

std::unique_ptr<seq::Driver> detect(const uint8_t* ram) {
    Layout L = detect_layout(ram);
    if (!L.valid()) return nullptr;
    return std::make_unique<AkaoDriver>(L);
}

std::string AkaoDriver::name() const {
    switch (L.variant) {
        case Variant::SMRPG: return "Square AKAO (Super Mario RPG)";
        case Variant::Rev1: return "Square AKAO rev.1 (Final Fantasy IV)";
        case Variant::Rev2: return "Square AKAO rev.2 (Romancing SaGa)";
        case Variant::Rev3: return "Square AKAO rev.3 (FF5 / SD2 / FFMQ)";
        case Variant::Rev4: return "Square AKAO rev.4 (FF6 / Chrono Trigger / ...)";
        default: return "Square AKAO";
    }
}

uint16_t AkaoDriver::to_aram(const uint8_t* ram, uint16_t addr) const {
    if (!L.rom_addresses) return addr;
    uint16_t rom_base = rd16(ram, L.header);
    return uint16_t(addr + (L.apu_base - rom_base));
}

int AkaoDriver::cmd_size(uint8_t op) const { const Cmd* c = cmd(op); return c ? 1 + c->argc : 0; }
const char* AkaoDriver::cmd_name(uint8_t op) const { const Cmd* c = cmd(op); return c ? c->name : "?"; }
const char* AkaoDriver::cmd_code(uint8_t op) const { const Cmd* c = cmd(op); return c ? c->code : "???"; }
FxClass AkaoDriver::cmd_class(uint8_t op) const { const Cmd* c = cmd(op); return c ? c->cls : FxClass::Invalid; }
bool AkaoDriver::is_instrument_cmd(uint8_t op) const { const Cmd* c = cmd(op); return c && c->kind == Kind::Instrument; }
uint8_t AkaoDriver::instrument_opcode() const {
    for (int i = 0; i < 0x100 - L.first_cmd; ++i) if (L.cmds[i].kind == Kind::Instrument) return uint8_t(L.first_cmd + i);
    return L.first_cmd;
}

bool AkaoDriver::transpose_event(Event& e, int semis) const {
    if (e.type != EventType::Note) return false;
    int key = key_of(e.b[0]) + semis, len = len_of(e.b[0]);
    if (key < 0 || key > 11) return false;
    e.b[0] = pack(key, len);
    if (e.pitch >= 0) e.pitch += semis;
    return true;
}

std::string AkaoDriver::event_text(const Event& e) const {
    char b[96];
    if (e.type == EventType::Note || e.type == EventType::Rest || e.type == EventType::Tie) {
        const char* what = e.type == EventType::Note ? note_name(e).c_str() : e.type == EventType::Tie ? "tie" : "rest";
        std::string nm = e.type == EventType::Note ? note_name(e) : std::string(what);
        std::snprintf(b, sizeof b, "%s  %d ticks%s", nm.c_str(), e.duration, e.size == 2 ? " (explicit)" : "");
        return b;
    }
    if (e.type == EventType::Command) {
        const Cmd* c = cmd(e.b[0]);
        if (c && (c->kind == Kind::Jump)) { std::snprintf(b, sizeof b, "Jump $%02X%02X", e.b[2], e.b[1]); return b; }
        if (c && c->kind == Kind::CondJump) { std::snprintf(b, sizeof b, "On pass %d jump to $%02X%02X", e.b[1], e.b[3], e.b[2]); return b; }
    }
    return seq::Driver::event_text(e);
}

int AkaoDriver::instrument_count(const uint8_t* ram) const {
    if (!L.inst_map) return 0;
    int last = -1;
    for (int i = 0; i < 0x80; ++i) if (ram[(L.inst_map + i) & 0xFFFF] != 0xFF) last = i;
    return last + 1;
}

bool AkaoDriver::instrument_used(const uint8_t* ram, int index) const {
    return L.inst_map && index >= 0 && index < 0x80 && ram[(L.inst_map + index) & 0xFFFF] != 0xFF;
}

seq::Instrument AkaoDriver::read_instrument(const uint8_t* ram, int index) const {
    seq::Instrument in{};
    if (!L.inst_map) { in.srcn = uint8_t(index); return in; }
    int k = ram[(L.inst_map + index) & 0xFFFF];
    if (k == 0xFF) { in.srcn = 0xFF; return in; }
    in.srcn = ram[(L.sample_pairs + 1 + k * 2) & 0xFFFF];
    if (L.adsr_pairs) { in.adsr0 = ram[(L.adsr_pairs + k * 2) & 0xFFFF]; in.adsr1 = ram[(L.adsr_pairs + 1 + k * 2) & 0xFFFF]; }
    if (L.tune_pairs) { in.pitch_lo = ram[(L.tune_pairs + k * 2) & 0xFFFF]; in.pitch_hi = ram[(L.tune_pairs + 1 + k * 2) & 0xFFFF]; }
    in.gain = 0x7F;
    return in;
}

bool AkaoDriver::preview_regs(const uint8_t* ram, uint8_t note_byte, int instrument, uint8_t regs[8]) const {
    seq::Instrument in = read_instrument(ram, instrument);
    int semitone = note_semitone(note_byte);
    double pitch = 0x1000 * std::pow(2.0, (semitone - 60) / 12.0);
    int p = std::clamp(int(pitch), 0, 0x3FFF);
    regs[0] = 0x30; regs[1] = 0x30; regs[2] = uint8_t(p & 0xFF); regs[3] = uint8_t(p >> 8);
    regs[4] = in.srcn; regs[5] = in.adsr0 ? in.adsr0 : 0x8F; regs[6] = in.adsr0 ? in.adsr1 : 0xE0; regs[7] = 0x7F;
    return true;
}

seq::Track AkaoDriver::parse_track(const uint8_t* ram, uint16_t start, int octave) const {
    seq::Track t;
    t.addr = start;
    if (!start) return t;
    struct Loop { int start_pc; int count; int pass; };
    std::vector<Loop> stack;
    std::vector<uint8_t> iter(0x10000, 0);
    int pc = start, tick = 0, transpose = 0, force_len = 0, idle = 0, mark = -1, mark_event = -1;
    const bool smrpg = L.variant == Variant::SMRPG;
    auto emit = [&](Event e) {
        e.in_sub = iter[e.addr] > 0;
        e.nest = uint8_t(std::min<size_t>(stack.size(), 255));
        e.sub_iter = iter[e.addr];
        if (iter[e.addr] < 255) ++iter[e.addr];
        t.events.push_back(e);
    };
    auto loop_target = [&](int addr) {
        for (size_t i = 0; i < t.events.size(); ++i) if (t.events[i].addr == addr) return int(i);
        return 0;
    };
    auto make = [&](EventType ty, int at, int size) {
        Event e{};
        e.type = ty; e.addr = uint16_t(at); e.size = uint8_t(std::min(size, 16));
        for (int i = 0; i < e.size; ++i) e.b[i] = ram[(at + i) & 0xFFFF];
        e.tick = tick;
        return e;
    };
    while (t.events.size() < 60000 && idle < 4000) {
        uint8_t b = ram[pc & 0xFFFF];
        if (b < L.first_cmd) {
            int key = key_of(b), li = len_of(b);
            bool tie = key == 12, rest = key == 13;
            if (L.rev1_tie_rest_swap) std::swap(tie, rest);
            int size = 1, dur;
            if (smrpg && li == L.note_len_count) { dur = ram[(pc + 1) & 0xFFFF] + L.len_plus; size = 2; }
            else dur = L.note_lens[std::min(li, 15)];
            if (force_len) { dur = force_len; force_len = 0; }
            Event e = make(tie ? EventType::Tie : rest ? EventType::Rest : EventType::Note, pc, size);
            e.duration = dur;
            if (!tie && !rest) e.pitch = octave * 12 + key + transpose;
            emit(e);
            tick += dur;
            idle = dur ? 0 : idle + 1;
            pc += size;
            continue;
        }
        ++idle;
        const Cmd& c = L.cmds[b - L.first_cmd];
        const int size = 1 + c.argc;
        const uint8_t* args = ram + ((pc + 1) & 0xFFFF);
        Event e = make(c.kind == Kind::End ? EventType::End : EventType::Command, pc, size);
        emit(e);
        int next = pc + size;
        switch (c.kind) {
            case Kind::End: t.terminated = true; goto done;
            case Kind::OctaveSet: octave = args[0]; break;
            case Kind::OctaveUp: ++octave; break;
            case Kind::OctaveDown: --octave; break;
            case Kind::TransposeAbs: transpose = int8_t(args[0]); break;
            case Kind::TransposeRel: transpose += int8_t(args[0]); break;
            case Kind::ForceLength: force_len = args[0]; break;
            case Kind::LoopStart: {
                int count = smrpg ? int(args[0]) : (args[0] == 0 ? 0 : args[0] + 1);   // 0 = forever
                if (stack.size() > 16) goto done;
                stack.push_back({next, count, 1});
                break;
            }
            case Kind::LoopEnd: case Kind::LoopEndCpu: {
                if (stack.empty()) break;
                Loop& lp = stack.back();
                if (lp.count == 0) { t.loops = true; t.loop_event = loop_target(lp.start_pc); t.terminated = true; goto done; }
                if (lp.count == 1) { stack.pop_back(); break; }
                --lp.count; ++lp.pass;
                next = lp.start_pc;
                break;
            }
            case Kind::CondJump: {
                if (stack.empty()) break;
                Loop& lp = stack.back();
                int target = to_aram(ram, rd16(ram, pc + 2));
                int pass = smrpg ? lp.pass : lp.pass;   // rev.1-3 count the checks themselves; close enough
                if (args[0] == pass) {
                    if (lp.count == 1) stack.pop_back();
                    if (target <= pc) { t.loops = true; t.loop_event = loop_target(target); t.terminated = true; goto done; }
                    next = target;
                }
                break;
            }
            case Kind::BreakLast: {
                if (stack.empty()) break;
                if (stack.back().count == 1) {
                    int depth = 0, q = next;
                    while (q < 0x10000) {
                        uint8_t op = ram[q];
                        if (op < L.first_cmd) { q += (smrpg && len_of(op) == L.note_len_count) ? 2 : 1; continue; }
                        const Cmd& cc = L.cmds[op - L.first_cmd];
                        if (cc.kind == Kind::LoopStart) ++depth;
                        else if (cc.kind == Kind::LoopEnd || cc.kind == Kind::LoopEndCpu) { if (depth == 0) { q += 1 + cc.argc; break; } --depth; }
                        else if (cc.kind == Kind::End) break;
                        q += 1 + cc.argc;
                    }
                    stack.pop_back();
                    next = q;
                }
                break;
            }
            case Kind::Jump: {
                int target = to_aram(ram, rd16(ram, pc + 1));
                if (target <= pc) { t.loops = true; t.loop_event = loop_target(target); t.terminated = true; goto done; }
                next = target;
                break;
            }
            case Kind::Mark: mark = next; mark_event = int(t.events.size()); break;
            case Kind::LoopToMark:
                if (mark >= 0) { t.loops = true; t.loop_event = mark_event; t.terminated = true; }
                else { t.events.back().type = EventType::End; t.terminated = true; }
                goto done;
            case Kind::SubseqJump: {
                if (!L.subseq_table) break;
                int entry = L.subseq_table + args[0] * 4 + (b == 0xCE ? 2 : 0);
                int target = rd16(ram, entry);
                if (!target) break;
                if (target <= pc) { t.loops = true; t.loop_event = loop_target(target); t.terminated = true; goto done; }
                next = target;
                break;
            }
            default: break;
        }
        pc = next;
    }
    if (idle >= 4000 || t.events.size() >= 60000) t.truncated = true;
done:
    t.total_ticks = tick;
    t.used_events = int(t.events.size());
    t.end_addr = uint16_t(pc);
    return t;
}

std::vector<seq::Song> AkaoDriver::find_songs(const uint8_t* ram, const uint8_t* dsp) const {
    (void)dsp;
    std::vector<seq::Song> songs;
    int p = L.header;
    int end_marker = -1;
    if (L.header_prefix) {
        while (ram[p & 0xFFFF] < 0x80 && p < L.header + 0x200) p += 5;
        p += 1;
    } else if (L.variant == Variant::Rev3 || L.variant == Variant::Rev4) {
        p += 2;
        if (L.variant == Variant::Rev4) { end_marker = rd16(ram, p); p += 2; }
        else end_marker = rd16(ram, p + 16);
    }
    seq::Pattern pat;
    pat.addr = uint16_t(p);
    bool any = false, loops = false;
    for (int v = 0; v < 8; ++v) {
        uint16_t raw = rd16(ram, p + v * 2);
        if (!raw || int(raw) == end_marker) continue;
        uint16_t start = to_aram(ram, raw);
        int octave = L.octave_init;
        if (octave < 0 || octave > 8) octave = 4;
        pat.tracks[v] = parse_track(ram, start, octave);
        if (pat.tracks[v].total_ticks > 0) any = true;
        if (pat.tracks[v].loops) loops = true;
        pat.length_ticks = std::max(pat.length_ticks, pat.tracks[v].total_ticks);
    }
    if (!any) return songs;
    seq::Song sg;
    sg.order_addr = uint16_t(p);
    sg.order_end = uint16_t(p + 16);
    sg.orders.push_back({sg.order_addr, pat.addr});
    sg.patterns.push_back(std::move(pat));
    sg.loop_count = loops ? 0xFF : 0;
    sg.loop_to = loops ? 0 : -1;
    char b[64];
    std::snprintf(b, sizeof b, "song @%04X (%d ticks%s)", p, sg.patterns[0].length_ticks, loops ? ", loops" : "");
    sg.label = b;
    songs.push_back(std::move(sg));
    return songs;
}

int AkaoDriver::bgm_slot(const uint8_t* ram) const {
    if (!L.bgm_slot_var) return 0;
    int s = ram[L.bgm_slot_var];
    return s == 8 ? 8 : 0;
}

seq::Position AkaoDriver::locate(const uint8_t* ram, const seq::Song& song, const seq::Position* prev) const {
    seq::Position pos;
    pos.track_ptr_base = uint16_t(L.track_ptr_base + bgm_slot(ram) * 2);
    pos.order_index = 0;
    uint16_t ptr[8];
    for (int v = 0; v < 8; ++v) ptr[v] = rd16(ram, pos.track_ptr_base + v * 2);
    seq::resolve_stream_position(song.patterns[0], ptr, prev, false, pos);
    return pos;
}

double AkaoDriver::ticks_per_second(const uint8_t* ram) const {
    if (L.variant == Variant::SMRPG) {
        int latch = ram[L.tempo_addr];
        return latch ? 8000.0 / latch : 0;
    }
    return 0;   // rev.1-4: the follow mode calibrates from the stream
}

bool AkaoDriver::tempo_writes(const uint8_t* ram, double tps, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    if (L.variant != Variant::SMRPG || tps <= 0) return false;
    int latch = std::clamp(int(8000.0 / tps + 0.5), 1, 255);
    int n = std::clamp(latch - ram[0x60], 1, 255);
    out.push_back({0x5F, uint8_t(n)});
    out.push_back({uint16_t(L.tempo_addr), uint8_t(latch)});
    return true;
}

std::vector<uint8_t> AkaoDriver::serialize_track(const std::vector<Event>& events) const {
    std::vector<uint8_t> out;
    for (const Event& e : events) {
        if (e.in_sub) continue;
        for (int i = 0; i < e.size; ++i) out.push_back(e.b[i]);
    }
    return out;
}

void AkaoDriver::retime(std::vector<Event>& ev) const {
    int tick = 0;
    for (Event& e : ev) {
        e.tick = tick;
        if (e.type == EventType::Note || e.type == EventType::Rest || e.type == EventType::Tie) {
            int li = len_of(e.b[0]);
            e.duration = e.size == 2 ? e.b[1] + L.len_plus : L.note_lens[std::min(li, 15)];
            tick += e.duration;
        } else e.duration = 0;
    }
}

void AkaoDriver::apply_note_byte(Event& e, uint8_t byte) const {
    int len = len_of(e.b[0]), key = key_of(byte);
    int old_key = key_of(e.b[0]);
    e.b[0] = pack(key, len);
    e.type = key == 12 ? EventType::Tie : key == 13 ? EventType::Rest : EventType::Note;
    if (L.rev1_tie_rest_swap && key >= 12) e.type = key == 12 ? EventType::Rest : EventType::Tie;
    if (e.type == EventType::Note) {
        if (e.pitch >= 0 && old_key < 12) e.pitch += key - old_key;
        else if (e.pitch < 0) e.pitch = 48 + key;
    } else e.pitch = -1;
}

bool AkaoDriver::set_duration(std::vector<Event>& ev, int i, int dur) const {
    if (i < 0 || i >= int(ev.size()) || dur < 1) return false;
    Event& e = ev[size_t(i)];
    int key = key_of(e.b[0]);
    for (int li = 0; li < L.note_len_count; ++li)
        if (L.note_lens[li] == dur) { e.b[0] = pack(key, li); e.size = 1; e.addr = 0; e.duration = dur; return true; }
    if (L.variant == Variant::SMRPG && dur - L.len_plus <= 255 && dur - L.len_plus >= 0) {
        e.b[0] = pack(key, 13); e.b[1] = uint8_t(dur - L.len_plus); e.size = 2; e.addr = 0; e.duration = dur;
        return true;
    }
    return false;   // not a length this revision can express
}

bool AkaoDriver::ends_stream(const Event& e) const {
    if (e.type != EventType::Command) return false;
    const Cmd* c = cmd(e.b[0]);
    return c && (c->kind == Kind::LoopToMark || c->kind == Kind::End || c->kind == Kind::Jump);
}

int AkaoDriver::jump_target(const Event& e) const {
    if (e.type != EventType::Command) return -1;
    const Cmd* c = cmd(e.b[0]);
    if (!c || c->kind != Kind::Jump || e.size < 3 || L.rom_addresses) return -1;
    return e.b[1] | (e.b[2] << 8);
}

void AkaoDriver::set_jump_target(Event& e, uint16_t addr) const { e.b[1] = uint8_t(addr & 0xFF); e.b[2] = uint8_t(addr >> 8); }

uint16_t AkaoDriver::from_aram(const uint8_t* ram, uint16_t addr) const {
    if (!L.rom_addresses) return addr;
    uint16_t rom_base = rd16(ram, L.header);
    return uint16_t(addr - (L.apu_base - rom_base));
}

void AkaoDriver::track_pointer_writes(const seq::Song& song, int pattern_idx, int voice, uint16_t dest, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    (void)pattern_idx;
    if (L.rom_addresses) return;
    uint16_t at = uint16_t(song.order_addr + voice * 2);
    out.push_back({at, uint8_t(dest & 0xFF)});
    out.push_back({uint16_t(at + 1), uint8_t(dest >> 8)});
}

void AkaoDriver::live_state_writes(const uint8_t* ram, const seq::Position& pos, int voice, uint16_t ptr, const Remap& remap, std::vector<std::pair<uint16_t, uint8_t>>& out) const {
    seq::Driver::live_state_writes(ram, pos, voice, ptr, remap, out);
    const int lv = (pos.track_ptr_base - L.track_ptr_base) / 2 + voice;   // logical voice = slot + voice
    if (L.mark_base) seq::remap_word(ram, uint16_t(L.mark_base + lv * 2), remap, out);
    for (int k = 0; k < 3; ++k) {
        if (L.rep_start_base) seq::remap_word(ram, uint16_t(L.rep_start_base + lv * 6 + k * 2), remap, out);
        if (L.rep_end_base) seq::remap_word(ram, uint16_t(L.rep_end_base + lv * 6 + k * 2), remap, out);
    }
}

bool AkaoDriver::set_note_at(std::vector<Event>& ev, int tick, uint8_t note_byte, int pattern_len) const {
    (void)pattern_len;
    return seq::stream_set_note_at(*this, ev, tick, note_byte, pattern_len);
}

bool AkaoDriver::remove_span(std::vector<Event>& ev, int tick, int ticks, bool keep_length) const {
    (void)keep_length;
    return seq::stream_remove_span(*this, ev, tick, ticks);
}

bool AkaoDriver::insert_span(std::vector<Event>& ev, int tick, int ticks, uint8_t byte) const {
    return seq::stream_insert_span(*this, ev, tick, ticks, byte);
}

bool AkaoDriver::enter_note(std::vector<Event>& ev, int tick, int semitone, int pattern_len) const {
    seq::stream_prepare(*this, ev, tick, pattern_len);
    int idx = seq::stream_timed_covering(ev, tick, true);
    if (idx < 0) return false;
    if (ev[size_t(idx)].tick != tick) {
        if (ev[size_t(idx)].in_sub || !seq::stream_split_at(*this, ev, tick)) return false;
        idx = -1;
        for (size_t i = 0; i < ev.size(); ++i) if (ev[i].duration > 0 && ev[i].tick == tick) { idx = int(i); break; }
        if (idx < 0) return false;
    }
    int octave = -1;
    for (int i = idx; i >= 0 && octave < 0; --i)
        if (ev[size_t(i)].type == EventType::Note && ev[size_t(i)].pitch >= 0) octave = (ev[size_t(i)].pitch - key_of(ev[size_t(i)].b[0])) / 12;
    if (octave < 0)
        for (int i = idx; i < int(ev.size()) && octave < 0; ++i)
            if (ev[size_t(i)].type == EventType::Note && ev[size_t(i)].pitch >= 0) octave = (ev[size_t(i)].pitch - key_of(ev[size_t(i)].b[0])) / 12;
    if (octave < 0) octave = 4;
    int want = semitone / 12, key = semitone % 12;
    if (want < 0 || want > 8) return false;
    if (!set_note_at(ev, tick, pack(key, 0), pattern_len)) return false;
    ev[size_t(idx)].pitch = semitone;
    if (want != octave) {
        int op = -1;
        for (int o = L.first_cmd; o < 0x100; ++o) if (cmd(uint8_t(o))->kind == Kind::OctaveSet && cmd(uint8_t(o))->argc == 1) { op = o; break; }
        if (op < 0) return false;
        Event set{}; set.type = EventType::Command; set.size = 2; set.b[0] = uint8_t(op); set.b[1] = uint8_t(want); set.addr = 0;
        Event back = set; back.b[1] = uint8_t(octave);
        bool later = false;
        for (size_t i = size_t(idx) + 1; i < ev.size(); ++i) if (ev[i].type == EventType::Note) { later = true; break; }
        if (later) ev.insert(ev.begin() + idx + 1, back);
        ev.insert(ev.begin() + idx, set);
        ev[size_t(idx) + 1].addr = 0;
    }
    retime(ev);
    return true;
}

bool AkaoDriver::insert_command_at(std::vector<Event>& ev, int tick, const uint8_t* bytes, int size) const {
    return seq::stream_insert_command_at(*this, ev, tick, bytes, size);
}

bool AkaoDriver::set_instrument(std::vector<Event>& ev, int tick0, int tick1, uint8_t ins) const {
    return seq::stream_set_instrument(*this, ev, tick0, tick1, ins);
}

}
