#pragma once

#include <glad/glad.h>
#include <string>
#include <vector>

namespace opendcad {

struct RenderObject {
    std::string name;
    std::vector<std::string> tags;

    // Face mesh
    GLuint faceVAO = 0, faceVBO = 0;
    GLsizei faceVertexCount = 0;

    // Edge mesh
    GLuint edgeVAO = 0, edgeVBO = 0;
    GLsizei edgeVertexCount = 0;

    // Bounding box
    float bboxMin[3] = {0, 0, 0};
    float bboxMax[3] = {0, 0, 0};

    // State
    bool visible = true;
    bool selected = false;

    // Face/edge metadata for properties panel
    int faceCount = 0;
    int edgeCount = 0;
    double volume = 0.0;
    double surfaceArea = 0.0;

    // Material metadata for material panel
    float materialColor[3] = {0.72f, 0.78f, 0.86f};
    float materialMetallic = 0.0f;
    float materialRoughness = 0.5f;
    std::string materialPreset;

    void destroy() {
        if (faceVAO) { glDeleteVertexArrays(1, &faceVAO); faceVAO = 0; }
        if (faceVBO) { glDeleteBuffers(1, &faceVBO); faceVBO = 0; }
        faceVertexCount = 0;

        if (edgeVAO) { glDeleteVertexArrays(1, &edgeVAO); edgeVAO = 0; }
        if (edgeVBO) { glDeleteBuffers(1, &edgeVBO); edgeVBO = 0; }
        edgeVertexCount = 0;
    }
};

} // namespace opendcad
