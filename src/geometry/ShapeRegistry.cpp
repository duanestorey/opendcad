#include "ShapeRegistry.h"
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
            const auto& v = arg->asVector();
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
            const auto& v = args[0]->asVector();
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
            const auto& v = args[0]->asVector();
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
                const auto& v = args[0]->asVector();
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
            const auto& v = args[0]->asVector();
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
}

} // namespace opendcad
