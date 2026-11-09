// LD_PRELOAD recorder: appends every path the process opens (open/openat/fopen families) to
// $VIXEN_CLOSURE_OUT/<pid>.paths, once per distinct path. The recording is the process's input
// closure as it actually ran: shader modules (SPIR-V), libraries, recipes, fonts, scene assets.
// closure_manifest merges the recordings into the affected-test manifest.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_set>

namespace {
std::mutex gMutex;
std::unordered_set<std::string>* gSeen = nullptr;
int gFd = -1;
pid_t gPid = 0;
thread_local bool tRecording = false;

template <class Fn> Fn Next(const char* name) { return reinterpret_cast<Fn>(dlsym(RTLD_NEXT, name)); }

void Record(int dirFd, const char* path) {
    if (!path || !*path || tRecording) return;
    tRecording = true;
    {
        std::lock_guard lock(gMutex);
        const char* dir = std::getenv("VIXEN_CLOSURE_OUT");
        if (dir && *dir) {
            if (gFd < 0 || gPid != getpid()) {  // first use, or a forked child with its own recording
                if (gFd >= 0) close(gFd);
                delete gSeen;
                gSeen = new std::unordered_set<std::string>();
                gPid = getpid();
                const std::string file = std::string(dir) + "/" + std::to_string(gPid) + ".paths";
                using OpenFn = int (*)(const char*, int, ...);
                gFd = Next<OpenFn>("open")(file.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
            }
            if (gFd >= 0) {
                std::string full = path;
                char cwd[PATH_MAX];
                if (path[0] != '/' && (dirFd == AT_FDCWD || dirFd < 0) && getcwd(cwd, sizeof cwd))
                    full = std::string(cwd) + "/" + path;
                if (gSeen->insert(full).second) {
                    full += '\n';
                    [[maybe_unused]] const ssize_t written = write(gFd, full.data(), full.size());
                }
            }
        }
    }
    tRecording = false;
}
}  // namespace

extern "C" {
int open(const char* path, int flags, ...) {
    mode_t mode = 0;
    if (flags & (O_CREAT | O_TMPFILE)) { va_list args; va_start(args, flags); mode = va_arg(args, mode_t); va_end(args); }
    Record(AT_FDCWD, path);
    return Next<int (*)(const char*, int, ...)>("open")(path, flags, mode);
}
int open64(const char* path, int flags, ...) {
    mode_t mode = 0;
    if (flags & (O_CREAT | O_TMPFILE)) { va_list args; va_start(args, flags); mode = va_arg(args, mode_t); va_end(args); }
    Record(AT_FDCWD, path);
    return Next<int (*)(const char*, int, ...)>("open64")(path, flags, mode);
}
int openat(int dirFd, const char* path, int flags, ...) {
    mode_t mode = 0;
    if (flags & (O_CREAT | O_TMPFILE)) { va_list args; va_start(args, flags); mode = va_arg(args, mode_t); va_end(args); }
    Record(dirFd, path);
    return Next<int (*)(int, const char*, int, ...)>("openat")(dirFd, path, flags, mode);
}
int openat64(int dirFd, const char* path, int flags, ...) {
    mode_t mode = 0;
    if (flags & (O_CREAT | O_TMPFILE)) { va_list args; va_start(args, flags); mode = va_arg(args, mode_t); va_end(args); }
    Record(dirFd, path);
    return Next<int (*)(int, const char*, int, ...)>("openat64")(dirFd, path, flags, mode);
}
FILE* fopen(const char* path, const char* mode) {
    Record(AT_FDCWD, path);
    return Next<FILE* (*)(const char*, const char*)>("fopen")(path, mode);
}
FILE* fopen64(const char* path, const char* mode) {
    Record(AT_FDCWD, path);
    return Next<FILE* (*)(const char*, const char*)>("fopen64")(path, mode);
}
}
