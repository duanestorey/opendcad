#pragma once

#include <chrono>
#include <memory>
#include <string>

using namespace std;
using namespace chrono;

class Timer {
public:
    static Timer *instance();
    std::string getTimeSinceStart() const;
protected:
    static Timer *mInstance;
    high_resolution_clock::time_point mStartTime;
    
private:
    Timer();
};
