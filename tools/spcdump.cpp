// spcdump: prints what the N-SPC parser sees in an SPC.
//   spcdump file.spc [--run SECONDS] [--tracks] [--song N]
// --run emulates for a while first so live pointers exist (AddmusicK dumps
// start with an empty zero page).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "SNES_SPC.h"
#include "driver/akao.hpp"
#include "driver/capcom.hpp"
#include "driver/follin.hpp"
#include "driver/nspc.hpp"
#include "spc700.hpp"
#include "spc_file.hpp"

int dump_generic(const seq::Driver& F, const uint8_t* ram, bool tracks) {
    if (const auto* FL = dynamic_cast<const follin::FollinDriver*>(&F)) {
        const follin::Layout& L = FL->L;
        std::printf("  voice ptrs @%04X  song tables %04X/%04X (%d slots)  pitch %04X/%04X  transpose %04X  tune %04X (%d instruments)  tempo $%02X = %.1f ticks/s\n",
                    L.voice_ptr_base, L.song_lo[0], L.song_hi[0], L.slots, L.pitch_lo, L.pitch_hi, L.transpose_table, L.mult_table,
                    L.instrument_count, ram[L.tempo_addr], F.ticks_per_second(ram));
    } else if (const auto* AK = dynamic_cast<const akao::AkaoDriver*>(&F)) {
        const akao::Layout& L = AK->L;
        std::printf("  first cmd %02X  cmd table %04X  len table %04X  note lengths @%04X (%d)  header %04X%s  live ptrs @%04X  %.1f ticks/s\n",
                    L.first_cmd, L.cmd_table, L.len_table, L.note_len_table, L.note_len_count, L.header,
                    L.rom_addresses ? " (ROM addresses)" : "", L.track_ptr_base + AK->bgm_slot(ram) * 2, F.ticks_per_second(ram));
        if (L.bgm_slot_var) std::printf("  song on logical voices %d-%d ($%02X)\n", AK->bgm_slot(ram), AK->bgm_slot(ram) + 7, L.bgm_slot_var);
    } else if (const auto* CP = dynamic_cast<const capcom::CapcomDriver*>(&F)) {
        const capcom::Layout& L = CP->L;
        std::printf("  cmd table %04X  ptrs $%02X/$%02X  ctl $%02X  durations %04X/%04X/%04X  octaves %04X  pitch %04X  instruments %04X (%d)  header %04X  list %04X  tempo $%02X = %.1f ticks/s\n",
                    L.cmd_table, L.ptr_lo, L.ptr_hi, L.ctl_zp, L.dur_normal, L.dur_dotted, L.dur_triplet, L.octave_table, L.pitch_table, L.ins_table,
                    F.instrument_count(ram), L.bgm_header, L.song_list, L.tempo_zp, F.ticks_per_second(ram));
    }
    auto songs = F.find_songs(ram, nullptr);
    int cur = F.pick_current_song(ram, songs);
    for (size_t i = 0; i < songs.size(); ++i) {
        const seq::Pattern& p = songs[i].patterns[0];
        std::printf("%c %s:", int(i) == cur ? '*' : ' ', songs[i].label.c_str());
        for (int v = 0; v < 8; ++v)
            if (p.tracks[v].addr) std::printf(" v%d@%04X:%d%s%s", v, p.tracks[v].addr, p.tracks[v].total_ticks, p.tracks[v].loops ? "" : " (ends)", p.tracks[v].truncated ? "!" : "");
        std::printf("\n");
    }
    if (cur < 0) return 0;
    seq::Position pos = F.locate(ram, songs[size_t(cur)], nullptr);
    for (int v = 0; v < 8; ++v)
        if (pos.voice_ptr[v]) std::printf("  v%d ptr %04X event %d tick %d\n", v, pos.voice_ptr[v], pos.voice_event[v], pos.voice_tick[v]);
    if (!tracks) return 0;
    const seq::Pattern& p = songs[size_t(cur)].patterns[0];
    for (int v = 0; v < 8; ++v) {
        const seq::Track& t = p.tracks[v];
        if (!t.addr) continue;
        std::printf("  v%d:", v);
        int col = 0;
        for (const seq::Event& e : t.events) {
            if (e.sub_iter > 0) continue;
            std::string txt;
            char buf[64];
            switch (e.type) {
                case seq::EventType::Note: std::snprintf(buf, sizeof buf, "%s/%d", F.note_name(e).c_str(), e.duration); txt = buf; break;
                case seq::EventType::Rest: std::snprintf(buf, sizeof buf, "--/%d", e.duration); txt = buf; break;
                case seq::EventType::End: txt = "END"; break;
                case seq::EventType::Tie: std::snprintf(buf, sizeof buf, "^^/%d", e.duration); txt = buf; break;
                default: {
                    std::snprintf(buf, sizeof buf, "[%s", F.cmd_code(e.b[0])); txt = buf;
                    for (int i = 1; i < e.size && i < 6; ++i) { std::snprintf(buf, sizeof buf, " %02X", e.b[i]); txt += buf; }
                    txt += "]";
                }
            }
            std::printf(" %s", txt.c_str());
            if (++col % 20 == 0) std::printf("\n     ");
        }
        std::printf("\n");
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s file.spc [--run SECONDS] [--tracks] [--song N] [--disasm HEXADDR[:COUNT]]\n", argv[0]); return 2; }
    double run = 0; bool tracks = false;
    int disasm_at = -1, disasm_n = 32;
    int song_arg = -1;                 // --song N: dump that song instead of the one the driver is on
    const char* dump_path = nullptr;   // --dumpram PATH: 64K RAM + 128 DSP bytes + PC (2) after --run
    for (int i = 2; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--run") && i + 1 < argc) run = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--tracks")) tracks = true;
        else if (!std::strcmp(argv[i], "--song") && i + 1 < argc) song_arg = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--dumpram") && i + 1 < argc) dump_path = argv[++i];
        else if (!std::strcmp(argv[i], "--disasm") && i + 1 < argc) {
            disasm_at = int(std::strtol(argv[++i], nullptr, 16));
            if (const char* c = std::strchr(argv[i], ':')) disasm_n = std::atoi(c + 1);
        }
    }

    SpcFile file;
    if (std::string err = load_spc_file(argv[1], file); !err.empty()) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }

    std::vector<uint8_t> ram(file.ram(), file.ram() + 0x10000);
    std::vector<uint8_t> dsp(file.dsp(), file.dsp() + 128);
    if (run > 0) {
        SNES_SPC spc;
        spc.init();
        spc.load_spc(file.data.data(), long(file.data.size()));
        int total = int(run * double(SNES_SPC::sample_rate)) * 2;
        while (total > 0) { int n = std::min(total, 4096); spc.play(n, nullptr); total -= n; }
        std::memcpy(ram.data(), spc.ram(), 0x10000);
        for (int i = 0; i < 128; ++i) dsp[i] = uint8_t(spc.dsp_ref().read(i));
        if (dump_path) {
            FILE* f = std::fopen(dump_path, "wb");
            if (f) {
                std::fwrite(ram.data(), 1, 0x10000, f);
                std::fwrite(dsp.data(), 1, 128, f);
                uint8_t pc[2] = {uint8_t(spc.cpu_pc() & 0xFF), uint8_t(spc.cpu_pc() >> 8)};
                std::fwrite(pc, 1, 2, f);
                std::fclose(f);
            }
        }
    }

    if (disasm_at >= 0) {
        uint16_t a = uint16_t(disasm_at);
        for (int i = 0; i < disasm_n; ++i) {
            spc700::Insn in = spc700::disassemble(ram.data(), a);
            std::printf("%04X  ", a);
            for (int k = 0; k < 3; ++k) std::printf(k < in.len ? "%02X " : "   ", in.bytes[k]);
            std::printf(" %s\n", in.text.c_str());
            a = uint16_t(a + in.len);
        }
        return 0;
    }

    std::printf("%s: \"%s\"\n", argv[1], file.title.c_str());
    std::unique_ptr<seq::Driver> drv = seq::detect_driver(ram.data(), dsp.data());
    std::printf("driver: %s\n", drv ? drv->name().c_str() : "unknown");
    if (!drv) return 0;
    const auto* N = dynamic_cast<const nspc::NspcDriver*>(drv.get());
    if (!N) return dump_generic(*drv, ram.data(), tracks);
    const nspc::Layout& L = N->L;
    std::printf("  cmd base %02X  len table %04X  inst table %04X (stride %d)  perc table %04X  tempo @$%02X = %.1f ticks/s\n",
                L.cmd_base, L.len_table, L.inst_table, L.inst_stride, L.perc_table, L.tempo_addr, N->ticks_per_second(ram.data()));

    auto songs = nspc::find_songs(ram.data(), L, dsp.data());
    int cur = song_arg >= 0 && song_arg < int(songs.size()) ? song_arg : nspc::pick_current_song(ram.data(), L, songs);
    std::printf("songs found: %zu\n", songs.size());
    for (size_t i = 0; i < songs.size(); ++i) {
        const nspc::Song& s = songs[i];
        std::printf("%c song %zu @%04X: %zu orders, %zu patterns, %d ticks, loop ", int(i) == cur ? '*' : ' ',
                    i, s.order_addr, s.orders.size(), s.patterns.size(), s.total_ticks());
        if (s.loop_to >= 0) std::printf("%dx -> %d\n", s.loop_count, s.loop_to); else std::printf("none\n");
    }
    if (cur < 0) return 0;

    const nspc::Song& s = songs[cur];
    nspc::Position pos = nspc::locate(ram.data(), L, s);
    std::printf("position: valid=%d order=%d trackptrs@%04X orderptr@%04X\n", pos.valid, pos.order_index,
                pos.track_ptr_base, pos.order_ptr_addr);
    for (int v = 0; v < 8; ++v)
        if (pos.voice_ptr[v]) std::printf("  v%d ptr %04X event %d tick %d\n", v, pos.voice_ptr[v], pos.voice_event[v], pos.voice_tick[v]);

    int ni = nspc::instrument_count(ram.data(), L);
    std::printf("instruments: %d\n", ni);
    for (int i = 0; i < ni; ++i) {
        nspc::Instrument in = nspc::read_instrument(ram.data(), L, i);
        std::printf("  %2d: srcn %02X adsr %02X %02X gain %02X pitch %02X.%02X\n", i, in.srcn, in.adsr0, in.adsr1, in.gain, in.pitch_hi, in.pitch_lo);
    }

    for (size_t pi = 0; pi < s.patterns.size(); ++pi) {
        const nspc::Pattern& p = s.patterns[pi];
        std::printf("pattern %zu @%04X: %d ticks |", pi, p.addr, p.length_ticks);
        for (int v = 0; v < 8; ++v)
            if (p.tracks[v].addr) std::printf(" v%d@%04X-%04X:%d%s%s", v, p.tracks[v].addr, p.tracks[v].end_addr, p.tracks[v].total_ticks,
                                             p.tracks[v].truncated ? "!" : "", p.tracks[v].terminated ? "" : "~");
        std::printf("\n");
        if (!tracks) continue;
        for (int v = 0; v < 8; ++v) {
            const nspc::Track& t = p.tracks[v];
            if (!t.addr) continue;
            std::printf("  v%d:", v);
            int col = 0;
            for (int ei = 0; ei < t.used_events; ++ei) {
                const nspc::Event& e = t.events[ei];
                if (e.in_sub && e.sub_iter) continue;
                std::string txt;
                char buf[64];
                switch (e.type) {
                    case nspc::EventType::Length: std::snprintf(buf, sizeof buf, e.size == 2 ? "l%02X/%02X" : "l%02X", e.b[0], e.b[1]); txt = buf; break;
                    case nspc::EventType::Note: txt = nspc::note_name(nspc::note_semitone(L, e.b[0])); break;
                    case nspc::EventType::Tie: txt = "^^"; break;
                    case nspc::EventType::Rest: txt = "--"; break;
                    case nspc::EventType::Percussion: std::snprintf(buf, sizeof buf, "P%d", e.b[0] - L.perc_base); txt = buf; break;
                    case nspc::EventType::SubCall: std::snprintf(buf, sizeof buf, "SUB(%02X%02X x%d)", e.b[2], e.b[1], e.b[3]); txt = buf; break;
                    case nspc::EventType::Command: {
                        std::snprintf(buf, sizeof buf, "[%s", L.cmd_name(e.b[0])); txt = buf;
                        for (int i = 1; i < e.size; ++i) { std::snprintf(buf, sizeof buf, " %02X", e.b[i]); txt += buf; }
                        txt += "]";
                        break;
                    }
                    case nspc::EventType::End: txt = "END"; break;
                }
                if (e.in_sub) txt = "(" + txt + ")";
                std::printf(" %s", txt.c_str());
                if (++col % 24 == 0) std::printf("\n     ");
            }
            std::printf("\n");
        }
    }
    return 0;
}
