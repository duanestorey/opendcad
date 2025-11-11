// viewer/viewer_main.cpp
#ifdef __APPLE__
#define GL_SILENCE_DEPRECATION
#endif

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <sys/stat.h>

#include <GLFW/glfw3.h>

#define STB_EASY_FONT_IMPLEMENTATION
#include "stb_easy_font.h"

// ───────────────────────────────────────────────────────────────────
// Logging + GLFW error
// ───────────────────────────────────────────────────────────────────
static void log_line(const char* msg) { std::cout << msg << std::endl; }
static void glfw_error_callback(int code, const char* desc) {
    std::fprintf(stderr, "GLFW error %d: %s\n", code, desc);
}

// Bigger, flicker-free text using immediate mode (no client arrays/VBO)
static void draw_text_screen(float x, float y, const char* txt, float scale = 2.0f)
{
    // stb_easy_font writes quads into this buffer: 4 floats per vertex, 4 verts per quad
    static char vbuf[64 * 1024];
    int quads = stb_easy_font_print(x / scale, y / scale, (char*)txt, nullptr,
                                    vbuf, sizeof(vbuf));
    if (quads <= 0) return;

    // Save + set clean 2D state
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glPushMatrix();
    glScalef(scale, scale, 1.0f);

    // Draw as immediate quads to avoid client-array/VBO state conflicts
    float* v = reinterpret_cast<float*>(vbuf);
    glColor3f(0.95f, 0.97f, 1.0f);
    glBegin(GL_QUADS);
    for (int i = 0; i < quads * 4; ++i) {
        // v layout per vertex: [x, y, ?unused, ?unused] with 16-byte stride
        float vx = v[i*4 + 0];
        float vy = v[i*4 + 1];
        glVertex2f(vx, vy);
    }
    glEnd();

    glPopMatrix();
    glPopAttrib();
}

// Crude width estimate (stb_easy_font uses ~7 px per char at scale=1)
static int text_width_px(const char* s, float scale = 2.0f) {
    return int(std::strlen(s) * 7 * scale);
}

// ───────────────────────────────────────────────────────────────────
// STL structs + robust loader (binary + ASCII)
// ───────────────────────────────────────────────────────────────────
struct Vec3 { float x,y,z; };
struct Tri  { Vec3 n, v0, v1, v2; };

static bool load_stl(const char* path, std::vector<Tri>& out) {
    struct stat st{};
    if (stat(path, &st) != 0) { std::perror(("stat " + std::string(path)).c_str()); return false; }
    const size_t fsize = static_cast<size_t>(st.st_size);

    // Try binary first
    {
        FILE* f = std::fopen(path, "rb");
        if (!f) { std::perror(path); return false; }
        unsigned char header[80];
        if (std::fread(header, 1, 80, f) == 80) {
            uint32_t ntri_hdr = 0;
            if (std::fread(&ntri_hdr, 4, 1, f) == 1) {
                const bool size_ok   = (fsize >= 84) && ((fsize - 84) % 50 == 0);
                const uint32_t ncalc = size_ok ? uint32_t((fsize - 84) / 50) : 0;
                const uint32_t ntri  = ncalc ? ncalc : ntri_hdr;
                if (ntri > 0 && fsize >= 84 + size_t(ntri) * 50) {
                    out.resize(ntri);
                    bool ok = true;
                    for (uint32_t i=0; i<ntri; ++i) {
                        float data[12];
                        if (std::fread(data, sizeof(float), 12, f) != 12) { ok = false; break; }
                        uint16_t attr = 0;
                        if (std::fread(&attr, 2, 1, f) != 1) { ok = false; break; }
                        Tri t{};
                        t.n  = {data[0], data[1], data[2]};
                        t.v0 = {data[3], data[4], data[5]};
                        t.v1 = {data[6], data[7], data[8]};
                        t.v2 = {data[9], data[10], data[11]};
                        out[i] = t;
                    }
                    std::fclose(f);
                    if (ok) return true;
                    out.clear();
                }
            }
        }
        std::fclose(f);
    }

    // ASCII fallback
    {
        FILE* f = std::fopen(path, "r");
        if (!f) { std::perror(path); return false; }

        out.clear();
        char line[512];
        Tri cur{}; int vcount = 0; bool inFacet = false;
        auto push_tri = [&](){ if (vcount == 3) out.push_back(cur); vcount = 0; inFacet = false; };

        while (std::fgets(line, sizeof(line), f)) {
            char* p = line; while (*p==' ' || *p=='\t') ++p;
            if (std::strncmp(p, "facet normal", 12) == 0) {
                inFacet = true; vcount = 0;
                std::sscanf(p, "facet normal %f %f %f", &cur.n.x, &cur.n.y, &cur.n.z);
            } else if (inFacet && std::strncmp(p, "vertex", 6) == 0) {
                float x,y,z;
                if (std::sscanf(p, "vertex %f %f %f", &x, &y, &z) == 3) {
                    if (vcount == 0) cur.v0 = {x,y,z};
                    else if (vcount == 1) cur.v1 = {x,y,z};
                    else if (vcount == 2) cur.v2 = {x,y,z};
                    vcount++;
                }
            } else if (inFacet && std::strncmp(p, "endfacet", 8) == 0) {
                push_tri();
            }
        }
        std::fclose(f);
        if (!out.empty()) return true;
    }

    std::fprintf(stderr, "STL parse failed: neither binary nor ASCII recognized (size=%zu)\n", fsize);
    return false;
}

// ───────────────────────────────────────────────────────────────────
// Camera (camera-space pan), bounds, fit distance
// ───────────────────────────────────────────────────────────────────
struct Camera {
    float yaw = 0.0f, pitch = 0.0f, dist = 200.0f;
    double lastX=0, lastY=0;
    bool rotating=false, panning=false;
    float panR=0.0f;   // pan along camera right vector (world units)
    float panU=0.0f;   // pan along camera up    vector (world units)
    float fovy_deg = 45.0f;
};

static void compute_bounds(const std::vector<Tri>& tris, Vec3& minB, Vec3& maxB) {
    if (tris.empty()) { minB = {0,0,0}; maxB = {1,1,1}; return; }
    minB = maxB = tris[0].v0;
    auto upd = [&](const Vec3& v){
        minB.x = std::min(minB.x, v.x); minB.y = std::min(minB.y, v.y); minB.z = std::min(minB.z, v.z);
        maxB.x = std::max(maxB.x, v.x); maxB.y = std::max(maxB.y, v.y); maxB.z = std::max(maxB.z, v.z);
    };
    for (auto& t : tris) { upd(t.v0); upd(t.v1); upd(t.v2); }
}

static float fit_distance_from_bbox(const Vec3& minB, const Vec3& maxB,
                                    int viewportW, int viewportH,
                                    float fovy_deg, float margin = 1.15f) {
    const float ex = (maxB.x - minB.x) * 0.5f;
    const float ey = (maxB.y - minB.y) * 0.5f;
    const float ez = (maxB.z - minB.z) * 0.5f;
    const float radius = std::max({ex, ey, ez, 1e-3f});

    const float aspect = std::max(1e-6f, float(viewportW) / float(viewportH));
    const float thetaY = fovy_deg * 0.5f * float(M_PI) / 180.0f;
    const float thetaX = std::atan(std::tan(thetaY) * aspect);

    const float dY = radius / std::tan(thetaY);
    const float dX = radius / std::tan(thetaX);
    return std::max(dX, dY) * margin;
}

// Global bbox for callbacks
static Vec3 g_minB, g_maxB;

// ───────────────────────────────────────────────────────────────────
// Tiny 4x4 math + lookAt (column-major, Z-up)
// ───────────────────────────────────────────────────────────────────
struct Mat4 { float m[16]; };
static Mat4 mat_identity() { Mat4 r{}; for (int i=0;i<16;++i) r.m[i]=(i%5==0)?1.f:0.f; return r; }
static Mat4 mat_mul(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int c=0;c<4;++c)
        for (int r0=0;r0<4;++r0)
            r.m[c*4+r0] = a.m[0*4+r0]*b.m[c*4+0] + a.m[1*4+r0]*b.m[c*4+1] +
                          a.m[2*4+r0]*b.m[c*4+2] + a.m[3*4+r0]*b.m[c*4+3];
    return r;
}
static Mat4 mat_perspective(float fovy_deg, float aspect, float znear, float zfar) {
    float f = 1.0f / std::tan(fovy_deg * 0.5f * 3.14159265f/180.0f);
    Mat4 r{};
    r.m[0]=f/aspect; r.m[5]=f; r.m[10]=(zfar+znear)/(znear-zfar); r.m[11]=-1.f; r.m[14]=(2*zfar*znear)/(znear-zfar);
    return r;
}

struct Vec3f { float x,y,z; };
static Vec3f vsub(const Vec3f& a, const Vec3f& b){ return {a.x-b.x, a.y-b.y, a.z-b.z}; }
static Vec3f vcross(const Vec3f& a, const Vec3f& b){ return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x}; }
static float vdot(const Vec3f& a, const Vec3f& b){ return a.x*b.x + a.y*b.y + a.z*b.z; }
static Vec3f vnorm(Vec3f a){ float L=std::sqrt(vdot(a,a)); return (L>1e-9f)? Vec3f{a.x/L,a.y/L,a.z/L}:Vec3f{0,0,1}; }

static Mat4 mat_lookAt(Vec3f eye, Vec3f target, Vec3f up)
{
    Vec3f f = vnorm(vsub(target, eye));  // forward
    Vec3f s = vnorm(vcross(f, up));      // right
    Vec3f u = vcross(s, f);              // true up

    Mat4 m = mat_identity();
    m.m[0] =  s.x; m.m[4] =  s.y; m.m[8]  =  s.z; m.m[12] = -vdot(s, eye);
    m.m[1] =  u.x; m.m[5] =  u.y; m.m[9]  =  u.z; m.m[13] = -vdot(u, eye);
    m.m[2] = -f.x; m.m[6] = -f.y; m.m[10] = -f.z; m.m[14] =  vdot(f, eye);
    return m;
}

// ───────────────────────────────────────────────────────────────────
// Shaders (GLSL 120): Blinn-Phong + rim light, subtle normal tint
// ───────────────────────────────────────────────────────────────────
static const char* VS_SRC = R"GLSL(
#version 120
attribute vec3 aPos;
attribute vec3 aNormal;

uniform mat4 uMVP;
uniform mat4 uModel;
uniform vec3 uLightDir;

varying vec3 vN;
varying vec3 vColorHint;
varying vec3 vViewDir;

void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    vec3 N = normalize((uModel * vec4(aNormal,0.0)).xyz);
    vN = N;
    vViewDir = normalize(vec3(0.0, 0.0, 1.0));
    vColorHint = 0.5 + 0.5 * N;
}
)GLSL";

static const char* FS_SRC = R"GLSL(
#version 120
varying vec3 vN;
varying vec3 vColorHint;
varying vec3 vViewDir;

uniform vec3 uLightDir;

const vec3 baseColor = vec3(0.72, 0.78, 0.86);

void main() {
    vec3 N = normalize(vN);
    vec3 L = normalize(uLightDir);
    vec3 V = normalize(vViewDir);

    float NdotL = max(dot(N, L), 0.0);
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 64.0);

    float rim = pow(1.0 - max(dot(N, V), 0.0), 3.5);

    vec3 diffuse  = baseColor * NdotL;
    vec3 ambient  = baseColor * 0.25;
    vec3 specular = vec3(0.9) * spec * 0.5;
    vec3 tint     = mix(vec3(0.0), vColorHint * 0.25, 0.6);

    vec3 col = ambient + diffuse + specular + rim*0.15 + tint;
    col = pow(col, vec3(1.0/2.2));
    gl_FragColor = vec4(col, 1.0);
}
)GLSL";

static GLuint compile_shader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok=0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len=0; glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        std::string log(len, '\0'); glGetShaderInfoLog(s, len, nullptr, log.data());
        std::fprintf(stderr, "Shader compile error:\n%s\n", log.c_str());
        glDeleteShader(s); return 0;
    }
    return s;
}
static GLuint link_program(GLuint vs, GLuint fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glBindAttribLocation(p, 0, "aPos");
    glBindAttribLocation(p, 1, "aNormal");
    glLinkProgram(p);
    GLint ok=0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len=0; glGetProgramiv(p, GL_INFO_LOG_LENGTH, &len);
        std::string log(len, '\0'); glGetProgramInfoLog(p, len, nullptr, log.data());
        std::fprintf(stderr, "Program link error:\n%s\n", log.c_str());
        glDeleteProgram(p); return 0;
    }
    return p;
}

// ───────────────────────────────────────────────────────────────────
// Interleaved VBO [pos(3), normal(3)] + flat-normal fallback
// ───────────────────────────────────────────────────────────────────
static void compute_flat_normal(const Tri& t, float n[3]) {
    float ux = t.v1.x - t.v0.x, uy = t.v1.y - t.v0.y, uz = t.v1.z - t.v0.z;
    float vx = t.v2.x - t.v0.x, vy = t.v2.y - t.v0.y, vz = t.v2.z - t.v0.z;
    float nx = uy*vz - uz*vy;
    float ny = uz*vx - ux*vz;
    float nz = ux*vy - uy*vx;
    float len = std::sqrt(nx*nx + ny*ny + nz*nz);
    if (len > 1e-12f) { n[0]=nx/len; n[1]=ny/len; n[2]=nz/len; }
    else { n[0]=0; n[1]=0; n[2]=1; }
}
static void build_interleaved_vbo(const std::vector<Tri>& tris, std::vector<float>& interleaved) {
    interleaved.clear();
    interleaved.reserve(tris.size() * 3 * 6);
    for (const auto& t : tris) {
        float n[3] = {t.n.x, t.n.y, t.n.z};
        if (std::abs(n[0])+std::abs(n[1])+std::abs(n[2]) < 1e-6f) compute_flat_normal(t, n);
        const Vec3 v[3] = {t.v0, t.v1, t.v2};
        for (int i=0;i<3;++i) {
            interleaved.push_back(v[i].x);
            interleaved.push_back(v[i].y);
            interleaved.push_back(v[i].z);
            interleaved.push_back(n[0]);
            interleaved.push_back(n[1]);
            interleaved.push_back(n[2]);
        }
    }
}

// ───────────────────────────────────────────────────────────────────
// Helpers: draw grid + axes (immediate mode)
// ───────────────────────────────────────────────────────────────────
static void draw_grid(float size=200.0f, float step=10.0f) {
    glDisable(GL_DEPTH_TEST);
    glBegin(GL_LINES);
    for (float x=-size; x<=size; x+=step) {
        float a = (std::fmod(std::fabs(x), 50.0f) < 1e-4f) ? 0.22f : 0.10f;
        glColor3f(a, a, a);
        glVertex3f(x, -size, 0.0f); glVertex3f(x, size, 0.0f);
    }
    for (float y=-size; y<=size; y+=step) {
        float a = (std::fmod(std::fabs(y), 50.0f) < 1e-4f) ? 0.22f : 0.10f;
        glColor3f(a, a, a);
        glVertex3f(-size, y, 0.0f); glVertex3f(size, y, 0.0f);
    }
    glEnd();
    glEnable(GL_DEPTH_TEST);
}
static void draw_axes(float len=25.0f) {
    glDisable(GL_DEPTH_TEST);
    glBegin(GL_LINES);
    glColor3f(1,0,0); glVertex3f(0,0,0); glVertex3f(len,0,0); // X
    glColor3f(0,1,0); glVertex3f(0,0,0); glVertex3f(0,len,0); // Y
    glColor3f(0,0,1); glVertex3f(0,0,0); glVertex3f(0,0,len); // Z
    glEnd();
    glEnable(GL_DEPTH_TEST);
}

// ───────────────────────────────────────────────────────────────────
// Main
// ───────────────────────────────────────────────────────────────────
int main(int argc, char** argv) {
    std::ios::sync_with_stdio(false);
    std::cout.setf(std::ios::unitbuf);

    const char* path = (argc >= 2 ? argv[1] : "build/bin/opendcad_test.stl");
    log_line("viewer: starting");

    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) { std::fprintf(stderr, "Failed to init GLFW\n"); return 1; }
    log_line("viewer: glfwInit OK");

    // GL 2.1 compat (macOS)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_ANY_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_FALSE);
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    GLFWwindow* win = glfwCreateWindow(1024, 768, "OpenDCAD Viewer (iso + zoom-to-cursor)", nullptr, nullptr);
    if (!win) { std::fprintf(stderr, "Failed to create window\n"); glfwTerminate(); return 1; }
    log_line("viewer: window created");

    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);
    log_line("viewer: context current");
    std::cout << "GL version: " << (const char*)glGetString(GL_VERSION) << std::endl;

    // Compile shaders
    auto compile_shader_l = [](GLenum type, const char* src)->GLuint{
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok=0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            GLint len=0; glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
            std::string log(len, '\0'); glGetShaderInfoLog(s, len, nullptr, log.data());
            std::fprintf(stderr, "Shader compile error:\n%s\n", log.c_str());
            glDeleteShader(s); return 0;
        }
        return s;
    };
    GLuint vs = compile_shader_l(GL_VERTEX_SHADER, VS_SRC);
    GLuint fs = compile_shader_l(GL_FRAGMENT_SHADER, FS_SRC);
    if (!vs || !fs) return 1;

    auto link_program_l = [](GLuint vs, GLuint fs)->GLuint{
        GLuint p = glCreateProgram();
        glAttachShader(p, vs);
        glAttachShader(p, fs);
        glBindAttribLocation(p, 0, "aPos");
        glBindAttribLocation(p, 1, "aNormal");
        glLinkProgram(p);
        GLint ok=0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
        if (!ok) {
            GLint len=0; glGetProgramiv(p, GL_INFO_LOG_LENGTH, &len);
            std::string log(len, '\0'); glGetProgramInfoLog(p, len, nullptr, log.data());
            std::fprintf(stderr, "Program link error:\n%s\n", log.c_str());
            glDeleteProgram(p); return 0;
        }
        return p;
    };
    GLuint prog = link_program_l(vs, fs);
    glDeleteShader(vs); glDeleteShader(fs);
    if (!prog) return 1;

    GLint loc_uMVP   = glGetUniformLocation(prog, "uMVP");
    GLint loc_uModel = glGetUniformLocation(prog, "uModel");
    GLint loc_uLight = glGetUniformLocation(prog, "uLightDir");

    // Load STL
    std::vector<Tri> tris;
    if (!load_stl(path, tris)) { std::fprintf(stderr, "Failed to load STL: %s\n", path); glfwDestroyWindow(win); glfwTerminate(); return 1; }
    std::cout << "Loaded " << tris.size() << " triangles from " << path << std::endl;

    // VBO
    std::vector<float> interleaved;
    build_interleaved_vbo(tris, interleaved);
    GLuint vbo = 0; glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, interleaved.size()*sizeof(float), interleaved.data(), GL_STATIC_DRAW);

    // Bounds + camera
    Vec3 minB, maxB; compute_bounds(tris, minB, maxB);
    g_minB = minB; g_maxB = maxB;

    int fbw=1024, fbh=768; glfwGetFramebufferSize(win, &fbw, &fbh);
    Camera cam;
    cam.dist = fit_distance_from_bbox(minB, maxB, fbw, fbh, cam.fovy_deg);

    auto deg2rad = [](float d){ return d * float(M_PI) / 180.0f; };
    cam.yaw   = deg2rad(45.0f);     // azimuth around Z
    cam.pitch = deg2rad(35.264f);   // elevation from XY
    cam.panR  = 0.0f;
    cam.panU  = 0.0f;

    // IMPORTANT: set user pointer BEFORE callbacks
    glfwSetWindowUserPointer(win, &cam);

    // Input: rotate LMB; pan RMB/MMB or Shift+LMB; zoom wheel (zoom-to-cursor)
    glfwSetMouseButtonCallback(win, [](GLFWwindow* w, int button, int action, int mods){
        auto* c = (Camera*)glfwGetWindowUserPointer(w);
        const bool shift = (mods & GLFW_MOD_SHIFT) != 0;
        if (button == GLFW_MOUSE_BUTTON_LEFT) {
            if (shift) { c->panning = (action==GLFW_PRESS); c->rotating = false; }
            else       { c->rotating = (action==GLFW_PRESS); if (action==GLFW_PRESS) c->panning=false; }
        } else if (button == GLFW_MOUSE_BUTTON_MIDDLE || button == GLFW_MOUSE_BUTTON_RIGHT) {
            c->panning = (action==GLFW_PRESS); if (action==GLFW_PRESS) c->rotating=false;
        }
        double x,y; glfwGetCursorPos(w, &x, &y); c->lastX=x; c->lastY=y;
    });

    glfwSetCursorPosCallback(win, [](GLFWwindow* w, double x, double y){
        auto* c = (Camera*)glfwGetWindowUserPointer(w);
        int vw, vh; glfwGetFramebufferSize(w, &vw, &vh);

        double dx = x - c->lastX, dy = y - c->lastY;
        c->lastX = x; c->lastY = y;

        if (c->rotating) {
            c->yaw   += float(dx) * 0.005f;
            c->pitch += float(dy) * 0.005f;
            c->pitch = std::clamp(c->pitch, -1.5f, 1.5f);
        }
        if (c->panning) {
            const float thetaY = c->fovy_deg * 0.5f * float(M_PI) / 180.0f;
            const float wppY = 2.0f * c->dist * std::tan(thetaY) / std::max(1, vh);
            const float wppX = wppY * (float)vw / std::max(1, vh);
            c->panR += float(dx) * wppX;   // right
            c->panU -= float(dy) * wppY;   // up
        }
    });
// Precise zoom-to-cursor: keeps the same world point under the cursor
glfwSetScrollCallback(win, [](GLFWwindow* w, double /*xoff*/, double yoff){
    auto* c = (Camera*)glfwGetWindowUserPointer(w);
    int vw, vh; glfwGetFramebufferSize(w, &vw, &vh);
    double mx, my; glfwGetCursorPos(w, &mx, &my);

    // Screen → NDC
    const float ndcX = float((mx / vw) * 2.0 - 1.0);
    const float ndcY = float(1.0 - (my / vh) * 2.0);

    // Camera intrinsics
    const float thetaY = c->fovy_deg * 0.5f * float(M_PI) / 180.0f;
    const float tanY   = std::tan(thetaY);
    const float aspect = (vw > 0 && vh > 0) ? float(vw) / float(vh) : 1.0f;
    const float tanX   = tanY * aspect;

    // Build camera basis from yaw/pitch (Z-up)
    const float ca = std::cos(c->yaw), sa = std::sin(c->yaw);
    const float ce = std::cos(c->pitch), se = std::sin(c->pitch);
    // forward points from target → eye
    Vec3f fwd = { ce*ca,  ce*sa,  se };
    float fl = std::sqrt(fwd.x*fwd.x + fwd.y*fwd.y + fwd.z*fwd.z); if (fl > 0) { fwd = { fwd.x/fl, fwd.y/fl, fwd.z/fl }; }
    Vec3f upWorld = {0,0,1};
    Vec3f right   = vcross(fwd, upWorld);
    float rl = std::sqrt(vdot(right,right)); if (rl < 1e-9f) right = {1,0,0}; else right = {right.x/rl, right.y/rl, right.z/rl};
    Vec3f up      = vcross(right, fwd);

    // Base center and current target (with camera-space pan)
    const float cx0 = (g_minB.x + g_maxB.x) * 0.5f;
    const float cy0 = (g_minB.y + g_maxB.y) * 0.5f;
    const float cz0 = (g_minB.z + g_maxB.z) * 0.5f;
    Vec3f baseC { cx0, cy0, cz0 };
    Vec3f panOff { right.x*c->panR + up.x*c->panU,
                   right.y*c->panR + up.y*c->panU,
                   right.z*c->panR + up.z*c->panU };
    Vec3f target { baseC.x - panOff.x, baseC.y - panOff.y, baseC.z - panOff.z };
    Vec3f eye    { target.x + fwd.x * c->dist,
                   target.y + fwd.y * c->dist,
                   target.z + fwd.z * c->dist };

    // Camera-space ray through the cursor (dirCam), then to world
    // In camera space, the view plane at z=1 has coords (x = ndcX*tanX, y = ndcY*tanY, z = 1)
    const float rx = ndcX * tanX;
    const float ry = ndcY * tanY;
    const float rz = 1.0f;
    // World ray dir = normalize(right*rx + up*ry + fwd*rz)
    Vec3f dir {
        right.x*rx + up.x*ry + fwd.x*rz,
        right.y*rx + up.y*ry + fwd.y*rz,
        right.z*rx + up.z*ry + fwd.z*rz
    };
    float dl = std::sqrt(vdot(dir,dir)); if (dl > 0) { dir = { dir.x/dl, dir.y/dl, dir.z/dl }; }

    // Intersect this ray with the plane through 'target' with normal 'fwd' → gives the world point under cursor
    const float denom = vdot(fwd, dir);
    if (std::fabs(denom) < 1e-9f) {
        // Ray nearly parallel to plane: just dolly without pan adjust
        const float zoomFactor = std::pow(0.9f, float(yoff));
        c->dist = std::max(0.001f, c->dist * zoomFactor);
        return;
    }
    const float t_plane = vdot(fwd, vsub(target, eye)) / denom;
    Vec3f P = { eye.x + dir.x * t_plane,
                eye.y + dir.y * t_plane,
                eye.z + dir.z * t_plane }; // ← world point under cursor (before zoom)

    // Apply zoom
    const float zoomFactor = std::pow(0.9f, float(yoff));
    const float newDist = std::max(0.001f, c->dist * zoomFactor);

    // Choose new target' so that the same world point P stays under the cursor after zoom.
    // Derivation in camera basis gives:
    //   target' = P + (newDist / rz) * (rx * right + ry * up)
    // (rz==1 in our construction; keep the division for clarity/robustness)
    const float k = (rz != 0.0f) ? (newDist / rz) : 0.0f;
    Vec3f targetPrime {
        P.x + k * (right.x*rx + up.x*ry),
        P.y + k * (right.y*rx + up.y*ry),
        P.z + k * (right.z*rx + up.z*ry)
    };

    // Convert target' back to camera-space pan amounts (panR', panU')
    Vec3f delta { baseC.x - targetPrime.x,
                  baseC.y - targetPrime.y,
                  baseC.z - targetPrime.z };
    c->panR = vdot(delta, right);
    c->panU = vdot(delta, up);

    c->dist = newDist;
});

    // Keyboard: F fit, Space reset, I isometric
glfwSetKeyCallback(win, [](GLFWwindow* w, int key, int /*scancode*/, int action, int /*mods*/){
    if (action != GLFW_PRESS) return;
    auto* c = (Camera*)glfwGetWindowUserPointer(w);
    int vw, vh; glfwGetFramebufferSize(w, &vw, &vh);

    if (key == GLFW_KEY_F) {
        c->dist = fit_distance_from_bbox(g_minB, g_maxB, vw, vh, c->fovy_deg);
    } else if (key == GLFW_KEY_SPACE) {
        c->yaw = 0.0f; c->pitch = 0.0f; c->panR = 0.0f; c->panU = 0.0f;
        c->dist = fit_distance_from_bbox(g_minB, g_maxB, vw, vh, c->fovy_deg);
    } else if (key == GLFW_KEY_I) {
        // snap to Z-up isometric
        const float yaw   = 45.0f     * float(M_PI) / 180.0f;
        const float pitch = 35.264f   * float(M_PI) / 180.0f;
        c->yaw = yaw; c->pitch = pitch; c->panR = 0.0f; c->panU = 0.0f;
        c->dist = fit_distance_from_bbox(g_minB, g_maxB, vw, vh, c->fovy_deg);
    }
});

    glEnable(GL_DEPTH_TEST);
    // glEnable(GL_CULL_FACE); // enable if STL winding is consistent

    // Render loop
    log_line("viewer: entering render loop");
    while (!glfwWindowShouldClose(win)) {
        int w,h; glfwGetFramebufferSize(win, &w, &h);
        glViewport(0,0,w,h);
        glClearColor(0.08f,0.09f,0.10f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Base center (bbox center)
        const float cx0 = (g_minB.x + g_maxB.x) * 0.5f;
        const float cy0 = (g_minB.y + g_maxB.y) * 0.5f;
        const float cz0 = (g_minB.z + g_maxB.z) * 0.5f;

        // Z-up spherical orbit: azimuth around Z, elevation from XY
        const float ca = std::cos(cam.yaw), sa = std::sin(cam.yaw);
        const float ce = std::cos(cam.pitch), se = std::sin(cam.pitch);

        // Forward (from target to eye), Right, Up
        Vec3f fwd = { ce*ca,  ce*sa,  se };
        float fl = std::sqrt(fwd.x*fwd.x + fwd.y*fwd.y + fwd.z*fwd.z);
        fwd = { fwd.x/fl, fwd.y/fl, fwd.z/fl };
        Vec3f upWorld = {0,0,1};
        Vec3f right   = vcross(fwd, upWorld);
        float rl = std::sqrt(vdot(right,right)); if (rl < 1e-9f) right = {1,0,0}; else right = {right.x/rl, right.y/rl, right.z/rl};
        Vec3f up      = vcross(right, fwd);

        // Apply camera-space pan to target
        Vec3f panOffset = { right.x*cam.panR + up.x*cam.panU,
                            right.y*cam.panR + up.y*cam.panU,
                            right.z*cam.panR + up.z*cam.panU };

        Vec3f target = { cx0 - panOffset.x, cy0 - panOffset.y, cz0 - panOffset.z };
        Vec3f eye    = { target.x + fwd.x * cam.dist,
                         target.y + fwd.y * cam.dist,
                         target.z + fwd.z * cam.dist };

        Mat4 V = mat_lookAt(eye, target, /*up*/ {0,0,1});
        Mat4 P = mat_perspective(cam.fovy_deg, (float)w/(float)h, 0.01f, 10000.0f);
        Mat4 M = mat_identity();
        Mat4 MVP = mat_mul(P, mat_mul(V, M));

        // Grid & axes (fixed-pipeline convenience)
        glMatrixMode(GL_PROJECTION); glLoadIdentity(); glMultMatrixf(P.m);
        glMatrixMode(GL_MODELVIEW);  glLoadIdentity(); glMultMatrixf(V.m);
        draw_grid(500.0f, 10.0f);
        draw_axes(30.0f);

        // Shaded mesh
        glUseProgram(prog);
        glUniformMatrix4fv(loc_uMVP,   1, GL_FALSE, MVP.m);
        glUniformMatrix4fv(loc_uModel, 1, GL_FALSE, M.m);

        float lightDir[3] = { 0.45f, 0.75f, 0.48f };
        float len = std::sqrt(lightDir[0]*lightDir[0] + lightDir[1]*lightDir[1] + lightDir[2]*lightDir[2]);
        if (len>0) { lightDir[0]/=len; lightDir[1]/=len; lightDir[2]/=len; }
        glUniform3fv(loc_uLight, 1, lightDir);

        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glEnableVertexAttribArray(0); // aPos
        glEnableVertexAttribArray(1); // aNormal
        const GLsizei stride = sizeof(float)*6;
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, (void*)(sizeof(float)*3));
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(interleaved.size()/6));
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(1);
        glUseProgram(0);

        // ------- Screen-space overlay (bottom-right) -------
{
    const float W = (float)w, H = (float)h;
    const float sx = (g_maxB.x - g_minB.x);
    const float sy = (g_maxB.y - g_minB.y);
    const float sz = (g_maxB.z - g_minB.z);
    const size_t triCount = interleaved.size() / 18;

    char l1[128], l2[128], l3[128];
    std::snprintf(l1, sizeof(l1), "Triangles: %zu", triCount);
    std::snprintf(l2, sizeof(l2), "Size X: %.3f  Y: %.3f  Z: %.3f", sx, sy, sz);
    std::snprintf(l3, sizeof(l3), "Units: mm   FOV: %.1f deg", cam.fovy_deg);

    const float scale = 3.0f;                // bigger, Retina-friendly
    const float lineH = 12.0f * scale;
    float maxw = (float)std::max({ text_width_px(l1,scale),
                                   text_width_px(l2,scale),
                                   text_width_px(l3,scale) });
    const float pad  = 10.0f;
    float boxW = maxw + pad * 2.0f;
    float boxH = lineH * 3.0f + pad * 2.0f;
    float x = W - boxW - 16.0f;
    float y = H - boxH - 16.0f;

    // ——— FULL STATE ISOLATION (server + client) ———
    glUseProgram(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    for (int i = 0; i < 8; ++i) glDisableVertexAttribArray(i);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);

    glPushAttrib(GL_ALL_ATTRIB_BITS);              // save EVERYTHING (depth, blend, multisample, etc.)
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_DITHER);
    glDisable(GL_TEXTURE_1D);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_TEXTURE_3D);
#ifdef GL_MULTISAMPLE
    glDisable(GL_MULTISAMPLE);                     // MSAA can cause HUD shimmer on macOS
#endif

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);  // ensure we’re actually writing color

    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glOrtho(0, W, H, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);  glPushMatrix(); glLoadIdentity();

    // Background panel
    glColor4f(0.05f, 0.06f, 0.07f, 0.85f);
    glBegin(GL_QUADS);
        glVertex2f(x,       y);
        glVertex2f(x+boxW,  y);
        glVertex2f(x+boxW,  y+boxH);
        glVertex2f(x,       y+boxH);
    glEnd();

    // Top separator
    glColor3f(0.18f, 0.20f, 0.23f);
    glBegin(GL_LINES);
        glVertex2f(x,      y);
        glVertex2f(x+boxW, y);
    glEnd();

    // Text (right-aligned)
    float tx = x + boxW - pad - maxw;
    float ty = y + pad + lineH;

    draw_text_screen(tx, ty - lineH*0.2f, l1, scale);
    draw_text_screen(tx, ty + lineH*0.8f, l2, scale);
    draw_text_screen(tx, ty + lineH*1.8f, l3, scale);

    // Restore matrices & ALL state
    glMatrixMode(GL_MODELVIEW);  glPopMatrix();
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glPopAttrib();
}
// ------- end overlay -------

        glfwSwapBuffers(win);


glfwSwapBuffers(win);

glfwSwapBuffers(win);

        glfwPollEvents();
    }

    glDeleteBuffers(1, &vbo);
    glDeleteProgram(prog);
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}