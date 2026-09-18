#include "update.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

#include <zlib.h>

#include "version.hpp"

#ifdef _WIN32
#include <process.h>
#include <windows.h>
#define popen _popen
#define pclose _pclose
#else
#include <sys/wait.h>
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace fs = std::filesystem;

namespace update {
namespace {

constexpr const char* kFallbackRepo = "https://github.com/c4nvm/BoomSPC.git";

// Leaked on purpose: a build may still be running when the process exits.
std::mutex& mutex() { static auto* m = new std::mutex; return *m; }
State& shared() { static auto* s = new State; return *s; }

void set(Stage st, const std::string& msg) {
    std::lock_guard<std::mutex> lock(mutex());
    shared().stage = st;
    shared().message = msg;
}

void note(const std::string& msg) {
    std::lock_guard<std::mutex> lock(mutex());
    shared().message = msg;
}

void log_append(const std::string& s) {
    std::lock_guard<std::mutex> lock(mutex());
    shared().log += s;
}

std::string quote(const std::string& s) {
#ifdef _WIN32
    return "\"" + s + "\"";
#else
    std::string out = "'";
    for (char c : s) { if (c == '\'') out += "'\\''"; else out += c; }
    return out + "'";
#endif
}

#ifdef _WIN32
// Tools installed by install_tools() live here; put them on the path of
// every command so a fresh install works without a new login.
std::string path_prefix() {
    std::string dirs;
    for (const char* d : {"C:\\msys64\\ucrt64\\bin", "C:\\msys64\\usr\\bin", "C:\\Program Files\\Git\\cmd"})
        if (fs::exists(d)) dirs += std::string(d) + ";";
    return dirs.empty() ? "" : "set \"PATH=" + dirs + "%PATH%\" && ";
}
#endif

// Runs a command with its output streamed into the log; returns the exit code.
int run(const std::string& cmd, const std::string& cwd, std::string* out = nullptr, bool quiet = false) {
    std::string full;
#ifdef _WIN32
    full = path_prefix();
    if (!cwd.empty()) full += "cd /d " + quote(cwd) + " && ";
    full += cmd + " 2>&1";
#else
    full = cwd.empty() ? cmd + " 2>&1" : "(cd " + quote(cwd) + " && " + cmd + ") 2>&1";
#endif
    if (!quiet) log_append("$ " + cmd + "\n");
    FILE* p = popen(full.c_str(), "r");
    if (!p) { if (!quiet) log_append("could not start the command\n"); return -1; }
    char buf[1024];
    while (std::fgets(buf, sizeof buf, p)) {
        if (!quiet) log_append(buf);
        if (out) *out += buf;
    }
    int rc = pclose(p);
#ifndef _WIN32
    rc = WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
#endif
    if (rc != 0 && !quiet) log_append("(exit code " + std::to_string(rc) + ")\n");
    return rc;
}

bool have(const std::string& probe) { std::string o; return run(probe, "", &o, true) == 0; }

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

std::string lower(std::string s) { for (char& c : s) c = char(std::tolower((unsigned char)c)); return s; }

std::string built_commit() {
    std::string c = build_info().commit;
    if (!c.empty() && c.back() == '+') c.pop_back();
    return c;
}

std::string short_hash(const std::string& h) { return h.substr(0, 7); }

std::string branch() { return build_info().branch; }

// origin may be an ssh remote; a clone on a machine without keys needs https.
std::string https_url() {
    std::string r = build_info().remote;
    if (r.empty()) return kFallbackRepo;
    const char* ssh[] = {"git@github.com:", "ssh://git@github.com/"};
    for (const char* p : ssh)
        if (r.rfind(p, 0) == 0) return "https://github.com/" + r.substr(std::strlen(p));
    return r;
}

// "owner/repo" from the remote, "" when it is not on GitHub.
std::string github_slug() {
    std::string u = https_url();
    const char* p = "https://github.com/";
    if (u.rfind(p, 0) != 0) return "";
    u = u.substr(std::strlen(p));
    if (u.size() > 4 && u.compare(u.size() - 4, 4, ".git") == 0) u.resize(u.size() - 4);
    while (!u.empty() && u.back() == '/') u.pop_back();
    return u;
}

std::string exe_dir() { return fs::path(exe_path()).parent_path().string(); }
std::string clone_dir() { return (fs::path(exe_dir()) / "BoomSPC-src").string(); }

std::string build_dir() {
    if (has_source()) return build_info().binary_dir;
    return (fs::path(clone_dir()) / "build").string();
}

// The freshly built executable: next to the cache, or in the config folder of
// a multi-config generator.
std::string built_exe(const std::string& bin) {
    const std::string name = fs::path(exe_path()).filename().string();
    const std::string type = build_info().build_type;
    for (const fs::path& cand : {fs::path(bin) / name, fs::path(bin) / (type.empty() ? "Release" : type) / name})
        if (fs::exists(cand)) return cand.string();
    return "";
}

std::vector<Incoming> parse_log(const std::string& text) {
    std::vector<Incoming> out;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\x1e', pos);
        if (end == std::string::npos) end = text.size();
        std::string rec = text.substr(pos, end - pos);
        pos = end + 1;
        std::string f[5];
        size_t a = 0;
        for (int i = 0; i < 5; ++i) {
            size_t b = rec.find('\x1f', a);
            f[i] = rec.substr(a, b == std::string::npos ? std::string::npos : b - a);
            if (b == std::string::npos) break;
            a = b + 1;
        }
        while (!f[0].empty() && (f[0][0] == '\n' || f[0][0] == '\r')) f[0].erase(0, 1);
        if (f[0].size() < 7) continue;
        out.push_back({f[0], f[1], f[3], trim(f[4]), std::atoll(f[2].c_str())});
    }
    return out;
}

// ---------------------------------------------------------------------------
// Just enough JSON for the GitHub API replies.
struct Json {
    enum Type { Null, Bool, Number, String, Array, Object } type = Null;
    std::string str;
    double num = 0;
    bool boolean = false;
    std::vector<Json> items;          // Array
    std::vector<std::string> keys;    // Object, parallel to items
    const Json* get(const char* k) const {
        if (type != Object) return nullptr;
        for (size_t i = 0; i < keys.size(); ++i) if (keys[i] == k) return &items[i];
        return nullptr;
    }
    std::string text(const char* k) const { const Json* j = get(k); return j && j->type == String ? j->str : ""; }
};

struct JsonParser {
    const std::string& s;
    size_t i = 0;
    void ws() { while (i < s.size() && std::strchr(" \t\r\n", s[i])) ++i; }
    bool lit(const char* w) { size_t n = std::strlen(w); if (s.compare(i, n, w) == 0) { i += n; return true; } return false; }
    bool string(std::string& out) {
        if (i >= s.size() || s[i] != '"') return false;
        ++i;
        while (i < s.size() && s[i] != '"') {
            char c = s[i++];
            if (c != '\\') { out += c; continue; }
            if (i >= s.size()) return false;
            char e = s[i++];
            switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    if (i + 4 > s.size()) return false;
                    unsigned cp = std::strtoul(s.substr(i, 4).c_str(), nullptr, 16); i += 4;
                    if (cp < 0x80) out += char(cp);
                    else if (cp < 0x800) { out += char(0xC0 | cp >> 6); out += char(0x80 | (cp & 0x3F)); }
                    else { out += char(0xE0 | cp >> 12); out += char(0x80 | (cp >> 6 & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
                    break;
                }
                default: out += e; break;
            }
        }
        if (i >= s.size()) return false;
        ++i;
        return true;
    }
    bool value(Json& j) {
        ws();
        if (i >= s.size()) return false;
        if (s[i] == '{') {
            j.type = Json::Object; ++i; ws();
            if (i < s.size() && s[i] == '}') { ++i; return true; }
            for (;;) {
                ws();
                std::string k;
                if (!string(k)) return false;
                ws();
                if (i >= s.size() || s[i++] != ':') return false;
                j.keys.push_back(k); j.items.emplace_back();
                if (!value(j.items.back())) return false;
                ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; return true; }
                return false;
            }
        }
        if (s[i] == '[') {
            j.type = Json::Array; ++i; ws();
            if (i < s.size() && s[i] == ']') { ++i; return true; }
            for (;;) {
                j.items.emplace_back();
                if (!value(j.items.back())) return false;
                ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; return true; }
                return false;
            }
        }
        if (s[i] == '"') { j.type = Json::String; return string(j.str); }
        if (lit("true")) { j.type = Json::Bool; j.boolean = true; return true; }
        if (lit("false")) { j.type = Json::Bool; return true; }
        if (lit("null")) return true;
        char* end = nullptr;
        j.num = std::strtod(s.c_str() + i, &end);
        if (end == s.c_str() + i) return false;
        i = end - s.c_str();
        j.type = Json::Number;
        return true;
    }
};

bool json_parse(const std::string& text, Json& out) { JsonParser p{text}; return p.value(out); }

// ---------------------------------------------------------------------------
// Downloads. curl ships with Windows 10, macOS and nearly every Linux.
std::string temp_dir() {
    fs::path d = fs::path(exe_dir()) / "update-tmp";
    std::error_code ec;
    fs::create_directories(d, ec);
    return d.string();
}

bool fetch(const std::string& url, const std::string& file, bool quiet) {
    std::error_code ec;
    fs::remove(file, ec);
    if (have("curl --version"))
        return run("curl -L --fail --silent --show-error -A BoomSPC -o " + quote(file) + " " + quote(url), "", nullptr, quiet) == 0 && fs::exists(file);
#ifdef _WIN32
    return run("powershell -NoProfile -Command \"Invoke-WebRequest -UseBasicParsing -Uri '" + url + "' -OutFile '" + file + "'\"", "", nullptr, quiet) == 0 && fs::exists(file);
#else
    if (have("wget --version"))
        return run("wget -q -O " + quote(file) + " " + quote(url), "", nullptr, quiet) == 0 && fs::exists(file);
    if (!quiet) log_append("neither curl nor wget is installed\n");
    return false;
#endif
}

bool fetch_json(const std::string& url, Json& out) {
    const std::string file = (fs::path(temp_dir()) / "api.json").string();
    if (!fetch(url, file, true)) return false;
    std::ifstream in(file, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::error_code ec;
    fs::remove(file, ec);
    return json_parse(text, out);
}

// The API root; a test can point it at a folder of canned replies.
std::string api_base() {
    if (const char* e = std::getenv("BOOMSPC_RELEASE_API")) return e;
    return "https://api.github.com/repos/" + github_slug();
}

long long parse_iso(const std::string& t) {
    std::tm tm{};
    if (std::sscanf(t.c_str(), "%d-%d-%dT%d:%d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6) return 0;
    tm.tm_year -= 1900; tm.tm_mon -= 1;
#ifdef _WIN32
    return _mkgmtime(&tm);
#else
    return timegm(&tm);
#endif
}

// The release asset built for this platform: the name says the OS, and the
// architecture when the release has several.
std::string pick_asset(const Json& assets, std::string* url) {
#if defined(_WIN32)
    const char* os[] = {"windows", "win64", "win32"};
#elif defined(__APPLE__)
    const char* os[] = {"macos", "darwin", "mac", "osx"};
#else
    const char* os[] = {"linux"};
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
    const char* arch[] = {"arm64", "aarch64"};
    const char* other[] = {"x64", "x86_64", "amd64", "x86"};
#else
    const char* arch[] = {"x64", "x86_64", "amd64"};
    const char* other[] = {"arm64", "aarch64"};
#endif
    std::string best, best_url;
    int best_score = -1;
    for (const Json& a : assets.items) {
        const std::string name = lower(a.text("name"));
        bool is_os = false;
        for (const char* o : os) if (name.find(o) != std::string::npos) is_os = true;
        if (!is_os) continue;
        const bool archive = name.size() > 4 && (name.compare(name.size() - 4, 4, ".zip") == 0 || name.find(".tar.gz") != std::string::npos || name.find(".tgz") != std::string::npos);
        if (!archive) continue;
        int score = 1;
        for (const char* x : arch) if (name.find(x) != std::string::npos) score = 2;
        for (const char* x : other) if (name.find(x) != std::string::npos) score = 0;
        if (score > best_score) { best_score = score; best = a.text("name"); best_url = a.text("browser_download_url"); }
    }
    if (url) *url = best_url;
    return best;
}

// Latest release with an asset for us, newer than this build. Fills the
// shared state; returns whether there is one.
bool check_release() {
    if (github_slug().empty() && !std::getenv("BOOMSPC_RELEASE_API")) return false;
    Json rel;
    if (!fetch_json(api_base() + "/releases/latest", rel) || rel.type != Json::Object) return false;
    const std::string tag = rel.text("tag_name");
    if (tag.empty()) return false;
    std::string url;
    const Json* assets = rel.get("assets");
    const std::string asset = assets ? pick_asset(*assets, &url) : "";
    if (asset.empty()) { log_append("release " + tag + " has no download for this platform\n"); return false; }

    bool newer;
    const std::string built = built_commit();
    if (!built.empty() && build_info().time > 0) {
        // A release made from the very commit we run is not an update.
        Json commit;
        std::string sha;
        if (fetch_json(api_base() + "/commits/" + tag, commit)) sha = commit.text("sha");
        newer = sha != built && parse_iso(rel.text("published_at")) > build_info().time;
    } else {
        std::string v = tag;
        if (!v.empty() && (v[0] == 'v' || v[0] == 'V')) v.erase(0, 1);
        newer = v != build_info().version;
    }
    if (!newer) return false;
    std::lock_guard<std::mutex> lock(mutex());
    shared().release_tag = tag;
    shared().release_asset = asset;
    shared().release_url = rel.text("html_url");
    return true;
}

// ---------------------------------------------------------------------------
// Zip extraction with zlib, so a release needs no unzip tool. Stored and
// deflated entries; no zip64.
uint32_t rd32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24; }
uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }

bool safe_name(const std::string& n) {
    if (n.empty() || n[0] == '/' || n[0] == '\\' || (n.size() > 1 && n[1] == ':')) return false;
    size_t pos = 0;
    while (pos <= n.size()) {
        size_t e = n.find_first_of("/\\", pos);
        if (e == std::string::npos) e = n.size();
        if (n.substr(pos, e - pos) == "..") return false;
        pos = e + 1;
    }
    return true;
}

bool unzip(const std::string& zip, const fs::path& dest, std::string& err) {
    std::ifstream in(zip, std::ios::binary);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (d.size() < 22) { err = "not a zip file"; return false; }
    size_t eocd = std::string::npos;
    const size_t lo = d.size() > 65557 + 22 ? d.size() - 65557 - 22 : 0;   // the comment can be 64K
    for (size_t i = d.size() - 22 + 1; i-- > lo;)
        if (rd32(&d[i]) == 0x06054b50) { eocd = i; break; }
    if (eocd == std::string::npos) { err = "no zip directory"; return false; }
    const int count = rd16(&d[eocd + 10]);
    size_t cd = rd32(&d[eocd + 16]);
    for (int n = 0; n < count; ++n) {
        if (cd + 46 > d.size() || rd32(&d[cd]) != 0x02014b50) { err = "corrupt zip directory"; return false; }
        const uint16_t method = rd16(&d[cd + 10]);
        const uint32_t csize = rd32(&d[cd + 20]), usize = rd32(&d[cd + 24]);
        const uint16_t nlen = rd16(&d[cd + 28]), elen = rd16(&d[cd + 30]), clen = rd16(&d[cd + 32]);
        const uint32_t attrs = rd32(&d[cd + 38]);
        const size_t lho = rd32(&d[cd + 42]);
        if (csize == 0xFFFFFFFF || usize == 0xFFFFFFFF) { err = "zip64 archives are not supported"; return false; }
        const std::string name(reinterpret_cast<const char*>(&d[cd + 46]), nlen);
        cd += 46 + nlen + elen + clen;
        if (!safe_name(name)) { err = "unsafe path in zip: " + name; return false; }
        const fs::path out = dest / name;
        std::error_code ec;
        if (name.back() == '/') { fs::create_directories(out, ec); continue; }
        fs::create_directories(out.parent_path(), ec);
        if (lho + 30 > d.size() || rd32(&d[lho]) != 0x04034b50) { err = "corrupt zip entry " + name; return false; }
        const size_t data = lho + 30 + rd16(&d[lho + 26]) + rd16(&d[lho + 28]);
        if (data + csize > d.size()) { err = "truncated zip entry " + name; return false; }
        std::vector<uint8_t> raw(usize);
        if (method == 0) std::memcpy(raw.data(), &d[data], csize);
        else if (method == 8) {
            z_stream z{};
            if (inflateInit2(&z, -MAX_WBITS) != Z_OK) { err = "zlib init failed"; return false; }
            z.next_in = const_cast<Bytef*>(&d[data]); z.avail_in = csize;
            z.next_out = raw.data(); z.avail_out = usize;
            const int rc = inflate(&z, Z_FINISH);
            inflateEnd(&z);
            if (rc != Z_STREAM_END && !(rc == Z_BUF_ERROR && z.avail_out == 0)) { err = "could not inflate " + name; return false; }
        } else { err = "unsupported compression in " + name; return false; }
        std::ofstream o(out, std::ios::binary);
        o.write(reinterpret_cast<const char*>(raw.data()), raw.size());
        if (!o) { err = "could not write " + out.string(); return false; }
        o.close();
        if (attrs >> 16) fs::permissions(out, fs::perms((attrs >> 16) & 0777), ec);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Putting new files in place. A running executable (and on Windows its DLLs)
// cannot be overwritten, but can be renamed; cleanup_old() sweeps the
// leftovers at the next start.
bool put_file(const fs::path& from, const fs::path& to, std::string& err) {
    std::error_code ec;
    fs::create_directories(to.parent_path(), ec);
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (!ec) return true;
    const fs::path aside = to.string() + ".old";
    fs::remove(aside, ec);
    fs::rename(to, aside, ec);
    if (ec) { err = "could not replace " + to.string() + ": " + ec.message(); return false; }
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec) { fs::rename(aside, to, ec); err = "could not copy " + to.string() + ": " + ec.message(); return false; }
    return true;
}

// Copies a payload folder over the executable's folder, keeping the user's
// ini files.
bool install_tree(const fs::path& root, std::string& err) {
    const fs::path dest = exe_dir();
    for (const fs::directory_entry& e : fs::recursive_directory_iterator(root)) {
        if (!e.is_regular_file()) continue;
        const fs::path rel = fs::relative(e.path(), root);
        if (lower(rel.extension().string()) == ".ini") continue;
        if (!put_file(e.path(), dest / rel, err)) return false;
    }
    return true;
}

fs::path find_exe(const fs::path& root) {
    const std::string name = lower(fs::path(exe_path()).filename().string());
    for (const fs::directory_entry& e : fs::recursive_directory_iterator(root))
        if (e.is_regular_file() && lower(e.path().filename().string()) == name) return e.path();
    return {};
}

// ---------------------------------------------------------------------------
// The toolchain for the build path, and how to get it on this machine.
std::vector<std::string> missing_tools() {
    std::vector<std::string> m;
    if (!have("git --version")) m.push_back("git");
    if (!have("cmake --version")) m.push_back("cmake");
    bool cxx = have("c++ --version") || have("g++ --version") || have("clang++ --version");
#ifdef _WIN32
    cxx = cxx || have("where cl");
#endif
    if (!cxx) m.push_back("a C++ compiler");
    if (have("pkg-config --version")) {
        if (!have("pkg-config --exists sdl2")) m.push_back("SDL2 development files");
        if (!have("pkg-config --exists zlib")) m.push_back("zlib development files");
    }
    return m;
}

// Package manager command for the missing pieces; "" when there is nothing
// this code knows how to drive.
std::string tools_command() {
#if defined(_WIN32)
    return "winget install -e --accept-source-agreements --accept-package-agreements --id Git.Git && "
           "winget install -e --accept-source-agreements --accept-package-agreements --id MSYS2.MSYS2 && "
           "C:\\msys64\\usr\\bin\\bash.exe -lc \"pacman -S --noconfirm --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-SDL2 mingw-w64-ucrt-x86_64-zlib\"";
#elif defined(__APPLE__)
    if (!have("brew --version")) return "";
    return "xcode-select --install; brew install git cmake ninja sdl2 pkg-config";
#else
    std::string pm;
    if (have("dnf --version")) pm = "dnf install -y git cmake ninja-build gcc-c++ pkgconf-pkg-config SDL2-devel zlib-devel";
    else if (have("apt-get --version")) pm = "apt-get install -y git cmake ninja-build g++ pkg-config libsdl2-dev zlib1g-dev";
    else if (have("pacman --version")) pm = "pacman -S --noconfirm --needed git cmake ninja gcc pkgconf sdl2 zlib";
    else if (have("zypper --version")) pm = "zypper install -y git cmake ninja gcc-c++ pkg-config SDL2-devel zlib-devel";
    if (pm.empty()) return "";
    if (have("pkexec --version")) return "pkexec sh -c " + quote(pm);
    // No polkit agent: a terminal asks for the password instead.
    const char* terms[] = {"x-terminal-emulator -e", "gnome-terminal --wait --", "konsole -e", "xfce4-terminal --disable-server -x", "xterm -e"};
    for (const char* t : terms) {
        std::string prog = std::string(t).substr(0, std::string(t).find(' '));
        if (have("command -v " + prog)) return std::string(t) + " sh -c " + quote("sudo " + pm + " || (echo; echo 'install failed'; read -r _)");
    }
    return "";
#endif
}

// ---------------------------------------------------------------------------
void check_thread() {
    set(Stage::Checking, "Checking GitHub...");
    {
        std::lock_guard<std::mutex> lock(mutex());
        shared().incoming.clear(); shared().remote_commit.clear();
        shared().release_tag.clear(); shared().release_asset.clear(); shared().release_url.clear();
    }
    const bool git = have("git --version");
    const std::string built = built_commit();
    std::string commits_msg;     // what the branch comparison found, "" when it could not be made
    bool ahead = false;
    if (git && has_source()) {
        const std::string src = build_info().source_dir;
        std::string o;
        // Straight from the https URL rather than origin: an ssh remote would
        // ask for a key passphrase on every start.
        if (run("git fetch --quiet " + https_url() + " " + branch(), src) != 0 || run("git rev-parse FETCH_HEAD", src, &o) != 0) { set(Stage::Failed, "Could not reach GitHub (see the log)."); return; }
        const std::string remote = trim(o);
        { std::lock_guard<std::mutex> lock(mutex()); shared().remote_commit = remote; }
        o.clear();
        if (built.empty()) { ahead = true; commits_msg = "This build is not from a git checkout, so it cannot be compared; GitHub is at " + short_hash(remote) + "."; }
        else if (run("git rev-list --count " + built + "..FETCH_HEAD", src, &o) != 0) { ahead = true; commits_msg = "The built commit " + short_hash(built) + " is not in this tree; GitHub is at " + short_hash(remote) + "."; }
        else if (const int n = std::atoi(o.c_str()); n > 0) {
            ahead = true;
            commits_msg = std::to_string(n) + (n == 1 ? " new commit" : " new commits") + " on GitHub.";
            o.clear();
            run("git log " + built + "..FETCH_HEAD --format=%H%x1f%as%x1f%at%x1f%s%x1f%b%x1e", src, &o);
            std::vector<Incoming> in = parse_log(o);
            std::lock_guard<std::mutex> lock(mutex());
            shared().incoming = std::move(in);
        }
    } else if (git) {
        std::string o;
        if (run("git ls-remote " + https_url() + " refs/heads/" + branch(), "", &o) != 0 || o.size() < 40) { set(Stage::Failed, "Could not reach GitHub (see the log)."); return; }
        const std::string remote = o.substr(0, 40);
        { std::lock_guard<std::mutex> lock(mutex()); shared().remote_commit = remote; }
        if (built.empty() || remote != built) {
            ahead = true;
            commits_msg = "GitHub is at " + short_hash(remote) + (built.empty() ? "." : ", this build is " + short_hash(built) + ".");
        }
    }

    const bool release = check_release();
    std::string msg;
    if (release) {
        std::lock_guard<std::mutex> lock(mutex());
        msg = "Release " + shared().release_tag + " is on GitHub with a build for this platform (" + shared().release_asset + ").";
        if (ahead) msg += " " + commits_msg;
    } else if (ahead) {
        msg = commits_msg;
        if (!has_source()) msg += " Updating clones the source next to the executable and builds it there.";
    } else if (!git && github_slug().empty()) { set(Stage::Failed, "git is not installed, so the branch cannot be compared."); return; }
    else if (!git) { set(Stage::UpToDate, "No newer release on GitHub. Install git to compare with the " + branch() + " branch too."); return; }
    else { set(Stage::UpToDate, "Up to date: " + short_hash(built) + " is the newest commit on " + branch() + "."); return; }
    set(Stage::Available, msg);
}

void download_thread() {
    std::string tag, asset;
    { std::lock_guard<std::mutex> lock(mutex()); tag = shared().release_tag; asset = shared().release_asset; }
    set(Stage::Updating, "Downloading " + asset + "...");
    Json rel;
    std::string url;
    if (fetch_json(api_base() + "/releases/latest", rel) && rel.get("assets")) pick_asset(*rel.get("assets"), &url);
    if (url.empty()) { set(Stage::Failed, "Could not find the download for " + tag + " any more."); return; }
    const fs::path tmp = temp_dir();
    const fs::path file = tmp / asset;
    if (!fetch(url, file.string(), false)) { set(Stage::Failed, "The download failed (see the log)."); return; }

    note("Unpacking...");
    const fs::path unpacked = tmp / "unpacked";
    std::error_code ec;
    fs::remove_all(unpacked, ec);
    fs::create_directories(unpacked, ec);
    std::string err;
    const std::string lname = lower(asset);
    if (lname.size() > 4 && lname.compare(lname.size() - 4, 4, ".zip") == 0) {
        if (!unzip(file.string(), unpacked, err)) { set(Stage::Failed, "Could not unpack " + asset + ": " + err); return; }
    } else if (run("tar -xzf " + quote(file.string()) + " -C " + quote(unpacked.string()), "") != 0) { set(Stage::Failed, "Could not unpack " + asset + " (see the log)."); return; }

    const fs::path fresh = find_exe(unpacked);
    if (fresh.empty()) { set(Stage::Failed, asset + " does not contain " + fs::path(exe_path()).filename().string() + "."); return; }
    note("Installing...");
    if (!install_tree(fresh.parent_path(), err)) { set(Stage::Failed, err); return; }
    fs::remove_all(tmp, ec);
    set(Stage::Built, "Installed " + tag + ". Restart BoomSPC to use it.");
}

void build_thread() {
    set(Stage::Updating, "Checking for git, cmake and a compiler...");
    std::vector<std::string> missing = missing_tools();
    if (!missing.empty()) {
        std::lock_guard<std::mutex> lock(mutex());
        shared().stage = Stage::NeedTools;
        shared().missing_tools = missing;
        shared().tools_command = tools_command();
        shared().message = "The update needs tools that are not installed.";
        return;
    }
    const std::string src = source_dir();
    const std::string bin = build_dir();
    const std::string exe = exe_path();

    if (fs::exists(fs::path(src) / ".git")) {
        note("Pulling " + branch() + "...");
        if (run("git pull --ff-only " + https_url() + " " + branch(), src) != 0) { set(Stage::Failed, "git pull failed: the source tree has local changes or diverged from GitHub (see the log)."); return; }
    } else {
        note("Cloning the source next to the executable...");
        if (run("git clone --branch " + branch() + " " + https_url() + " " + quote(src), exe_dir()) != 0) { set(Stage::Failed, "git clone failed (see the log)."); return; }
    }

    // The cmake that made this build knows the generator; a binary from
    // another machine uses whatever cmake is on the path.
    std::string cmake = build_info().cmake;
    if (cmake.empty() || !fs::exists(cmake)) cmake = "cmake";
    const bool same_machine = cmake != "cmake";
    if (!fs::exists(fs::path(bin) / "CMakeCache.txt")) {
        note("Configuring...");
        std::string cmd = quote(cmake) + " -S " + quote(src) + " -B " + quote(bin);
        if (same_machine && *build_info().generator) cmd += " -G " + quote(build_info().generator);
        else if (have("ninja --version")) cmd += " -G Ninja";
        if (*build_info().build_type) cmd += " -DCMAKE_BUILD_TYPE=" + std::string(build_info().build_type);
        if (run(cmd, "") != 0) { set(Stage::Failed, "cmake could not configure the build: a C++20 compiler, SDL2 and zlib development files are needed (see the log)."); return; }
    }

    std::string aside;
#ifdef _WIN32
    // Windows keeps a running executable locked, but lets it be renamed.
    if (fs::path(built_exe(bin)).lexically_normal() == fs::path(exe).lexically_normal()) {
        aside = exe + ".old";
        std::error_code ec;
        fs::remove(aside, ec);
        fs::rename(exe, aside, ec);
        if (ec) { set(Stage::Failed, "Could not move the running executable aside: " + ec.message()); return; }
    }
#endif
    auto restore = [&] {
        if (aside.empty()) return;
        std::error_code ec;
        fs::rename(aside, exe, ec);
    };
    note("Building... this takes a minute or two.");
    std::string cmd = quote(cmake) + " --build " + quote(bin) + " --target boomspc boomspc_assets --parallel";
    if (*build_info().build_type) cmd += " --config " + std::string(build_info().build_type);
    if (run(cmd, "") != 0) { restore(); set(Stage::Failed, "The build failed (see the log)."); return; }

    const std::string fresh = built_exe(bin);
    if (fresh.empty()) { restore(); set(Stage::Failed, "The build finished but no executable was found in " + bin + "."); return; }
    if (fs::path(fresh).lexically_normal() != fs::path(exe).lexically_normal()) {
        note("Installing...");
        std::string err;
        if (!put_file(fresh, exe, err)) { set(Stage::Failed, err); return; }
        std::error_code ec;
        fs::copy(fs::path(bin) / "assets", fs::path(exe_dir()) / "assets", fs::copy_options::overwrite_existing | fs::copy_options::recursive, ec);
        if (ec) log_append("assets were not copied: " + ec.message() + "\n");
    }
    std::string o;
    run("git rev-parse --short HEAD", src, &o);
    set(Stage::Built, "Built " + trim(o) + ". Restart BoomSPC to use it.");
}

void tools_thread() {
    std::string cmd;
    { std::lock_guard<std::mutex> lock(mutex()); cmd = shared().tools_command; }
    set(Stage::InstallingTools, "Installing the tools... answer the password prompt if one appears.");
    if (cmd.empty() || run(cmd, "") != 0) {
        set(Stage::Failed, "The tools could not be installed (see the log). Download the new version from the GitHub page instead.");
        return;
    }
    std::vector<std::string> missing = missing_tools();
    if (!missing.empty()) {
        std::string list;
        for (const std::string& m : missing) list += (list.empty() ? "" : ", ") + m;
        set(Stage::Failed, "Still missing after the install: " + list + ". Download the new version from the GitHub page instead.");
        return;
    }
    build_thread();
}

void start(void (*fn)()) {
    {
        std::lock_guard<std::mutex> lock(mutex());
        if (shared().busy()) return;
    }
    // Nothing here may stop and wait for a password: a private repo over
    // https fails instead of prompting.
#ifdef _WIN32
    _putenv("GIT_TERMINAL_PROMPT=0");
#else
    setenv("GIT_TERMINAL_PROMPT", "0", 1);
#endif
    std::thread(fn).detach();
}

}

void check() { start(check_thread); }
void download() { start(download_thread); }
void build() { start(build_thread); }
void install_tools() { start(tools_thread); }
void dismiss_tools() {
    std::lock_guard<std::mutex> lock(mutex());
    if (shared().stage == Stage::NeedTools) { shared().stage = Stage::Available; shared().message = "Update skipped: the tools are missing."; }
}

State state() {
    std::lock_guard<std::mutex> lock(mutex());
    return shared();
}

Stage stage() {
    std::lock_guard<std::mutex> lock(mutex());
    return shared().stage;
}

bool has_source() {
    const char* d = build_info().source_dir;
    return *d && fs::exists(fs::path(d) / ".git") && fs::exists(fs::path(d) / "CMakeLists.txt");
}

std::string source_dir() { return has_source() ? build_info().source_dir : clone_dir(); }

std::string releases_url() {
    std::string page;
    { std::lock_guard<std::mutex> lock(mutex()); page = shared().release_url; }
    if (!page.empty()) return page;
    const std::string slug = github_slug();
    return "https://github.com/" + (slug.empty() ? std::string("c4nvm/BoomSPC") : slug) + "/releases";
}

std::string exe_path() {
    static std::string path = [] {
        char buf[4096] = {};
#if defined(_WIN32)
        GetModuleFileNameA(nullptr, buf, sizeof buf - 1);
#elif defined(__APPLE__)
        uint32_t n = sizeof buf;
        _NSGetExecutablePath(buf, &n);
#else
        ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
        if (n > 0) buf[n] = 0;
#endif
        return std::string(buf);
    }();
    return path;
}

void cleanup_old() {
    std::error_code ec;
    for (const fs::path& dir : {fs::path(exe_dir()), fs::path(exe_dir()) / "assets"})
        for (const fs::directory_entry& e : fs::directory_iterator(dir, ec))
            if (e.is_regular_file(ec) && e.path().extension() == ".old") fs::remove(e.path(), ec);
    fs::remove_all(fs::path(exe_dir()) / "update-tmp", ec);
}

void restart() {
    const std::string exe = exe_path();
#ifdef _WIN32
    _spawnl(_P_NOWAIT, exe.c_str(), quote(exe).c_str(), nullptr);
#else
    execl(exe.c_str(), exe.c_str(), (char*)nullptr);
#endif
}

}
