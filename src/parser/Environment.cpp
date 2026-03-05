#include "Environment.h"
#include "Error.h"

namespace opendcad {

Environment::Environment(EnvironmentPtr parent)
    : parent_(std::move(parent)) {}

void Environment::define(const std::string& name, ValuePtr value) {
    bindings_[name] = std::move(value);
}

void Environment::defineConst(const std::string& name, ValuePtr value) {
    bindings_[name] = std::move(value);
    constants_.insert(name);
}

void Environment::assign(const std::string& name, ValuePtr value) {
    // Walk the chain to find where the name is defined
    auto it = bindings_.find(name);
    if (it != bindings_.end()) {
        if (constants_.count(name))
            throw EvalError("cannot reassign const '" + name + "'");
        it->second = std::move(value);
        return;
    }
    if (parent_) {
        parent_->assign(name, std::move(value));
        return;
    }
    throw EvalError("undefined variable '" + name + "'");
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

bool Environment::isConst(const std::string& name) const {
    if (constants_.count(name))
        return true;
    if (parent_)
        return parent_->isConst(name);
    return false;
}

} // namespace opendcad
