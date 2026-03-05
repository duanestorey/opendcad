#include "Environment.h"
#include "Error.h"

namespace opendcad {

Environment::Environment(EnvironmentPtr parent)
    : parent_(std::move(parent)) {}

void Environment::define(const std::string& name, ValuePtr value) {
    bindings_[name] = std::move(value);
}

ValuePtr Environment::lookup(const std::string& name) const {
    auto it = bindings_.find(name);
    if (it != bindings_.end())
        return it->second;
    if (parent_)
        return parent_->lookup(name);
    throw EvalError("undefined variable '" + name + "'");
}

bool Environment::has(const std::string& name) const {
    if (bindings_.count(name))
        return true;
    if (parent_)
        return parent_->has(name);
    return false;
}

} // namespace opendcad
