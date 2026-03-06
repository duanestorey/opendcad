#pragma once

#include <vector>
#include <cstdint>
#include <TopoDS_Shape.hxx>

namespace opendcad {

struct TessVertex {
    float pos[3];
    float normal[3];
    float faceID;       // integer as float for shader attribute
    float color[3];     // per-face color (default: object color)
    float metallic;
    float roughness;
};

struct TessEdgeVertex {
    float pos[3];
    float edgeID;       // integer as float
};

struct TessFaceInfo {
    int faceID;
    int triangleOffset;    // first vertex index / 3
    int triangleCount;
    int surfaceType;       // GeomAbs_SurfaceType enum
    double area;
};

struct TessEdgeInfo {
    int edgeID;
    int vertexOffset;
    int vertexCount;       // number of line segment vertices
    double length;
};

struct TessResult {
    std::vector<TessVertex> vertices;          // 3 per triangle, flat
    std::vector<TessFaceInfo> faces;
    std::vector<TessEdgeVertex> edgeVertices;  // 2 per line segment
    std::vector<TessEdgeInfo> edges;

    float bboxMin[3];
    float bboxMax[3];

    int totalTriangles() const { return static_cast<int>(vertices.size()) / 3; }
};

class Tessellator {
public:
    static TessResult tessellate(const TopoDS_Shape& shape,
                                 double deflection = 0.01,
                                 double angle = 0.5);

    // Overload with per-face color defaults
    static TessResult tessellate(const TopoDS_Shape& shape,
                                 float defaultR, float defaultG, float defaultB,
                                 float metallic, float roughness,
                                 double deflection = 0.01,
                                 double angle = 0.5);

private:
    static void extractFaces(const TopoDS_Shape& shape, TessResult& result,
                             float r, float g, float b, float met, float rough);
    static void extractEdges(const TopoDS_Shape& shape, TessResult& result);
    static void computeBounds(TessResult& result);
};

} // namespace opendcad
