// Headless checks for the tracker edit primitives: build a track from bytes,
// apply an operation, serialise, re-parse, and verify what the driver would
// see. Run: build/edittest
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "driver/akao.hpp"
#include "driver/capcom.hpp"
#include "driver/follin.hpp"
#include "driver/nspc.hpp"
#include "driver/rare.hpp"
#include "project.hpp"
#include "snsf.hpp"
#include "spc_file.hpp"
#include "tracker.hpp"

using seq::Event;
using seq::EventType;

namespace {
int g_fail = 0;
#define CHECK(cond, ...) do { if (!(cond)) { std::printf("  FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); ++g_fail; } } while (0)

const nspc::Layout L = nspc::layout_for(nspc::Variant::SMW);
const nspc::NspcDriver N(L);
constexpr uint16_t kBase = 0x2000;

std::vector<Event> parse(const std::vector<uint8_t>& bytes) {
    std::vector<uint8_t> ram(0x10000, 0);
    std::memcpy(ram.data() + kBase, bytes.data(), bytes.size());
    nspc::Track t = nspc::parse_track(ram.data(), L, kBase);
    std::vector<Event> ev = t.events;
    while (!ev.empty() && ev.back().type == EventType::End) ev.pop_back();
    return ev;
}

std::vector<Event> roundtrip(const std::vector<Event>& ev) { return parse(nspc::serialize_track(ev)); }

std::string dump(const std::vector<Event>& ev) {
    std::string s;
    char b[64];
    for (const Event& e : ev) {
        switch (e.type) {
            case EventType::Length: std::snprintf(b, sizeof b, e.size == 2 ? "l%02X/%02X " : "l%02X ", e.b[0], e.b[1]); break;
            case EventType::Note: std::snprintf(b, sizeof b, "%s@%d ", N.note_name(e.b[0]).c_str(), e.tick); break;
            case EventType::Tie: std::snprintf(b, sizeof b, "tie@%d ", e.tick); break;
            case EventType::Rest: std::snprintf(b, sizeof b, "rest@%d ", e.tick); break;
            case EventType::Command: std::snprintf(b, sizeof b, "[%02X%s%02X] ", e.b[0], e.size > 1 ? " " : "", e.size > 1 ? e.b[1] : 0); break;
            default: std::snprintf(b, sizeof b, "? "); break;
        }
        s += b;
    }
    return s;
}

int total_ticks(const std::vector<Event>& ev) { int t = 0; for (const Event& e : ev) t = std::max(t, e.tick + e.duration); return t; }
int timed_at(const std::vector<Event>& ev, int tick) { for (size_t i = 0; i < ev.size(); ++i) if (ev[i].duration > 0 && ev[i].tick == tick) return int(i); return -1; }
int qv_of(const std::vector<Event>& ev, int idx) { for (int i = idx - 1; i >= 0; --i) if (ev[i].type == EventType::Length && ev[i].size == 2) return ev[i].b[1]; return -1; }
int len_of(const std::vector<Event>& ev, int idx) { return ev[idx].duration; }

const std::vector<uint8_t> kFour = {0x18, 0x6F, 0x80 + 36, 0x80 + 38, 0x80 + 40, 0x80 + 41};

void test_set_qv() {
    std::printf("set_qv\n");
    std::vector<Event> ev = parse(kFour);
    int i = timed_at(ev, 24);
    CHECK(N.set_qv(ev, i, 3, 5), "set_qv failed");
    std::vector<Event> r = roundtrip(ev);
    CHECK(total_ticks(r) == 96, "length changed: %d  (%s)", total_ticks(r), dump(r).c_str());
    CHECK(qv_of(r, timed_at(r, 0)) == 0x6F, "first note qv changed");
    CHECK(qv_of(r, timed_at(r, 24)) == 0x35, "edited note qv %02X", qv_of(r, timed_at(r, 24)));
    CHECK(qv_of(r, timed_at(r, 48)) == 0x6F, "third note qv leaked: %02X (%s)", qv_of(r, timed_at(r, 48)), dump(r).c_str());
    CHECK(len_of(r, timed_at(r, 48)) == 24, "third note length changed");
}

void test_set_qv_no_prior_qv() {
    std::printf("set_qv without prior qv\n");
    std::vector<Event> ev = parse({0x18, 0x80 + 36, 0x80 + 38, 0x80 + 40});
    CHECK(N.set_qv(ev, timed_at(ev, 24), 7, 15), "set_qv failed");
    std::vector<Event> r = roundtrip(ev);
    CHECK(total_ticks(r) == 72, "length changed: %s", dump(r).c_str());
    CHECK(qv_of(r, timed_at(r, 24)) == 0x7F, "qv not set");
}

void test_set_instrument() {
    std::printf("set_instrument\n");
    std::vector<Event> ev = parse(kFour);
    CHECK(N.set_instrument(ev, 48, 72, 0x05), "insert failed");
    std::vector<Event> r = roundtrip(ev);
    CHECK(total_ticks(r) == 96, "length changed: %s", dump(r).c_str());
    bool found = false;
    for (const Event& e : r) if (e.type == EventType::Command && e.b[0] == L.cmd_base && e.b[1] == 5 && e.tick == 48) found = true;
    CHECK(found, "instrument command not at tick 48: %s", dump(r).c_str());
    CHECK(N.set_instrument(r, 48, 72, 0x09), "replace failed");
    std::vector<Event> r2 = roundtrip(r);
    int count = 0;
    for (const Event& e : r2) if (e.type == EventType::Command && e.b[0] == L.cmd_base) { ++count; CHECK(e.b[1] == 9, "not replaced"); }
    CHECK(count == 1, "duplicate instrument commands: %s", dump(r2).c_str());
}

void test_remove_span_keep() {
    std::printf("remove_span (pull delete)\n");
    std::vector<Event> ev = parse(kFour);
    CHECK(N.remove_span(ev, 24, 24, true), "remove failed");
    std::vector<Event> r = roundtrip(ev);
    CHECK(total_ticks(r) == 96, "length not kept: %d (%s)", total_ticks(r), dump(r).c_str());
    int i = timed_at(r, 24);
    CHECK(i >= 0 && r[i].type == EventType::Note && nspc::note_semitone(L, r[i].b[0]) == 40, "E-4 did not move up: %s", dump(r).c_str());
    int last = timed_at(r, 48);
    CHECK(last >= 0 && r[last].duration == 48, "last note not stretched: %s", dump(r).c_str());
    CHECK(qv_of(r, last) == 0x6F, "qv lost on stretched note");
}

void test_remove_span_partial() {
    std::printf("remove_span across a note boundary\n");
    std::vector<Event> ev = parse(kFour);
    CHECK(N.remove_span(ev, 12, 24, false), "remove failed");
    std::vector<Event> r = roundtrip(ev);
    CHECK(total_ticks(r) == 72, "length %d (%s)", total_ticks(r), dump(r).c_str());
    CHECK(r[timed_at(r, 0)].duration == 12, "first note %d", r[timed_at(r, 0)].duration);
    CHECK(timed_at(r, 12) >= 0 && r[timed_at(r, 12)].duration == 12, "second note: %s", dump(r).c_str());
    CHECK(timed_at(r, 24) >= 0 && r[timed_at(r, 24)].duration == 24, "third note: %s", dump(r).c_str());
    CHECK(qv_of(r, timed_at(r, 24)) == 0x6F, "qv lost");
}

void test_insert_span() {
    std::printf("insert_span (insert row)\n");
    std::vector<Event> ev = parse(kFour);
    CHECK(N.insert_span(ev, 24, 24, L.tie), "insert failed");
    std::vector<Event> r = roundtrip(ev);
    CHECK(total_ticks(r) == 96, "length not kept: %d (%s)", total_ticks(r), dump(r).c_str());
    CHECK(r[timed_at(r, 24)].type == EventType::Tie, "no tie at 24: %s", dump(r).c_str());
    int d = timed_at(r, 48);
    CHECK(d >= 0 && nspc::note_semitone(L, r[d].b[0]) == 38, "D-4 not pushed to 48: %s", dump(r).c_str());
    CHECK(timed_at(r, 96) < 0 && r.back().type == EventType::Note && nspc::note_semitone(L, r.back().b[0]) == 40, "F-4 should have fallen off: %s", dump(r).c_str());
    CHECK(qv_of(r, d) == 0x6F, "qv lost after insert");
}

void test_insert_span_mid_note() {
    std::printf("insert_span inside a note\n");
    std::vector<Event> ev = parse(kFour);
    CHECK(N.insert_span(ev, 12, 12, L.tie), "insert failed");
    std::vector<Event> r = roundtrip(ev);
    CHECK(total_ticks(r) == 96, "length: %s", dump(r).c_str());
    CHECK(r[timed_at(r, 0)].duration == 12, "head not shortened: %s", dump(r).c_str());
    CHECK(timed_at(r, 12) >= 0 && r[timed_at(r, 12)].type == EventType::Tie, "inserted tie missing: %s", dump(r).c_str());
    CHECK(timed_at(r, 24) >= 0 && r[timed_at(r, 24)].type == EventType::Tie, "continuation tie missing: %s", dump(r).c_str());
    CHECK(timed_at(r, 36) >= 0 && nspc::note_semitone(L, r[timed_at(r, 36)].b[0]) == 38, "D-4 not at 36: %s", dump(r).c_str());
}

void test_set_note_split_keeps_qv() {
    std::printf("set_note_at split keeps qv\n");
    std::vector<Event> ev = parse(kFour);
    CHECK(N.set_note_at(ev, 12, 0x80 + 43, 96), "set_note failed");
    std::vector<Event> r = roundtrip(ev);
    CHECK(total_ticks(r) == 96, "length: %s", dump(r).c_str());
    CHECK(qv_of(r, timed_at(r, 12)) == 0x6F, "qv lost on split: %s", dump(r).c_str());
    CHECK(qv_of(r, timed_at(r, 24)) == 0x6F, "qv lost after split: %s", dump(r).c_str());
    CHECK(r[timed_at(r, 24)].duration == 24, "following note length wrong: %s", dump(r).c_str());
}

void test_sequence_like_ui() {
    std::printf("ui-like sequence: split, split, instrument, then find the note\n");
    std::vector<Event> ev = parse({0x08, 0x6F, 0xC7, 0xC7, 0xA4, 0xA3, 0xA4, 0xA3, 0x18, 0xC7, 0xA9});
    CHECK(N.set_note_at(ev, 28, 0xA4, 96), "split 1");
    ev = roundtrip(ev);
    CHECK(N.set_note_at(ev, 36, 0xB0, 96), "split 2");
    ev = roundtrip(ev);
    CHECK(N.set_instrument(ev, 40, 44, 0), "ins 0");
    ev = roundtrip(ev);
    CHECK(N.set_instrument(ev, 40, 44, 5), "ins 5");
    ev = roundtrip(ev);
    std::printf("  %s\n", dump(ev).c_str());
    int i = timed_at(ev, 40);
    CHECK(i >= 0, "no timed event at 40");
    CHECK(i >= 0 && ev[i].type == EventType::Note && nspc::note_semitone(L, ev[i].b[0]) == 35, "B-4 expected at 40");
    CHECK(timed_at(ev, 48) >= 0 && ev[timed_at(ev, 48)].duration == 24, "l18 rest at 48");
}

void test_insert_command_mid_note() {
    std::printf("insert_command_at inside a note splits it\n");
    std::vector<Event> ev = parse(kFour);
    uint8_t pan[2] = {0xDB, 0x0A};
    CHECK(N.insert_command_at(ev, 36, pan, 2), "insert failed");
    std::vector<Event> r = roundtrip(ev);
    CHECK(total_ticks(r) == 96, "length: %s", dump(r).c_str());
    bool ok = false;
    for (const Event& e : r) if (e.type == EventType::Command && e.b[0] == 0xDB && e.tick == 36) ok = true;
    CHECK(ok, "command not at 36: %s", dump(r).c_str());
    CHECK(timed_at(r, 36) >= 0 && r[timed_at(r, 36)].type == EventType::Tie && r[timed_at(r, 36)].duration == 12, "tie after split: %s", dump(r).c_str());
    CHECK(timed_at(r, 48) >= 0 && r[timed_at(r, 48)].duration == 24 && qv_of(r, timed_at(r, 48)) == 0x6F, "following note: %s", dump(r).c_str());
}

std::vector<uint8_t> follin_ram(const std::vector<uint8_t>& stream, uint16_t at) {
    std::vector<uint8_t> ram(0x10000, 0);
    std::memcpy(ram.data() + at, stream.data(), stream.size());
    return ram;
}

void test_follin_stream() {
    std::printf("follin: notes, default duration, repeat, call, loop\n");
    follin::Layout FL;
    FL.song_lo[0] = 0x1380; FL.song_hi[0] = 0x1385;
    follin::FollinDriver F(FL);
    std::vector<uint8_t> main_s = {0x89, 0x05, 0x3E, 12, 0x00, 6, 0x86, 8, 0x40, 0x84, 3, 0x42, 0x85, 0x82, 0x00, 0x21, 0x81, 0x00, 0x20};
    std::vector<uint8_t> sub_s = {0x87, 0x3E, 20, 0x83};
    std::vector<uint8_t> ram = follin_ram(main_s, 0x2000);
    std::memcpy(ram.data() + 0x2100, sub_s.data(), sub_s.size());
    seq::Track t = F.parse_stream(ram.data(), 0x2000);
    CHECK(t.loops, "loop not detected");
    CHECK(t.total_ticks == 70, "total ticks %d", t.total_ticks);
    int notes = 0, repeats = 0;
    int shared = 0;
    for (const Event& e : t.events) { if (e.type == EventType::Note) ++notes; if (e.sub_iter > 0) ++repeats; if (e.in_sub) ++shared; }
    CHECK(notes == 1 + 1 + 3 + 1, "notes %d", notes);
    CHECK(repeats == 4, "repeat visits (note + repeat-end, twice) flagged: %d", repeats);
    CHECK(shared == 4 + 3, "repeat re-runs and the called sub are shared: %d", shared);
    int i26 = -1; for (size_t i = 0; i < t.events.size(); ++i) if (t.events[i].tick == 26 && t.events[i].duration > 0) i26 = int(i);
    CHECK(i26 >= 0 && t.events[size_t(i26)].size == 1 && t.events[size_t(i26)].duration == 8, "default-duration note");
    std::vector<Event> ev = t.events;
    CHECK(F.set_note_at(ev, 12, 0x30, 70), "set rest -> note");
    CHECK(F.set_note_at(ev, 13, 0x30, 70), "mid-note set splits the note");
    F.retime(ev);
    {
        int a = -1, b = -1;
        for (size_t i = 0; i < ev.size(); ++i) { if (ev[i].duration > 0 && ev[i].tick == 12) a = int(i); if (ev[i].duration > 0 && ev[i].tick == 13) b = int(i); }
        CHECK(a >= 0 && b >= 0 && ev[size_t(a)].duration == 1 && ev[size_t(b)].duration == 5 && ev[size_t(b)].addr == 0, "split 6 -> 1 + 5 (%d %d)", a >= 0 ? ev[size_t(a)].duration : -1, b >= 0 ? ev[size_t(b)].duration : -1);
    }
    CHECK(F.set_note_at(ev, 22, 0x41, 70), "split the default-duration D-5");
    {
        int k = -1; for (size_t i = 0; i < ev.size(); ++i) if (ev[i].duration > 0 && ev[i].tick == 18) k = int(i);
        CHECK(k > 0 && ev[size_t(k - 1)].type == EventType::Command && ev[size_t(k - 1)].b[0] == 0x87 && ev[size_t(k)].size == 2 && ev[size_t(k)].duration == 4, "87 inserted before the shortened note");
    }
    int total_before = 0; for (const Event& e : ev) total_before = std::max(total_before, e.tick + e.duration);
    CHECK(total_before == 70, "edits kept the length: %d", total_before);
    std::vector<uint8_t> out = F.serialize_relocated(ev, 0x2200);
    std::vector<uint8_t> ram2 = ram;
    std::memcpy(ram2.data() + 0x2200, out.data(), out.size());
    seq::Track t2 = F.parse_stream(ram2.data(), 0x2200);
    CHECK(t2.loops && t2.total_ticks == 70, "relocated stream loops with the same length: loops=%d ticks=%d", t2.loops, t2.total_ticks);
    int notes2 = 0; for (const Event& e : t2.events) if (e.type == EventType::Note) ++notes2;
    CHECK(notes2 == notes + 3, "relocated stream has the rest-turned-note and both splits: %d", notes2);
    CHECK(seq::stream_structure_changed(ev, t.events), "structure change detected");
    ev = t.events;
    CHECK(F.set_note_at(ev, 12, 0x30, 70) && !seq::stream_structure_changed(ev, t.events), "byte-for-byte edit keeps the structure");
    CHECK(F.remove_span(ev, 0, 12, false), "remove the first note's 12 ticks");
    F.retime(ev);
    { int tt = 0; for (const Event& e : ev) tt = std::max(tt, e.tick + e.duration); CHECK(tt == 58, "removed 12 ticks: %d", tt); }
    CHECK(F.insert_span(ev, 0, 12, 0), "insert a 12-tick rest at the start");
    F.retime(ev);
    { int tt = 0; for (const Event& e : ev) tt = std::max(tt, e.tick + e.duration); CHECK(tt == 70 && ev[0].type == EventType::Command, "rest inserted: %d", tt); }
    CHECK(F.note_name(0x3E) == "C-5", "note naming: %s", F.note_name(0x3E).c_str());
    CHECK(F.cmd_size(0xA3) == 2 && F.cmd_size(0xAF) == 9 && F.cmd_size(0xC0) == 1, "command sizes");
}

void test_follin_slide() {
    std::printf("follin: slides (glide idiom), sweep decoding\n");
    follin::Layout FL;
    FL.song_lo[0] = 0x1380; FL.song_hi[0] = 0x1385;
    follin::FollinDriver F(FL);
    std::vector<uint8_t> main_s = {0x89, 0x05, 0x8F, 0x0C, 0x04, 0x09, 0x3E, 24, 0x40, 24, 0x91, 0x42, 24, 0xBA};
    std::vector<uint8_t> ram = follin_ram(main_s, 0x2000);
    seq::Track t = F.parse_stream(ram.data(), 0x2000);
    CHECK(t.total_ticks == 72, "ticks %d", t.total_ticks);
    seq::PitchFx fx;
    CHECK(F.pitch_fx(t.events[1], fx) && fx.kind == seq::PitchFx::Slide && fx.sticky && fx.units == -4 && fx.delay == 12 && fx.length == 9, "sweep decoded: units %d delay %d length %d", fx.units, fx.delay, fx.length);
    CHECK(F.pitch_fx(t.events[4], fx) && fx.kind == seq::PitchFx::SlideOff && fx.sticky, "sweep off decoded");
    CHECK(F.can_slide(), "can_slide");
    std::vector<Event> ev = t.events;
    CHECK(F.set_slide(ev, 24, 24, 62, 65, -1), "set_slide");
    F.retime(ev);
    CHECK(total_ticks(ev) == 72, "length kept: %d", total_ticks(ev));
    int a = timed_at(ev, 24), b = timed_at(ev, 25), c = timed_at(ev, 48);
    CHECK(a >= 0 && ev[size_t(a)].duration == 1 && ev[size_t(a)].b[0] == 0x40, "first tick keeps D-5");
    CHECK(b >= 0 && ev[size_t(b)].duration == 23 && ev[size_t(b)].b[0] == F.note_byte(65), "rest of the note is the target: %d ticks byte %02X", b >= 0 ? ev[size_t(b)].duration : -1, b >= 0 ? ev[size_t(b)].b[0] : 0);
    CHECK(!F.note_retriggers(ev, b) && F.note_retriggers(ev, a), "target is legato, the start keys on");
    CHECK(c >= 0 && F.note_retriggers(ev, c), "the note after keys on again");
    int glide_before = -1, glide_after = -1;
    for (int k = b - 1; k > a; --k) if (ev[size_t(k)].type == EventType::Command && ev[size_t(k)].b[0] == 0x90) glide_before = ev[size_t(k)].b[1];
    for (int k = c - 1; k > b; --k) if (ev[size_t(k)].type == EventType::Command && ev[size_t(k)].b[0] == 0x90) glide_after = ev[size_t(k)].b[1];
    CHECK(glide_before > 0 && glide_after == 0, "glide speed set (%d) then cleared (%d)", glide_before, glide_after);
    const double gap = std::fabs(F.pitch_units(65) - F.pitch_units(62));
    CHECK(glide_before * 23 >= gap && (glide_before - 1) * 23 < gap, "speed fits the note: %d/tick for %.0f units", glide_before, gap);
    CHECK(F.set_slide(ev, 25, 23, 62, 60, b), "retarget");
    F.retime(ev);
    b = timed_at(ev, 25);
    CHECK(b >= 0 && ev[size_t(b)].b[0] == F.note_byte(60), "retargeted to C-5");
    for (int k = b - 1; k > a; --k) if (ev[size_t(k)].type == EventType::Command && ev[size_t(k)].b[0] == 0x90) { glide_before = ev[size_t(k)].b[1]; break; }
    CHECK(glide_before > 0 && glide_before * 23 >= std::fabs(F.pitch_units(60) - F.pitch_units(62)), "speed refitted: %d", glide_before);
    std::vector<uint8_t> out = F.serialize_relocated(ev, 0x2200);
    std::vector<uint8_t> ram2 = ram;
    std::memcpy(ram2.data() + 0x2200, out.data(), out.size());
    seq::Track t2 = F.parse_stream(ram2.data(), 0x2200);
    CHECK(t2.total_ticks == 72, "relocated: %d ticks", t2.total_ticks);
    ev = t.events;
    ev[1].b[3] = 0;
    CHECK(F.set_slide(ev, 0, 24, 60, 60, 1), "switch the sweep off");
    CHECK(ev[1].type == EventType::Command && ev[1].b[0] == 0x91, "sweep replaced by $91: %02X", ev[1].b[0]);
    ev = t.events; ev[1].b[3] = 0;
    CHECK(F.set_slide(ev, 0, 24, 60, 64, 1) && ev[1].b[0] == 0x8E && ev[1].b[2] > 0, "sweep refitted upward: %02X rate %d", ev[1].b[0], ev[1].b[2]);
}

void test_follin_detect_plok() {
    const char* path = std::getenv("PLOK_SPC");
    if (!path) { std::printf("follin: set PLOK_SPC=<file> to test detection on a real rip\n"); return; }
    std::printf("follin: detection on %s\n", path);
    FILE* f = std::fopen(path, "rb");
    if (!f) { CHECK(false, "cannot open"); return; }
    std::vector<uint8_t> file(0x10100);
    size_t n = std::fread(file.data(), 1, file.size(), f);
    std::fclose(f);
    CHECK(n >= 0x10100, "short file");
    const uint8_t* ram = file.data() + 0x100;
    follin::Layout FL = follin::detect_layout(ram);
    CHECK(FL.valid(), "not detected");
    CHECK(FL.song_lo[0] == 0x1380 && FL.song_hi[0] == 0x1385 && FL.slots == 5, "song table %04X/%04X slots %d", FL.song_lo[0], FL.song_hi[0], FL.slots);
    CHECK(FL.pitch_lo == 0x1200 && FL.pitch_hi == 0x1255, "pitch table %04X/%04X", FL.pitch_lo, FL.pitch_hi);
    CHECK(FL.transpose_table == 0x38B8 && FL.mult_table == 0x38E5, "instrument tables %04X/%04X", FL.transpose_table, FL.mult_table);
    follin::FollinDriver F(FL);
    std::vector<seq::Song> songs = F.find_songs(ram, nullptr);
    CHECK(!songs.empty(), "no songs");
    int cur = F.pick_current_song(ram, songs);
    CHECK(cur >= 0, "no current song");
    if (cur >= 0) {
        const seq::Pattern& p = songs[size_t(cur)].patterns[0];
        std::printf("  %s: %d ticks, v0 %zu events\n", songs[size_t(cur)].label.c_str(), p.length_ticks, p.tracks[0].events.size());
        CHECK(p.length_ticks > 100, "song too short");
    }
}

void test_project_roundtrip() {
    std::printf("project file round trip\n");
    ProjectMeta m;
    m.source = "/some/dir/song.spc"; m.song = 3; m.ticks_per_row = 12; m.octave = 5; m.edit_step = 2; m.view_order = 7; m.reclaim = true;
    std::vector<uint8_t> spc(0x10200);
    for (size_t i = 0; i < spc.size(); ++i) spc[i] = uint8_t(i * 7 + 3);
    std::memcpy(spc.data(), "SNES-SPC700 Sound File Data", 27);
    spc[100] = '-'; spc[101] = '-'; spc[102] = '-'; spc[103] = '\n';
    const char* path = "/tmp/boomspc_test.boomspc";
    CHECK(save_project(path, m, spc).empty(), "save failed");
    ProjectMeta r; std::vector<uint8_t> back;
    CHECK(load_project(path, r, back).empty(), "load failed");
    CHECK(back == spc, "image differs (%zu vs %zu)", back.size(), spc.size());
    CHECK(r.source == m.source && r.song == 3 && r.ticks_per_row == 12 && r.octave == 5 && r.edit_step == 2 && r.view_order == 7 && r.reclaim, "meta differs");
    CHECK(is_project_path("x.boomspc") && !is_project_path("x.spc"), "extension check");
    std::remove(path);
}

void test_akao_stream() {
    std::printf("akao (SMRPG): notes, repeat, explicit length, mark loop\n");
    akao::AkaoDriver A(akao::smrpg_layout_for_tests());
    std::vector<uint8_t> st = {0xD7, 0xC6, 0x04, 0xDE, 0x05, 0x7E, 0xD4, 0x02, 0x82, 0xD5, 0xB8, 0x20, 0x8B, 0xD0};
    std::vector<uint8_t> ram(0x10000, 0);
    std::memcpy(ram.data() + 0x3000, st.data(), st.size());
    seq::Track t = A.parse_track(ram.data(), 0x3000, 4);
    CHECK(t.loops && t.loop_event == 1, "loop to mark: loops=%d loop_event=%d", t.loops, t.loop_event);
    CHECK(t.total_ticks == 12 + 24 + 32 + 12, "ticks %d", t.total_ticks);
    int notes = 0;
    for (const seq::Event& e : t.events) if (e.type == EventType::Note) ++notes;
    CHECK(notes == 4, "notes %d", notes);
    const seq::Event* c = nullptr; const seq::Event* d = nullptr;
    for (const seq::Event& e : t.events) { if (e.type == EventType::Note && e.tick == 0) c = &e; if (e.type == EventType::Note && e.tick == 36) d = &e; }
    CHECK(c && c->pitch == 48 && A.note_name(*c) == "C-4", "C-4 pitch");
    CHECK(d && d->pitch == 50 && d->size == 2 && d->duration == 32, "explicit-length D-4: %s", d ? A.event_text(*d).c_str() : "missing");
    CHECK(A.key_of(0x82) == 4 && A.len_of(0x82) == 9 && A.pack(4, 9) == 0x82, "note packing");
    std::vector<seq::Event> ev = t.events;
    CHECK(A.enter_note(ev, 0, 55, 80), "enter G-4");
    CHECK(ev[3].b[0] == A.pack(7, 9) && ev[3].pitch == 55, "G-4 written: %02X", ev[3].b[0]);
    CHECK(A.transpose_event(ev[3], 2) && ev[3].pitch == 57, "transpose to A-4");
    CHECK(!A.transpose_event(ev[3], 4), "transpose past B-4 must refuse");
    A.retime(ev);
    CHECK(ev[3].duration == 12, "retime keeps the table length");
    CHECK(A.enter_note(ev, 0, 60, 80), "enter C-5 with an octave switch");
    A.retime(ev);
    { int k = -1; for (size_t i = 0; i < ev.size(); ++i) if (ev[i].duration > 0 && ev[i].tick == 0) k = int(i);
      CHECK(k > 0 && ev[size_t(k - 1)].b[0] == 0xC6 && ev[size_t(k - 1)].b[1] == 5 && ev[size_t(k + 1)].b[0] == 0xC6 && ev[size_t(k + 1)].b[1] == 4 && ev[size_t(k)].pitch == 60, "octave 5 set before, 4 restored after"); }
    CHECK(A.set_note_at(ev, 6, A.pack(2, 0), 80), "split C-5 at 6");
    A.retime(ev);
    { int a = -1, b = -1; for (size_t i = 0; i < ev.size(); ++i) { if (ev[i].duration > 0 && ev[i].tick == 0) a = int(i); if (ev[i].duration > 0 && ev[i].tick == 6) b = int(i); }
      CHECK(a >= 0 && b >= 0 && ev[size_t(a)].duration == 6 && ev[size_t(b)].duration == 6 && ev[size_t(b)].size == 1, "6 + 6 from the table"); }
    CHECK(A.set_note_at(ev, 7, A.pack(4, 0), 80), "split at 7 needs an explicit length");
    A.retime(ev);
    { int b = -1; for (size_t i = 0; i < ev.size(); ++i) if (ev[i].duration > 0 && ev[i].tick == 7) b = int(i);
      CHECK(b >= 0 && ev[size_t(b)].size == 2 && ev[size_t(b)].duration == 5 && A.len_of(ev[size_t(b)].b[0]) == 13, "explicit 5-tick note"); }
    int tt = 0; for (const seq::Event& e : ev) tt = std::max(tt, e.tick + e.duration);
    CHECK(tt == 80, "length unchanged by splits: %d", tt);
    std::vector<uint8_t> out = A.serialize_relocated(ev, 0x3100);
    std::memcpy(ram.data() + 0x3100, out.data(), out.size());
    seq::Track t2 = A.parse_track(ram.data(), 0x3100, 4);
    CHECK(t2.loops && t2.total_ticks == 80, "relocated AKAO stream: loops=%d ticks=%d", t2.loops, t2.total_ticks);
}

void test_rare_stream() {
    std::printf("rare (DKC): fixed durations, two-byte durations, call, loop, edits\n");
    rare::RareDriver R(rare::layout_for_tests(rare::Variant::DKC1));
    std::vector<uint8_t> ram(0x10000, 0);
    std::vector<uint8_t> main_s = {0x01, 0x02, 0x9F, 16, 0x06, 8, 0xA0, 0xA2, 0x07, 0x04, 0x02, 0x00, 0x21, 0x2B, 0x80, 0x01, 0x00, 0x2C, 0x03, 0x00, 0x20};
    std::vector<uint8_t> sub_s = {0xA4, 4, 0x05};
    std::memcpy(ram.data() + 0x2000, main_s.data(), main_s.size());
    std::memcpy(ram.data() + 0x2100, sub_s.data(), sub_s.size());
    for (int i = 0; i < 64; ++i) ram[0x11E1 + i * 2] = 0x00;
    seq::Track t = R.parse_track(ram.data(), 0x2000, 0);
    CHECK(t.loops, "loop detected");
    CHECK(t.total_ticks == 296, "ticks %d", t.total_ticks);
    int shared = 0; for (const Event& e : t.events) if (e.in_sub) ++shared;
    CHECK(shared == 2, "sub body twice: %d shared", shared);
    std::vector<Event> ev = t.events;
    CHECK(R.set_note_at(ev, 20, 0x90, 296), "split inside fixed duration");
    R.retime(ev);
    { int a = -1, b = -1, c = -1;
      for (size_t i = 0; i < ev.size(); ++i) { if (ev[i].duration > 0 && ev[i].tick == 16) a = int(i); if (ev[i].duration > 0 && ev[i].tick == 20) b = int(i); if (ev[i].duration > 0 && ev[i].tick == 24) c = int(i); }
      CHECK(a > 0 && ev[size_t(a - 1)].b[0] == 0x07 && ev[size_t(a)].size == 2 && ev[size_t(a)].duration == 4, "07 + explicit 4");
      CHECK(b > 0 && ev[size_t(b)].b[0] == 0x90 && ev[size_t(b)].duration == 4, "new note 4 ticks");
      CHECK(c > 0 && ev[size_t(c)].size == 1 && ev[size_t(c)].duration == 8, "fixed duration restored for the next note (size %d dur %d)", c > 0 ? ev[size_t(c)].size : -1, c > 0 ? ev[size_t(c)].duration : -1); }
    CHECK(R.set_note_at(ev, 32, 0x91, 296), "sub note replaced in place");
    CHECK(R.set_note_at(ev, 34, 0x91, 296), "sub note split after unrolling");
    { int shared2 = 0, calls = 0; for (const Event& e : ev) { if (e.in_sub) ++shared2; if (e.type == EventType::Command && e.b[0] == 0x04) { ++calls; CHECK(e.b[1] == 1, "remaining call counts %d", e.b[1]); } }
      CHECK(shared2 == 1 && calls == 1, "unrolled one pass: %d shared, %d calls left", shared2, calls); }
    CHECK(R.remove_span(ev, 60, 156, false), "remove 156 ticks of the long rest");
    R.retime(ev);
    int tt = 0; for (const Event& e : ev) tt = std::max(tt, e.tick + e.duration);
    CHECK(tt == 140, "length now %d", tt);
    uint8_t vol[3] = {0x02, 0x40, 0x40};
    CHECK(R.insert_command_at(ev, 20, vol, 3), "insert a volume command at 20");
    std::vector<uint8_t> out = R.serialize_relocated(ev, 0x2300);
    std::memcpy(ram.data() + 0x2300, out.data(), out.size());
    seq::Track t2 = R.parse_track(ram.data(), 0x2300, 0);
    CHECK(t2.loops && t2.total_ticks == 140, "relocated: loops=%d ticks=%d", t2.loops, t2.total_ticks);
    int n2 = 0, vols = 0; for (const Event& e : t2.events) { if (e.type == EventType::Note) ++n2; if (e.type == EventType::Command && e.b[0] == 0x02) ++vols; }
    CHECK(n2 == 4 + 2 + 1 && vols == 1, "relocated stream: %d notes (4 main + inline pass split in 2 + 1 called), %d volume commands", n2, vols);
}

void test_capcom_stream() {
    std::printf("capcom (MMX): note bytes, dotted, triplet, loop with break, tied splits\n");
    capcom::CapcomDriver C(capcom::layout_for_tests());
    std::vector<uint8_t> ram(0x10000, 0);
    std::vector<uint8_t> bytes = {0x08, 0x02, 0x09, 0x04, 0x81, 0x02, 0x83, 0x00, 0x85, 0x00,
                                  0x60, 0x12, 0x00, 0x20, 0x14, 0x88, 0x0E, 0x01, 0x20, 0x0A,
                                  0x8A, 0x16, 0x20, 0x00};
    std::memcpy(ram.data() + 0x2000, bytes.data(), bytes.size());
    seq::Track t = C.parse_track(ram.data(), 0x2000, 0);
    CHECK(t.loops && t.terminated, "loop detected");
    CHECK(t.total_ticks == 148, "ticks %d", t.total_ticks);
    int shared = 0, notes = 0; for (const Event& e : t.events) { if (e.in_sub) ++shared; if (e.type == EventType::Note) ++notes; }
    CHECK(shared == 2 && notes == 5, "second loop pass shared: %d shared, %d notes", shared, notes);
    { int i = -1; for (size_t k = 0; k < t.events.size(); ++k) if (t.events[k].type == EventType::Note && t.events[k].tick == 24) i = int(k);
      CHECK(i >= 0 && t.events[size_t(i)].duration == 36 && t.events[size_t(i)].pitch == 48 + 2, "dotted D-4 lasts 36: dur %d pitch %d", i >= 0 ? t.events[size_t(i)].duration : -1, i >= 0 ? t.events[size_t(i)].pitch : -1); }
    std::vector<Event> ev = t.events;
    CHECK(C.set_note_at(ev, 15, 0x23, 148), "split at 15");
    C.retime(ev);
    { std::vector<int> durs; int slurs = 0;
      for (const Event& e : ev) { if (e.tick < 24 && e.duration > 0) durs.push_back(e.duration); if (e.tick <= 24 && e.type == EventType::Command && e.b[0] == 0x01) ++slurs; }
      CHECK(durs.size() == 3 && durs[0] + durs[1] == 15 && durs[2] == 9, "pieces %zu: %d %d %d", durs.size(), durs.size() > 0 ? durs[0] : -1, durs.size() > 1 ? durs[1] : -1, durs.size() > 2 ? durs[2] : -1);
      CHECK(slurs == 2, "slur on and off around the tie: %d", slurs); }
    int tt = 0; for (const Event& e : ev) tt = std::max(tt, e.tick + e.duration);
    CHECK(tt == 148, "length kept: %d", tt);
    CHECK(C.set_note_at(ev, 24 + 36 + 16 + 36 + 6, 0x21, 148), "loop pass 2 rest split after unrolling");
    { int shared2 = 0, loops = 0; for (const Event& e : ev) { if (e.in_sub) ++shared2; if (e.type == EventType::Command && e.b[0] >= 0x0E && e.b[0] <= 0x15) ++loops; }
      CHECK(shared2 == 0 && loops == 0, "unrolled: %d shared, %d loop commands left", shared2, loops); }
    C.retime(ev);
    tt = 0; for (const Event& e : ev) tt = std::max(tt, e.tick + e.duration);
    CHECK(tt == 148, "length kept through the unroll: %d", tt);
    CHECK(C.enter_note(ev, 24, 48 + 2 + 24, 148), "enter D-6 over the dotted D");
    C.retime(ev);
    { int i = -1; for (size_t k = 0; k < ev.size(); ++k) if (ev[k].type == EventType::Note && ev[k].tick == 24) i = int(k);
      CHECK(i > 0 && ev[size_t(i)].pitch == 48 + 26 && ev[size_t(i)].duration == 36, "D-6 dotted: pitch %d dur %d", i > 0 ? ev[size_t(i)].pitch : -1, i > 0 ? ev[size_t(i)].duration : -1);
      int j = -1; for (size_t k = 0; k < ev.size(); ++k) if (ev[k].type == EventType::Note && ev[k].tick == 60) j = int(k);
      CHECK(j > 0 && ev[size_t(j)].pitch == 48 + 4, "next note keeps its octave: pitch %d", j > 0 ? ev[size_t(j)].pitch : -1); }
    std::vector<uint8_t> out = C.serialize_relocated(ev, 0x2300);
    std::memcpy(ram.data() + 0x2300, out.data(), out.size());
    seq::Track t2 = C.parse_track(ram.data(), 0x2300, 0);
    CHECK(t2.loops && t2.total_ticks == 148, "relocated: loops=%d ticks=%d", t2.loops, t2.total_ticks);
    int n2 = 0; for (const Event& e : t2.events) if (e.type == EventType::Note && !e.in_sub) ++n2;
    CHECK(n2 == 5 + 2 + 1, "relocated stream (tie split + unrolled pass split): %d notes", n2);
    std::vector<Event> ev3 = t.events;
    CHECK(C.remove_span(ev3, 24, 36, false), "remove the dotted D");
    C.retime(ev3);
    { int i = -1; for (size_t k = 0; k < ev3.size(); ++k) if (ev3[k].type == EventType::Note && ev3[k].tick == 24) i = int(k);
      CHECK(i >= 0 && ev3[size_t(i)].pitch == 48 + 4 && ev3[size_t(i)].duration == 16, "E-4 follows undotted: pitch %d dur %d", i >= 0 ? ev3[size_t(i)].pitch : -1, i >= 0 ? ev3[size_t(i)].duration : -1); }
    std::vector<Event> ev4 = t.events;
    CHECK(C.insert_span(ev4, 24, 6, C.rest_byte()), "insert a rest before the dotted D");
    C.retime(ev4);
    { int i = -1; for (size_t k = 0; k < ev4.size(); ++k) if (ev4[k].type == EventType::Note && ev4[k].tick == 30) i = int(k);
      CHECK(i >= 0 && ev4[size_t(i)].duration == 36, "D-4 stays dotted after the rest: dur %d", i >= 0 ? ev4[size_t(i)].duration : -1); }
}

void test_live_edit(const char* env, const char* label) {
    const char* path = std::getenv(env);
    if (!path) { std::printf("%s: set %s=<file> to test live edits on a real rip\n", label, env); return; }
    std::printf("%s: live edits on %s\n", label, path);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 0);
    SpcFile f;
    if (std::string err = load_spc_file(path, f); !err.empty()) { CHECK(false, "load: %s", err.c_str()); return; }
    Engine eng;
    if (std::string err = eng.init(); !err.empty()) { CHECK(false, "engine init: %s", err.c_str()); return; }
    CHECK(eng.load(f).empty(), "engine load");
    eng.run_silent(0.02);
    EngineSnapshot s;
    eng.snapshot(s);
    Tracker T;
    T.analyze(s);
    if (!T.drv || !T.song()) { CHECK(false, "no driver/song"); return; }
    T.reclaim_other_songs = true;
    const seq::Driver& D = *T.drv;
    seq::Pattern& pat = T.song()->patterns[0];
    int v = -1, tick = -1, semitone = -1;
    for (int cand = 0; cand < 8 && v < 0; ++cand) {
        for (const Event& e : pat.tracks[cand].events)
            if (e.type == EventType::Note && !e.in_sub && e.duration >= 4 && D.event_semitone(e) >= 12) { v = cand; tick = e.tick + 2; semitone = D.event_semitone(e) + 2; break; }
    }
    if (v < 0) { CHECK(false, "no splittable note"); return; }
    std::vector<Event> ev(pat.tracks[v].events.begin(), pat.tracks[v].events.begin() + pat.tracks[v].used_events);
    const int before = pat.length_ticks;
    const seq::Position image_before = D.locate(eng.image_ram(), *T.song(), nullptr);
    const int old_dur = image_before.valid && image_before.voice_event[v] >= 0 ? pat.tracks[v].events[size_t(image_before.voice_event[v])].duration : 0;
    CHECK(D.enter_note(ev, tick, semitone, before), "enter a note mid-note on v%d at tick %d", v, tick);
    Tracker::Result r = T.write_track(eng, 0, v, ev);
    CHECK(r.ok, "write_track: %s", r.msg.c_str());
    std::printf("  v%d tick %d: %s\n", v, tick, r.msg.c_str());
    {
        seq::Position ip = D.locate(eng.image_ram(), *T.song(), nullptr);
        CHECK(ip.valid == image_before.valid, "image pointer of v%d still locates", v);
        if (ip.valid && image_before.valid && ip.voice_event[v] >= 0 && image_before.voice_event[v] >= 0) {
            const Event& now = T.song()->patterns[0].tracks[v].events[size_t(ip.voice_event[v])];
            const int end_before = image_before.voice_tick[v] + old_dur;
            CHECK(now.tick + now.duration == end_before, "image pointer of v%d fetches at tick %d, was %d", v, now.tick + now.duration, end_before);
        }
    }
    int found = -1;
    for (const Event& e : T.song()->patterns[0].tracks[v].events) if (e.type == EventType::Note && e.tick == tick && !e.in_sub) { found = D.event_semitone(e); break; }
    CHECK(found == semitone, "re-parsed stream has the note at %d (semitone %d, wanted %d)", tick, found, semitone);
    CHECK(T.song()->patterns[0].length_ticks == before, "song length kept: %d -> %d", before, T.song()->patterns[0].length_ticks);
    const int voice_len = T.song()->patterns[0].tracks[v].total_ticks;
    if (std::getenv("BOOMSPC_DEBUG_EDIT")) { std::fprintf(stderr, "  song %s v%d len %d\n", T.song()->label.c_str(), v, voice_len); for (const Event& e : T.song()->patterns[0].tracks[v].events) if (e.type == EventType::Command && D.cmd_class(e.b[0]) == seq::FxClass::Song) std::fprintf(stderr, "    t%d %04X %s%s\n", e.tick, e.addr, e.in_sub ? "S " : "", D.event_text(e).c_str()); }
    auto events_of = [&]() { const seq::Track& tr = T.song()->patterns[0].tracks[v]; return std::vector<Event>(tr.events.begin(), tr.events.begin() + tr.used_events); };
    std::vector<Event> ev2 = events_of();
    if (D.insert_span(ev2, tick, 3, D.rest_byte())) {
        Tracker::Result r2 = T.write_track(eng, 0, v, ev2);
        CHECK(r2.ok, "insert row: %s", r2.msg.c_str());
        const int grown = T.song()->patterns[0].tracks[v].total_ticks - voice_len;
        CHECK(grown >= 3 && grown % 3 == 0, "voice grew by a multiple of 3: %d -> %d", voice_len, T.song()->patterns[0].tracks[v].total_ticks);
        std::vector<Event> ev3 = events_of();
        CHECK(D.remove_span(ev3, tick, 3, false), "pull the row back out");
        Tracker::Result r3 = T.write_track(eng, 0, v, ev3);
        if (std::getenv("BOOMSPC_DEBUG_EDIT")) { std::fprintf(stderr, "  after remove: %s -> song %s (%zu songs)\n  hdr:", r3.msg.c_str(), T.song()->label.c_str(), T.songs.size()); EngineSnapshot s3; eng.snapshot(s3); for (int i = 0; i < 20; ++i) std::fprintf(stderr, " %02X", s3.ram[T.song()->order_addr + i]); std::fprintf(stderr, "  v0 %04X\n", T.song()->patterns[0].tracks[v].addr); }
        CHECK(r3.ok && T.song()->patterns[0].tracks[v].total_ticks == voice_len, "length after insert+remove: %d (was %d)", T.song()->patterns[0].tracks[v].total_ticks, voice_len);
        if (std::getenv("BOOMSPC_DEBUG_EDIT") && T.song()->patterns[0].tracks[v].total_ticks != voice_len) { const seq::Track& tr = T.song()->patterns[0].tracks[v]; for (size_t i = tr.events.size() > 8 ? tr.events.size() - 8 : 0; i < tr.events.size(); ++i) std::fprintf(stderr, "    end %04X+%d t%d %s\n", tr.events[i].addr, tr.events[i].size, tr.events[i].tick, D.event_text(tr.events[i]).c_str()); }
    } else std::printf("  (insert_span not possible at tick %d on this stream)\n", tick);
    {
        int sv = -1, stick = -1, ssemi = -1;
        for (int cand = 0; cand < 8 && sv < 0; ++cand)
            for (const Event& e : T.song()->patterns[0].tracks[cand].events)
                if (e.type == EventType::Note && e.in_sub && e.duration >= 4 && D.event_semitone(e) >= 12) { sv = cand; stick = e.tick + 2; ssemi = D.event_semitone(e) + 1; break; }
        if (sv >= 0) {
            const seq::Track& st = T.song()->patterns[0].tracks[sv];
            std::vector<Event> sev(st.events.begin(), st.events.begin() + st.used_events);
            CHECK(D.enter_note(sev, stick, ssemi, T.song()->patterns[0].length_ticks), "note inside shared bytes on v%d at tick %d", sv, stick);
            bool still_shared = false;
            for (const Event& e : sev) if (e.in_sub && e.duration > 0 && e.tick <= stick && stick < e.tick + e.duration) still_shared = true;
            CHECK(!still_shared, "the edited tick is plain data after the unroll");
            Tracker::Result rs = T.write_track(eng, 0, sv, sev);
            CHECK(rs.ok, "write unrolled voice: %s", rs.msg.c_str());
            std::printf("  v%d tick %d (shared): %s\n", sv, stick, rs.msg.c_str());
            int found = -1;
            for (const Event& e : T.song()->patterns[0].tracks[sv].events) if (e.type == EventType::Note && e.tick == stick) { found = D.event_semitone(e); break; }
            CHECK(found == ssemi, "re-parsed unrolled voice has the note at %d (semitone %d, wanted %d)", stick, found, ssemi);
        } else std::printf("  (no shared notes in this rip's first pattern)\n");
    }
    const char* tmp = "/tmp/boomspc_edited.spc";
    CHECK(eng.export_spc(tmp).empty(), "export");
    const char* pc = std::getenv("PARSECHECK") ? std::getenv("PARSECHECK") : "build/parsecheck";
    std::string cmd = std::string(pc) + " '" + tmp + "' 8 > /tmp/boomspc_parsecheck_" + label + ".txt 2>&1";
    int rc = std::system(cmd.c_str());
    CHECK(rc == 0, "parsecheck on the edited rip failed (see /tmp/boomspc_parsecheck_%s.txt)", label);
    if (rc == 0) std::remove(tmp); else std::printf("  (edited rip kept at %s)\n", tmp);
}

void test_snsf_live_edit() {
    const char* path = std::getenv("WW_SNSF");
    if (!path) { std::printf("wario: set WW_SNSF=<file.minisnsf> to test the SNES-side engine\n"); return; }
    std::printf("wario: live edits on %s\n", path);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 0);
    SnsfFile f;
    if (std::string err = load_snsf_file(path, f); !err.empty()) { CHECK(false, "load: %s", err.c_str()); return; }
    Engine eng;
    if (std::string err = eng.init(); !err.empty()) { CHECK(false, "engine init: %s", err.c_str()); return; }
    CHECK(eng.load_snsf(f).empty(), "engine load_snsf");
    eng.run_silent(3.0);
    EngineSnapshot s;
    eng.snapshot(s);
    Tracker T;
    T.analyze(s);
    if (!T.drv || !T.song()) { CHECK(false, "no SNES-side driver / song"); return; }
    const seq::Driver& D = *T.drv;
    int song = D.pick_current_song(s.ram, T.songs);
    CHECK(song >= 0, "current song");
    T.song_index = song; T.song_pinned = true;
    eng.set_bank_window(D.song_bank(*T.song()));
    eng.snapshot(s);
    T.reparse(s);
    std::printf("  %s: %zu songs, playing %s\n", D.name().c_str(), T.songs.size(), T.song()->label.c_str());
    seq::Position p0 = D.locate(s.ram, *T.song(), nullptr);
    CHECK(p0.valid, "live position located in the parse");
    seq::Pattern& pat = T.song()->patterns[0];
    int v = -1, tick = -1, semitone = -1;
    for (int pass = 0; pass < 2 && v < 0; ++pass)
        for (int cand = 0; cand < 8 && v < 0; ++cand)
            for (const Event& e : pat.tracks[cand].events)
                if (e.type == EventType::Note && (pass == 1 || !e.in_sub) && e.duration >= 4) { v = cand; tick = e.tick + 2; semitone = D.event_semitone(e) + 2; break; }
    if (v < 0) { CHECK(false, "no splittable note"); return; }
    T.reclaim_other_songs = false;
    std::vector<Event> ev(pat.tracks[v].events.begin(), pat.tracks[v].events.begin() + pat.tracks[v].used_events);
    const int before = pat.length_ticks;
    CHECK(D.enter_note(ev, tick, semitone, before), "enter a note on v%d at tick %d", v, tick);
    Tracker::Result r = T.write_track(eng, 0, v, ev);
    CHECK(r.ok, "write_track: %s", r.msg.c_str());
    std::printf("  v%d tick %d: %s\n", v, tick, r.msg.c_str());
    int found = -1;
    for (const Event& e : T.song()->patterns[0].tracks[v].events) if (e.type == EventType::Note && e.tick == tick && !e.in_sub) { found = D.event_semitone(e); break; }
    CHECK(found == semitone, "re-parsed stream has the note at %d (semitone %d, wanted %d)", tick, found, semitone);
    CHECK(T.song()->patterns[0].length_ticks == before, "song length kept: %d -> %d", before, T.song()->patterns[0].length_ticks);
    int last = -1, forward = 0, lost = 0;
    for (int i = 0; i < 20; ++i) {
        eng.run_silent(0.1);
        eng.snapshot(s);
        seq::Position p = D.locate(s.ram, *T.song(), nullptr);
        if (!p.valid) { ++lost; continue; }
        int o = -1; for (int w = 0; w < 8; ++w) o = std::max(o, p.voice_tick[w]);
        if (o > last) ++forward;
        last = o;
    }
    CHECK(lost == 0 && forward >= 10, "playback follows the edited stream: %d located, %d forward steps", 20 - lost, forward);
    const char* tmp = "/tmp/boomspc_edited.snsf";
    CHECK(eng.export_snsf(tmp).empty(), "export snsf");
    SnsfFile g;
    CHECK(load_snsf_file(tmp, g).empty() && g.rom.size() == f.rom.size(), "exported set reloads");
    std::remove(tmp);
}

void test_akao_detect_smrpg() {
    const char* path = std::getenv("SMRPG_SPC");
    if (!path) { std::printf("akao: set SMRPG_SPC=<file> to test detection on a real rip\n"); return; }
    std::printf("akao: detection on %s\n", path);
    FILE* f = std::fopen(path, "rb");
    if (!f) { CHECK(false, "cannot open"); return; }
    std::vector<uint8_t> file(0x10100);
    size_t n = std::fread(file.data(), 1, file.size(), f);
    std::fclose(f);
    CHECK(n >= 0x10100, "short file");
    const uint8_t* ram = file.data() + 0x100;
    akao::Layout L = akao::detect_layout(ram);
    CHECK(L.valid() && L.variant == akao::Variant::SMRPG, "not detected as SMRPG");
    CHECK(L.first_cmd == 0xC4 && L.cmd_table == 0x173F && L.len_table == 0x17B9 && L.note_len_table == 0x17F5, "tables");
    CHECK(L.header == 0x2000 && L.track_ptr_base == 0x1BFC && L.octave_base == 0x183C, "header %04X ptrs %04X oct %04X", L.header, L.track_ptr_base, L.octave_base);
    CHECK(L.inst_map == 0x4680 && L.sample_pairs == 0x4700 && L.adsr_pairs == 0x4740 && L.tune_pairs == 0x4780, "instrument tables %04X %04X %04X %04X", L.inst_map, L.sample_pairs, L.adsr_pairs, L.tune_pairs);
    akao::AkaoDriver A(L);
    std::vector<seq::Song> songs = A.find_songs(ram, nullptr);
    CHECK(songs.size() == 1 && songs[0].patterns[0].length_ticks > 100, "song parse");
}

}

int main() {
    test_rare_stream();
    test_live_edit("DKC_SPC", "rare");
    test_live_edit("KI_SPC", "rare-ki");
    test_live_edit("PLOK_SPC", "follin");
    test_live_edit("SMRPG_SPC", "akao");
    test_snsf_live_edit();
    test_live_edit("MMX_SPC", "capcom");
    test_live_edit("KONAMI_SPC", "konami");
    test_live_edit("KONAMI1_SPC", "konami-v1");
    test_live_edit("HUDSON_SPC", "hudson");
    test_live_edit("CHUN_SPC", "chun");
    test_live_edit("MINT_SPC", "mint");
    test_live_edit("COMPILE_SPC", "compile");
    test_live_edit("PANDORA_SPC", "pandora");
    test_live_edit("PRISM_SPC", "prism");
    test_live_edit("GRAPHRES_SPC", "graphres");
    test_live_edit("ASCII_SPC", "ascii");
    test_live_edit("FALCOM_SPC", "falcom");
    test_live_edit("HEARTBEAT_SPC", "heartbeat");
    test_capcom_stream();
    test_akao_stream();
    test_akao_detect_smrpg();
    test_project_roundtrip();
    test_follin_stream();
    test_follin_slide();
    test_follin_detect_plok();
    test_sequence_like_ui();
    test_insert_command_mid_note();
    test_set_qv();
    test_set_qv_no_prior_qv();
    test_set_instrument();
    test_remove_span_keep();
    test_remove_span_partial();
    test_insert_span();
    test_insert_span_mid_note();
    test_set_note_split_keeps_qv();
    if (g_fail) { std::printf("%d failure(s)\n", g_fail); return 1; }
    std::printf("all edit tests passed\n");
    return 0;
}
