// Self-update. The check compares the built commit with the branch on GitHub
// and looks at the latest GitHub release. An update either downloads the
// release asset built for this platform, or pulls the source tree this
// binary was built from (or clones one next to the executable) and rebuilds
// it with the same cmake setup, offering to install the toolchain when it
// is missing. Everything runs on a worker thread; the UI polls state().
#pragma once

#include <string>
#include <vector>

namespace update {

enum class Stage {
    Idle,            // nothing checked yet
    Checking,
    UpToDate,
    Available,       // GitHub has something newer
    NeedTools,       // the build path is missing tools; the UI asks what to do
    InstallingTools,
    Updating,        // download / pull / build in progress
    Built,           // new binary in place, restart to use it
    Failed,
};

struct Incoming {
    std::string hash, date, subject, body;
    long long time = 0;
};

struct State {
    Stage stage = Stage::Idle;
    std::string message;             // one line for the panel
    std::string remote_commit;       // newest commit on the branch, when known
    std::vector<Incoming> incoming;  // commits the update would bring, newest first (empty when unknown)
    std::string release_tag;         // latest release newer than this build, "" = none
    std::string release_asset;       // its asset for this platform, "" = none
    std::string release_url;         // its page
    std::vector<std::string> missing_tools;   // for NeedTools
    std::string tools_command;       // what "install them" would run
    std::string log;                 // output of the commands run so far
    bool busy() const { return stage == Stage::Checking || stage == Stage::Updating || stage == Stage::InstallingTools; }
};

void  check();           // fetch and compare; no-op while busy
void  download();        // install the release asset; no-op while busy
void  build();           // pull/clone + build + swap the executable; goes to NeedTools first when something is missing
void  install_tools();   // from NeedTools: run the package manager, then build()
void  dismiss_tools();   // from NeedTools: back to Available
State state();
Stage stage();           // cheaper than state() for a per-frame poll
bool  has_source();      // the tree this binary was built from is still there
std::string source_dir();    // where the build path pulls to (built tree or the clone next to the exe)
std::string releases_url();  // the GitHub releases page
std::string version_label(); // "0.4.0", or "0.4.0 +3" for commits past the tag, or the project version when untagged
bool  parse_version(const std::string& tag, int out[3]);   // "v0.4.0" / "0.4.0"

std::string exe_path();
void  cleanup_old();     // remove files an earlier update renamed aside
void  restart();         // replaces this process with the executable at exe_path(); call after SDL_Quit

}
