#include "Value.h"
#include "FunctionDef.h"
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

ValuePtr Value::makeList(const std::vector<ValuePtr>& elements) {
    auto val = std::make_shared<Value>();
    val->type_ = ValueType::LIST;
    val->list_ = elements;
    return val;
}

ValuePtr Value::makeFunction(FunctionDefPtr fn) {
    auto val = std::make_shared<Value>();
    val->type_ = ValueType::FUNCTION;
    val->functionDef_ = std::move(fn);
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
        case ValueType::LIST:          return "list";
        case ValueType::FUNCTION:      return "function";
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

const std::vector<ValuePtr>& Value::asList() const {
    if (type_ != ValueType::LIST)
        throw EvalError("expected list, got " + typeName());
    return list_;
}

void Value::listPush(ValuePtr element) {
    if (type_ != ValueType::LIST)
        throw EvalError("push() requires a list, got " + typeName());
    list_.push_back(std::move(element));
}

ValuePtr Value::listGet(int index) const {
    if (type_ != ValueType::LIST)
        throw EvalError("indexing requires a list, got " + typeName());
    if (index < 0 || index >= static_cast<int>(list_.size()))
        throw EvalError("list index " + std::to_string(index) + " out of bounds (size " + std::to_string(list_.size()) + ")");
    return list_[index];
}

int Value::listLength() const {
    if (type_ != ValueType::LIST)
        throw EvalError("length requires a list, got " + typeName());
    return static_cast<int>(list_.size());
}

std::vector<double> Value::toVector() const {
    if (type_ == ValueType::VECTOR) return vector_;
    if (type_ == ValueType::LIST) {
        std::vector<double> result;
        result.reserve(list_.size());
        for (const auto& elem : list_) {
            result.push_back(elem->asNumber());
        }
        return result;
    }
    throw EvalError("expected vector or list of numbers, got " + typeName());
}

FunctionDefPtr Value::asFunction() const {
    if (type_ != ValueType::FUNCTION)
        throw EvalError("expected function, got " + typeName());
    return functionDef_;
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
        case ValueType::LIST:          return !list_.empty();
        case ValueType::FUNCTION:      return true;
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

ValuePtr Value::equal(const ValuePtr& other) const {
    if (type_ != other->type_) return makeBool(false);
    switch (type_) {
        case ValueType::NUMBER: return makeBool(number_ == other->number_);
        case ValueType::STRING: return makeBool(string_ == other->string_);
        case ValueType::BOOL:   return makeBool(bool_ == other->bool_);
        case ValueType::NIL:    return makeBool(true);
        default: return makeBool(false);
    }
}

ValuePtr Value::notEqual(const ValuePtr& other) const {
    return makeBool(!equal(other)->asBool());
}

ValuePtr Value::lessThan(const ValuePtr& other) const {
    if (type_ == ValueType::NUMBER && other->type_ == ValueType::NUMBER)
        return makeBool(number_ < other->number_);
    throw EvalError("cannot compare " + typeName() + " and " + other->typeName() + " with <");
}

ValuePtr Value::greaterThan(const ValuePtr& other) const {
    if (type_ == ValueType::NUMBER && other->type_ == ValueType::NUMBER)
        return makeBool(number_ > other->number_);
    throw EvalError("cannot compare " + typeName() + " and " + other->typeName() + " with >");
}

ValuePtr Value::lessEqual(const ValuePtr& other) const {
    if (type_ == ValueType::NUMBER && other->type_ == ValueType::NUMBER)
        return makeBool(number_ <= other->number_);
    throw EvalError("cannot compare " + typeName() + " and " + other->typeName() + " with <=");
}

ValuePtr Value::greaterEqual(const ValuePtr& other) const {
    if (type_ == ValueType::NUMBER && other->type_ == ValueType::NUMBER)
        return makeBool(number_ >= other->number_);
    throw EvalError("cannot compare " + typeName() + " and " + other->typeName() + " with >=");
}

ValuePtr Value::logicalNot() const {
    return makeBool(!isTruthy());
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
        case ValueType::LIST: {
            std::ostringstream oss;
            oss << "[";
            for (size_t i = 0; i < list_.size(); i++) {
                if (i > 0) oss << ", ";
                oss << list_[i]->toString();
            }
            oss << "]";
            return oss.str();
        }
        case ValueType::FUNCTION:
            return "<fn " + (functionDef_ ? functionDef_->name : "?") + ">";
    }
    return "unknown";
}

} // namespace opendcad
