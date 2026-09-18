// What this binary was built from. Filled in by cmake/version.cmake at build
// time; the updater compares it with GitHub and rebuilds from the same tree.
#pragma once

struct Commit {
    const char* hash;
    const char* date;      // YYYY-MM-DD
    long long   time;      // unix time
    const char* subject;
    const char* body;
};

struct BuildInfo {
    const char* version;
    const char* commit;     // full hash, "" when built outside git, trailing '+' = local changes
    const char* branch;
    const char* date;
    long long   time;       // unix time of the commit, 0 when unknown
    const char* remote;     // origin URL of the source tree
    const char* source_dir;
    const char* binary_dir;
    const char* cmake;      // the cmake that configured the build
    const char* generator;
    const char* build_type;
    const Commit* log;      // newest first
    int log_count;
};

const BuildInfo& build_info();
