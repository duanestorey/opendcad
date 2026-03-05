#pragma once

#include "Value.h"
#include "Shape.h"
#include <string>
#include <functional>
#include <unordered_map>
#include <vector>

namespace opendcad {

using ShapeFactory = std::function<ShapePtr(const std::vector<ValuePtr>& args)>;
using ShapeMethod = std::function<ValuePtr(ShapePtr self, const std::vector<ValuePtr>& args)>;

class ShapeRegistry {
public:
    static ShapeRegistry& instance();

    void registerFactory(const std::string& name, ShapeFactory fn);
    void registerMethod(const std::string& name, ShapeMethod fn);

    bool hasFactory(const std::string& name) const;
    bool hasMethod(const std::string& name) const;

    ShapePtr callFactory(const std::string& name, const std::vector<ValuePtr>& args) const;
    ValuePtr callMethod(const std::string& name, ShapePtr self, const std::vector<ValuePtr>& args) const;

    void registerDefaults();

private:
    ShapeRegistry() = default;
    std::unordered_map<std::string, ShapeFactory> factories_;
    std::unordered_map<std::string, ShapeMethod> methods_;
};

} // namespace opendcad
