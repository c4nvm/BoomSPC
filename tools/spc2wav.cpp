// Headless renderer: spc2wav in.spc out.wav [seconds]
// Useful for regression-testing the core without audio hardware.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#include "SNES_SPC.h"
#include "SPC_Filter.h"
#include "spc_file.hpp"

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s in.spc out.wav [seconds]\n", argv[0]);
        return 2;
    }
    double seconds = argc > 3 ? std::atof(argv[3]) : 0;

    SpcFile file;
    if (std::string err = load_spc_file(argv[1], file); !err.empty()) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    if (seconds <= 0) seconds = file.total_ms() > 0 ? file.total_ms() / 1000.0 : 30.0;

    SNES_SPC spc;
    SPC_Filter filter;
    if (spc.init()) return 1;
    if (blargg_err_t e = spc.load_spc(file.data.data(), long(file.data.size()))) {
        std::fprintf(stderr, "load_spc: %s\n", e);
        return 1;
    }
    spc.clear_echo();

    const int frames = int(seconds * double(SNES_SPC::sample_rate));
    std::vector<int16_t> pcm(size_t(frames) * 2);
    const int chunk = 2048;
    for (int i = 0; i < frames * 2; i += chunk) {
        int n = std::min(chunk, frames * 2 - i);
        if (blargg_err_t e = spc.play(n, pcm.data() + i)) {
            std::fprintf(stderr, "play: %s\n", e);
            return 1;
        }
        filter.run(pcm.data() + i, n);
    }

    std::ofstream out(argv[2], std::ios::binary);
    auto u32 = [&](uint32_t v) { out.write(reinterpret_cast<char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { out.write(reinterpret_cast<char*>(&v), 2); };
    const uint32_t data_bytes = uint32_t(pcm.size() * 2);
    out.write("RIFF", 4); u32(36 + data_bytes); out.write("WAVE", 4);
    out.write("fmt ", 4); u32(16); u16(1); u16(2); u32(SNES_SPC::sample_rate);
    u32(SNES_SPC::sample_rate * 4); u16(4); u16(16);
    out.write("data", 4); u32(data_bytes);
    out.write(reinterpret_cast<const char*>(pcm.data()), data_bytes);

    std::printf("%s: \"%s\" by %s, rendered %.1fs\n", argv[1], file.title.c_str(), file.artist.c_str(), seconds);
    return 0;
}
