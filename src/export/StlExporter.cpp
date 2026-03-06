#include "StlExporter.h"
#include "Shape.h"
#include "Debug.h"
#include <BRepMesh_IncrementalMesh.hxx>
#include <StlAPI_Writer.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <BRep_Builder.hxx>
#include <Poly_Triangulation.hxx>
#include <BRep_Tool.hxx>

namespace opendcad {

static size_t countTris(const TopoDS_Shape& shape) {
    size_t count = 0;
    for (TopExp_Explorer f(shape, TopAbs_FACE); f.More(); f.Next()) {
        TopLoc_Location loc;
        Handle(Poly_Triangulation) tri =
            BRep_Tool::Triangulation(TopoDS::Face(f.Current()), loc);
        if (!tri.IsNull())
            count += tri->NbTriangles();
    }
    return count;
}

size_t StlExporter::countTriangles(const TopoDS_Shape& shape, const StlParams& params) {
    BRepMesh_IncrementalMesh mesher(shape, params.deflection, false, params.angle, params.parallel);
    mesher.Perform();
    if (!mesher.IsDone()) return 0;
    return countTris(shape);
}

StlResult StlExporter::write(const TopoDS_Shape& shape, const std::string& path,
                              const StlParams& params) {
    DEBUG_INFO("Writing STL file [" << path << "] with deflection [" << params.deflection << "]");

    BRepMesh_IncrementalMesh mesher(shape, params.deflection, false, params.angle, params.parallel);
    mesher.Perform();
    if (!mesher.IsDone()) return {false, 0};

    size_t triCount = countTris(shape);
    DEBUG_INFO("...mesh tessellation complete - triangles [" << triCount << "]");

    StlAPI_Writer writer;
    bool ok = writer.Write(shape, path.c_str());
    DEBUG_INFO("...STL file written");
    return {ok, triCount};
}

StlResult StlExporter::writeAssembly(const std::vector<ShapePtr>& shapes,
                                      const std::string& path,
                                      const StlParams& params) {
    if (shapes.size() == 1) {
        return write(shapes[0]->getShape(), path, params);
    }

    DEBUG_INFO("Writing assembly STL [" << path << "] (" << shapes.size() << " shapes)");

    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);
    for (const auto& shape : shapes) {
        builder.Add(compound, shape->getShape());
    }

    return write(compound, path, params);
}

} // namespace opendcad
