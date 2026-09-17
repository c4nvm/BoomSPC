// S-DSP BRR sample codec.
// A BRR block is 9 bytes: header (shift<<4 | filter<<2 | loop<<1 | end) and
// 16 signed 4-bit samples. Decoded values are 15-bit like the real DSP.
#pragma once

#include <cstdint>
#include <vector>

namespace brr {
constexpr int kBlockBytes   = 9;
constexpr int kBlockSamples = 16;

struct Info {
    int  blocks = 0;        // blocks until (and including) the END block
    bool loops  = false;    // END block has the loop flag
    bool truncated = false; // ran off the end of RAM without an END block
};

Info scan(const uint8_t* ram, uint16_t addr);

std::vector<int16_t> decode(const uint8_t* ram, uint16_t addr, int blocks);

std::vector<uint8_t> encode(const std::vector<int16_t>& pcm, int& loop_start);

}
