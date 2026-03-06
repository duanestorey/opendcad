#include "ShapeRegistry.h"
#include "FaceRef.h"
#include "FaceSelector.h"
#include "Workplane.h"
#include "Sketch.h"
#include "EdgeSelector.h"
#include "Color.h"
#include "Error.h"

namespace opendcad {

ShapeRegistry& ShapeRegistry::instance() {
    static ShapeRegistry reg;
    return reg;
}

void ShapeRegistry::registerFactory(const std::string& name, ShapeFactory fn) {
    factories_[name] = std::move(fn);
}

void ShapeRegistry::registerMethod(const std::string& name, ShapeMethod fn) {
    methods_[name] = std::move(fn);
}

bool ShapeRegistry::hasFactory(const std::string& name) const {
    return factories_.count(name) > 0;
}

bool ShapeRegistry::hasMethod(const std::string& name) const {
    return methods_.count(name) > 0;
}

ShapePtr ShapeRegistry::callFactory(const std::string& name, const std::vector<ValuePtr>& args) const {
    auto it = factories_.find(name);
    if (it == factories_.end())
        throw EvalError("unknown factory '" + name + "'");
    return it->second(args);
}

ValuePtr ShapeRegistry::callMethod(const std::string& name, ShapePtr self, const std::vector<ValuePtr>& args) const {
    auto it = methods_.find(name);
    if (it == methods_.end())
        throw EvalError("unknown method '" + name + "'");
    return it->second(std::move(self), args);
}

void ShapeRegistry::registerTypedMethod(ValueType type, const std::string& name, TypedMethod fn) {
    typedMethods_[static_cast<int>(type)][name] = std::move(fn);
}

bool ShapeRegistry::hasTypedMethod(ValueType type, const std::string& name) const {
    auto typeIt = typedMethods_.find(static_cast<int>(type));
    if (typeIt == typedMethods_.end()) return false;
    return typeIt->second.count(name) > 0;
}

ValuePtr ShapeRegistry::callTypedMethod(ValueType type, const std::string& name,
                                        ValuePtr self, const std::vector<ValuePtr>& args) const {
    auto typeIt = typedMethods_.find(static_cast<int>(type));
    if (typeIt == typedMethods_.end())
        throw EvalError("no methods registered for type");
    auto methodIt = typeIt->second.find(name);
    if (methodIt == typeIt->second.end())
        throw EvalError("unknown method '" + name + "'");
    return methodIt->second(std::move(self), args);
}

static double requireNumber(const std::vector<ValuePtr>& args, size_t index, const std::string& context) {
    if (index >= args.size())
        throw EvalError(context + ": expected argument " + std::to_string(index + 1));
    return args[index]->asNumber();
}

static ShapePtr requireShape(const std::vector<ValuePtr>& args, size_t index, const std::string& context) {
    if (index >= args.size())
        throw EvalError(context + ": expected argument " + std::to_string(index + 1));
    return args[index]->asShape();
}

void ShapeRegistry::registerDefaults() {
    // =========================================================================
    // Factories
    // =========================================================================

    registerFactory("box", [](const std::vector<ValuePtr>& args) -> ShapePtr {
        if (args.size() != 3)
            throw EvalError("box() requires 3 arguments (width, depth, height), got " + std::to_string(args.size()));
        return Shape::createBox(args[0]->asNumber(), args[1]->asNumber(), args[2]->asNumber());
    });

    registerFactory("cylinder", [](const std::vector<ValuePtr>& args) -> ShapePtr {
        if (args.size() != 2)
            throw EvalError("cylinder() requires 2 arguments (radius, height), got " + std::to_string(args.size()));
        return Shape::createCylinder(args[0]->asNumber(), args[1]->asNumber());
    });

    registerFactory("torus", [](const std::vector<ValuePtr>& args) -> ShapePtr {
        if (args.size() != 2)
            throw EvalError("torus() requires 2 arguments (r1, r2), got " + std::to_string(args.size()));
        return Shape::createTorus(args[0]->asNumber(), args[1]->asNumber());
    });

    registerFactory("bin", [](const std::vector<ValuePtr>& args) -> ShapePtr {
        if (args.size() != 4)
            throw EvalError("bin() requires 4 arguments (width, depth, height, thickness), got " + std::to_string(args.size()));
        return Shape::createBin(args[0]->asNumber(), args[1]->asNumber(),
                                args[2]->asNumber(), args[3]->asNumber());
    });

    registerFactory("sphere", [](const std::vector<ValuePtr>& args) -> ShapePtr {
        if (args.size() != 1)
            throw EvalError("sphere() requires 1 argument (radius), got " + std::to_string(args.size()));
        return Shape::createSphere(args[0]->asNumber());
    });

    registerFactory("cone", [](const std::vector<ValuePtr>& args) -> ShapePtr {
        if (args.size() != 3)
            throw EvalError("cone() requires 3 arguments (r1, r2, height), got " + std::to_string(args.size()));
        return Shape::createCone(args[0]->asNumber(), args[1]->asNumber(), args[2]->asNumber());
    });

    registerFactory("wedge", [](const std::vector<ValuePtr>& args) -> ShapePtr {
        if (args.size() != 4)
            throw EvalError("wedge() requires 4 arguments (dx, dy, dz, ltx), got " + std::to_string(args.size()));
        return Shape::createWedge(args[0]->asNumber(), args[1]->asNumber(),
                                  args[2]->asNumber(), args[3]->asNumber());
    });

    registerFactory("circle", [](const std::vector<ValuePtr>& args) -> ShapePtr {
        if (args.size() != 1)
            throw EvalError("circle() requires 1 argument (radius), got " + std::to_string(args.size()));
        return Shape::createCircle(args[0]->asNumber());
    });

    registerFactory("rectangle", [](const std::vector<ValuePtr>& args) -> ShapePtr {
        if (args.size() != 2)
            throw EvalError("rectangle() requires 2 arguments (width, height), got " + std::to_string(args.size()));
        return Shape::createRectangle(args[0]->asNumber(), args[1]->asNumber());
    });

    registerFactory("polygon", [](const std::vector<ValuePtr>& args) -> ShapePtr {
        if (args.size() < 3)
            throw EvalError("polygon() requires at least 3 vector arguments (points), got " + std::to_string(args.size()));
        std::vector<std::pair<double,double>> pts;
        for (const auto& arg : args) {
            const auto& v = arg->toVector();
            if (v.size() < 2)
                throw EvalError("polygon() each point must have at least 2 coordinates");
            pts.emplace_back(v[0], v[1]);
        }
        return Shape::createPolygon(pts);
    });

    registerFactory("loft", [](const std::vector<ValuePtr>& args) -> ShapePtr {
        if (args.size() < 2)
            throw EvalError("loft() requires at least 2 profile arguments, got " + std::to_string(args.size()));
        std::vector<ShapePtr> profiles;
        for (const auto& arg : args)
            profiles.push_back(arg->asShape());
        return Shape::createLoft(profiles);
    });

    registerFactory("import_step", [](const std::vector<ValuePtr>& args) -> ShapePtr {
        if (args.size() != 1)
            throw EvalError("import_step() requires 1 argument (path), got " + std::to_string(args.size()));
        return Shape::importSTEP(args[0]->asString());
    });

    registerFactory("import_stl", [](const std::vector<ValuePtr>& args) -> ShapePtr {
        if (args.size() != 1)
            throw EvalError("import_stl() requires 1 argument (path), got " + std::to_string(args.size()));
        return Shape::importSTL(args[0]->asString());
    });

    // =========================================================================
    // Methods
    // =========================================================================

    registerMethod("fuse", [](ShapePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
        auto other = requireShape(args, 0, "fuse");
        return Value::makeShape(self->fuse(other));
    });

    registerMethod("cut", [](ShapePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
        auto other = requireShape(args, 0, "cut");
        return Value::makeShape(self->cut(other));
    });

    registerMethod("intersect", [](ShapePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
        auto other = requireShape(args, 0, "intersect");
        return Value::makeShape(self->intersect(other));
    });

    registerMethod("fillet", [](ShapePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
        double amount = requireNumber(args, 0, "fillet");
        return Value::makeShape(self->fillet(amount));
    });

    registerMethod("chamfer", [](ShapePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
        double distance = requireNumber(args, 0, "chamfer");
        return Value::makeShape(self->chamfer(distance));
    });

    registerMethod("flip", [](ShapePtr self, const std::vector<ValuePtr>& /*args*/) -> ValuePtr {
        return Value::makeShape(self->flip());
    });

    registerMethod("translate", [](ShapePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
        double x = 0, y = 0, z = 0;
        if (args.size() == 1 && args[0]->type() == ValueType::VECTOR) {
            const auto& v = args[0]->toVector();
            x = v.size() > 0 ? v[0] : 0;
            y = v.size() > 1 ? v[1] : 0;
            z = v.size() > 2 ? v[2] : 0;
        } else if (args.size() >= 1) {
            x = args[0]->asNumber();
            y = args.size() > 1 ? args[1]->asNumber() : 0;
            z = args.size() > 2 ? args[2]->asNumber() : 0;
        } else {
            throw EvalError("translate() requires at least 1 argument");
        }
        return Value::makeShape(self->translate(x, y, z));
    });

    registerMethod("rotate", [](ShapePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
        double rx = 0, ry = 0, rz = 0;
        if (args.size() == 1 && args[0]->type() == ValueType::VECTOR) {
            const auto& v = args[0]->toVector();
            rx = v.size() > 0 ? v[0] : 0;
            ry = v.size() > 1 ? v[1] : 0;
            rz = v.size() > 2 ? v[2] : 0;
        } else if (args.size() == 3) {
            rx = args[0]->asNumber();
            ry = args[1]->asNumber();
            rz = args[2]->asNumber();
        } else {
            throw EvalError("rotate() requires a vector or 3 number arguments");
        }
        return Value::makeShape(self->rotate(rx, ry, rz));
    });

    registerMethod("scale", [](ShapePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
        if (args.size() == 1) {
            if (args[0]->type() == ValueType::VECTOR) {
                const auto& v = args[0]->toVector();
                if (v.size() != 3)
                    throw EvalError("scale() vector must have 3 components");
                return Value::makeShape(self->scale(v[0], v[1], v[2]));
            }
            return Value::makeShape(self->scale(args[0]->asNumber()));
        } else if (args.size() == 3) {
            return Value::makeShape(self->scale(
                args[0]->asNumber(), args[1]->asNumber(), args[2]->asNumber()));
        }
        throw EvalError("scale() requires 1 number, 1 vector, or 3 numbers");
    });

    registerMethod("mirror", [](ShapePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
        double nx = 0, ny = 0, nz = 0;
        if (args.size() == 1 && args[0]->type() == ValueType::VECTOR) {
            const auto& v = args[0]->toVector();
            nx = v.size() > 0 ? v[0] : 0;
            ny = v.size() > 1 ? v[1] : 0;
            nz = v.size() > 2 ? v[2] : 0;
        } else if (args.size() == 3) {
            nx = args[0]->asNumber();
            ny = args[1]->asNumber();
            nz = args[2]->asNumber();
        } else {
            throw EvalError("mirror() requires a vector or 3 number arguments");
        }
        return Value::makeShape(self->mirror(nx, ny, nz));
    });

    registerMethod("shell", [](ShapePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
        double thickness = requireNumber(args, 0, "shell");
        return Value::makeShape(self->shell(thickness));
    });

    registerMethod("linear_extrude", [](ShapePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
        double height = requireNumber(args, 0, "linear_extrude");
        return Value::makeShape(self->linearExtrude(height));
    });

    registerMethod("rotate_extrude", [](ShapePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
        double angle = 360.0;
        if (args.size() >= 1)
            angle = args[0]->asNumber();
        return Value::makeShape(self->rotateExtrude(angle));
    });

    registerMethod("sweep", [](ShapePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
        auto path = requireShape(args, 0, "sweep");
        return Value::makeShape(self->sweep(path));
    });

    registerMethod("x", [](ShapePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
        return Value::makeShape(self->x(requireNumber(args, 0, "x")));
    });

    registerMethod("y", [](ShapePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
        return Value::makeShape(self->y(requireNumber(args, 0, "y")));
    });

    registerMethod("z", [](ShapePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
        return Value::makeShape(self->z(requireNumber(args, 0, "z")));
    });

    registerMethod("placeCorners", [](ShapePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
        if (args.size() != 3)
            throw EvalError("placeCorners() requires 3 arguments (shape, xOffset, yOffset), got " + std::to_string(args.size()));
        auto shape = args[0]->asShape();
        double xOff = args[1]->asNumber();
        double yOff = args[2]->asNumber();
        return Value::makeShape(self->placeCorners(shape, xOff, yOff));
    });

    // =========================================================================
    // Typed Methods — SHAPE
    // =========================================================================

    registerTypedMethod(ValueType::SHAPE, "face",
        [](ValuePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
            auto shape = self->asShape();
            std::string sel = args.at(0)->asString();
            return Value::makeFaceRef(shape->face(sel));
        });

    registerTypedMethod(ValueType::SHAPE, "faces",
        [](ValuePtr self, const std::vector<ValuePtr>& /*args*/) -> ValuePtr {
            return Value::makeFaceSelector(self->asShape()->faces());
        });

    registerTypedMethod(ValueType::SHAPE, "edges",
        [](ValuePtr self, const std::vector<ValuePtr>& /*args*/) -> ValuePtr {
            return Value::makeEdgeSelector(self->asShape()->edges());
        });

    registerTypedMethod(ValueType::SHAPE, "topFace",
        [](ValuePtr self, const std::vector<ValuePtr>& /*args*/) -> ValuePtr {
            return Value::makeFaceRef(self->asShape()->topFace());
        });

    registerTypedMethod(ValueType::SHAPE, "bottomFace",
        [](ValuePtr self, const std::vector<ValuePtr>& /*args*/) -> ValuePtr {
            return Value::makeFaceRef(self->asShape()->bottomFace());
        });

    registerTypedMethod(ValueType::SHAPE, "linearPattern",
        [](ValuePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
            double dx = 0, dy = 0, dz = 0;
            int count = 0;
            if (args.size() >= 2 && args[0]->type() == ValueType::VECTOR) {
                const auto& v = args[0]->toVector();
                dx = v.size() > 0 ? v[0] : 0;
                dy = v.size() > 1 ? v[1] : 0;
                dz = v.size() > 2 ? v[2] : 0;
                count = static_cast<int>(args[1]->asNumber());
            } else {
                throw EvalError("linearPattern() requires (vector, count)");
            }
            return Value::makeShape(self->asShape()->linearPattern(dx, dy, dz, count));
        });

    registerTypedMethod(ValueType::SHAPE, "circularPattern",
        [](ValuePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
            if (args.size() < 2 || args[0]->type() != ValueType::VECTOR)
                throw EvalError("circularPattern() requires (axis_vector, count[, angle])");
            const auto& v = args[0]->toVector();
            double ax = v.size() > 0 ? v[0] : 0;
            double ay = v.size() > 1 ? v[1] : 0;
            double az = v.size() > 2 ? v[2] : 0;
            int count = static_cast<int>(args[1]->asNumber());
            double angle = args.size() > 2 ? args[2]->asNumber() : 360.0;
            return Value::makeShape(self->asShape()->circularPattern(ax, ay, az, count, angle));
        });

    registerTypedMethod(ValueType::SHAPE, "mirrorFeature",
        [](ValuePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
            double nx = 0, ny = 0, nz = 0;
            if (args.size() == 1 && args[0]->type() == ValueType::VECTOR) {
                const auto& v = args[0]->toVector();
                nx = v.size() > 0 ? v[0] : 0;
                ny = v.size() > 1 ? v[1] : 0;
                nz = v.size() > 2 ? v[2] : 0;
            } else if (args.size() == 3) {
                nx = args[0]->asNumber();
                ny = args[1]->asNumber();
                nz = args[2]->asNumber();
            } else {
                throw EvalError("mirrorFeature() requires a normal vector");
            }
            return Value::makeShape(self->asShape()->mirrorFeature(nx, ny, nz));
        });

    registerTypedMethod(ValueType::SHAPE, "draft",
        [](ValuePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
            if (args.size() < 2)
                throw EvalError("draft() requires (angle, direction_vector)");
            double angle = args[0]->asNumber();
            const auto& v = args[1]->toVector();
            if (v.size() < 3)
                throw EvalError("draft() direction vector must have 3 components");
            return Value::makeShape(self->asShape()->draft(angle, v[0], v[1], v[2]));
        });

    registerTypedMethod(ValueType::SHAPE, "splitAt",
        [](ValuePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
            if (args.size() < 2)
                throw EvalError("splitAt() requires (point_vector, normal_vector)");
            const auto& p = args[0]->toVector();
            const auto& n = args[1]->toVector();
            if (p.size() < 3 || n.size() < 3)
                throw EvalError("splitAt() vectors must have 3 components");
            return Value::makeShape(self->asShape()->splitAt(p[0], p[1], p[2], n[0], n[1], n[2]));
        });

    // =========================================================================
    // Typed Methods — FACE_REF
    // =========================================================================

    registerTypedMethod(ValueType::FACE_REF, "draw",
        [](ValuePtr self, const std::vector<ValuePtr>& /*args*/) -> ValuePtr {
            return Value::makeSketch(self->asFaceRef()->draw());
        });

    registerTypedMethod(ValueType::FACE_REF, "workplane",
        [](ValuePtr self, const std::vector<ValuePtr>& /*args*/) -> ValuePtr {
            return Value::makeWorkplane(self->asFaceRef()->workplane());
        });

    registerTypedMethod(ValueType::FACE_REF, "center",
        [](ValuePtr self, const std::vector<ValuePtr>& /*args*/) -> ValuePtr {
            gp_Pnt c = self->asFaceRef()->center();
            return Value::makeVector({c.X(), c.Y(), c.Z()});
        });

    registerTypedMethod(ValueType::FACE_REF, "normal",
        [](ValuePtr self, const std::vector<ValuePtr>& /*args*/) -> ValuePtr {
            gp_Dir n = self->asFaceRef()->normal();
            return Value::makeVector({n.X(), n.Y(), n.Z()});
        });

    registerTypedMethod(ValueType::FACE_REF, "area",
        [](ValuePtr self, const std::vector<ValuePtr>& /*args*/) -> ValuePtr {
            return Value::makeNumber(self->asFaceRef()->area());
        });

    registerTypedMethod(ValueType::FACE_REF, "isPlanar",
        [](ValuePtr self, const std::vector<ValuePtr>& /*args*/) -> ValuePtr {
            return Value::makeBool(self->asFaceRef()->isPlanar());
        });

    // =========================================================================
    // Typed Methods — FACE_SELECTOR
    // =========================================================================

    registerTypedMethod(ValueType::FACE_SELECTOR, "top",
        [](ValuePtr self, const std::vector<ValuePtr>& /*args*/) -> ValuePtr {
            return Value::makeFaceRef(self->asFaceSelector()->top());
        });

    registerTypedMethod(ValueType::FACE_SELECTOR, "bottom",
        [](ValuePtr self, const std::vector<ValuePtr>& /*args*/) -> ValuePtr {
            return Value::makeFaceRef(self->asFaceSelector()->bottom());
        });

    registerTypedMethod(ValueType::FACE_SELECTOR, "front",
        [](ValuePtr self, const std::vector<ValuePtr>& /*args*/) -> ValuePtr {
            return Value::makeFaceRef(self->asFaceSelector()->front());
        });

    registerTypedMethod(ValueType::FACE_SELECTOR, "back",
        [](ValuePtr self, const std::vector<ValuePtr>& /*args*/) -> ValuePtr {
            return Value::makeFaceRef(self->asFaceSelector()->back());
        });

    registerTypedMethod(ValueType::FACE_SELECTOR, "left",
        [](ValuePtr self, const std::vector<ValuePtr>& /*args*/) -> ValuePtr {
            return Value::makeFaceRef(self->asFaceSelector()->left());
        });

    registerTypedMethod(ValueType::FACE_SELECTOR, "right",
        [](ValuePtr self, const std::vector<ValuePtr>& /*args*/) -> ValuePtr {
            return Value::makeFaceRef(self->asFaceSelector()->right());
        });

    registerTypedMethod(ValueType::FACE_SELECTOR, "largest",
        [](ValuePtr self, const std::vector<ValuePtr>& /*args*/) -> ValuePtr {
            return Value::makeFaceRef(self->asFaceSelector()->largest());
        });

    registerTypedMethod(ValueType::FACE_SELECTOR, "smallest",
        [](ValuePtr self, const std::vector<ValuePtr>& /*args*/) -> ValuePtr {
            return Value::makeFaceRef(self->asFaceSelector()->smallest());
        });

    registerTypedMethod(ValueType::FACE_SELECTOR, "planar",
        [](ValuePtr self, const std::vector<ValuePtr>& /*args*/) -> ValuePtr {
            return Value::makeFaceSelector(self->asFaceSelector()->planar());
        });

    registerTypedMethod(ValueType::FACE_SELECTOR, "cylindrical",
        [](ValuePtr self, const std::vector<ValuePtr>& /*args*/) -> ValuePtr {
            return Value::makeFaceSelector(self->asFaceSelector()->cylindrical());
        });

    registerTypedMethod(ValueType::FACE_SELECTOR, "byIndex",
        [](ValuePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
            int idx = static_cast<int>(requireNumber(args, 0, "byIndex"));
            return Value::makeFaceRef(self->asFaceSelector()->byIndex(idx));
        });

    registerTypedMethod(ValueType::FACE_SELECTOR, "count",
        [](ValuePtr self, const std::vector<ValuePtr>& /*args*/) -> ValuePtr {
            return Value::makeNumber(self->asFaceSelector()->count());
        });

    registerTypedMethod(ValueType::FACE_SELECTOR, "nearestTo",
        [](ValuePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
            const auto& v = args.at(0)->toVector();
            if (v.size() < 3)
                throw EvalError("nearestTo() requires a 3-component vector");
            return Value::makeFaceRef(self->asFaceSelector()->nearestTo(gp_Pnt(v[0], v[1], v[2])));
        });

    registerTypedMethod(ValueType::FACE_SELECTOR, "farthestFrom",
        [](ValuePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
            const auto& v = args.at(0)->toVector();
            if (v.size() < 3)
                throw EvalError("farthestFrom() requires a 3-component vector");
            return Value::makeFaceRef(self->asFaceSelector()->farthestFrom(gp_Pnt(v[0], v[1], v[2])));
        });

    registerTypedMethod(ValueType::FACE_SELECTOR, "areaGreaterThan",
        [](ValuePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
            double minArea = requireNumber(args, 0, "areaGreaterThan");
            return Value::makeFaceSelector(self->asFaceSelector()->areaGreaterThan(minArea));
        });

    registerTypedMethod(ValueType::FACE_SELECTOR, "areaLessThan",
        [](ValuePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
            double maxArea = requireNumber(args, 0, "areaLessThan");
            return Value::makeFaceSelector(self->asFaceSelector()->areaLessThan(maxArea));
        });

    // =========================================================================
    // Typed Methods — WORKPLANE
    // =========================================================================

    registerTypedMethod(ValueType::WORKPLANE, "draw",
        [](ValuePtr self, const std::vector<ValuePtr>& /*args*/) -> ValuePtr {
            return Value::makeSketch(self->asWorkplane()->draw());
        });

    registerTypedMethod(ValueType::WORKPLANE, "offset",
        [](ValuePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
            double dist = requireNumber(args, 0, "offset");
            return Value::makeWorkplane(self->asWorkplane()->offset(dist));
        });

    // =========================================================================
    // Typed Methods — SKETCH
    // =========================================================================

    registerTypedMethod(ValueType::SKETCH, "circle",
        [](ValuePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
            double r = requireNumber(args, 0, "circle");
            double cx = args.size() > 1 ? args[1]->asNumber() : 0;
            double cy = args.size() > 2 ? args[2]->asNumber() : 0;
            return Value::makeSketch(self->asSketch()->circle(r, cx, cy));
        });

    registerTypedMethod(ValueType::SKETCH, "rect",
        [](ValuePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
            double w = requireNumber(args, 0, "rect");
            double h = requireNumber(args, 1, "rect");
            double cx = args.size() > 2 ? args[2]->asNumber() : 0;
            double cy = args.size() > 3 ? args[3]->asNumber() : 0;
            return Value::makeSketch(self->asSketch()->rect(w, h, cx, cy));
        });

    registerTypedMethod(ValueType::SKETCH, "polygon",
        [](ValuePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
            std::vector<std::pair<double,double>> pts;
            for (const auto& arg : args) {
                const auto& v = arg->toVector();
                if (v.size() < 2)
                    throw EvalError("polygon point must have at least 2 coordinates");
                pts.emplace_back(v[0], v[1]);
            }
            return Value::makeSketch(self->asSketch()->polygon(pts));
        });

    registerTypedMethod(ValueType::SKETCH, "moveTo",
        [](ValuePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
            double x = requireNumber(args, 0, "moveTo");
            double y = requireNumber(args, 1, "moveTo");
            return Value::makeSketch(self->asSketch()->moveTo(x, y));
        });

    registerTypedMethod(ValueType::SKETCH, "lineTo",
        [](ValuePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
            double x = requireNumber(args, 0, "lineTo");
            double y = requireNumber(args, 1, "lineTo");
            return Value::makeSketch(self->asSketch()->lineTo(x, y));
        });

    registerTypedMethod(ValueType::SKETCH, "close",
        [](ValuePtr self, const std::vector<ValuePtr>& /*args*/) -> ValuePtr {
            return Value::makeSketch(self->asSketch()->close());
        });

    registerTypedMethod(ValueType::SKETCH, "extrude",
        [](ValuePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
            double h = requireNumber(args, 0, "extrude");
            return Value::makeShape(self->asSketch()->extrude(h));
        });

    registerTypedMethod(ValueType::SKETCH, "cutBlind",
        [](ValuePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
            double d = requireNumber(args, 0, "cutBlind");
            return Value::makeShape(self->asSketch()->cutBlind(d));
        });

    registerTypedMethod(ValueType::SKETCH, "cutThrough",
        [](ValuePtr self, const std::vector<ValuePtr>& /*args*/) -> ValuePtr {
            return Value::makeShape(self->asSketch()->cutThrough());
        });

    registerTypedMethod(ValueType::SKETCH, "slot",
        [](ValuePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
            double length = requireNumber(args, 0, "slot");
            double width = requireNumber(args, 1, "slot");
            double cx = args.size() > 2 ? args[2]->asNumber() : 0;
            double cy = args.size() > 3 ? args[3]->asNumber() : 0;
            return Value::makeSketch(self->asSketch()->slot(length, width, cx, cy));
        });

    registerTypedMethod(ValueType::SKETCH, "arcTo",
        [](ValuePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
            double x = requireNumber(args, 0, "arcTo");
            double y = requireNumber(args, 1, "arcTo");
            double bulge = requireNumber(args, 2, "arcTo");
            return Value::makeSketch(self->asSketch()->arcTo(x, y, bulge));
        });

    registerTypedMethod(ValueType::SKETCH, "splineTo",
        [](ValuePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
            // Last two args are end x, y; preceding args are through-point vectors
            if (args.size() < 3)
                throw EvalError("splineTo() requires at least 1 through-point vector and end x, y");
            double ey = args.back()->asNumber();
            double ex = args[args.size() - 2]->asNumber();
            std::vector<std::pair<double,double>> throughPts;
            for (size_t i = 0; i < args.size() - 2; ++i) {
                const auto& v = args[i]->toVector();
                if (v.size() < 2)
                    throw EvalError("splineTo through-point must have at least 2 coordinates");
                throughPts.emplace_back(v[0], v[1]);
            }
            return Value::makeSketch(self->asSketch()->splineTo(throughPts, ex, ey));
        });

    registerTypedMethod(ValueType::SKETCH, "revolve",
        [](ValuePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
            double angle = 360.0;
            if (!args.empty())
                angle = args[0]->asNumber();
            return Value::makeShape(self->asSketch()->revolve(angle));
        });

    registerTypedMethod(ValueType::SKETCH, "fillet2D",
        [](ValuePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
            double r = requireNumber(args, 0, "fillet2D");
            return Value::makeSketch(self->asSketch()->fillet2D(r));
        });

    registerTypedMethod(ValueType::SKETCH, "chamfer2D",
        [](ValuePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
            double d = requireNumber(args, 0, "chamfer2D");
            return Value::makeSketch(self->asSketch()->chamfer2D(d));
        });

    registerTypedMethod(ValueType::SKETCH, "offset",
        [](ValuePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
            double d = requireNumber(args, 0, "offset");
            return Value::makeSketch(self->asSketch()->offset(d));
        });

    // =========================================================================
    // Typed Methods — FACE_REF (parametric holes)
    // =========================================================================

    registerTypedMethod(ValueType::FACE_REF, "hole",
        [](ValuePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
            double dia = requireNumber(args, 0, "hole");
            double depth = requireNumber(args, 1, "hole");
            double cx = args.size() > 2 ? args[2]->asNumber() : 0;
            double cy = args.size() > 3 ? args[3]->asNumber() : 0;
            return Value::makeShape(self->asFaceRef()->hole(dia, depth, cx, cy));
        });

    registerTypedMethod(ValueType::FACE_REF, "throughHole",
        [](ValuePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
            double dia = requireNumber(args, 0, "throughHole");
            double cx = args.size() > 1 ? args[1]->asNumber() : 0;
            double cy = args.size() > 2 ? args[2]->asNumber() : 0;
            return Value::makeShape(self->asFaceRef()->throughHole(dia, cx, cy));
        });

    registerTypedMethod(ValueType::FACE_REF, "counterbore",
        [](ValuePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
            double holeDia = requireNumber(args, 0, "counterbore");
            double boreDia = requireNumber(args, 1, "counterbore");
            double boreDepth = requireNumber(args, 2, "counterbore");
            double holeDepth = requireNumber(args, 3, "counterbore");
            double cx = args.size() > 4 ? args[4]->asNumber() : 0;
            double cy = args.size() > 5 ? args[5]->asNumber() : 0;
            return Value::makeShape(self->asFaceRef()->counterbore(holeDia, boreDia, boreDepth, holeDepth, cx, cy));
        });

    registerTypedMethod(ValueType::FACE_REF, "countersink",
        [](ValuePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
            double holeDia = requireNumber(args, 0, "countersink");
            double sinkDia = requireNumber(args, 1, "countersink");
            double sinkAngle = requireNumber(args, 2, "countersink");
            double holeDepth = requireNumber(args, 3, "countersink");
            double cx = args.size() > 4 ? args[4]->asNumber() : 0;
            double cy = args.size() > 5 ? args[5]->asNumber() : 0;
            return Value::makeShape(self->asFaceRef()->countersink(holeDia, sinkDia, sinkAngle, holeDepth, cx, cy));
        });

    // =========================================================================
    // Typed Methods — EDGE_SELECTOR
    // =========================================================================

    registerTypedMethod(ValueType::EDGE_SELECTOR, "parallelTo",
        [](ValuePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
            const auto& v = args.at(0)->toVector();
            if (v.size() < 3)
                throw EvalError("parallelTo() requires a 3-component vector");
            gp_Dir dir(v[0], v[1], v[2]);
            return Value::makeEdgeSelector(self->asEdgeSelector()->parallelTo(dir));
        });

    registerTypedMethod(ValueType::EDGE_SELECTOR, "vertical",
        [](ValuePtr self, const std::vector<ValuePtr>& /*args*/) -> ValuePtr {
            return Value::makeEdgeSelector(self->asEdgeSelector()->vertical());
        });

    registerTypedMethod(ValueType::EDGE_SELECTOR, "horizontal",
        [](ValuePtr self, const std::vector<ValuePtr>& /*args*/) -> ValuePtr {
            return Value::makeEdgeSelector(self->asEdgeSelector()->horizontal());
        });

    registerTypedMethod(ValueType::EDGE_SELECTOR, "fillet",
        [](ValuePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
            double r = requireNumber(args, 0, "fillet");
            return Value::makeShape(self->asEdgeSelector()->fillet(r));
        });

    registerTypedMethod(ValueType::EDGE_SELECTOR, "chamfer",
        [](ValuePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
            double d = requireNumber(args, 0, "chamfer");
            return Value::makeShape(self->asEdgeSelector()->chamfer(d));
        });

    registerTypedMethod(ValueType::EDGE_SELECTOR, "count",
        [](ValuePtr self, const std::vector<ValuePtr>& /*args*/) -> ValuePtr {
            return Value::makeNumber(self->asEdgeSelector()->count());
        });

    registerTypedMethod(ValueType::EDGE_SELECTOR, "longest",
        [](ValuePtr self, const std::vector<ValuePtr>& /*args*/) -> ValuePtr {
            return Value::makeEdgeSelector(self->asEdgeSelector()->longest());
        });

    registerTypedMethod(ValueType::EDGE_SELECTOR, "shortest",
        [](ValuePtr self, const std::vector<ValuePtr>& /*args*/) -> ValuePtr {
            return Value::makeEdgeSelector(self->asEdgeSelector()->shortest());
        });

    registerTypedMethod(ValueType::EDGE_SELECTOR, "longerThan",
        [](ValuePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
            double minLen = requireNumber(args, 0, "longerThan");
            return Value::makeEdgeSelector(self->asEdgeSelector()->longerThan(minLen));
        });

    registerTypedMethod(ValueType::EDGE_SELECTOR, "shorterThan",
        [](ValuePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
            double maxLen = requireNumber(args, 0, "shorterThan");
            return Value::makeEdgeSelector(self->asEdgeSelector()->shorterThan(maxLen));
        });

    registerTypedMethod(ValueType::EDGE_SELECTOR, "ofFace",
        [](ValuePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
            if (args.empty() || args[0]->type() != ValueType::FACE_REF)
                throw EvalError("ofFace() requires a face reference argument");
            return Value::makeEdgeSelector(self->asEdgeSelector()->ofFace(args[0]->asFaceRef()));
        });

    // ===== List methods =====

    registerTypedMethod(ValueType::LIST, "push",
        [](ValuePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
            if (args.empty())
                throw EvalError("push() requires at least 1 argument");
            for (const auto& arg : args)
                self->listPush(arg);
            return self;
        });

    registerTypedMethod(ValueType::LIST, "length",
        [](ValuePtr self, const std::vector<ValuePtr>&) -> ValuePtr {
            return Value::makeNumber(self->listLength());
        });

    registerTypedMethod(ValueType::LIST, "get",
        [](ValuePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
            if (args.size() != 1)
                throw EvalError("get() requires 1 argument (index)");
            return self->listGet(static_cast<int>(args[0]->asNumber()));
        });

    // ===== Color/Material on shapes =====

    registerTypedMethod(ValueType::SHAPE, "color",
        [](ValuePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
            if (args.empty() || args[0]->type() != ValueType::COLOR)
                throw EvalError("color() requires a color argument");
            auto shape = self->asShape();
            shape->setColor(args[0]->asColor());
            return Value::makeShape(shape);
        });

    registerTypedMethod(ValueType::SHAPE, "material",
        [](ValuePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
            if (args.empty() || args[0]->type() != ValueType::MATERIAL)
                throw EvalError("material() requires a material argument");
            auto shape = self->asShape();
            shape->setMaterial(args[0]->asMaterial());
            return Value::makeShape(shape);
        });

    registerTypedMethod(ValueType::SHAPE, "tag",
        [](ValuePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
            if (args.empty() || args[0]->type() != ValueType::STRING)
                throw EvalError("tag() requires a string argument");
            auto shape = self->asShape();
            shape->addTag(args[0]->asString());
            return Value::makeShape(shape);
        });

    registerTypedMethod(ValueType::SHAPE, "tags",
        [](ValuePtr self, const std::vector<ValuePtr>& /*args*/) -> ValuePtr {
            auto shape = self->asShape();
            std::vector<ValuePtr> tagList;
            for (const auto& t : shape->tags())
                tagList.push_back(Value::makeString(t));
            return Value::makeList(tagList);
        });

    registerTypedMethod(ValueType::SHAPE, "hasTag",
        [](ValuePtr self, const std::vector<ValuePtr>& args) -> ValuePtr {
            if (args.empty() || args[0]->type() != ValueType::STRING)
                throw EvalError("hasTag() requires a string argument");
            return Value::makeBool(self->asShape()->hasTag(args[0]->asString()));
        });
}

} // namespace opendcad
