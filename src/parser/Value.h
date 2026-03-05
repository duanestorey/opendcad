#pragma once

#include <memory>
#include <string>
#include <vector>

namespace opendcad {

class Shape;
using ShapePtr = std::shared_ptr<Shape>;

class FaceRef;
using FaceRefPtr = std::shared_ptr<FaceRef>;

class FaceSelector;
using FaceSelectorPtr = std::shared_ptr<FaceSelector>;

class Workplane;
using WorkplanePtr = std::shared_ptr<Workplane>;

class Sketch;
using SketchPtr = std::shared_ptr<Sketch>;

class EdgeSelector;
using EdgeSelectorPtr = std::shared_ptr<EdgeSelector>;

class Value;
using ValuePtr = std::shared_ptr<Value>;

struct FunctionDef;
using FunctionDefPtr = std::shared_ptr<FunctionDef>;

struct Color;
using ColorPtr = std::shared_ptr<Color>;

struct Material;
using MaterialPtr = std::shared_ptr<Material>;

enum class ValueType {
    NUMBER, STRING, BOOL, VECTOR, SHAPE, NIL,
    FACE_REF, FACE_SELECTOR, WORKPLANE, SKETCH, EDGE_SELECTOR,
    LIST, FUNCTION, COLOR, MATERIAL
};

class Value {
public:
    static ValuePtr makeNumber(double v);
    static ValuePtr makeString(const std::string& v);
    static ValuePtr makeBool(bool v);
    static ValuePtr makeVector(const std::vector<double>& v);
    static ValuePtr makeShape(ShapePtr v);
    static ValuePtr makeNil();
    static ValuePtr makeFaceRef(FaceRefPtr v);
    static ValuePtr makeFaceSelector(FaceSelectorPtr v);
    static ValuePtr makeWorkplane(WorkplanePtr v);
    static ValuePtr makeSketch(SketchPtr v);
    static ValuePtr makeEdgeSelector(EdgeSelectorPtr v);
    static ValuePtr makeList(const std::vector<ValuePtr>& elements);
    static ValuePtr makeFunction(FunctionDefPtr fn);
    static ValuePtr makeColor(ColorPtr c);
    static ValuePtr makeMaterial(MaterialPtr m);

    ValueType type() const { return type_; }
    std::string typeName() const;

    double asNumber() const;
    const std::string& asString() const;
    bool asBool() const;
    const std::vector<double>& asVector() const;
    ShapePtr asShape() const;
    FaceRefPtr asFaceRef() const;
    FaceSelectorPtr asFaceSelector() const;
    WorkplanePtr asWorkplane() const;
    SketchPtr asSketch() const;
    EdgeSelectorPtr asEdgeSelector() const;
    const std::vector<ValuePtr>& asList() const;
    void listPush(ValuePtr element);
    ValuePtr listGet(int index) const;
    int listLength() const;
    std::vector<double> toVector() const;
    FunctionDefPtr asFunction() const;
    ColorPtr asColor() const;
    MaterialPtr asMaterial() const;
    bool isTruthy() const;

    ValuePtr add(const ValuePtr& other) const;
    ValuePtr subtract(const ValuePtr& other) const;
    ValuePtr multiply(const ValuePtr& other) const;
    ValuePtr divide(const ValuePtr& other) const;
    ValuePtr modulo(const ValuePtr& other) const;
    ValuePtr negate() const;

    ValuePtr equal(const ValuePtr& other) const;
    ValuePtr notEqual(const ValuePtr& other) const;
    ValuePtr lessThan(const ValuePtr& other) const;
    ValuePtr greaterThan(const ValuePtr& other) const;
    ValuePtr lessEqual(const ValuePtr& other) const;
    ValuePtr greaterEqual(const ValuePtr& other) const;
    ValuePtr logicalNot() const;

    std::string toString() const;

private:
    ValueType type_ = ValueType::NIL;
    double number_ = 0;
    std::string string_;
    bool bool_ = false;
    std::vector<double> vector_;
    ShapePtr shape_;
    FaceRefPtr faceRef_;
    FaceSelectorPtr faceSelector_;
    WorkplanePtr workplane_;
    SketchPtr sketch_;
    EdgeSelectorPtr edgeSelector_;
    std::vector<ValuePtr> list_;
    FunctionDefPtr functionDef_;
    ColorPtr color_;
    MaterialPtr material_;
};

} // namespace opendcad
