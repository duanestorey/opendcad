#pragma once

#include "RenderObject.h"
#include <string>
#include <vector>
#include <set>
#include <TopoDS_Shape.hxx>

namespace opendcad {

struct Color;
struct Material;
struct TessResult;

class RenderScene {
public:
    RenderScene() = default;
    ~RenderScene();

    // Tessellate shape and upload to GPU. Returns index of new object.
    int addShape(const std::string& name,
                 const TopoDS_Shape& shape,
                 const float color[3],
                 float metallic, float roughness,
                 const std::vector<std::string>& tags,
                 double deflection = 0.01,
                 double angle = 0.5);

    void clear();

    std::vector<RenderObject>& objects() { return objects_; }
    const std::vector<RenderObject>& objects() const { return objects_; }

    // Layer system
    std::set<std::string> uniqueTags() const;
    void setTagVisibility(const std::string& tag, bool visible);

    // Scene bounding box (union of all objects)
    void sceneBounds(float outMin[3], float outMax[3]) const;

private:
    std::vector<RenderObject> objects_;

    // Upload TessResult to GPU, populate RenderObject's VAO/VBO
    void uploadToGPU(RenderObject& obj, const TessResult& tess);
};

} // namespace opendcad
