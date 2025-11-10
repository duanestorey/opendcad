#pragma once
#include <iostream>

namespace termcolor {

inline std::ostream& reset(std::ostream& os)   { return os << "\033[0m";  }
inline std::ostream& red(std::ostream& os)     { return os << "\033[31m"; }
inline std::ostream& green(std::ostream& os)   { return os << "\033[32m"; }
inline std::ostream& yellow(std::ostream& os)  { return os << "\033[33m"; }
inline std::ostream& blue(std::ostream& os)    { return os << "\033[34m"; }
inline std::ostream& magenta(std::ostream& os) { return os << "\033[35m"; }
inline std::ostream& cyan(std::ostream& os)    { return os << "\033[36m"; }
inline std::ostream& white(std::ostream& os)   { return os << "\033[37m"; }
inline std::ostream& bold(std::ostream& os)    { return os << "\033[1m";  }

} // namespace termcolor