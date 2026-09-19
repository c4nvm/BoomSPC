#include "crash.hpp"

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <atomic>
#include <exception>

#include "version.hpp"

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#define WRITE _write
#define CLOSE _close
#else
#include <dlfcn.h>
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>
#define WRITE write
#define CLOSE close
#endif

namespace crash {
namespace {

// Everything the handlers touch is preallocated: a crash may sit inside malloc.
char g_log[1024];
char g_pending[1024];
char g_header[1024];
char g_ctx[CTX_COUNT][512];
const char* const kCtxNames[CTX_COUNT] = {"file", "project", "driver", "song", "status", "action"};
std::atomic<int> g_busy{0};

void put(int fd, const char* s) { if (s) (void)WRITE(fd, s, unsigned(std::strlen(s))); }

void put_hex(int fd, uintptr_t v) {
    char b[2 + sizeof(v) * 2 + 1];
    int n = 0;
    b[n++] = '0'; b[n++] = 'x';
    bool started = false;
    for (int i = int(sizeof(v) * 2) - 1; i >= 0; --i) {
        const int d = int((v >> (i * 4)) & 0xF);
        if (!d && !started && i) continue;
        started = true;
        b[n++] = "0123456789abcdef"[d];
    }
    b[n] = 0;
    put(fd, b);
}

void put_dec(int fd, long long v) {
    char b[24]; int n = 0;
    if (v < 0) { put(fd, "-"); v = -v; }
    do { b[n++] = char('0' + v % 10); v /= 10; } while (v);
    char out[24]; int m = 0;
    while (n) out[m++] = b[--n];
    out[m] = 0;
    put(fd, out);
}

void put_pad2(int fd, int v) { if (v < 10) put(fd, "0"); put_dec(fd, v); }

// UTC date from a unix time without the C library (localtime is not signal safe).
void put_time(int fd, long long t) {
    const long long days = t / 86400, secs = t % 86400;
    const long long z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = unsigned(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
    const long long y = (long long)yoe + era * 400 + (m <= 2);
    put_dec(fd, y); put(fd, "-"); put_pad2(fd, int(m)); put(fd, "-"); put_pad2(fd, int(d));
    put(fd, " "); put_pad2(fd, int(secs / 3600)); put(fd, ":"); put_pad2(fd, int(secs / 60 % 60)); put(fd, ":"); put_pad2(fd, int(secs % 60));
    put(fd, " UTC");
}

const char* base_name(const char* path) {
    const char* b = path;
    for (const char* p = path; *p; ++p) if (*p == '/' || *p == '\\') b = p + 1;
    return b;
}

int open_log() {
#ifdef _WIN32
    return _open(g_log, _O_WRONLY | _O_CREAT | _O_APPEND | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
    return open(g_log, O_WRONLY | O_CREAT | O_APPEND, 0644);
#endif
}

void touch_pending() {
#ifdef _WIN32
    int fd = _open(g_pending, _O_WRONLY | _O_CREAT | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
    int fd = open(g_pending, O_WRONLY | O_CREAT, 0644);
#endif
    if (fd >= 0) CLOSE(fd);
}

// "#3  boomspc@0x412345  symbol+0x1a": the module address as addr2line wants
// it (link-time address, so the load base is undone for PIE and ASLR).
void put_frame(int fd, int i, const void* addr) {
    put(fd, "  #"); put_dec(fd, i); put(fd, i < 10 ? "   " : "  ");
#ifdef _WIN32
    HMODULE hm = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, LPCSTR(addr), &hm) && hm) {
        // The loader rewrites ImageBase in the mapped header, so print the RVA:
        // addr2line wants the link-time base plus it (0x140000000 for the mingw exes).
        char name[MAX_PATH] = {};
        GetModuleFileNameA(hm, name, sizeof name);
        put(fd, base_name(name)); put(fd, "+"); put_hex(fd, uintptr_t(addr) - uintptr_t(hm));
    } else {
        put_hex(fd, uintptr_t(addr));
    }
#else
    Dl_info info;
    if (dladdr(addr, &info) && info.dli_fname && info.dli_fbase) {
        const bool pie = reinterpret_cast<const unsigned char*>(info.dli_fbase)[16] == 3;   // ELF e_type ET_DYN
        put(fd, base_name(info.dli_fname)); put(fd, "@"); put_hex(fd, pie ? uintptr_t(addr) - uintptr_t(info.dli_fbase) : uintptr_t(addr));
        if (info.dli_sname) { put(fd, "  "); put(fd, info.dli_sname); put(fd, "+"); put_hex(fd, uintptr_t(addr) - uintptr_t(info.dli_saddr)); }
    } else {
        put_hex(fd, uintptr_t(addr));
    }
#endif
    put(fd, "\n");
}

void put_report_head(int fd, const char* what, const void* fault) {
    put(fd, "\n==== BoomSPC crash  ");
    put_time(fd, (long long)std::time(nullptr));
    put(fd, " ====\n");
    put(fd, g_header);
    put(fd, "what: "); put(fd, what);
    if (fault) { put(fd, " at "); put_hex(fd, uintptr_t(fault)); }
    put(fd, "\n");
    for (int i = 0; i < CTX_COUNT; ++i) {
        put(fd, kCtxNames[i]); put(fd, ": "); put(fd, g_ctx[i]); put(fd, "\n");
    }
    put(fd, "stack:\n");
}

#ifdef _WIN32

void write_stack_here(int fd) {
    void* frames[64];
    const USHORT n = CaptureStackBackTrace(0, 64, frames, nullptr);
    for (USHORT i = 0; i < n; ++i) put_frame(fd, i, frames[i]);
}

const char* exception_name(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:      return "access violation";
        case EXCEPTION_STACK_OVERFLOW:        return "stack overflow";
        case EXCEPTION_ILLEGAL_INSTRUCTION:   return "illegal instruction";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "integer divide by zero";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "array bounds exceeded";
        case EXCEPTION_PRIV_INSTRUCTION:      return "privileged instruction";
        case EXCEPTION_IN_PAGE_ERROR:         return "in-page error";
        default:                              return "exception";
    }
}

LONG WINAPI on_exception(EXCEPTION_POINTERS* ep) {
    if (g_busy++) return EXCEPTION_CONTINUE_SEARCH;
    const int fd = open_log();
    if (fd >= 0) {
        const DWORD code = ep->ExceptionRecord->ExceptionCode;
        put_report_head(fd, exception_name(code), ep->ExceptionRecord->ExceptionAddress);
        put(fd, "  code "); put_hex(fd, code);
        if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
            put(fd, ep->ExceptionRecord->ExceptionInformation[0] ? "  writing " : "  reading ");
            put_hex(fd, uintptr_t(ep->ExceptionRecord->ExceptionInformation[1]));
        }
        put(fd, "\n");
        // Walk from the faulting context: the filter's own frames are of no use.
        CONTEXT ctx = *ep->ContextRecord;
        int i = 0;
        put_frame(fd, i++, ep->ExceptionRecord->ExceptionAddress);
#if defined(_M_X64) || defined(__x86_64__)
        while (i < 64) {
            DWORD64 base = 0;
            RUNTIME_FUNCTION* rf = RtlLookupFunctionEntry(ctx.Rip, &base, nullptr);
            if (!rf) {   // leaf function: the return address sits at rsp
                ctx.Rip = *reinterpret_cast<DWORD64*>(ctx.Rsp);
                ctx.Rsp += 8;
            } else {
                void* handler_data = nullptr; DWORD64 frame = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, ctx.Rip, rf, &ctx, &handler_data, &frame, nullptr);
            }
            if (!ctx.Rip) break;
            put_frame(fd, i++, reinterpret_cast<void*>(ctx.Rip));
        }
#else
        write_stack_here(fd);
#endif
        CLOSE(fd);
    }
    touch_pending();
    return EXCEPTION_EXECUTE_HANDLER;   // end now, without the error-reporting pause
}

void on_abort(int) {
    if (g_busy++) return;
    const int fd = open_log();
    if (fd >= 0) { put_report_head(fd, "abort", nullptr); write_stack_here(fd); CLOSE(fd); }
    touch_pending();
    signal(SIGABRT, SIG_DFL);
}

#else

const char* signal_name(int sig) {
    switch (sig) {
        case SIGSEGV: return "segmentation fault";
        case SIGBUS:  return "bus error";
        case SIGFPE:  return "arithmetic error";
        case SIGILL:  return "illegal instruction";
        case SIGABRT: return "abort";
        default:      return "signal";
    }
}

void write_stack_here(int fd) {
    void* frames[64];
    const int n = backtrace(frames, 64);
    for (int i = 0; i < n; ++i) put_frame(fd, i, frames[i]);
}

void on_signal(int sig, siginfo_t* si, void*) {
    if (g_busy++) _exit(128 + sig);
    const int fd = open_log();
    if (fd >= 0) {
        put_report_head(fd, signal_name(sig), sig == SIGABRT ? nullptr : si->si_addr);
        write_stack_here(fd);
        CLOSE(fd);
    }
    touch_pending();
    signal(sig, SIG_DFL);
    raise(sig);
}

#endif

void on_terminate() {
    if (g_busy) std::abort();
    const char* what = "uncaught exception";
    char buf[512];
    try {
        if (std::exception_ptr p = std::current_exception()) std::rethrow_exception(p);
    } catch (const std::exception& e) {
        std::snprintf(buf, sizeof buf, "uncaught exception: %s", e.what());
        what = buf;
    } catch (...) {
        what = "uncaught exception (not a std::exception)";
    }
    g_busy++;
    const int fd = open_log();
    if (fd >= 0) { put_report_head(fd, what, nullptr); write_stack_here(fd); CLOSE(fd); }
    touch_pending();
    signal(SIGABRT, SIG_DFL);
    std::abort();
}

}

void install(const std::string& path) {
    std::snprintf(g_log, sizeof g_log, "%s", path.c_str());
    std::snprintf(g_pending, sizeof g_pending, "%s.pending", path.c_str());
    const BuildInfo& bi = build_info();
    std::snprintf(g_header, sizeof g_header, "build: %s  commit %s  branch %s  %s  %s\nplatform: %s\n",
                  bi.version, *bi.commit ? bi.commit : "(no git)", bi.branch, bi.date, bi.build_type,
#ifdef _WIN32
                  "windows"
#elif defined(__APPLE__)
                  "macos"
#else
                  "linux"
#endif
    );
    for (auto& c : g_ctx) c[0] = 0;
    std::set_terminate(on_terminate);
#ifdef _WIN32
    SetUnhandledExceptionFilter(on_exception);
    signal(SIGABRT, on_abort);
#else
    static char alt[64 * 1024];   // a stack overflow needs somewhere to run the handler
    stack_t ss = {};
    ss.ss_sp = alt; ss.ss_size = sizeof alt;
    sigaltstack(&ss, nullptr);
    struct sigaction sa = {};
    sa.sa_sigaction = on_signal;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    sigemptyset(&sa.sa_mask);
    for (int sig : {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT}) sigaction(sig, &sa, nullptr);
#endif
}

void set_context(Context which, const char* value) {
    if (which < 0 || which >= CTX_COUNT) return;
    std::snprintf(g_ctx[which], sizeof g_ctx[which], "%s", value ? value : "");
}

bool take_pending() {
    if (!*g_pending) return false;
    FILE* f = std::fopen(g_pending, "r");
    if (!f) return false;
    std::fclose(f);
    std::remove(g_pending);
    return true;
}

std::string log_path() { return g_log; }

}
