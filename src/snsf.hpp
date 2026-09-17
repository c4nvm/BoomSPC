// SNSF / miniSNSF loading (PSF version 0x23): a zlib-packed program section
// that is a ROM image patch (offset, size, data), optionally on top of a
// shared `_lib` file, plus the usual tag block.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct SnsfFile {
    std::vector<uint8_t> rom;                 // assembled cartridge image, headerless
    std::map<std::string, std::string> tags;  // lowercase keys
    std::string title, game, artist, copyright, year, comment;
    int length_ms = 0, fade_ms = 0;           // 0 = untagged
    std::string tag(const char* k) const { auto it = tags.find(k); return it == tags.end() ? "" : it->second; }
};

std::string load_snsf_file(const std::string& path, SnsfFile& out);
bool is_snsf_path(const std::string& path);
