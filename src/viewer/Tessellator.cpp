#include "Tessellator.h"

#include <cfloat>
#include <cmath>

#include <BRepMesh_IncrementalMesh.hxx>
#include <TopExp_Explorer.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <Poly_Triangle.hxx>
#include <Poly_Polygon3D.hxx>
#include <Poly_PolygonOnTriangulation.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopAbs_Orientation.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

namespace opendcad {

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

TessResult Tessellator::tessellate(const TopoDS_Shape& shape,
                                   double deflection, double angle) {
    // Default color: light steel blue, non-metallic, medium roughness
    return tessellate(shape, 0.72f, 0.78f, 0.86f, 0.0f, 0.5f,
                      deflection, angle);
}

TessResult Tessellator::tessellate(const TopoDS_Shape& shape,
                                   float defaultR, float defaultG, float defaultB,
                                   float metallic, float roughness,
                                   double deflection, double angle) {
    // Tessellate the BRep shape into a polygon mesh
    BRepMesh_IncrementalMesh mesher(shape, deflection, false, angle);
    mesher.Perform();

    TessResult result;

    extractFaces(shape, result, defaultR, defaultG, defaultB, metallic, roughness);
    extractEdges(shape, result);
    computeBounds(result);

    return result;
}

// ---------------------------------------------------------------------------
// Face extraction
// ---------------------------------------------------------------------------

void Tessellator::extractFaces(const TopoDS_Shape& shape, TessResult& result,
                               float r, float g, float b,
                               float met, float rough) {
    int faceID = 0;

    for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next()) {
        const TopoDS_Face& face = TopoDS::Face(explorer.Current());

        TopLoc_Location loc;
        Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
        if (tri.IsNull()) {
            ++faceID;
            continue;
        }

        const gp_Trsf trsf = loc.IsIdentity() ? gp_Trsf() : loc.Transformation();
        const bool reversed = (face.Orientation() == TopAbs_REVERSED);

        // Surface type
        BRepAdaptor_Surface adaptor(face);
        int surfaceType = static_cast<int>(adaptor.GetType());

        // Face area
        GProp_GProps props;
        BRepGProp::SurfaceProperties(face, props);
        double area = props.Mass();

        // Record face info
        TessFaceInfo faceInfo;
        faceInfo.faceID = faceID;
        faceInfo.triangleOffset = static_cast<int>(result.vertices.size()) / 3;
        faceInfo.triangleCount = tri->NbTriangles();
        faceInfo.surfaceType = surfaceType;
        faceInfo.area = area;
        result.faces.push_back(faceInfo);

        bool hasNormals = tri->HasNormals();

        // Iterate triangles (OCCT uses 1-based indexing)
        for (int i = 1; i <= tri->NbTriangles(); ++i) {
            const Poly_Triangle& triangle = tri->Triangle(i);
            Standard_Integer n1, n2, n3;
            triangle.Get(n1, n2, n3);

            // Swap winding for reversed faces
            if (reversed) {
                std::swap(n1, n3);
            }

            // Get node positions, apply location transform
            gp_Pnt p1 = tri->Node(n1).Transformed(trsf);
            gp_Pnt p2 = tri->Node(n2).Transformed(trsf);
            gp_Pnt p3 = tri->Node(n3).Transformed(trsf);

            // Compute or get normals
            float nx1, ny1, nz1;
            float nx2, ny2, nz2;
            float nx3, ny3, nz3;

            if (hasNormals) {
                gp_Dir dir1 = tri->Normal(n1).Transformed(trsf);
                gp_Dir dir2 = tri->Normal(n2).Transformed(trsf);
                gp_Dir dir3 = tri->Normal(n3).Transformed(trsf);

                if (reversed) {
                    dir1.Reverse();
                    dir2.Reverse();
                    dir3.Reverse();
                }

                nx1 = static_cast<float>(dir1.X());
                ny1 = static_cast<float>(dir1.Y());
                nz1 = static_cast<float>(dir1.Z());
                nx2 = static_cast<float>(dir2.X());
                ny2 = static_cast<float>(dir2.Y());
                nz2 = static_cast<float>(dir2.Z());
                nx3 = static_cast<float>(dir3.X());
                ny3 = static_cast<float>(dir3.Y());
                nz3 = static_cast<float>(dir3.Z());
            } else {
                // Compute flat normal from triangle edges
                gp_Vec edge1(p1, p2);
                gp_Vec edge2(p1, p3);
                gp_Vec normal = edge1.Crossed(edge2);

                if (normal.Magnitude() > 1e-12) {
                    normal.Normalize();
                } else {
                    normal = gp_Vec(0, 0, 1);
                }

                nx1 = nx2 = nx3 = static_cast<float>(normal.X());
                ny1 = ny2 = ny3 = static_cast<float>(normal.Y());
                nz1 = nz2 = nz3 = static_cast<float>(normal.Z());
            }

            float fid = static_cast<float>(faceID);

            // Vertex 1
            TessVertex v1;
            v1.pos[0] = static_cast<float>(p1.X());
            v1.pos[1] = static_cast<float>(p1.Y());
            v1.pos[2] = static_cast<float>(p1.Z());
            v1.normal[0] = nx1; v1.normal[1] = ny1; v1.normal[2] = nz1;
            v1.faceID = fid;
            v1.color[0] = r; v1.color[1] = g; v1.color[2] = b;
            v1.metallic = met;
            v1.roughness = rough;
            result.vertices.push_back(v1);

            // Vertex 2
            TessVertex v2;
            v2.pos[0] = static_cast<float>(p2.X());
            v2.pos[1] = static_cast<float>(p2.Y());
            v2.pos[2] = static_cast<float>(p2.Z());
            v2.normal[0] = nx2; v2.normal[1] = ny2; v2.normal[2] = nz2;
            v2.faceID = fid;
            v2.color[0] = r; v2.color[1] = g; v2.color[2] = b;
            v2.metallic = met;
            v2.roughness = rough;
            result.vertices.push_back(v2);

            // Vertex 3
            TessVertex v3;
            v3.pos[0] = static_cast<float>(p3.X());
            v3.pos[1] = static_cast<float>(p3.Y());
            v3.pos[2] = static_cast<float>(p3.Z());
            v3.normal[0] = nx3; v3.normal[1] = ny3; v3.normal[2] = nz3;
            v3.faceID = fid;
            v3.color[0] = r; v3.color[1] = g; v3.color[2] = b;
            v3.metallic = met;
            v3.roughness = rough;
            result.vertices.push_back(v3);
        }

        ++faceID;
    }
}

// ---------------------------------------------------------------------------
// Edge extraction
// ---------------------------------------------------------------------------

void Tessellator::extractEdges(const TopoDS_Shape& shape, TessResult& result) {
    TopTools_IndexedMapOfShape edgeMap;
    TopExp::MapShapes(shape, TopAbs_EDGE, edgeMap);

    for (int i = 1; i <= edgeMap.Extent(); ++i) {
        const TopoDS_Edge& edge = TopoDS::Edge(edgeMap(i));
        int edgeID = i - 1;  // 0-based

        // Edge length
        GProp_GProps props;
        BRepGProp::LinearProperties(edge, props);
        double length = props.Mass();

        // Try Polygon3D first
        TopLoc_Location loc;
        Handle(Poly_Polygon3D) poly = BRep_Tool::Polygon3D(edge, loc);

        if (!poly.IsNull()) {
            const gp_Trsf trsf = loc.IsIdentity() ? gp_Trsf() : loc.Transformation();
            const TColgp_Array1OfPnt& nodes = poly->Nodes();
            int vertexOffset = static_cast<int>(result.edgeVertices.size());
            int segmentVertices = 0;

            for (int j = nodes.Lower(); j < nodes.Upper(); ++j) {
                gp_Pnt p1 = nodes(j).Transformed(trsf);
                gp_Pnt p2 = nodes(j + 1).Transformed(trsf);

                TessEdgeVertex ev1;
                ev1.pos[0] = static_cast<float>(p1.X());
                ev1.pos[1] = static_cast<float>(p1.Y());
                ev1.pos[2] = static_cast<float>(p1.Z());
                ev1.edgeID = static_cast<float>(edgeID);
                result.edgeVertices.push_back(ev1);

                TessEdgeVertex ev2;
                ev2.pos[0] = static_cast<float>(p2.X());
                ev2.pos[1] = static_cast<float>(p2.Y());
                ev2.pos[2] = static_cast<float>(p2.Z());
                ev2.edgeID = static_cast<float>(edgeID);
                result.edgeVertices.push_back(ev2);

                segmentVertices += 2;
            }

            TessEdgeInfo edgeInfo;
            edgeInfo.edgeID = edgeID;
            edgeInfo.vertexOffset = vertexOffset;
            edgeInfo.vertexCount = segmentVertices;
            edgeInfo.length = length;
            result.edges.push_back(edgeInfo);
            continue;
        }

        // Fallback: PolygonOnTriangulation — find a face that shares this edge
        bool found = false;
        for (TopExp_Explorer faceExplorer(shape, TopAbs_FACE); faceExplorer.More(); faceExplorer.Next()) {
            const TopoDS_Face& face = TopoDS::Face(faceExplorer.Current());
            TopLoc_Location faceLoc;
            Handle(Poly_Triangulation) faceTri = BRep_Tool::Triangulation(face, faceLoc);
            if (faceTri.IsNull()) continue;

            Handle(Poly_PolygonOnTriangulation) polyOnTri =
                BRep_Tool::PolygonOnTriangulation(edge, faceTri, faceLoc);
            if (polyOnTri.IsNull()) continue;

            const gp_Trsf trsf = faceLoc.IsIdentity() ? gp_Trsf() : faceLoc.Transformation();
            const TColStd_Array1OfInteger& nodeIndices = polyOnTri->Nodes();
            int vertexOffset = static_cast<int>(result.edgeVertices.size());
            int segmentVertices = 0;

            for (int j = nodeIndices.Lower(); j < nodeIndices.Upper(); ++j) {
                gp_Pnt p1 = faceTri->Node(nodeIndices(j)).Transformed(trsf);
                gp_Pnt p2 = faceTri->Node(nodeIndices(j + 1)).Transformed(trsf);

                TessEdgeVertex ev1;
                ev1.pos[0] = static_cast<float>(p1.X());
                ev1.pos[1] = static_cast<float>(p1.Y());
                ev1.pos[2] = static_cast<float>(p1.Z());
                ev1.edgeID = static_cast<float>(edgeID);
                result.edgeVertices.push_back(ev1);

                TessEdgeVertex ev2;
                ev2.pos[0] = static_cast<float>(p2.X());
                ev2.pos[1] = static_cast<float>(p2.Y());
                ev2.pos[2] = static_cast<float>(p2.Z());
                ev2.edgeID = static_cast<float>(edgeID);
                result.edgeVertices.push_back(ev2);

                segmentVertices += 2;
            }

            TessEdgeInfo edgeInfo;
            edgeInfo.edgeID = edgeID;
            edgeInfo.vertexOffset = vertexOffset;
            edgeInfo.vertexCount = segmentVertices;
            edgeInfo.length = length;
            result.edges.push_back(edgeInfo);
            found = true;
            break;  // Only need one face's tessellation per edge
        }

        // If neither method worked, skip this edge (degenerate or untessellated)
        if (!found) {
            // Still record the edge with zero vertices so edgeID mapping stays consistent
        }
    }
}

// ---------------------------------------------------------------------------
// Bounding box
// ---------------------------------------------------------------------------

void Tessellator::computeBounds(TessResult& result) {
    result.bboxMin[0] = result.bboxMin[1] = result.bboxMin[2] = FLT_MAX;
    result.bboxMax[0] = result.bboxMax[1] = result.bboxMax[2] = -FLT_MAX;

    for (const auto& v : result.vertices) {
        for (int i = 0; i < 3; ++i) {
            if (v.pos[i] < result.bboxMin[i]) result.bboxMin[i] = v.pos[i];
            if (v.pos[i] > result.bboxMax[i]) result.bboxMax[i] = v.pos[i];
        }
    }

    // If no vertices, set bounds to zero
    if (result.vertices.empty()) {
        result.bboxMin[0] = result.bboxMin[1] = result.bboxMin[2] = 0.0f;
        result.bboxMax[0] = result.bboxMax[1] = result.bboxMax[2] = 0.0f;
    }
}

} // namespace opendcad
