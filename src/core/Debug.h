#pragma once

#include "output.h"
#include <string>
#include "Timer.h"

using namespace termcolor;

namespace opendcad {
    inline bool& debugQuiet() { static bool q = false; return q; }
}

#define DEBUG_INFO(s) { if (!opendcad::debugQuiet()) { std::cout << reset << bold << white << "[INFO ] [" << Timer::instance()->getTimeSinceStart() << "]  " << s << "\n"; } }
#define DEBUG_WARN(s) { if (!opendcad::debugQuiet()) { std::cout << reset << yellow << "[WARN ] [" << Timer::instance()->getTimeSinceStart() << "]  " << s << "\n"; } }
#define DEBUG_VERBOSE(s) { if (!opendcad::debugQuiet()) { std::cout << reset << yellow << "[VERB ] [" << Timer::instance()->getTimeSinceStart() << "]  " << s << "\n"; } }
#define DEBUG_ERROR(s) { std::cout << reset << red << "[ERROR] [" << Timer::instance()->getTimeSinceStart() << "]  " << s << "\n"; }
