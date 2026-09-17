// ramtrace: plays an SPC and prints selected RAM bytes every N samples, for
// reverse-engineering drivers.  ramtrace file.spc seconds step_samples ADDR[:LEN] ...
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "SNES_SPC.h"
#include "spc_file.hpp"

int main(int argc, char** argv) {
    if (argc < 5) { std::fprintf(stderr, "usage: ramtrace file.spc seconds step_samples ADDR[:LEN] ...\n"); return 2; }
    SpcFile f;
    if (std::string err = load_spc_file(argv[1], f); !err.empty()) { std::fprintf(stderr, "%s\n", err.c_str()); return 2; }
    const double secs = std::atof(argv[2]);
    const int step = std::atoi(argv[3]);
    std::vector<std::pair<int, int>> watch;
    for (int i = 4; i < argc; ++i) {
        int a = 0, n = 1;
        std::sscanf(argv[i], "%x:%d", &a, &n);
        watch.push_back({a, n});
    }
    SNES_SPC spc;
    spc.init();
    spc.load_spc(f.data.data(), long(f.data.size()));
    std::vector<uint8_t> last;
    std::vector<int16_t> buf(size_t(step) * 2);
    for (long s = 0; s < long(secs * 32000); s += step) {
        spc.play(step * 2, buf.data());
        std::vector<uint8_t> cur;
        for (auto& w : watch) for (int k = 0; k < w.second; ++k) cur.push_back(spc.ram()[(w.first + k) & 0xFFFF]);
        if (cur != last) {
            std::printf("%8ld ", s + step);
            for (auto& w : watch) { std::printf(" %04X:", w.first); for (int k = 0; k < w.second; ++k) std::printf("%02X ", spc.ram()[(w.first + k) & 0xFFFF]); }
            std::printf("\n");
            last = cur;
        }
    }
    return 0;
}
