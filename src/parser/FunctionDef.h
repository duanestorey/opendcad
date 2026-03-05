#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include "Value.h"

// Forward declare ANTLR types to avoid including the full parser header
namespace OpenDCAD { class OpenDCADParser; }
namespace antlr4 { class ParserRuleContext; }

namespace opendcad {

class Environment;
using EnvironmentPtr = std::shared_ptr<Environment>;

struct FunctionDef {
    std::string name;
    std::vector<std::string> params;
    std::unordered_map<std::string, ValuePtr> defaults;
    antlr4::ParserRuleContext* body = nullptr;  // points into parse tree (not owned)
    EnvironmentPtr closure;
};

} // namespace opendcad
