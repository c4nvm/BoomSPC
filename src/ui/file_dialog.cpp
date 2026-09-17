#include "file_dialog.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace filedlg {
namespace {
std::string g_error;

#if defined(__linux__)

std::string shq(const std::string& s) {
    std::string out = "'";
    for (char c : s) { if (c == '\'') out += "'\\''"; else out += c; }
    return out + "'";
}

std::string run(const std::string& cmd, int& status) {
    std::string out;
    FILE* p = popen((cmd + " 2>/dev/null").c_str(), "r");
    if (!p) { status = 127; return out; }
    char buf[4096];
    while (std::fgets(buf, sizeof buf, p)) out += buf;
    int rc = pclose(p);
    status = rc == -1 ? 127 : (rc >> 8) & 0xFF;
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
}

bool prefer_kde() {
    const char* d = std::getenv("XDG_CURRENT_DESKTOP");
    return d && std::strstr(d, "KDE");
}

std::string zenity(bool save, const std::string& title, const std::vector<Filter>& filters, const std::string& start, int& status) {
    std::string cmd = "zenity --file-selection --title=" + shq(title);
    if (save) cmd += " --save --confirm-overwrite";
    if (!start.empty()) cmd += " --filename=" + shq(start);
    for (const Filter& f : filters) cmd += " --file-filter=" + shq(f.name + " | " + f.patterns);
    return run(cmd, status);
}

std::string kdialog(bool save, const std::string& title, const std::vector<Filter>& filters, const std::string& start, int& status) {
    std::string filt;
    for (const Filter& f : filters) { if (!filt.empty()) filt += "\n"; filt += f.patterns + "|" + f.name; }
    std::string cmd = std::string("kdialog --title ") + shq(title) + (save ? " --getsavefilename " : " --getopenfilename ") +
                      shq(start.empty() ? "." : start) + " " + shq(filt);
    return run(cmd, status);
}

std::string native(bool save, const std::string& title, const std::vector<Filter>& filters, const std::string& start) {
    g_error.clear();
    typedef std::string (*Backend)(bool, const std::string&, const std::vector<Filter>&, const std::string&, int&);
    Backend order[2] = {&zenity, &kdialog};
    if (prefer_kde()) { order[0] = &kdialog; order[1] = &zenity; }
    for (Backend b : order) {
        int status = 0;
        std::string path = b(save, title, filters, start, status);
        if (status == 127) continue;
        if (status != 0 || path.empty()) return {};
        return path;
    }
    g_error = "no file dialog found: install zenity or kdialog";
    return {};
}

#elif defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), &w[0], n);
    return w;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

std::string native(bool save, const std::string& title, const std::vector<Filter>& filters, const std::string& start) {
    g_error.clear();
    std::wstring filt;
    for (const Filter& f : filters) {
        std::string pats = f.patterns;
        for (char& c : pats) if (c == ' ') c = ';';
        filt += widen(f.name + " (" + f.patterns + ")"); filt.push_back(0);
        filt += widen(pats); filt.push_back(0);
    }
    filt.push_back(0);
    wchar_t file[4096] = {};
    std::wstring initial_dir;
    if (!start.empty()) {
        std::string dir = start, base;
        size_t slash = start.find_last_of("/\\");
        if (start.back() == '/' || start.back() == '\\') dir = start.substr(0, start.size() - 1);
        else if (slash != std::string::npos) { dir = start.substr(0, slash); base = start.substr(slash + 1); }
        else { dir.clear(); base = start; }
        initial_dir = widen(dir);
        std::wstring wb = widen(base);
        wcsncpy(file, wb.c_str(), 4095);
    }
    std::wstring wtitle = widen(title);
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof ofn;
    ofn.lpstrFilter = filt.c_str();
    ofn.lpstrFile = file;
    ofn.nMaxFile = 4096;
    ofn.lpstrTitle = wtitle.c_str();
    ofn.lpstrInitialDir = initial_dir.empty() ? nullptr : initial_dir.c_str();
    ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    const BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    if (!ok) return {};
    return narrow(file);
}

#elif defined(__APPLE__)

std::string native(bool save, const std::string& title, const std::vector<Filter>& filters, const std::string& start) {
    (void)filters;
    g_error.clear();
    auto asq = [](const std::string& s) { std::string o = "\""; for (char c : s) { if (c == '"' || c == '\\') o += '\\'; o += c; } return o + "\""; };
    std::string script;
    if (save) {
        std::string dir = start, base = "edited.spc";
        size_t slash = start.find_last_of('/');
        if (slash != std::string::npos) { dir = start.substr(0, slash + 1); base = start.substr(slash + 1); }
        script = "POSIX path of (choose file name with prompt " + asq(title) + " default name " + asq(base) +
                 (dir.empty() ? "" : " default location POSIX file " + asq(dir)) + ")";
    } else {
        script = "POSIX path of (choose file with prompt " + asq(title) + (start.empty() ? "" : " default location POSIX file " + asq(start)) + ")";
    }
    std::string cmd = "osascript -e ";
    std::string sq = "'";
    for (char c : script) { if (c == '\'') sq += "'\\''"; else sq += c; }
    sq += "'";
    cmd += sq + " 2>/dev/null";
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) { g_error = "osascript failed"; return {}; }
    std::string out;
    char buf[4096];
    while (std::fgets(buf, sizeof buf, p)) out += buf;
    pclose(p);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
}

#else

std::string native(bool save, const std::string& title, const std::vector<Filter>& filters, const std::string& start) {
    (void)save; (void)title; (void)filters; (void)start;
    g_error = "no native file dialog on this platform; type the path instead";
    return {};
}

#endif

}

std::string open_file(const std::string& title, const std::vector<Filter>& filters, const std::string& start_dir) {
    std::string start = start_dir;
    if (!start.empty() && start.back() != '/') start += '/';
    return native(false, title, filters, start);
}

std::string save_file(const std::string& title, const std::vector<Filter>& filters, const std::string& suggested_path) {
    return native(true, title, filters, suggested_path);
}

const std::string& last_error() { return g_error; }

}
