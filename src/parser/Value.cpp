#include "Value.h"
#include "Error.h"
#include "Shape.h"
#include <cmath>
#include <sstream>

namespace opendcad {

ValuePtr Value::makeNumber(double v) {
    auto val = std::make_shared<Value>();
    val->type_ = ValueType::NUMBER;
    val->number_ = v;
    return val;
}

ValuePtr Value::makeString(const std::string& v) {
    auto val = std::make_shared<Value>();
    val->type_ = ValueType::STRING;
    val->string_ = v;
    return val;
}

ValuePtr Value::makeBool(bool v) {
    auto val = std::make_shared<Value>();
    val->type_ = ValueType::BOOL;
    val->bool_ = v;
    return val;
}

ValuePtr Value::makeVector(const std::vector<double>& v) {
    auto val = std::make_shared<Value>();
    val->type_ = ValueType::VECTOR;
    val->vector_ = v;
    return val;
}

ValuePtr Value::makeShape(ShapePtr v) {
    auto val = std::make_shared<Value>();
    val->type_ = ValueType::SHAPE;
    val->shape_ = std::move(v);
    return val;
}

ValuePtr Value::makeNil() {
    auto val = std::make_shared<Value>();
    val->type_ = ValueType::NIL;
    return val;
}

ValuePtr Value::makeFaceRef(FaceRefPtr v) {
    auto val = std::make_shared<Value>();
    val->type_ = ValueType::FACE_REF;
    val->faceRef_ = std::move(v);
    return val;
}

ValuePtr Value::makeFaceSelector(FaceSelectorPtr v) {
    auto val = std::make_shared<Value>();
    val->type_ = ValueType::FACE_SELECTOR;
    val->faceSelector_ = std::move(v);
    return val;
}

ValuePtr Value::makeWorkplane(WorkplanePtr v) {
    auto val = std::make_shared<Value>();
    val->type_ = ValueType::WORKPLANE;
    val->workplane_ = std::move(v);
    return val;
}

ValuePtr Value::makeSketch(SketchPtr v) {
    auto val = std::make_shared<Value>();
    val->type_ = ValueType::SKETCH;
    val->sketch_ = std::move(v);
    return val;
}

ValuePtr Value::makeEdgeSelector(EdgeSelectorPtr v) {
    auto val = std::make_shared<Value>();
    val->type_ = ValueType::EDGE_SELECTOR;
    val->edgeSelector_ = std::move(v);
    return val;
}

std::string Value::typeName() const {
    switch (type_) {
        case ValueType::NUMBER:        return "number";
        case ValueType::STRING:        return "string";
        case ValueType::BOOL:          return "bool";
        case ValueType::VECTOR:        return "vector";
        case ValueType::SHAPE:         return "shape";
        case ValueType::NIL:           return "nil";
        case ValueType::FACE_REF:      return "face_ref";
        case ValueType::FACE_SELECTOR: return "face_selector";
        case ValueType::WORKPLANE:     return "workplane";
        case ValueType::SKETCH:        return "sketch";
        case ValueType::EDGE_SELECTOR: return "edge_selector";
    }
    return "unknown";
}

double Value::asNumber() const {
    if (type_ != ValueType::NUMBER)
        throw EvalError("expected number, got " + typeName());
    return number_;
}

const std::string& Value::asString() const {
    if (type_ != ValueType::STRING)
        throw EvalError("expected string, got " + typeName());
    return string_;
}

bool Value::asBool() const {
    if (type_ != ValueType::BOOL)
        throw EvalError("expected bool, got " + typeName());
    return bool_;
}

const std::vector<double>& Value::asVector() const {
    if (type_ != ValueType::VECTOR)
        throw EvalError("expected vector, got " + typeName());
    return vector_;
}

ShapePtr Value::asShape() const {
    if (type_ != ValueType::SHAPE)
        throw EvalError("expected shape, got " + typeName());
    return shape_;
}

FaceRefPtr Value::asFaceRef() const {
    if (type_ != ValueType::FACE_REF)
        throw EvalError("expected face_ref, got " + typeName());
    return faceRef_;
}

FaceSelectorPtr Value::asFaceSelector() const {
    if (type_ != ValueType::FACE_SELECTOR)
        throw EvalError("expected face_selector, got " + typeName());
    return faceSelector_;
}

WorkplanePtr Value::asWorkplane() const {
    if (type_ != ValueType::WORKPLANE)
        throw EvalError("expected workplane, got " + typeName());
    return workplane_;
}

SketchPtr Value::asSketch() const {
    if (type_ != ValueType::SKETCH)
        throw EvalError("expected sketch, got " + typeName());
    return sketch_;
}

EdgeSelectorPtr Value::asEdgeSelector() const {
    if (type_ != ValueType::EDGE_SELECTOR)
        throw EvalError("expected edge_selector, got " + typeName());
    return edgeSelector_;
}

bool Value::isTruthy() const {
    switch (type_) {
        case ValueType::NIL:           return false;
        case ValueType::BOOL:          return bool_;
        case ValueType::NUMBER:        return number_ != 0.0;
        case ValueType::STRING:        return !string_.empty();
        case ValueType::VECTOR:        return !vector_.empty();
        case ValueType::SHAPE:         return shape_ != nullptr;
        case ValueType::FACE_REF:      return faceRef_ != nullptr;
        case ValueType::FACE_SELECTOR: return faceSelector_ != nullptr;
        case ValueType::WORKPLANE:     return workplane_ != nullptr;
        case ValueType::SKETCH:        return sketch_ != nullptr;
        case ValueType::EDGE_SELECTOR: return edgeSelector_ != nullptr;
    }
    return false;
}

ValuePtr Value::add(const ValuePtr& other) const {
    if (type_ == ValueType::NUMBER && other->type_ == ValueType::NUMBER)
        return makeNumber(number_ + other->number_);
    if (type_ == ValueType::VECTOR && other->type_ == ValueType::VECTOR) {
        if (vector_.size() != other->vector_.size())
            throw EvalError("vector size mismatch in addition");
        std::vector<double> result(vector_.size());
        for (size_t i = 0; i < vector_.size(); i++)
            result[i] = vector_[i] + other->vector_[i];
        return makeVector(result);
    }
    if (type_ == ValueType::STRING && other->type_ == ValueType::STRING)
        return makeString(string_ + other->string_);
    throw EvalError("cannot add " + typeName() + " and " + other->typeName());
}

ValuePtr Value::subtract(const ValuePtr& other) const {
    if (type_ == ValueType::NUMBER && other->type_ == ValueType::NUMBER)
        return makeNumber(number_ - other->number_);
    if (type_ == ValueType::VECTOR && other->type_ == ValueType::VECTOR) {
        if (vector_.size() != other->vector_.size())
            throw EvalError("vector size mismatch in subtraction");
        std::vector<double> result(vector_.size());
        for (size_t i = 0; i < vector_.size(); i++)
            result[i] = vector_[i] - other->vector_[i];
        return makeVector(result);
    }
    throw EvalError("cannot subtract " + other->typeName() + " from " + typeName());
}

ValuePtr Value::multiply(const ValuePtr& other) const {
    if (type_ == ValueType::NUMBER && other->type_ == ValueType::NUMBER)
        return makeNumber(number_ * other->number_);
    if (type_ == ValueType::NUMBER && other->type_ == ValueType::VECTOR) {
        std::vector<double> result(other->vector_.size());
        for (size_t i = 0; i < other->vector_.size(); i++)
            result[i] = number_ * other->vector_[i];
        return makeVector(result);
    }
    if (type_ == ValueType::VECTOR && other->type_ == ValueType::NUMBER) {
        std::vector<double> result(vector_.size());
        for (size_t i = 0; i < vector_.size(); i++)
            result[i] = vector_[i] * other->number_;
        return makeVector(result);
    }
    throw EvalError("cannot multiply " + typeName() + " and " + other->typeName());
}

ValuePtr Value::divide(const ValuePtr& other) const {
    if (type_ == ValueType::NUMBER && other->type_ == ValueType::NUMBER) {
        if (other->number_ == 0.0)
            throw EvalError("division by zero");
        return makeNumber(number_ / other->number_);
    }
    if (type_ == ValueType::VECTOR && other->type_ == ValueType::NUMBER) {
        if (other->number_ == 0.0)
            throw EvalError("division by zero");
        std::vector<double> result(vector_.size());
        for (size_t i = 0; i < vector_.size(); i++)
            result[i] = vector_[i] / other->number_;
        return makeVector(result);
    }
    throw EvalError("cannot divide " + typeName() + " by " + other->typeName());
}

ValuePtr Value::modulo(const ValuePtr& other) const {
    if (type_ == ValueType::NUMBER && other->type_ == ValueType::NUMBER) {
        if (other->number_ == 0.0)
            throw EvalError("modulo by zero");
        return makeNumber(std::fmod(number_, other->number_));
    }
    throw EvalError("cannot modulo " + typeName() + " by " + other->typeName());
}

ValuePtr Value::negate() const {
    if (type_ == ValueType::NUMBER)
        return makeNumber(-number_);
    if (type_ == ValueType::VECTOR) {
        std::vector<double> result(vector_.size());
        for (size_t i = 0; i < vector_.size(); i++)
            result[i] = -vector_[i];
        return makeVector(result);
    }
    throw EvalError("cannot negate " + typeName());
}

std::string Value::toString() const {
    switch (type_) {
        case ValueType::NUMBER: {
            std::ostringstream oss;
            oss << number_;
            return oss.str();
        }
        case ValueType::STRING: return "\"" + string_ + "\"";
        case ValueType::BOOL:   return bool_ ? "true" : "false";
        case ValueType::VECTOR: {
            std::ostringstream oss;
            oss << "[";
            for (size_t i = 0; i < vector_.size(); i++) {
                if (i > 0) oss << ", ";
                oss << vector_[i];
            }
            oss << "]";
            return oss.str();
        }
        case ValueType::SHAPE:         return "<shape>";
        case ValueType::NIL:           return "nil";
        case ValueType::FACE_REF:      return "<face_ref>";
        case ValueType::FACE_SELECTOR: return "<face_selector>";
        case ValueType::WORKPLANE:     return "<workplane>";
        case ValueType::SKETCH:        return "<sketch>";
        case ValueType::EDGE_SELECTOR: return "<edge_selector>";
    }
    return "unknown";
}

} // namespace opendcad
