#pragma once

#include <string>
#include <vector>
#include <memory>
#include <TopoDS_Shape.hxx>

namespace opendcad {

class Shape;
using ShapePtr = std::shared_ptr<Shape>;

class StepExporter {
public:
    static bool write(const TopoDS_Shape& shape, const std::string& path);
    static bool writeWithMetadata(const std::vector<ShapePtr>& shapes,
                                   const std::string& name,
                                   const std::string& path);
};

} // namespace opendcad
