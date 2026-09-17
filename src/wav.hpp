// Minimal RIFF/WAVE reader and writer for sample import/export.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct WavData {
    int sample_rate = 0;
    std::vector<int16_t> mono;   // channels mixed down
};

std::string read_wav(const std::string& path, WavData& out);
std::string write_wav_mono(const std::string& path, const std::vector<int16_t>& pcm, int sample_rate);

std::vector<int16_t> resample(const std::vector<int16_t>& in, int from_rate, int to_rate);
