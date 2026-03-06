#include "Camera.h"
#include <cstring>   // memset

namespace opendcad {

// -----------------------------------------------------------------------
// Free-function matrix / vector math
// -----------------------------------------------------------------------

Mat4 mat4Identity() {
    Mat4 r{};
    for (int i = 0; i < 16; ++i)
        r.m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    return r;
}

Mat4 mat4Mul(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row)
            r.m[c * 4 + row] =
                a.m[0 * 4 + row] * b.m[c * 4 + 0] +
                a.m[1 * 4 + row] * b.m[c * 4 + 1] +
                a.m[2 * 4 + row] * b.m[c * 4 + 2] +
                a.m[3 * 4 + row] * b.m[c * 4 + 3];
    return r;
}

Mat4 mat4Perspective(float fovyDeg, float aspect, float znear, float zfar) {
    const float f = 1.0f / std::tan(fovyDeg * 0.5f * 3.14159265f / 180.0f);
    Mat4 r{};
    r.m[0]  = f / aspect;
    r.m[5]  = f;
    r.m[10] = (zfar + znear) / (znear - zfar);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * zfar * znear) / (znear - zfar);
    return r;
}

Mat4 mat4LookAt(Vec3f eye, Vec3f target, Vec3f upHint) {
    Vec3f f = vec3Normalize(vec3Sub(target, eye)); // forward (toward target)
    Vec3f s = vec3Normalize(vec3Cross(f, upHint)); // right
    Vec3f u = vec3Cross(s, f);                     // true up

    Mat4 m = mat4Identity();
    m.m[0]  =  s.x;  m.m[4]  =  s.y;  m.m[8]  =  s.z;  m.m[12] = -vec3Dot(s, eye);
    m.m[1]  =  u.x;  m.m[5]  =  u.y;  m.m[9]  =  u.z;  m.m[13] = -vec3Dot(u, eye);
    m.m[2]  = -f.x;  m.m[6]  = -f.y;  m.m[10] = -f.z;  m.m[14] =  vec3Dot(f, eye);
    return m;
}

Vec3f vec3Sub(const Vec3f& a, const Vec3f& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3f vec3Cross(const Vec3f& a, const Vec3f& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

float vec3Dot(const Vec3f& a, const Vec3f& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3f vec3Normalize(Vec3f v) {
    float len = std::sqrt(vec3Dot(v, v));
    if (len > 1e-9f)
        return {v.x / len, v.y / len, v.z / len};
    return {0.0f, 0.0f, 1.0f};
}

// -----------------------------------------------------------------------
// Camera implementation
// -----------------------------------------------------------------------

Camera::Camera() = default;

// -- Orbital rotation ---------------------------------------------------

void Camera::rotate(float deltaYaw, float deltaPitch) {
    yaw_   += deltaYaw;
    pitch_ += deltaPitch;
    pitch_  = std::clamp(pitch_, -1.5f, 1.5f);
}

// -- Screen-space pan ---------------------------------------------------

void Camera::pan(float deltaRight, float deltaUp, int /*viewportW*/, int viewportH) {
    const float thetaY = fovyDeg_ * 0.5f * static_cast<float>(M_PI) / 180.0f;
    const float wppY = 2.0f * dist_ * std::tan(thetaY) / static_cast<float>(std::max(1, viewportH));
    const float wppX = wppY;  // square pixels: same world-units-per-pixel in both axes
    panR_ += deltaRight * wppX;
    panU_ -= deltaUp    * wppY;
}

// -- Zoom-to-cursor (keeps world point under cursor fixed) --------------

void Camera::zoomToCursor(float scrollY, double cursorX, double cursorY,
                          int viewportW, int viewportH) {
    const int vw = viewportW;
    const int vh = viewportH;

    // Screen -> NDC
    const float ndcX = static_cast<float>((cursorX / vw) * 2.0 - 1.0);
    const float ndcY = static_cast<float>(1.0 - (cursorY / vh) * 2.0);

    // Camera intrinsics
    const float thetaY = fovyDeg_ * 0.5f * static_cast<float>(M_PI) / 180.0f;
    const float tanY   = std::tan(thetaY);
    const float aspect = (vw > 0 && vh > 0) ? static_cast<float>(vw) / static_cast<float>(vh) : 1.0f;
    const float tanX   = tanY * aspect;

    // Camera basis (Z-up spherical)
    Vec3f fwd   = forward();
    Vec3f rt    = right();
    Vec3f upDir = up();

    // Bounding-box center
    const float cx0 = (bboxMin_[0] + bboxMax_[0]) * 0.5f;
    const float cy0 = (bboxMin_[1] + bboxMax_[1]) * 0.5f;
    const float cz0 = (bboxMin_[2] + bboxMax_[2]) * 0.5f;
    Vec3f baseC{cx0, cy0, cz0};

    // Current target (with camera-space pan)
    Vec3f panOff{
        rt.x * panR_ + upDir.x * panU_,
        rt.y * panR_ + upDir.y * panU_,
        rt.z * panR_ + upDir.z * panU_
    };
    Vec3f tgt{baseC.x - panOff.x, baseC.y - panOff.y, baseC.z - panOff.z};
    Vec3f eye{
        tgt.x + fwd.x * dist_,
        tgt.y + fwd.y * dist_,
        tgt.z + fwd.z * dist_
    };

    // Camera-space ray through the cursor
    const float rx = ndcX * tanX;
    const float ry = ndcY * tanY;
    const float rz = 1.0f;

    Vec3f dir{
        rt.x * rx + upDir.x * ry + fwd.x * rz,
        rt.y * rx + upDir.y * ry + fwd.y * rz,
        rt.z * rx + upDir.z * ry + fwd.z * rz
    };
    dir = vec3Normalize(dir);

    // Intersect ray with the plane through target (normal = fwd)
    const float denom = vec3Dot(fwd, dir);
    if (std::fabs(denom) < 1e-9f) {
        // Ray nearly parallel -- just dolly without pan adjust
        const float zoomFactor = std::pow(0.9f, scrollY);
        dist_ = std::max(0.001f, dist_ * zoomFactor);
        return;
    }
    const float tPlane = vec3Dot(fwd, vec3Sub(tgt, eye)) / denom;
    Vec3f P{
        eye.x + dir.x * tPlane,
        eye.y + dir.y * tPlane,
        eye.z + dir.z * tPlane
    };

    // Apply zoom
    const float zoomFactor = std::pow(0.9f, scrollY);
    const float newDist = std::max(0.001f, dist_ * zoomFactor);

    // Choose new target so the same world point P stays under the cursor.
    // target' = P + (newDist / rz) * (rx * right + ry * up)
    const float k = (rz != 0.0f) ? (newDist / rz) : 0.0f;
    Vec3f targetPrime{
        P.x + k * (rt.x * rx + upDir.x * ry),
        P.y + k * (rt.y * rx + upDir.y * ry),
        P.z + k * (rt.z * rx + upDir.z * ry)
    };

    // Convert target' back to camera-space pan amounts
    Vec3f delta{
        baseC.x - targetPrime.x,
        baseC.y - targetPrime.y,
        baseC.z - targetPrime.z
    };
    panR_ = vec3Dot(delta, rt);
    panU_ = vec3Dot(delta, upDir);
    dist_ = newDist;
}

// -- Fit / Reset / Isometric -------------------------------------------

float Camera::fitDistance(int viewportW, int viewportH) const {
    const float ex = (bboxMax_[0] - bboxMin_[0]) * 0.5f;
    const float ey = (bboxMax_[1] - bboxMin_[1]) * 0.5f;
    const float ez = (bboxMax_[2] - bboxMin_[2]) * 0.5f;
    const float radius = std::max({ex, ey, ez, 1e-3f});

    const float aspect = std::max(1e-6f, static_cast<float>(viewportW) / static_cast<float>(viewportH));
    const float thetaY = fovyDeg_ * 0.5f * static_cast<float>(M_PI) / 180.0f;
    const float thetaX = std::atan(std::tan(thetaY) * aspect);

    const float dY = radius / std::tan(thetaY);
    const float dX = radius / std::tan(thetaX);
    const float margin = 1.15f;
    return std::max(dX, dY) * margin;
}

void Camera::fitToBounds(float minX, float minY, float minZ,
                         float maxX, float maxY, float maxZ,
                         int viewportW, int viewportH) {
    setBounds(minX, minY, minZ, maxX, maxY, maxZ);
    panR_ = 0.0f;
    panU_ = 0.0f;
    dist_ = fitDistance(viewportW, viewportH);
}

void Camera::reset(int viewportW, int viewportH) {
    yaw_   = 0.0f;
    pitch_ = 0.0f;
    panR_  = 0.0f;
    panU_  = 0.0f;
    dist_  = fitDistance(viewportW, viewportH);
}

void Camera::setIsometric(int viewportW, int viewportH) {
    yaw_   = 45.0f    * static_cast<float>(M_PI) / 180.0f;
    pitch_ = 35.264f  * static_cast<float>(M_PI) / 180.0f;
    panR_  = 0.0f;
    panU_  = 0.0f;
    dist_  = fitDistance(viewportW, viewportH);
}

// -- Matrices -----------------------------------------------------------

Mat4 Camera::viewMatrix() const {
    Vec3f eye = eyePosition();
    Vec3f tgt = targetPosition();
    return mat4LookAt(eye, tgt, Vec3f{0.0f, 0.0f, 1.0f});
}

Mat4 Camera::projectionMatrix(int viewportW, int viewportH) const {
    const float aspect = static_cast<float>(viewportW) / static_cast<float>(std::max(1, viewportH));
    return mat4Perspective(fovyDeg_, aspect, 0.01f, 10000.0f);
}

Mat4 Camera::viewProjectionMatrix(int viewportW, int viewportH) const {
    return mat4Mul(projectionMatrix(viewportW, viewportH), viewMatrix());
}

// -- Derived vectors ----------------------------------------------------

Vec3f Camera::forward() const {
    const float ca = std::cos(yaw_),  sa = std::sin(yaw_);
    const float ce = std::cos(pitch_), se = std::sin(pitch_);
    Vec3f fwd{ce * ca, ce * sa, se};
    return vec3Normalize(fwd);
}

Vec3f Camera::right() const {
    Vec3f fwd = forward();
    Vec3f rt = vec3Cross(fwd, Vec3f{0.0f, 0.0f, 1.0f});
    float len = std::sqrt(vec3Dot(rt, rt));
    if (len < 1e-9f)
        return {1.0f, 0.0f, 0.0f}; // degenerate when looking straight up/down
    return {rt.x / len, rt.y / len, rt.z / len};
}

Vec3f Camera::up() const {
    return vec3Cross(right(), forward());
}

Vec3f Camera::eyePosition() const {
    Vec3f tgt = targetPosition();
    Vec3f fwd = forward();
    return {
        tgt.x + fwd.x * dist_,
        tgt.y + fwd.y * dist_,
        tgt.z + fwd.z * dist_
    };
}

Vec3f Camera::targetPosition() const {
    const float cx0 = (bboxMin_[0] + bboxMax_[0]) * 0.5f;
    const float cy0 = (bboxMin_[1] + bboxMax_[1]) * 0.5f;
    const float cz0 = (bboxMin_[2] + bboxMax_[2]) * 0.5f;

    Vec3f rt    = right();
    Vec3f upDir = up();
    Vec3f panOff{
        rt.x * panR_ + upDir.x * panU_,
        rt.y * panR_ + upDir.y * panU_,
        rt.z * panR_ + upDir.z * panU_
    };
    return {cx0 - panOff.x, cy0 - panOff.y, cz0 - panOff.z};
}

// -- Bounding box -------------------------------------------------------

void Camera::setBounds(float minX, float minY, float minZ,
                       float maxX, float maxY, float maxZ) {
    bboxMin_[0] = minX; bboxMin_[1] = minY; bboxMin_[2] = minZ;
    bboxMax_[0] = maxX; bboxMax_[1] = maxY; bboxMax_[2] = maxZ;
}

} // namespace opendcad
