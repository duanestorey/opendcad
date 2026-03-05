#include "StlExporter.h"
#include "Debug.h"
#include <BRepMesh_IncrementalMesh.hxx>
#include <StlAPI_Writer.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <Poly_Triangulation.hxx>
#include <BRep_Tool.hxx>

namespace opendcad {

bool StlExporter::write(const TopoDS_Shape& shape, const std::string& path,
                        const StlParams& params) {
    DEBUG_INFO("Writing STL file [" << path << "] with deflection [" << params.deflection << "]");

    BRepMesh_IncrementalMesh mesher(shape, params.deflection, false, params.angle, params.parallel);
    mesher.Perform();
    if (!mesher.IsDone()) return false;

    size_t triCount = 0;
    for (TopExp_Explorer f(shape, TopAbs_FACE); f.More(); f.Next()) {
        TopLoc_Location loc;
        Handle(Poly_Triangulation) tri =
            BRep_Tool::Triangulation(TopoDS::Face(f.Current()), loc);
        if (!tri.IsNull())
            triCount += tri->NbTriangles();
    }
    DEBUG_INFO("...mesh tessellation complete - triangles [" << triCount << "]");

    StlAPI_Writer writer;
    bool result = writer.Write(shape, path.c_str());
    DEBUG_INFO("...STL file written");
    return result;
}

} // namespace opendcad
