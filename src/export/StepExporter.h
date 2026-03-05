#pragma once

#include <string>
#include <TopoDS_Shape.hxx>

namespace opendcad {

class StepExporter {
public:
    static bool write(const TopoDS_Shape& shape, const std::string& path);
};

} // namespace opendcad
