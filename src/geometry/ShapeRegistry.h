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
using TypedMethod = std::function<ValuePtr(ValuePtr self, const std::vector<ValuePtr>& args)>;

class ShapeRegistry {
public:
    static ShapeRegistry& instance();

    void registerFactory(const std::string& name, ShapeFactory fn);
    void registerMethod(const std::string& name, ShapeMethod fn);

    // Typed method dispatch (works on any ValueType, not just SHAPE)
    void registerTypedMethod(ValueType type, const std::string& name, TypedMethod fn);
    bool hasTypedMethod(ValueType type, const std::string& name) const;
    ValuePtr callTypedMethod(ValueType type, const std::string& name,
                             ValuePtr self, const std::vector<ValuePtr>& args) const;

    bool hasFactory(const std::string& name) const;
    bool hasMethod(const std::string& name) const;

    ShapePtr callFactory(const std::string& name, const std::vector<ValuePtr>& args) const;
    ValuePtr callMethod(const std::string& name, ShapePtr self, const std::vector<ValuePtr>& args) const;

    void registerDefaults();

private:
    ShapeRegistry() = default;
    std::unordered_map<std::string, ShapeFactory> factories_;
    std::unordered_map<std::string, ShapeMethod> methods_;
    std::unordered_map<int, std::unordered_map<std::string, TypedMethod>> typedMethods_;
};

} // namespace opendcad
