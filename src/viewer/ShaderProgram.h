#pragma once

#include <glad/glad.h>
#include <string>
#include <unordered_map>

namespace opendcad {

class ShaderProgram {
public:
    ShaderProgram() = default;
    ~ShaderProgram();

    // Non-copyable, movable
    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;
    ShaderProgram(ShaderProgram&& other) noexcept;
    ShaderProgram& operator=(ShaderProgram&& other) noexcept;

    // Compile and link from source strings. Returns false on error (check error()).
    bool build(const char* vertexSrc, const char* fragmentSrc);

    void use() const;
    GLuint id() const { return program_; }

    // Uniform setters (cache locations)
    void setInt(const char* name, int value);
    void setFloat(const char* name, float value);
    void setVec2(const char* name, float x, float y);
    void setVec3(const char* name, float x, float y, float z);
    void setVec3(const char* name, const float* v);
    void setMat3(const char* name, const float* m);
    void setMat4(const char* name, const float* m);

    const std::string& error() const { return error_; }

private:
    GLint getLocation(const char* name);
    GLuint compileShader(GLenum type, const char* src);

    GLuint program_ = 0;
    std::unordered_map<std::string, GLint> locationCache_;
    std::string error_;
};

} // namespace opendcad
