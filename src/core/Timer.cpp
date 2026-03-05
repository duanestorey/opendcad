#include "Timer.h"
#include <iostream>

using namespace std;
using namespace chrono;

Timer *Timer::mInstance = 0;

Timer *
Timer::instance() {
    if ( mInstance == 0 ) {
        mInstance = new Timer();
    }
    
    return mInstance;
}

Timer::Timer() : mStartTime( high_resolution_clock::now() ) {}

std::string 
Timer::getTimeSinceStart() const {
    char temp[256];
    auto tm_duration = std::chrono::duration<double, std::milli>(high_resolution_clock::now() - mStartTime).count();
    snprintf( temp, sizeof temp, "%7.3f s", tm_duration/1000.0f );
    return std::string( temp );
}