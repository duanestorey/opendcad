#pragma once

#include <stdexcept>
#include <string>

namespace opendcad {

struct SourceLoc {
    std::string filename;
    size_t line = 0;
    size_t col = 0;

    std::string str() const {
        std::string result = filename.empty() ? "<input>" : filename;
        if (line > 0) {
            result += ":" + std::to_string(line);
            if (col > 0) result += ":" + std::to_string(col);
        }
        return result;
    }
};

class EvalError : public std::runtime_error {
public:
    EvalError(const std::string& msg, const SourceLoc& loc = {})
        : std::runtime_error(loc.line > 0 ? (loc.str() + ": " + msg) : msg)
        , loc_(loc) {}

    const SourceLoc& location() const { return loc_; }

private:
    SourceLoc loc_;
};

class GeometryError : public std::runtime_error {
public:
    explicit GeometryError(const std::string& msg)
        : std::runtime_error(msg) {}
};

} // namespace opendcad
