#pragma once

#include <glad/glad.h>
#include <string>

namespace opendcad {

class ShaderProgram;

class EnvironmentMap {
public:
    EnvironmentMap() = default;
    ~EnvironmentMap();

    // Non-copyable
    EnvironmentMap(const EnvironmentMap&) = delete;
    EnvironmentMap& operator=(const EnvironmentMap&) = delete;

    // Generate a procedural studio HDR environment and precompute IBL textures.
    bool init();

    // IBL textures for the lighting shader
    GLuint irradianceMap() const { return irradianceMap_; }
    GLuint prefilteredMap() const { return prefilteredMap_; }
    GLuint brdfLUT() const { return brdfLUT_; }

    void destroy();

private:
    // Generate procedural equirectangular HDR studio environment
    GLuint generateProceduralHDR(int width, int height);

    // Convert equirectangular to cubemap
    GLuint equirectToCubemap(GLuint equirectTex, int faceSize);

    // Precompute irradiance cubemap from environment cubemap
    GLuint computeIrradiance(GLuint envCubemap, int size);

    // Precompute prefiltered specular cubemap with mip chain
    GLuint computePrefiltered(GLuint envCubemap, int size);

    // Precompute BRDF integration LUT
    GLuint computeBRDFLUT(int size);

    // Helper: compile a temporary shader program (returns 0 on failure)
    GLuint buildShader(const char* vertSrc, const char* fragSrc);

    // Helper: render unit cube into each face of a cubemap
    void renderCubeFaces(GLuint shader, GLuint cubemapTex, int faceSize,
                         int mipLevel = 0);

    // Unit cube mesh for cubemap rendering
    GLuint cubeVAO_ = 0;
    GLuint cubeVBO_ = 0;
    void buildCubeMesh();
    void destroyCubeMesh();

    GLuint envCubemap_ = 0;
    GLuint irradianceMap_ = 0;
    GLuint prefilteredMap_ = 0;
    GLuint brdfLUT_ = 0;

    GLuint captureFBO_ = 0;
    GLuint captureRBO_ = 0;
};

} // namespace opendcad
