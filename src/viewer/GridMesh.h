#pragma once

#include <glad/glad.h>

namespace opendcad {

class ShaderProgram;

class GridMesh {
public:
    GridMesh() = default;
    ~GridMesh();

    // Non-copyable
    GridMesh(const GridMesh&) = delete;
    GridMesh& operator=(const GridMesh&) = delete;

    // Build grid lines + axis lines into a single VBO.
    // Vertex format: [x, y, z, r, g, b, a] = 28 bytes per vertex.
    void build(float gridSize = 500.0f, float gridStep = 10.0f,
               float majorStep = 50.0f, float axisLength = 30.0f);

    void draw(ShaderProgram& shader, const float* mvp);
    void destroy();

private:
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLsizei gridVertexCount_ = 0;
    GLsizei axisVertexCount_ = 0;
    GLsizei totalVertexCount_ = 0;
};

} // namespace opendcad
