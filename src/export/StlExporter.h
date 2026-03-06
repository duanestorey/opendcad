#pragma once

#include <string>
#include <vector>
#include <memory>
#include <TopoDS_Shape.hxx>

namespace opendcad {

class Shape;
using ShapePtr = std::shared_ptr<Shape>;

struct StlParams {
    double deflection = 0.1;
    double angle = 0.5;
    bool parallel = true;
};

struct StlResult {
    bool success = false;
    size_t triangleCount = 0;
};

class StlExporter {
public:
    static StlResult write(const TopoDS_Shape& shape, const std::string& path,
                           const StlParams& params = {});
    static StlResult writeAssembly(const std::vector<ShapePtr>& shapes,
                                   const std::string& path,
                                   const StlParams& params = {});

    // Count triangles for a meshed shape (used by manifest even when STL not written)
    static size_t countTriangles(const TopoDS_Shape& shape, const StlParams& params);
};

} // namespace opendcad
