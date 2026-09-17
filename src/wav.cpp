#include "wav.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace {
uint32_t le32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
}

std::string read_wav(const std::string& path, WavData& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return "could not open " + path;
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) || std::memcmp(d.data() + 8, "WAVE", 4)) return "not a WAV file";

    int channels = 0, bits = 0, format = 0;
    const uint8_t* data = nullptr;
    size_t data_len = 0;
    size_t p = 12;
    while (p + 8 <= d.size()) {
        const uint8_t* id = d.data() + p;
        uint32_t len = le32(d.data() + p + 4);
        const uint8_t* body = d.data() + p + 8;
        if (p + 8 + len > d.size()) len = uint32_t(d.size() - p - 8);
        if (!std::memcmp(id, "fmt ", 4) && len >= 16) {
            format = le16(body);
            channels = le16(body + 2);
            out.sample_rate = int(le32(body + 4));
            bits = le16(body + 14);
            if (format == 0xFFFE && len >= 26) format = le16(body + 24);
        } else if (!std::memcmp(id, "data", 4)) {
            data = body; data_len = len;
        }
        p += 8 + len + (len & 1);
    }
    if (!data || !channels || !bits) return "missing fmt or data chunk";
    if (format != 1 && format != 3) return "unsupported WAV encoding (need PCM or float)";
    const int bytes = bits / 8;
    if (bytes < 1 || bytes > 4) return "unsupported bit depth";

    const size_t frames = data_len / size_t(bytes * channels);
    out.mono.resize(frames);
    for (size_t f = 0; f < frames; ++f) {
        double acc = 0;
        for (int c = 0; c < channels; ++c) {
            const uint8_t* s = data + (f * channels + c) * bytes;
            double v = 0;
            if (format == 3) { float fv; std::memcpy(&fv, s, 4); v = fv * 32767.0; }
            else if (bytes == 1) v = (int(s[0]) - 128) * 256.0;
            else if (bytes == 2) v = int16_t(le16(s));
            else if (bytes == 3) v = (int32_t((s[0] << 8) | (s[1] << 16) | (s[2] << 24)) >> 8) / 256.0;
            else v = int32_t(le32(s)) / 65536.0;
            acc += v;
        }
        out.mono[f] = int16_t(std::clamp(acc / channels, -32768.0, 32767.0));
    }
    return {};
}

std::string write_wav_mono(const std::string& path, const std::vector<int16_t>& pcm, int sample_rate) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return "could not open " + path;
    auto u32 = [&](uint32_t v) { out.write(reinterpret_cast<char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { out.write(reinterpret_cast<char*>(&v), 2); };
    const uint32_t bytes = uint32_t(pcm.size() * 2);
    out.write("RIFF", 4); u32(36 + bytes); out.write("WAVE", 4);
    out.write("fmt ", 4); u32(16); u16(1); u16(1); u32(uint32_t(sample_rate)); u32(uint32_t(sample_rate) * 2); u16(2); u16(16);
    out.write("data", 4); u32(bytes);
    out.write(reinterpret_cast<const char*>(pcm.data()), bytes);
    return out ? std::string{} : "write failed";
}

std::vector<int16_t> resample(const std::vector<int16_t>& in, int from_rate, int to_rate) {
    if (in.empty() || from_rate <= 0 || to_rate <= 0 || from_rate == to_rate) return in;
    const double step = double(from_rate) / to_rate;
    const size_t n = size_t(in.size() / step);
    std::vector<int16_t> out(n);
    for (size_t i = 0; i < n; ++i) {
        double pos = i * step;
        size_t a = size_t(pos);
        size_t b = std::min(a + 1, in.size() - 1);
        double t = pos - a;
        out[i] = int16_t(in[a] * (1 - t) + in[b] * t);
    }
    return out;
}
