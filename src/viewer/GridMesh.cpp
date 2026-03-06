#include "GridMesh.h"
#include "ShaderProgram.h"

#include <cmath>
#include <vector>

namespace opendcad {

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

GridMesh::~GridMesh() {
    destroy();
}

// ---------------------------------------------------------------------------
// build() — pre-compute grid + axis vertices into a single VBO
// ---------------------------------------------------------------------------

void GridMesh::build(float gridSize, float gridStep, float majorStep, float axisLength) {
    // Clean up any previous buffers
    destroy();

    // Vertex format: [x, y, z, r, g, b, a] = 7 floats per vertex
    std::vector<float> vertices;

    // Estimate capacity: grid lines along X and Y, plus 3 axis lines (6 vertices)
    // Each direction: roughly (2 * gridSize / gridStep + 1) lines, 2 vertices each
    const int linesPerAxis = static_cast<int>(2.0f * gridSize / gridStep) + 1;
    vertices.reserve(static_cast<size_t>((linesPerAxis * 2 * 2 + 6) * 7));

    // -----------------------------------------------------------------------
    // Grid lines (Z = 0 plane)
    // -----------------------------------------------------------------------

    // Lines parallel to Y axis (varying X)
    for (float x = -gridSize; x <= gridSize + 0.001f; x += gridStep) {
        bool isMajor = (std::fmod(std::fabs(x), majorStep) < 1e-4f);
        float c = isMajor ? 0.22f : 0.10f;
        float a = isMajor ? 0.22f : 0.10f;

        // Start vertex
        vertices.push_back(x);
        vertices.push_back(-gridSize);
        vertices.push_back(0.0f);
        vertices.push_back(c);
        vertices.push_back(c);
        vertices.push_back(c);
        vertices.push_back(a);

        // End vertex
        vertices.push_back(x);
        vertices.push_back(gridSize);
        vertices.push_back(0.0f);
        vertices.push_back(c);
        vertices.push_back(c);
        vertices.push_back(c);
        vertices.push_back(a);
    }

    // Lines parallel to X axis (varying Y)
    for (float y = -gridSize; y <= gridSize + 0.001f; y += gridStep) {
        bool isMajor = (std::fmod(std::fabs(y), majorStep) < 1e-4f);
        float c = isMajor ? 0.22f : 0.10f;
        float a = isMajor ? 0.22f : 0.10f;

        // Start vertex
        vertices.push_back(-gridSize);
        vertices.push_back(y);
        vertices.push_back(0.0f);
        vertices.push_back(c);
        vertices.push_back(c);
        vertices.push_back(c);
        vertices.push_back(a);

        // End vertex
        vertices.push_back(gridSize);
        vertices.push_back(y);
        vertices.push_back(0.0f);
        vertices.push_back(c);
        vertices.push_back(c);
        vertices.push_back(c);
        vertices.push_back(a);
    }

    gridVertexCount_ = static_cast<GLsizei>(vertices.size() / 7);

    // -----------------------------------------------------------------------
    // Axis lines (drawn on top with depth test enabled)
    // -----------------------------------------------------------------------

    // X axis (red): origin to (axisLength, 0, 0)
    vertices.push_back(0.0f);  vertices.push_back(0.0f);  vertices.push_back(0.0f);
    vertices.push_back(1.0f);  vertices.push_back(0.0f);  vertices.push_back(0.0f);  vertices.push_back(1.0f);

    vertices.push_back(axisLength); vertices.push_back(0.0f);  vertices.push_back(0.0f);
    vertices.push_back(1.0f);  vertices.push_back(0.0f);  vertices.push_back(0.0f);  vertices.push_back(1.0f);

    // Y axis (green): origin to (0, axisLength, 0)
    vertices.push_back(0.0f);  vertices.push_back(0.0f);  vertices.push_back(0.0f);
    vertices.push_back(0.0f);  vertices.push_back(1.0f);  vertices.push_back(0.0f);  vertices.push_back(1.0f);

    vertices.push_back(0.0f);  vertices.push_back(axisLength); vertices.push_back(0.0f);
    vertices.push_back(0.0f);  vertices.push_back(1.0f);  vertices.push_back(0.0f);  vertices.push_back(1.0f);

    // Z axis (blue): origin to (0, 0, axisLength)
    vertices.push_back(0.0f);  vertices.push_back(0.0f);  vertices.push_back(0.0f);
    vertices.push_back(0.0f);  vertices.push_back(0.0f);  vertices.push_back(1.0f);  vertices.push_back(1.0f);

    vertices.push_back(0.0f);  vertices.push_back(0.0f);  vertices.push_back(axisLength);
    vertices.push_back(0.0f);  vertices.push_back(0.0f);  vertices.push_back(1.0f);  vertices.push_back(1.0f);

    axisVertexCount_ = static_cast<GLsizei>(vertices.size() / 7) - gridVertexCount_;
    totalVertexCount_ = gridVertexCount_ + axisVertexCount_;

    // -----------------------------------------------------------------------
    // Create VAO + VBO, upload data
    // -----------------------------------------------------------------------

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
                 vertices.data(), GL_STATIC_DRAW);

    // Stride: 7 floats = 28 bytes per vertex
    const GLsizei stride = 7 * sizeof(float);

    // Location 0: position (vec3), offset 0
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(0));

    // Location 1: color (vec4), offset 12 bytes (3 floats)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(3 * sizeof(float)));

    // Unbind VAO (leave VBO bound to VAO state)
    glBindVertexArray(0);
}

// ---------------------------------------------------------------------------
// draw() — render grid then axes using the provided shader
// ---------------------------------------------------------------------------

void GridMesh::draw(ShaderProgram& shader, const float* mvp) {
    if (totalVertexCount_ == 0) return;

    shader.use();
    shader.setMat4("uMVP", mvp);

    glBindVertexArray(vao_);

    // Grid lines: draw without depth test so grid is always visible
    glDisable(GL_DEPTH_TEST);
    glDrawArrays(GL_LINES, 0, gridVertexCount_);

    // Axis lines: draw with depth test so they interact with geometry
    glEnable(GL_DEPTH_TEST);
    glDrawArrays(GL_LINES, gridVertexCount_, axisVertexCount_);

    glBindVertexArray(0);
}

// ---------------------------------------------------------------------------
// destroy() — release GPU resources
// ---------------------------------------------------------------------------

void GridMesh::destroy() {
    if (vbo_ != 0) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    gridVertexCount_ = 0;
    axisVertexCount_ = 0;
    totalVertexCount_ = 0;
}

} // namespace opendcad
