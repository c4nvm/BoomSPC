// Crash log. A fatal signal, an unhandled Windows exception or an uncaught
// C++ exception appends a report to crash.log in the config folder: build,
// what was loaded, the last status line and action, and the call stack as
// module+offset (addr2line / c++filt resolve them against the same build).
// A marker file tells the next run that this one died.
#pragma once

#include <string>

namespace crash {

enum Context { CTX_FILE, CTX_PROJECT, CTX_DRIVER, CTX_SONG, CTX_STATUS, CTX_ACTION, CTX_COUNT };

void install(const std::string& log_path);   // handlers write log_path and log_path + ".pending"
void set_context(Context which, const char* value);
bool take_pending();                         // true once after a crash; clears the marker
std::string log_path();

}
