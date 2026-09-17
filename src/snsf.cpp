#include "snsf.hpp"

#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace {
std::string dir_of(const std::string& p) {
    size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? "" : p.substr(0, s + 1);
}

std::string lower(std::string s) { for (char& c : s) c = char(std::tolower(uint8_t(c))); return s; }

bool read_file(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(size_t(std::max(0L, n)));
    size_t got = n > 0 ? std::fread(out.data(), 1, out.size(), f) : 0;
    std::fclose(f);
    return got == out.size();
}

bool inflate_all(const uint8_t* in, size_t n, std::vector<uint8_t>& out) {
    z_stream zs{};
    if (inflateInit(&zs) != Z_OK) return false;
    zs.next_in = const_cast<Bytef*>(in);
    zs.avail_in = uInt(n);
    out.clear();
    uint8_t buf[65536];
    int rc;
    do {
        zs.next_out = buf; zs.avail_out = sizeof buf;
        rc = inflate(&zs, Z_NO_FLUSH);
        if (rc != Z_OK && rc != Z_STREAM_END) { inflateEnd(&zs); return false; }
        out.insert(out.end(), buf, buf + (sizeof buf - zs.avail_out));
    } while (rc != Z_STREAM_END);
    inflateEnd(&zs);
    return true;
}

int parse_time(const std::string& s) {
    if (s.empty()) return 0;
    double secs = 0; int h = 0, m = 0; double sec = 0;
    const int parts = int(std::count(s.begin(), s.end(), ':'));
    if (parts == 2) { if (std::sscanf(s.c_str(), "%d:%d:%lf", &h, &m, &sec) == 3) secs = h * 3600 + m * 60 + sec; }
    else if (parts == 1) { if (std::sscanf(s.c_str(), "%d:%lf", &m, &sec) == 2) secs = m * 60 + sec; }
    else secs = std::atof(s.c_str());
    return int(secs * 1000.0 + 0.5);
}

std::string load_one(const std::string& path, SnsfFile& out, int depth) {
    if (depth > 8) return "library chain too deep";
    std::vector<uint8_t> d;
    if (!read_file(path, d)) return "cannot open " + path;
    if (d.size() < 16 || std::memcmp(d.data(), "PSF", 3) != 0) return "not a PSF file: " + path;
    if (d[3] != 0x23) { char b[64]; std::snprintf(b, sizeof b, "PSF version %02X is not SNSF", d[3]); return b; }
    const uint32_t res = uint32_t(d[4] | d[5] << 8 | d[6] << 16 | uint32_t(d[7]) << 24);
    const uint32_t plen = uint32_t(d[8] | d[9] << 8 | d[10] << 16 | uint32_t(d[11]) << 24);
    if (16 + size_t(res) + plen > d.size()) return "truncated PSF: " + path;
    std::map<std::string, std::string> tags;
    const size_t tag_at = 16 + res + plen;
    if (d.size() >= tag_at + 5 && std::memcmp(d.data() + tag_at, "[TAG]", 5) == 0) {
        std::string text(d.begin() + long(tag_at + 5), d.end());
        size_t pos = 0;
        while (pos < text.size()) {
            size_t nl = text.find('\n', pos);
            std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
            pos = nl == std::string::npos ? text.size() : nl + 1;
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = lower(line.substr(0, eq)), v = line.substr(eq + 1);
            while (!k.empty() && k.back() == ' ') k.pop_back();
            while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.pop_back();
            while (!v.empty() && v.front() == ' ') v.erase(v.begin());
            if (tags.count(k)) tags[k] += "\n" + v; else tags[k] = v;
        }
    }
    std::vector<std::string> libs;
    if (tags.count("_lib")) libs.push_back(tags["_lib"]);
    for (int i = 2; i < 10; ++i) { std::string k = "_lib" + std::to_string(i); if (tags.count(k)) libs.push_back(tags[k]); }
    for (const std::string& lib : libs) {
        std::string err = load_one(dir_of(path) + lib, out, depth + 1);
        if (!err.empty()) return err;
    }
    if (plen > 0) {
        std::vector<uint8_t> prog;
        if (!inflate_all(d.data() + 16 + res, plen, prog)) return "bad zlib data in " + path;
        if (prog.size() < 8) return "program section too short in " + path;
        const uint32_t off = uint32_t(prog[0] | prog[1] << 8 | prog[2] << 16 | uint32_t(prog[3]) << 24);
        const uint32_t size = uint32_t(prog[4] | prog[5] << 8 | prog[6] << 16 | uint32_t(prog[7]) << 24);
        if (size > 0x800000 || off > 0x800000) return "program section out of range in " + path;
        const size_t have = std::min<size_t>(size, prog.size() - 8);
        if (out.rom.size() < off + have) out.rom.resize(off + have, 0);
        std::memcpy(out.rom.data() + off, prog.data() + 8, have);
    }
    for (auto& [k, v] : tags) if (k.empty() || k[0] != '_') out.tags[k] = v;
    return "";
}

}

bool is_snsf_path(const std::string& path) {
    std::string l = lower(path);
    return l.size() > 5 && (l.compare(l.size() - 5, 5, ".snsf") == 0 || (l.size() > 9 && l.compare(l.size() - 9, 9, ".minisnsf") == 0));
}

std::string load_snsf_file(const std::string& path, SnsfFile& out) {
    out = SnsfFile{};
    std::string err = load_one(path, out, 0);
    if (!err.empty()) return err;
    if (out.rom.empty()) return "no ROM data";
    out.title = out.tag("title"); out.game = out.tag("game"); out.artist = out.tag("artist");
    out.copyright = out.tag("copyright"); out.year = out.tag("year"); out.comment = out.tag("comment");
    out.length_ms = parse_time(out.tag("length"));
    out.fade_ms = parse_time(out.tag("fade"));
    return "";
}
