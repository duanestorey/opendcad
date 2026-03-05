#pragma once

#include <memory>
#include <string>
#include <vector>

namespace opendcad {

class Shape;
using ShapePtr = std::shared_ptr<Shape>;

class Value;
using ValuePtr = std::shared_ptr<Value>;

enum class ValueType { NUMBER, STRING, BOOL, VECTOR, SHAPE, NIL };

class Value {
public:
    static ValuePtr makeNumber(double v);
    static ValuePtr makeString(const std::string& v);
    static ValuePtr makeBool(bool v);
    static ValuePtr makeVector(const std::vector<double>& v);
    static ValuePtr makeShape(ShapePtr v);
    static ValuePtr makeNil();

    ValueType type() const { return type_; }
    std::string typeName() const;

    double asNumber() const;
    const std::string& asString() const;
    bool asBool() const;
    const std::vector<double>& asVector() const;
    ShapePtr asShape() const;
    bool isTruthy() const;

    ValuePtr add(const ValuePtr& other) const;
    ValuePtr subtract(const ValuePtr& other) const;
    ValuePtr multiply(const ValuePtr& other) const;
    ValuePtr divide(const ValuePtr& other) const;
    ValuePtr modulo(const ValuePtr& other) const;
    ValuePtr negate() const;

    std::string toString() const;

private:
    ValueType type_ = ValueType::NIL;
    double number_ = 0;
    std::string string_;
    bool bool_ = false;
    std::vector<double> vector_;
    ShapePtr shape_;
};

} // namespace opendcad
