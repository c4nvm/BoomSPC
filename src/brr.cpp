#include "brr.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace brr {
namespace {
inline int clamp16(int v) { return v < -32768 ? -32768 : v > 32767 ? 32767 : v; }
inline int wrap15(int v) { return int(int16_t(v * 2)) / 2; }

inline int predict(int filter, int p1, int p2) {
    switch (filter) {
        case 1: return p1 + ((-p1) >> 4);
        case 2: return p1 * 2 + ((-p1 * 3) >> 5) - p2 + (p2 >> 4);
        case 3: return p1 * 2 + ((-p1 * 13) >> 6) - p2 + ((p2 * 3) >> 4);
        default: return 0;
    }
}

inline int sample_value(int nibble, int shift) {
    if (shift > 12) shift = 12;
    return (nibble << shift) >> 1;
}

}

Info scan(const uint8_t* ram, uint16_t addr) {
    Info info;
    uint32_t a = addr;
    for (int n = 0; n < 0x10000 / kBlockBytes; ++n) {
        if (a + kBlockBytes > 0x10000) { info.truncated = true; return info; }
        uint8_t h = ram[a];
        ++info.blocks;
        if (h & 1) { info.loops = h & 2; return info; }
        a += kBlockBytes;
    }
    info.truncated = true;
    return info;
}

std::vector<int16_t> decode(const uint8_t* ram, uint16_t addr, int blocks) {
    std::vector<int16_t> out;
    out.reserve(size_t(blocks) * kBlockSamples);
    int p1 = 0, p2 = 0;
    uint32_t a = addr;
    for (int b = 0; b < blocks && a + kBlockBytes <= 0x10000; ++b, a += kBlockBytes) {
        uint8_t h = ram[a];
        int shift = h >> 4, filter = (h >> 2) & 3;
        for (int i = 0; i < kBlockSamples; ++i) {
            uint8_t byte = ram[a + 1 + i / 2];
            int nib = (i & 1) ? (byte & 0x0F) : (byte >> 4);
            if (nib >= 8) nib -= 16;
            int v = sample_value(nib, shift) + predict(filter, p1, p2);
            v = wrap15(clamp16(v));
            p2 = p1; p1 = v;
            out.push_back(int16_t(v * 2));
        }
    }
    return out;
}

std::vector<uint8_t> encode(const std::vector<int16_t>& pcm16, int& loop_start) {
    std::vector<int> pcm(pcm16.size());
    for (size_t i = 0; i < pcm16.size(); ++i) pcm[i] = pcm16[i] / 2;
    if (pcm.empty()) pcm.push_back(0);

    if (loop_start >= 0) loop_start = std::min(loop_start, int(pcm.size()) - 1) / kBlockSamples * kBlockSamples;
    while (pcm.size() % kBlockSamples) pcm.push_back(0);
    const int blocks = int(pcm.size()) / kBlockSamples;
    const int loop_block = loop_start >= 0 ? loop_start / kBlockSamples : -1;

    std::vector<uint8_t> out;
    out.reserve(size_t(blocks) * kBlockBytes);
    int p1 = 0, p2 = 0;

    for (int b = 0; b < blocks; ++b) {
        const int* src = pcm.data() + size_t(b) * kBlockSamples;
        const bool filter0_only = b == 0 || b == loop_block;

        double best_err = 1e300;
        int best_filter = 0, best_shift = 0;
        int best_nib[kBlockSamples] = {};
        int best_p1 = p1, best_p2 = p2;

        for (int filter = 0; filter < (filter0_only ? 1 : 4); ++filter) {
            for (int shift = 0; shift <= 12; ++shift) {
                int q1 = p1, q2 = p2;
                double err = 0;
                int nib[kBlockSamples];
                for (int i = 0; i < kBlockSamples; ++i) {
                    int pred = predict(filter, q1, q2);
                    int target = src[i] - pred;
                    int n = int(std::lround(double(target) * 2.0 / double(1 << shift)));
                    n = std::clamp(n, -8, 7);
                    int v = wrap15(clamp16(sample_value(n, shift) + pred));
                    double d = double(v - src[i]);
                    err += d * d;
                    nib[i] = n;
                    q2 = q1; q1 = v;
                }
                if (err < best_err) {
                    best_err = err; best_filter = filter; best_shift = shift;
                    std::copy(nib, nib + kBlockSamples, best_nib);
                    best_p1 = q1; best_p2 = q2;
                }
                if (err == 0) break;
            }
        }

        uint8_t header = uint8_t((best_shift << 4) | (best_filter << 2));
        if (b == blocks - 1) header |= uint8_t(1 | (loop_start >= 0 ? 2 : 0));
        out.push_back(header);
        for (int i = 0; i < kBlockSamples; i += 2)
            out.push_back(uint8_t(((best_nib[i] & 0x0F) << 4) | (best_nib[i + 1] & 0x0F)));
        p1 = best_p1; p2 = best_p2;
    }
    return out;
}

}
