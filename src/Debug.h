#pragma once

#include "output.h"
#include <string>
#include "Timer.h"

using namespace termcolor;

#define DEBUG_INFO(s) { std::cout << reset << bold << white << "[INFO ] [" << Timer::instance()->getTimeSinceStart() << "]  " << s << "\n"; }
#define DEBUG_WARN(s) { std::cout << reset << yellow << "[WARN ] [" << Timer::instance()->getTimeSinceStart() << "]  " << s << "\n"; }
#define DEBUG_VERBOSE(s) { std::cout << reset << yellow << "[VERB ] [" << Timer::instance()->getTimeSinceStart() << "]  " << s << "\n"; }
#define DEBUG_ERROR(s) { std::cout << reset << red << "[ERROR] [" << Timer::instance()->getTimeSinceStart() << "]  " << s << "\n"; }