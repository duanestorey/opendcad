#include "ShaderProgram.h"

#include <vector>

namespace opendcad {

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

ShaderProgram::~ShaderProgram() {
    if (program_ != 0) {
        glDeleteProgram(program_);
    }
}

ShaderProgram::ShaderProgram(ShaderProgram&& other) noexcept
    : program_(other.program_),
      locationCache_(std::move(other.locationCache_)),
      error_(std::move(other.error_)) {
    other.program_ = 0;
}

ShaderProgram& ShaderProgram::operator=(ShaderProgram&& other) noexcept {
    if (this != &other) {
        if (program_ != 0) {
            glDeleteProgram(program_);
        }
        program_ = other.program_;
        locationCache_ = std::move(other.locationCache_);
        error_ = std::move(other.error_);
        other.program_ = 0;
    }
    return *this;
}

// ---------------------------------------------------------------------------
// Compilation helpers
// ---------------------------------------------------------------------------

GLuint ShaderProgram::compileShader(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLint len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(static_cast<size_t>(len));
        glGetShaderInfoLog(shader, len, nullptr, log.data());

        const char* label = (type == GL_VERTEX_SHADER) ? "vertex" : "fragment";
        error_ = std::string(label) + " shader compile error:\n" + log.data();

        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool ShaderProgram::build(const char* vertexSrc, const char* fragmentSrc) {
    error_.clear();
    locationCache_.clear();

    // Clean up any previous program
    if (program_ != 0) {
        glDeleteProgram(program_);
        program_ = 0;
    }

    GLuint vs = compileShader(GL_VERTEX_SHADER, vertexSrc);
    if (vs == 0) {
        return false;
    }

    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragmentSrc);
    if (fs == 0) {
        glDeleteShader(vs);
        return false;
    }

    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);

    // Shaders are no longer needed after linking
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint success = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &success);
    if (!success) {
        GLint len = 0;
        glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(static_cast<size_t>(len));
        glGetProgramInfoLog(program_, len, nullptr, log.data());

        error_ = std::string("shader link error:\n") + log.data();

        glDeleteProgram(program_);
        program_ = 0;
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Usage
// ---------------------------------------------------------------------------

void ShaderProgram::use() const {
    glUseProgram(program_);
}

// ---------------------------------------------------------------------------
// Uniform location cache
// ---------------------------------------------------------------------------

GLint ShaderProgram::getLocation(const char* name) {
    auto it = locationCache_.find(name);
    if (it != locationCache_.end()) {
        return it->second;
    }
    GLint loc = glGetUniformLocation(program_, name);
    locationCache_[name] = loc;
    return loc;
}

// ---------------------------------------------------------------------------
// Uniform setters
// ---------------------------------------------------------------------------

void ShaderProgram::setInt(const char* name, int value) {
    glUniform1i(getLocation(name), value);
}

void ShaderProgram::setFloat(const char* name, float value) {
    glUniform1f(getLocation(name), value);
}

void ShaderProgram::setVec2(const char* name, float x, float y) {
    glUniform2f(getLocation(name), x, y);
}

void ShaderProgram::setVec3(const char* name, float x, float y, float z) {
    glUniform3f(getLocation(name), x, y, z);
}

void ShaderProgram::setVec3(const char* name, const float* v) {
    glUniform3fv(getLocation(name), 1, v);
}

void ShaderProgram::setMat3(const char* name, const float* m) {
    glUniformMatrix3fv(getLocation(name), 1, GL_FALSE, m);
}

void ShaderProgram::setMat4(const char* name, const float* m) {
    glUniformMatrix4fv(getLocation(name), 1, GL_FALSE, m);
}

} // namespace opendcad
