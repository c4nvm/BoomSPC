#include "spc_file.hpp"

#include <cctype>
#include <cstring>
#include <fstream>

namespace {
std::string fixed_string(const uint8_t* p, size_t n) {
    size_t len = 0;
    while (len < n && p[len] != 0) ++len;
    std::string s(reinterpret_cast<const char*>(p), len);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

bool all_text_digits(const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (p[i] == 0 || p[i] == ' ') continue;
        if (!std::isdigit(p[i])) return false;
    }
    return true;
}

int text_int(const uint8_t* p, size_t n) {
    int v = 0;
    for (size_t i = 0; i < n && std::isdigit(p[i]); ++i) v = v * 10 + (p[i] - '0');
    return v;
}

uint32_t le32(const uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24);
}
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }

void parse_id666(SpcFile& f) {
    const uint8_t* d = f.data.data();
    f.title    = fixed_string(d + 0x2E, 32);
    f.game     = fixed_string(d + 0x4E, 32);
    f.dumper   = fixed_string(d + 0x6E, 16);
    f.comments = fixed_string(d + 0x7E, 32);

    f.binary_tags = !all_text_digits(d + 0xAC, 5) || !all_text_digits(d + 0xA9, 3);

    if (f.binary_tags) {
        f.date     = "";
        f.intro_ms = int(d[0xA9] | (d[0xAA] << 8) | (d[0xAB] << 16)) * 1000;
        f.fade_ms  = int(le32(d + 0xAC));
        f.artist   = fixed_string(d + 0xB0, 32);
        f.default_mutes = d[0xD0];
    } else {
        f.date     = fixed_string(d + 0x9E, 11);
        f.intro_ms = text_int(d + 0xA9, 3) * 1000;
        f.fade_ms  = text_int(d + 0xAC, 5);
        f.artist   = fixed_string(d + 0xB1, 32);
        f.default_mutes = d[0xD1];
    }
}

void parse_xid6(SpcFile& f) {
    const size_t base = SpcFile::kBaseSize;
    if (f.data.size() < base + 8) return;
    const uint8_t* d = f.data.data();
    if (std::memcmp(d + base, "xid6", 4) != 0) return;

    size_t end = base + 8 + le32(d + base + 4);
    if (end > f.data.size()) end = f.data.size();

    int intro = -1, loop = -1, endlen = -1, fade = -1, loop_count = -1;
    size_t p = base + 8;
    while (p + 4 <= end) {
        uint8_t  id   = d[p];
        uint8_t  type = d[p + 1];
        uint16_t len  = le16(d + p + 2);
        p += 4;

        const uint8_t* payload = d + p;
        size_t payload_len = 0;
        uint32_t value = len;   // type 0: data lives in the length field
        if (type != 0) {
            payload_len = len;
            if (p + payload_len > end) break;
            if (type == 4 && len >= 4) value = le32(payload);
            p += (payload_len + 3) & ~size_t(3);
        }

        auto str = [&] { return fixed_string(payload, payload_len); };
        switch (id) {
            case 0x01: f.title = str(); break;
            case 0x02: f.game = str(); break;
            case 0x03: f.artist = str(); break;
            case 0x04: f.dumper = str(); break;
            case 0x05: f.date = std::to_string(value); break;
            case 0x07: f.comments = str(); break;
            case 0x10: f.ost = str(); break;
            case 0x13: f.publisher = str(); break;
            case 0x30: intro  = int(value); break;
            case 0x31: loop   = int(value); break;
            case 0x32: endlen = int(value); break;
            case 0x33: fade   = int(value); break;
            case 0x34: f.default_mutes = uint8_t(value); break;
            case 0x35: loop_count = int(value); break;
            default: break;
        }
    }

    if (intro >= 0 || loop >= 0 || endlen >= 0) {
        long ticks = 0;
        if (intro > 0) ticks += intro;
        if (loop > 0) ticks += long(loop) * (loop_count > 0 ? loop_count : 1);
        if (endlen > 0) ticks += endlen;
        if (ticks > 0) f.intro_ms = int(ticks / 64);
    }
    if (fade >= 0) f.fade_ms = fade / 64;
}

}

std::string parse_spc(std::vector<uint8_t> bytes, SpcFile& out) {
    if (bytes.size() < SpcFile::kMinSize) return "file too small to be an SPC";
    if (std::memcmp(bytes.data(), "SNES-SPC700 Sound File Data", 27) != 0)
        return "missing SNES-SPC700 signature";

    out = SpcFile{};
    out.data = std::move(bytes);
    const uint8_t* d = out.data.data();

    out.has_id666 = d[0x23] == 26;
    out.pc  = le16(d + 0x25);
    out.a   = d[0x27];
    out.x   = d[0x28];
    out.y   = d[0x29];
    out.psw = d[0x2A];
    out.sp  = d[0x2B];

    if (out.has_id666) parse_id666(out);
    parse_xid6(out);
    return {};
}

std::string load_spc_file(const std::string& path, SpcFile& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return "could not open " + path;
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string err = parse_spc(std::move(bytes), out);
    if (err.empty()) out.path = path;
    return err;
}
