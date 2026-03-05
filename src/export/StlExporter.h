#pragma once

#include <string>
#include <TopoDS_Shape.hxx>

namespace opendcad {

struct StlParams {
    double deflection = 0.1;
    double angle = 0.5;
    bool parallel = true;
};

class StlExporter {
public:
    static bool write(const TopoDS_Shape& shape, const std::string& path,
                      const StlParams& params = {});
};

} // namespace opendcad
