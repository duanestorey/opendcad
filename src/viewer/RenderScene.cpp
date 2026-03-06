#include "RenderScene.h"
#include "Tessellator.h"

#include <glad/glad.h>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>

#include <algorithm>
#include <cstddef>
#include <limits>

namespace opendcad {

RenderScene::~RenderScene() {
    clear();
}

int RenderScene::addShape(const std::string& name,
                          const TopoDS_Shape& shape,
                          const float color[3],
                          float metallic, float roughness,
                          const std::vector<std::string>& tags,
                          double deflection,
                          double angle) {
    // Tessellate the shape
    TessResult tess = Tessellator::tessellate(
        shape, color[0], color[1], color[2], metallic, roughness,
        deflection, angle);

    // Create RenderObject
    RenderObject obj;
    obj.name = name;
    obj.tags = tags;

    // Copy bounding box
    for (int i = 0; i < 3; ++i) {
        obj.bboxMin[i] = tess.bboxMin[i];
        obj.bboxMax[i] = tess.bboxMax[i];
    }

    // Face/edge metadata
    obj.faceCount = static_cast<int>(tess.faces.size());
    obj.edgeCount = static_cast<int>(tess.edges.size());

    // Compute volume and surface area via OCCT
    GProp_GProps volumeProps;
    BRepGProp::VolumeProperties(shape, volumeProps);
    obj.volume = volumeProps.Mass();

    GProp_GProps surfaceProps;
    BRepGProp::SurfaceProperties(shape, surfaceProps);
    obj.surfaceArea = surfaceProps.Mass();

    // Copy material metadata for UI panels
    for (int i = 0; i < 3; ++i) {
        obj.materialColor[i] = color[i];
    }
    obj.materialMetallic = metallic;
    obj.materialRoughness = roughness;

    // Upload geometry to GPU
    uploadToGPU(obj, tess);

    // Store and return index
    int index = static_cast<int>(objects_.size());
    objects_.push_back(std::move(obj));
    return index;
}

void RenderScene::uploadToGPU(RenderObject& obj, const TessResult& tess) {
    // --- Face VAO/VBO ---
    if (!tess.vertices.empty()) {
        glGenVertexArrays(1, &obj.faceVAO);
        glGenBuffers(1, &obj.faceVBO);

        glBindVertexArray(obj.faceVAO);
        glBindBuffer(GL_ARRAY_BUFFER, obj.faceVBO);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(tess.vertices.size() * sizeof(TessVertex)),
                     tess.vertices.data(),
                     GL_STATIC_DRAW);

        const GLsizei stride = sizeof(TessVertex);

        // location 0: pos (vec3, offset 0)
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void*>(offsetof(TessVertex, pos)));

        // location 1: normal (vec3, offset 12)
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void*>(offsetof(TessVertex, normal)));

        // location 2: faceID (float, offset 24)
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void*>(offsetof(TessVertex, faceID)));

        // location 3: color (vec3, offset 28)
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void*>(offsetof(TessVertex, color)));

        // location 4: matParams (vec2 = metallic + roughness, offset 40)
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void*>(offsetof(TessVertex, metallic)));

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        obj.faceVertexCount = static_cast<GLsizei>(tess.vertices.size());
    }

    // --- Edge VAO/VBO ---
    if (!tess.edgeVertices.empty()) {
        glGenVertexArrays(1, &obj.edgeVAO);
        glGenBuffers(1, &obj.edgeVBO);

        glBindVertexArray(obj.edgeVAO);
        glBindBuffer(GL_ARRAY_BUFFER, obj.edgeVBO);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(tess.edgeVertices.size() * sizeof(TessEdgeVertex)),
                     tess.edgeVertices.data(),
                     GL_STATIC_DRAW);

        const GLsizei edgeStride = sizeof(TessEdgeVertex);

        // location 0: pos (vec3, offset 0)
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, edgeStride,
                              reinterpret_cast<const void*>(offsetof(TessEdgeVertex, pos)));

        // location 1: edgeID (float, offset 12)
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, edgeStride,
                              reinterpret_cast<const void*>(offsetof(TessEdgeVertex, edgeID)));

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        obj.edgeVertexCount = static_cast<GLsizei>(tess.edgeVertices.size());
    }
}

void RenderScene::clear() {
    for (auto& obj : objects_) {
        obj.destroy();
    }
    objects_.clear();
}

std::set<std::string> RenderScene::uniqueTags() const {
    std::set<std::string> result;
    for (const auto& obj : objects_) {
        for (const auto& tag : obj.tags) {
            result.insert(tag);
        }
    }
    return result;
}

void RenderScene::setTagVisibility(const std::string& tag, bool visible) {
    for (auto& obj : objects_) {
        for (const auto& t : obj.tags) {
            if (t == tag) {
                obj.visible = visible;
                break;
            }
        }
    }
}

void RenderScene::sceneBounds(float outMin[3], float outMax[3]) const {
    if (objects_.empty()) {
        for (int i = 0; i < 3; ++i) {
            outMin[i] = 0.0f;
            outMax[i] = 0.0f;
        }
        return;
    }

    for (int i = 0; i < 3; ++i) {
        outMin[i] = std::numeric_limits<float>::max();
        outMax[i] = std::numeric_limits<float>::lowest();
    }

    for (const auto& obj : objects_) {
        for (int i = 0; i < 3; ++i) {
            outMin[i] = std::min(outMin[i], obj.bboxMin[i]);
            outMax[i] = std::max(outMax[i], obj.bboxMax[i]);
        }
    }
}

} // namespace opendcad
