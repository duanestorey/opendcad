#include "FileWatcher.h"

#include <sys/stat.h>
#include <cstdio>

#ifdef __linux__
#include <sys/inotify.h>
#include <unistd.h>
#include <poll.h>
#include <climits>
#endif

#ifdef __APPLE__
#include <sys/event.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace opendcad {

// ---------------------------------------------------------------------------
// Portable: stat()-based modification time
// ---------------------------------------------------------------------------

std::time_t FileWatcher::getModTime(const std::string& path) {
    struct stat st{};
    if (stat(path.c_str(), &st) != 0) return 0;
    return st.st_mtime;
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

FileWatcher::FileWatcher() {
#ifdef __linux__
    initInotify();
#endif
}

FileWatcher::~FileWatcher() {
#ifdef __linux__
    for (int wd : watchDescriptors_) {
        if (inotifyFd_ >= 0 && wd >= 0) {
            inotify_rm_watch(inotifyFd_, wd);
        }
    }
    if (inotifyFd_ >= 0) {
        close(inotifyFd_);
        inotifyFd_ = -1;
    }
#endif
}

// ---------------------------------------------------------------------------
// watch() — start watching a single file (primary entry point)
// ---------------------------------------------------------------------------

void FileWatcher::watch(const std::string& path) {
    addPath(path);
}

// ---------------------------------------------------------------------------
// addPath() — add a file to the watch list
// ---------------------------------------------------------------------------

void FileWatcher::addPath(const std::string& path) {
    WatchedFile wf;
    wf.path = path;
    wf.lastModified = getModTime(path);
    files_.push_back(wf);

#ifdef __linux__
    if (inotifyFd_ >= 0) {
        int wd = inotify_add_watch(inotifyFd_, path.c_str(),
                                    IN_MODIFY | IN_CLOSE_WRITE | IN_MOVE_SELF);
        watchDescriptors_.push_back(wd);
    }
#endif
}

// ---------------------------------------------------------------------------
// Linux: inotify
// ---------------------------------------------------------------------------

#ifdef __linux__
void FileWatcher::initInotify() {
    inotifyFd_ = inotify_init1(IN_NONBLOCK);
    if (inotifyFd_ < 0) {
        std::fprintf(stderr, "FileWatcher: inotify_init1 failed, falling back to stat()\n");
    }
}

bool FileWatcher::pollInotify() {
    if (inotifyFd_ < 0) return false;

    struct pollfd pfd = { inotifyFd_, POLLIN, 0 };
    if (::poll(&pfd, 1, 0) <= 0) return false;

    // Drain all pending events
    bool changed = false;
    char buf[sizeof(struct inotify_event) + NAME_MAX + 1];
    while (true) {
        ssize_t len = read(inotifyFd_, buf, sizeof(buf));
        if (len <= 0) break;
        changed = true;
    }
    return changed;
}
#endif

// ---------------------------------------------------------------------------
// poll() — check if any watched file has changed
// ---------------------------------------------------------------------------

bool FileWatcher::poll() {
#ifdef __linux__
    if (inotifyFd_ >= 0 && pollInotify()) return true;
#endif

    // stat() fallback / cross-platform check
    bool changed = false;
    for (auto& wf : files_) {
        auto mt = getModTime(wf.path);
        if (mt > wf.lastModified) {
            wf.lastModified = mt;
            changed = true;
        }
    }
    return changed;
}

} // namespace opendcad
