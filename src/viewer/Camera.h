#pragma once
#include <cmath>
#include <algorithm>

namespace opendcad {

// -----------------------------------------------------------------------
// Minimal linear-algebra types for the camera (column-major for OpenGL)
// -----------------------------------------------------------------------
struct Mat4 { float m[16]; };
struct Vec3f { float x, y, z; };

// 4x4 matrix operations (column-major for OpenGL)
Mat4 mat4Identity();
Mat4 mat4Mul(const Mat4& a, const Mat4& b);
Mat4 mat4Perspective(float fovyDeg, float aspect, float znear, float zfar);
Mat4 mat4LookAt(Vec3f eye, Vec3f target, Vec3f up);

// Vector math
Vec3f vec3Sub(const Vec3f& a, const Vec3f& b);
Vec3f vec3Cross(const Vec3f& a, const Vec3f& b);
float vec3Dot(const Vec3f& a, const Vec3f& b);
Vec3f vec3Normalize(Vec3f v);

// -----------------------------------------------------------------------
// Orbital camera with Z-up convention, camera-space pan, zoom-to-cursor
// -----------------------------------------------------------------------
class Camera {
public:
    Camera();

    /// Rotate the orbit: deltaYaw (radians around Z), deltaPitch (radians from XY).
    void rotate(float deltaYaw, float deltaPitch);

    /// Pan in screen space (pixel deltas mapped to world units).
    void pan(float deltaRight, float deltaUp, int viewportW, int viewportH);

    /// Zoom keeping the world point under the cursor fixed.
    void zoomToCursor(float scrollY, double cursorX, double cursorY,
                      int viewportW, int viewportH);

    /// Adjust distance and reset pan so the bounding box fills the viewport.
    void fitToBounds(float minX, float minY, float minZ,
                     float maxX, float maxY, float maxZ,
                     int viewportW, int viewportH);

    /// Reset to front view (yaw=0, pitch=0) fitted to bounds.
    void reset(int viewportW, int viewportH);

    /// Snap to Z-up isometric preset (yaw=45, pitch=35.264) fitted to bounds.
    void setIsometric(int viewportW, int viewportH);

    // Matrices ----------------------------------------------------------
    Mat4 viewMatrix() const;
    Mat4 projectionMatrix(int viewportW, int viewportH) const;
    Mat4 viewProjectionMatrix(int viewportW, int viewportH) const;

    // Derived vectors ---------------------------------------------------
    Vec3f eyePosition() const;
    Vec3f targetPosition() const;
    Vec3f forward() const;   ///< Unit vector from target toward eye (Z-up spherical)
    Vec3f right() const;     ///< Unit vector pointing screen-right
    Vec3f up() const;        ///< Unit vector pointing screen-up

    // Bounding box ------------------------------------------------------
    void setBounds(float minX, float minY, float minZ,
                   float maxX, float maxY, float maxZ);

    // Accessors ---------------------------------------------------------
    float fovyDeg() const { return fovyDeg_; }
    float distance() const { return dist_; }

private:
    float yaw_     = 0.0f;    // azimuth around Z (radians)
    float pitch_   = 0.0f;    // elevation from XY plane (radians)
    float dist_    = 200.0f;  // distance from target to eye
    float panR_    = 0.0f;    // pan along camera right (world units)
    float panU_    = 0.0f;    // pan along camera up    (world units)
    float fovyDeg_ = 45.0f;  // vertical field of view (degrees)

    float bboxMin_[3] = {0, 0, 0};
    float bboxMax_[3] = {1, 1, 1};

    /// Compute the orbit distance that fits the current bounding box.
    float fitDistance(int viewportW, int viewportH) const;
};

} // namespace opendcad
