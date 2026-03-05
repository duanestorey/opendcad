#pragma once

#include "Value.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <memory>

namespace opendcad {

class Environment;
using EnvironmentPtr = std::shared_ptr<Environment>;

class Environment {
public:
    explicit Environment(EnvironmentPtr parent = nullptr);

    void define(const std::string& name, ValuePtr value);
    void defineConst(const std::string& name, ValuePtr value);
    void assign(const std::string& name, ValuePtr value);
    ValuePtr lookup(const std::string& name) const;
    bool has(const std::string& name) const;
    bool isConst(const std::string& name) const;

    EnvironmentPtr parent() const { return parent_; }

private:
    EnvironmentPtr parent_;
    std::unordered_map<std::string, ValuePtr> bindings_;
    std::unordered_set<std::string> constants_;
};

} // namespace opendcad
