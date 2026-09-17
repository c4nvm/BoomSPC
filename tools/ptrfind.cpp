// ptrfind: plays an SPC and lists RAM words that walk through an address
// range, to find a driver's live sequence pointers.
//   ptrfind file.spc seconds lo hi [min_distinct]
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "SNES_SPC.h"
#include "spc_file.hpp"

int main(int argc, char** argv) {
    if (argc < 5) { std::fprintf(stderr, "usage: ptrfind file.spc seconds lo hi [min_distinct]\n"); return 2; }
    SpcFile f;
    if (std::string err = load_spc_file(argv[1], f); !err.empty()) { std::fprintf(stderr, "%s\n", err.c_str()); return 2; }
    const double secs = std::atof(argv[2]);
    const int lo = int(std::strtol(argv[3], nullptr, 16)), hi = int(std::strtol(argv[4], nullptr, 16));
    const int min_distinct = argc > 5 ? std::atoi(argv[5]) : 8;
    SNES_SPC spc;
    spc.init();
    spc.load_spc(f.data.data(), long(f.data.size()));
    struct Stat { int distinct = 0, ups = 0, downs = 0, out = 0, last = -1, first = -1, min = 0x10000, max = -1; };
    std::vector<Stat> st(0x10000);
    for (long s = 0; s < long(secs * 32000); s += 64) {
        spc.play(128, nullptr);
        const uint8_t* ram = spc.ram();
        for (int a = 0; a < 0xFFFF; ++a) {
            int w = ram[a] | (ram[a + 1] << 8);
            Stat& t = st[size_t(a)];
            if (w == t.last) continue;
            if (w < lo || w > hi) { ++t.out; t.last = w; continue; }
            if (t.first < 0) t.first = w;
            if (t.last >= lo && t.last <= hi) { if (w > t.last) ++t.ups; else ++t.downs; }
            ++t.distinct;
            if (w < t.min) t.min = w;
            if (w > t.max) t.max = w;
            t.last = w;
        }
    }
    for (int a = 0; a < 0xFFFF; ++a) {
        const Stat& t = st[size_t(a)];
        if (t.distinct < min_distinct || t.out > t.distinct) continue;
        std::printf("%04X: %4d values  first %04X  range %04X-%04X  up %d down %d out %d\n", a, t.distinct, t.first, t.min, t.max, t.ups, t.downs, t.out);
    }
    return 0;
}
