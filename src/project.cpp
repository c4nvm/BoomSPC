#include "project.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace {
const char* kMagic = "BoomSPC project 1\n";
const char* kEnd = "---\n";
}

bool is_project_path(const std::string& path) {
    size_t n = path.size();
    return n >= 8 && path.compare(n - 8, 8, ".boomspc") == 0;
}

std::string save_project(const std::string& path, const ProjectMeta& m, const std::vector<uint8_t>& spc) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return "could not open " + path;
    out << kMagic;
    out << "source=" << m.source << "\n";
    out << "song=" << m.song << "\nsong_addr=" << m.song_addr << "\nticks_per_row=" << m.ticks_per_row << "\nticks_per_beat=" << m.ticks_per_beat << "\noctave=" << m.octave
        << "\nedit_step=" << m.edit_step << "\nview_order=" << m.view_order << "\nreclaim=" << (m.reclaim ? 1 : 0) << "\n";
    out << kEnd;
    out.write(reinterpret_cast<const char*>(spc.data()), std::streamsize(spc.size()));
    if (!out) return "write failed";
    return {};
}

std::string load_project(const std::string& path, ProjectMeta& m, std::vector<uint8_t>& spc) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return "could not open " + path;
    std::vector<uint8_t> all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const size_t magic_len = std::strlen(kMagic);
    if (all.size() < magic_len || std::memcmp(all.data(), kMagic, magic_len) != 0) return "not a BoomSPC project";
    size_t pos = magic_len, end = std::string::npos;
    for (size_t i = pos; i + 4 <= all.size(); ++i)
        if (std::memcmp(all.data() + i, kEnd, 4) == 0 && (i == 0 || all[i - 1] == '\n')) { end = i; break; }
    if (end == std::string::npos) return "project header is damaged";
    std::string header(all.begin() + long(pos), all.begin() + long(end));
    std::istringstream hs(header);
    std::string line;
    m = ProjectMeta{};
    while (std::getline(hs, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "source") m.source = v;
        else if (k == "song") m.song = std::atoi(v.c_str());
        else if (k == "song_addr") m.song_addr = std::atoi(v.c_str());
        else if (k == "ticks_per_row") m.ticks_per_row = std::atoi(v.c_str());
        else if (k == "ticks_per_beat") m.ticks_per_beat = std::atoi(v.c_str());
        else if (k == "octave") m.octave = std::atoi(v.c_str());
        else if (k == "edit_step") m.edit_step = std::atoi(v.c_str());
        else if (k == "view_order") m.view_order = std::atoi(v.c_str());
        else if (k == "reclaim") m.reclaim = std::atoi(v.c_str()) != 0;
    }
    spc.assign(all.begin() + long(end + 4), all.end());
    return {};
}
