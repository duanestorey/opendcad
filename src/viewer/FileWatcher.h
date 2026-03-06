#pragma once

#include <string>
#include <vector>
#include <ctime>

namespace opendcad {

class FileWatcher {
public:
    FileWatcher();
    ~FileWatcher();

    void watch(const std::string& path);
    void addPath(const std::string& path);
    bool poll();  // returns true if any watched file changed

private:
    struct WatchedFile {
        std::string path;
        std::time_t lastModified = 0;
    };

    std::vector<WatchedFile> files_;

    static std::time_t getModTime(const std::string& path);

#ifdef __linux__
    int inotifyFd_ = -1;
    std::vector<int> watchDescriptors_;
    void initInotify();
    bool pollInotify();
#endif
};

} // namespace opendcad
