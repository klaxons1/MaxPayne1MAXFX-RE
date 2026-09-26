#include "viewer/Renderer.h"

#include "maxfx/core/Fs.h"
#include "maxfx/image/Image.h"
#include "maxfx/kf2/Kf2.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

namespace maxfx {
namespace {

const char* kMeshVS =
    "#version 330 core\n"
    "layout(location=0) in vec3 aPos;\n"
    "layout(location=1) in vec3 aNrm;\n"
    "layout(location=2) in vec2 aUV;\n"
    "layout(location=3) in vec2 aLM;\n"
    "layout(location=4) in vec3 aColor;\n"
    "uniform mat4 uViewProj;\n"
    "out vec2 vUV;\n"
    "out vec2 vLM;\n"
    "out vec3 vColor;\n"
    "void main(){\n"
    "  gl_Position = uViewProj * vec4(aPos,1.0);\n"
    "  vUV = aUV;\n"
    "  vLM = aLM;\n"
    "  vColor = aColor;\n"
    "}\n";

const char* kMeshFS =
    "#version 330 core\n"
    "in vec2 vUV;\n"
    "in vec2 vLM;\n"
    "in vec3 vColor;\n"
    "uniform sampler2D uDiffuse;\n"
    "uniform sampler2D uLightmap;\n"
    "uniform int uMode;\n"
    "uniform int uAlphaTest;\n"
    "uniform int uVertexLit;\n"
    "uniform float uLmScale;\n"
    "uniform float uAlphaRef;\n"
    "out vec4 frag;\n"
    "void main(){\n"
    "  vec4 diff = texture(uDiffuse, vUV);\n"
    "  if (uAlphaTest != 0 && diff.a < uAlphaRef) discard;\n"
    "  vec3 lm = texture(uLightmap, vLM).rgb * uLmScale;\n"
    "  vec3 color;\n"
    "  if (uMode == 1) color = diff.rgb;\n"
    "  else if (uMode == 2) color = texture(uLightmap, vLM).rgb;\n"
    "  else if (uMode == 3) color = diff.rgb * vColor;\n"
    "  else if (uVertexLit != 0) color = diff.rgb * vColor;\n"
    "  else color = diff.rgb * lm;\n"
    "  frag = vec4(color, diff.a);\n"
    "}\n";

const char* kLineVS =
    "#version 330 core\n"
    "layout(location=0) in vec3 aPos;\n"
    "layout(location=1) in vec4 aColor;\n"
    "uniform mat4 uViewProj;\n"
    "out vec4 vColor;\n"
    "void main(){\n"
    "  gl_Position = uViewProj * vec4(aPos,1.0);\n"
    "  vColor = aColor;\n"
    "}\n";

const char* kLineFS =
    "#version 330 core\n"
    "in vec4 vColor;\n"
    "out vec4 frag;\n"
    "void main(){ frag = vColor; }\n";

const char* kFontVS =
    "#version 330 core\n"
    "layout(location=0) in vec2 aPos;\n"
    "layout(location=1) in vec2 aUV;\n"
    "layout(location=2) in vec3 aColor;\n"
    "out vec2 vUV;\n"
    "out vec3 vColor;\n"
    "void main(){\n"
    "  gl_Position = vec4(aPos, 0.0, 1.0);\n"
    "  vUV = aUV;\n"
    "  vColor = aColor;\n"
    "}\n";

const char* kFontFS =
    "#version 330 core\n"
    "in vec2 vUV;\n"
    "in vec3 vColor;\n"
    "uniform sampler2D uFont;\n"
    "out vec4 frag;\n"
    "void main(){\n"
    "  float a = texture(uFont, vUV).a;\n"
    "  if (a < 0.5) discard;\n"
    "  frag = vec4(vColor, 1.0);\n"
    "}\n";

Vec3 mirrorX(const Vec3& v) { return Vec3(-v.x, v.y, v.z); }

void splitDirs(const std::string& s, std::vector<std::string>* out) {
    std::string cur;
    for (std::size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == ';') {
            if (!cur.empty()) {
                out->push_back(nativeSeparators(cur));
                cur.clear();
            }
        } else {
            cur.push_back(s[i]);
        }
    }
}

std::string locateKf2Texture(const std::string& texName, const std::string& modelDir,
                             const std::string& textureDirs, const Database* database) {
    const std::string file = fileName(nativeSeparators(texName));
    std::vector<std::string> dirs;
    splitDirs(textureDirs, &dirs);
    std::vector<std::string> cands;
    cands.push_back(joinPath(joinPath(modelDir, "textures"), file));
    cands.push_back(joinPath(modelDir, nativeSeparators(texName)));
    cands.push_back(joinPath(modelDir, file));
    cands.push_back(joinPath(joinPath(parentDir(modelDir), "textures"), file));
    for (std::size_t i = 0; i < dirs.size(); ++i) {
        cands.push_back(joinPath(joinPath(modelDir, dirs[i]), file));
        cands.push_back(joinPath(joinPath(parentDir(modelDir), dirs[i]), file));
    }
    if (database != 0 && !database->root.empty()) {
        cands.push_back(joinPath(joinPath(database->root, "sharedtextures"), file));
        cands.push_back(joinPath(joinPath(joinPath(database->root, "skins"), "sharedtextures"), file));
        cands.push_back(joinPath(joinPath(database->root, "textures"), file));
    }
    for (std::size_t i = 0; i < cands.size(); ++i) {
        const std::string hit = existingPathIgnoreCase(cands[i]);
        if (!hit.empty()) {
            return hit;
        }
    }
    return std::string();
}

Mat4x3 roomTransform(const Level& level, int roomId) {
    const Room* room = level.findRoom(roomId);
    if (room == 0 || room->staticMeshes.empty()) {
        return Mat4x3();
    }
    const StaticMesh* mesh = level.findStaticMesh(room->staticMeshes[0]);
    if (mesh == 0) {
        return Mat4x3();
    }
    return mesh->transform;
}

Vec3 worldPoint(const Mat4x3& roomXform, const Mat4x3& objectToRoom) {
    return mirrorX(transformPoint(combine(roomXform, objectToRoom), Vec3(0, 0, 0)));
}

float yawFromTransform(const Mat4x3& roomXform, const Mat4x3& objectToRoom) {
    const Mat4x3 world = combine(roomXform, objectToRoom);
    const Vec3 fwd = mirrorX(transformVector(world, Vec3(0, 0, 1)));
    return std::atan2(fwd.x, -fwd.z);
}

// Public-domain 8x8 font, ASCII 32..127 (row-major, LSB = leftmost pixel).
const unsigned char kFont8x8[96][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, {0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x00},
    {0x6C,0x6C,0x6C,0x00,0x00,0x00,0x00,0x00}, {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00},
    {0x0C,0x3F,0x68,0x3E,0x0B,0x7E,0x18,0x00}, {0x60,0x66,0x0C,0x18,0x30,0x66,0x06,0x00},
    {0x38,0x6C,0x38,0x70,0xDE,0xCC,0x76,0x00}, {0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00},
    {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00}, {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00},
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30}, {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}, {0x03,0x06,0x0C,0x18,0x30,0x60,0xC0,0x00},
    {0x3C,0x66,0x6E,0x7E,0x76,0x66,0x3C,0x00}, {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00},
    {0x3C,0x66,0x06,0x0C,0x18,0x30,0x7E,0x00}, {0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00},
    {0x0C,0x1C,0x3C,0x6C,0x7E,0x0C,0x0C,0x00}, {0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00},
    {0x1C,0x30,0x60,0x7C,0x66,0x66,0x3C,0x00}, {0x7E,0x06,0x0C,0x18,0x30,0x30,0x30,0x00},
    {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00}, {0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0x00},
    {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00}, {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x30},
    {0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x00}, {0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00},
    {0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x00}, {0x3C,0x66,0x06,0x0C,0x18,0x00,0x18,0x00},
    {0x3C,0x66,0x6E,0x6A,0x6E,0x60,0x3C,0x00}, {0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0x00},
    {0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00}, {0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00},
    {0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0x00}, {0x7E,0x60,0x60,0x7C,0x60,0x60,0x7E,0x00},
    {0x7E,0x60,0x60,0x7C,0x60,0x60,0x60,0x00}, {0x3C,0x66,0x60,0x6E,0x66,0x66,0x3C,0x00},
    {0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00}, {0x7E,0x18,0x18,0x18,0x18,0x18,0x7E,0x00},
    {0x3E,0x0C,0x0C,0x0C,0x0C,0x6C,0x38,0x00}, {0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x00},
    {0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00}, {0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0x00},
    {0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0x00}, {0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00},
    {0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00}, {0x3C,0x66,0x66,0x66,0x6A,0x6C,0x36,0x00},
    {0x7C,0x66,0x66,0x7C,0x6C,0x66,0x66,0x00}, {0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0x00},
    {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00}, {0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00},
    {0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x00}, {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00},
    {0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0x00}, {0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x00},
    {0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0x00}, {0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00},
    {0xC0,0x60,0x30,0x18,0x0C,0x06,0x03,0x00}, {0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00},
    {0x18,0x3C,0x66,0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF},
    {0x18,0x18,0x0C,0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x3C,0x06,0x3E,0x66,0x3E,0x00},
    {0x60,0x60,0x7C,0x66,0x66,0x66,0x7C,0x00}, {0x00,0x00,0x3C,0x66,0x60,0x66,0x3C,0x00},
    {0x06,0x06,0x3E,0x66,0x66,0x66,0x3E,0x00}, {0x00,0x00,0x3C,0x66,0x7E,0x60,0x3C,0x00},
    {0x1C,0x30,0x7C,0x30,0x30,0x30,0x30,0x00}, {0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x3C},
    {0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0x00}, {0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00},
    {0x0C,0x00,0x1C,0x0C,0x0C,0x0C,0x6C,0x38}, {0x60,0x60,0x66,0x6C,0x78,0x6C,0x66,0x00},
    {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, {0x00,0x00,0x76,0x7F,0x6B,0x63,0x63,0x00},
    {0x00,0x00,0x7C,0x66,0x66,0x66,0x66,0x00}, {0x00,0x00,0x3C,0x66,0x66,0x66,0x3C,0x00},
    {0x00,0x00,0x7C,0x66,0x66,0x7C,0x60,0x60}, {0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x06},
    {0x00,0x00,0x6C,0x76,0x60,0x60,0x60,0x00}, {0x00,0x00,0x3E,0x60,0x3C,0x06,0x7C,0x00},
    {0x30,0x30,0x7C,0x30,0x30,0x30,0x1C,0x00}, {0x00,0x00,0x66,0x66,0x66,0x66,0x3E,0x00},
    {0x00,0x00,0x66,0x66,0x66,0x3C,0x18,0x00}, {0x00,0x00,0x63,0x6B,0x7F,0x3E,0x36,0x00},
    {0x00,0x00,0x66,0x3C,0x18,0x3C,0x66,0x00}, {0x00,0x00,0x66,0x66,0x66,0x3E,0x06,0x3C},
    {0x00,0x00,0x7E,0x0C,0x18,0x30,0x7E,0x00}, {0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0x00},
    {0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00}, {0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0x00},
    {0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}
};

void addLine(std::vector<LineVertex>& lines, const Vec3& a, const Vec3& b, float r, float g, float bl,
             float alpha = 1.0f) {
    LineVertex va;
    va.x = a.x;
    va.y = a.y;
    va.z = a.z;
    va.r = r;
    va.g = g;
    va.b = bl;
    va.a = alpha;
    LineVertex vb = va;
    vb.x = b.x;
    vb.y = b.y;
    vb.z = b.z;
    lines.push_back(va);
    lines.push_back(vb);
}

void addAxes(std::vector<LineVertex>& lines, const Vec3& p, const Vec3& x, const Vec3& y, const Vec3& z,
             float s) {
    addLine(lines, p, p + x * s, 1, 0.25f, 0.25f);
    addLine(lines, p, p + y * s, 0.25f, 1, 0.25f);
    addLine(lines, p, p + z * s, 0.35f, 0.55f, 1);
}

void addDiamond(std::vector<LineVertex>& lines, const Vec3& p, float s, float r, float g, float b) {
    const Vec3 px(s, 0, 0), py(0, s, 0), pz(0, 0, s);
    addLine(lines, p + py, p + px, r, g, b);
    addLine(lines, p + py, p - px, r, g, b);
    addLine(lines, p + py, p + pz, r, g, b);
    addLine(lines, p + py, p - pz, r, g, b);
    addLine(lines, p - py, p + px, r, g, b);
    addLine(lines, p - py, p - px, r, g, b);
    addLine(lines, p - py, p + pz, r, g, b);
    addLine(lines, p - py, p - pz, r, g, b);
}

void addCircle(std::vector<LineVertex>& lines, const Vec3& p, float radius, float r, float g, float b) {
    const int seg = 24;
    Vec3 prev = p + Vec3(radius, 0, 0);
    for (int i = 1; i <= seg; ++i) {
        const float a = (6.2831853f * static_cast<float>(i)) / static_cast<float>(seg);
        const Vec3 cur = p + Vec3(std::cos(a) * radius, 0.0f, std::sin(a) * radius);
        addLine(lines, prev, cur, r, g, b);
        prev = cur;
    }
}

}  // namespace

Renderer::Renderer()
    : meshProgram_(0),
      lineProgram_(0),
      fontProgram_(0),
      whiteTex_(0),
      greyTex_(0),
      fontTex_(0),
      persistentTextureCount_(0),
      lineVao_(0),
      lineVbo_(0),
      lineCount_(0),
      hudVao_(0),
      hudVbo_(0),
      shading_(kShadeLit),
      wireframe_(false),
      showDynamic_(true),
      showHelpers_(true),
      showService_(false),
      showHud_(true),
      isolatedRoom_(-1),
      width_(1),
      height_(1),
      triangleCount_(0),
      entityMeshCount_(0),
      entityTriangleCount_(0),
      entityPlaceholderCount_(0),
      database_(0),
      recordingAnimated_(false) {}

Renderer::~Renderer() {
    // GPU objects are released by shutdown() while the GL context is still alive.
}

bool Renderer::init(char* error, std::size_t errorSize) {
    meshProgram_ = compileProgram(kMeshVS, kMeshFS, error, errorSize);
    if (meshProgram_ == 0) {
        return false;
    }
    lineProgram_ = compileProgram(kLineVS, kLineFS, error, errorSize);
    if (lineProgram_ == 0) {
        return false;
    }
    fontProgram_ = compileProgram(kFontVS, kFontFS, error, errorSize);
    if (fontProgram_ == 0) {
        return false;
    }
    whiteTex_ = makeSolidTexture(255, 255, 255);
    greyTex_ = makeSolidTexture(128, 128, 128);
    buildFont();
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    persistentTextureCount_ = ownedTextures_.size();
    return true;
}

void Renderer::clearLevel() { clearLevelGpu(); }

void Renderer::clearLevelGpu() {
    if (glDeleteBuffers == 0) {
        return;
    }
    for (std::size_t i = 0; i < batches_.size(); ++i) {
        glDeleteVertexArrays(1, &batches_[i].vao);
        glDeleteBuffers(1, &batches_[i].vbo);
        glDeleteBuffers(1, &batches_[i].ebo);
    }
    batches_.clear();
    cpuBatches_.clear();
    cpuKeys_.clear();
    destroyAnimatedGpu();
    animCpu_.clear();
    animKeys_.clear();
    textureByPath_.clear();
    lights_.clear();
    database_ = 0;
    recordingAnimated_ = false;
    triangleCount_ = 0;
    entityMeshCount_ = 0;
    entityTriangleCount_ = 0;
    entityPlaceholderCount_ = 0;
    spawns_.clear();
    isolatedRoom_ = -1;
    lineCount_ = 0;
    if (lineVao_) {
        glDeleteVertexArrays(1, &lineVao_);
        lineVao_ = 0;
    }
    if (lineVbo_) {
        glDeleteBuffers(1, &lineVbo_);
        lineVbo_ = 0;
    }
    while (ownedTextures_.size() > persistentTextureCount_) {
        GLuint tex = ownedTextures_.back();
        ownedTextures_.pop_back();
        glDeleteTextures(1, &tex);
    }
}

void Renderer::shutdown() {
    if (glDeleteProgram == 0) {
        return;
    }
    for (std::size_t i = 0; i < batches_.size(); ++i) {
        glDeleteVertexArrays(1, &batches_[i].vao);
        glDeleteBuffers(1, &batches_[i].vbo);
        glDeleteBuffers(1, &batches_[i].ebo);
    }
    batches_.clear();
    if (!ownedTextures_.empty()) {
        glDeleteTextures(static_cast<GLsizei>(ownedTextures_.size()), &ownedTextures_[0]);
        ownedTextures_.clear();
    }
    if (lineVao_) {
        glDeleteVertexArrays(1, &lineVao_);
        lineVao_ = 0;
    }
    if (lineVbo_) {
        glDeleteBuffers(1, &lineVbo_);
        lineVbo_ = 0;
    }
    if (hudVao_) {
        glDeleteVertexArrays(1, &hudVao_);
        hudVao_ = 0;
    }
    if (hudVbo_) {
        glDeleteBuffers(1, &hudVbo_);
        hudVbo_ = 0;
    }
    if (meshProgram_) {
        glDeleteProgram(meshProgram_);
        meshProgram_ = 0;
    }
    if (lineProgram_) {
        glDeleteProgram(lineProgram_);
        lineProgram_ = 0;
    }
    if (fontProgram_) {
        glDeleteProgram(fontProgram_);
        fontProgram_ = 0;
    }
    whiteTex_ = greyTex_ = fontTex_ = 0;
}

GLuint Renderer::uploadTexture(const unsigned char* rgba, int w, int h, bool mipmaps, bool clamp) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, clamp ? GL_CLAMP_TO_EDGE : GL_REPEAT);
    if (mipmaps) {
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    ownedTextures_.push_back(tex);
    return tex;
}

GLuint Renderer::makeSolidTexture(unsigned char r, unsigned char g, unsigned char b) {
    unsigned char px[4] = {r, g, b, 255};
    return uploadTexture(px, 1, 1, false, true);
}

void Renderer::buildFont() {
    const int cols = 16;
    const int rows = 6;
    const int w = cols * 8;
    const int h = rows * 8;
    std::vector<unsigned char> pixels(static_cast<std::size_t>(w * h), 0);
    for (int ch = 0; ch < 96; ++ch) {
        const int cx = (ch % cols) * 8;
        const int cy = (ch / cols) * 8;
        for (int row = 0; row < 8; ++row) {
            unsigned char bits = kFont8x8[ch][row];
            for (int col = 0; col < 8; ++col) {
                if (bits & (0x80 >> col)) {
                    pixels[static_cast<std::size_t>((cy + row) * w + (cx + col))] = 255;
                }
            }
        }
    }
    std::vector<unsigned char> rgba(static_cast<std::size_t>(w * h * 4));
    for (int i = 0; i < w * h; ++i) {
        rgba[static_cast<std::size_t>(i * 4 + 0)] = 255;
        rgba[static_cast<std::size_t>(i * 4 + 1)] = 255;
        rgba[static_cast<std::size_t>(i * 4 + 2)] = 255;
        rgba[static_cast<std::size_t>(i * 4 + 3)] = pixels[static_cast<std::size_t>(i)];
    }
    fontTex_ = uploadTexture(&rgba[0], w, h, false, true);

    glGenVertexArrays(1, &hudVao_);
    glGenBuffers(1, &hudVbo_);
    glBindVertexArray(hudVao_);
    glBindBuffer(GL_ARRAY_BUFFER, hudVbo_);
    // Sized on first flushHud(); the old 64 KiB cap overflowed once the
    // status line grew, and glBufferSubData past the end filled the screen
    // with garbage glyphs.
    glBufferData(GL_ARRAY_BUFFER, 4, 0, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 7 * sizeof(float), 0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 7 * sizeof(float),
                          reinterpret_cast<void*>(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float),
                          reinterpret_cast<void*>(4 * sizeof(float)));
    glBindVertexArray(0);
}

bool Renderer::loadLevel(const Level& level, const Database* database, char* error,
                         std::size_t errorSize) {
    (void)error;
    (void)errorSize;
    clearLevelGpu();
    database_ = database;

    std::vector<Image> decodedTex(level.textures.size());
    std::vector<char> decodedOk(level.textures.size(), 0);
    for (std::size_t i = 0; i < level.textures.size(); ++i) {
        if (decodeEmbeddedImage(level.textures[i].fileType,
                                level.textures[i].data.empty() ? 0 : &level.textures[i].data[0],
                                level.textures[i].data.size(), decodedTex[i], 0) &&
            !decodedTex[i].empty()) {
            decodedOk[i] = 1;
        }
    }

    std::vector<GLuint> texGpu(level.textures.size(), whiteTex_);
    std::vector<char> uploaded(level.textures.size(), 0);
    for (std::size_t i = 0; i < level.materials.size(); ++i) {
        const Material& mat = level.materials[i];
        const int diff = mat.diffuseTexture;
        if (diff < 0 || static_cast<std::size_t>(diff) >= decodedTex.size() || !decodedOk[static_cast<std::size_t>(diff)]) {
            continue;
        }
        Image img = decodedTex[static_cast<std::size_t>(diff)];
        if (mat.alphaTexture >= 0 && static_cast<std::size_t>(mat.alphaTexture) < decodedTex.size() &&
            decodedOk[static_cast<std::size_t>(mat.alphaTexture)]) {
            applyAlphaMap(img, decodedTex[static_cast<std::size_t>(mat.alphaTexture)]);
        }
        texGpu[static_cast<std::size_t>(diff)] = uploadTexture(&img.pixels[0], img.width, img.height, true, false);
        uploaded[static_cast<std::size_t>(diff)] = 1;
    }
    for (std::size_t i = 0; i < decodedTex.size(); ++i) {
        if (!uploaded[i] && decodedOk[i]) {
            texGpu[i] = uploadTexture(&decodedTex[i].pixels[0], decodedTex[i].width, decodedTex[i].height, true,
                                      false);
        }
    }

    int maxLmId = static_cast<int>(level.lightmaps.size()) - 1;
    for (std::size_t i = 0; i < level.lightmaps.size(); ++i) {
        if (level.lightmaps[i].id > maxLmId) {
            maxLmId = level.lightmaps[i].id;
        }
    }
    std::vector<GLuint> lmGpu(static_cast<std::size_t>(maxLmId < 0 ? 0 : maxLmId + 1), greyTex_);
    for (std::size_t i = 0; i < level.lightmaps.size(); ++i) {
        Image img;
        if (decodeEmbeddedImage(level.lightmaps[i].fileType,
                                level.lightmaps[i].data.empty() ? 0 : &level.lightmaps[i].data[0],
                                level.lightmaps[i].data.size(), img, 0) &&
            !img.empty()) {
            GLuint tex = uploadTexture(&img.pixels[0], img.width, img.height, false, true);
            if (i < lmGpu.size()) {
                lmGpu[i] = tex;
            }
            if (level.lightmaps[i].id >= 0 &&
                static_cast<std::size_t>(level.lightmaps[i].id) < lmGpu.size()) {
                lmGpu[static_cast<std::size_t>(level.lightmaps[i].id)] = tex;
            }
        }
    }

    int maxMatId = static_cast<int>(level.materials.size()) - 1;
    for (std::size_t i = 0; i < level.materials.size(); ++i) {
        if (level.materials[i].id > maxMatId) {
            maxMatId = level.materials[i].id;
        }
    }
    std::vector<GLuint> matTex(static_cast<std::size_t>(maxMatId < 0 ? 0 : maxMatId + 1), whiteTex_);
    for (std::size_t i = 0; i < level.materials.size(); ++i) {
        const int diff = level.materials[i].diffuseTexture;
        if (diff < 0 || static_cast<std::size_t>(diff) >= texGpu.size()) {
            continue;
        }
        const GLuint tex = texGpu[static_cast<std::size_t>(diff)];
        if (i < matTex.size()) {
            matTex[i] = tex;
        }
        if (level.materials[i].id >= 0 &&
            static_cast<std::size_t>(level.materials[i].id) < matTex.size()) {
            matTex[static_cast<std::size_t>(level.materials[i].id)] = tex;
        }
    }

    std::vector<WorldLight> lights;
    lights.reserve(level.pointLights.size() + level.staticLights.size());
    for (std::size_t i = 0; i < level.pointLights.size(); ++i) {
        const PointLight& pl = level.pointLights[i];
        WorldLight w;
        const Mat4x3 roomX = roomTransform(level, pl.properties.roomId);
        w.position = worldPoint(roomX, pl.properties.objectToRoom);
        w.color = Vec3(pl.r, pl.g, pl.b);
        w.intensity = pl.intensity > 0.0f ? pl.intensity : 1.0f;
        w.falloff = pl.falloff;
        lights.push_back(w);
    }
    for (std::size_t i = 0; i < level.staticLights.size(); ++i) {
        const StaticLight& sl = level.staticLights[i];
        WorldLight w;
        const Mat4x3 roomX = roomTransform(level, sl.properties.roomId);
        w.position = worldPoint(roomX, sl.properties.objectToRoom);
        w.color = Vec3(sl.r, sl.g, sl.b);
        w.intensity = (sl.intensity > 0.0f ? sl.intensity : 1.0f) * (sl.colorMultiplier > 0.0f ? sl.colorMultiplier : 1.0f);
        w.falloff = sl.falloffRange;
        lights.push_back(w);
    }
    lights_ = lights;

    for (std::size_t i = 0; i < level.staticMeshes.size(); ++i) {
        const StaticMesh& mesh = level.staticMeshes[i];
        int roomId = -1;
        for (std::size_t r = 0; r < level.rooms.size(); ++r) {
            for (std::size_t s = 0; s < level.rooms[r].staticMeshes.size(); ++s) {
                if (level.rooms[r].staticMeshes[s] == mesh.groupId) {
                    roomId = level.rooms[r].id;
                }
            }
        }
        appendMesh(mesh.vertices, mesh.normals, level.staticTextureVertices, mesh.polygons,
                   mesh.transform, roomId, false, level, database, matTex, lmGpu, mesh.radiosity, lights);
    }

    for (std::size_t i = 0; i < level.dynamicMeshes.size(); ++i) {
        const DynamicMesh& mesh = level.dynamicMeshes[i];
        const Mat4x3 roomX = roomTransform(level, mesh.properties.roomId);
        const Mat4x3 world = combine(roomX, mesh.properties.objectToRoom);
        appendMesh(mesh.vertices, mesh.normals, level.dynamicTextureVertices, mesh.polygons, world,
                   mesh.properties.roomId, true, level, database, matTex, lmGpu, mesh.radiosity, lights);
    }

    for (std::size_t i = 0; i < level.items.size(); ++i) {
        const LevelItem& it = level.items[i];
        const Mat4x3 roomX = roomTransform(level, it.properties.roomId);
        const Mat4x3 entity = combine(roomX, it.properties.objectToRoom);
        const Kf2File* kf = 0;
        if (database) {
            const ItemDef* def = database->findItem(it.itemName);
            if (def != 0 && !def->lods.empty()) {
                kf = database->model(def->lods[0].resolvedExport);
            }
        }
        int added = 0;
        if (kf != 0) {
            added = appendKf2File(*kf, entity, it.properties.roomId, lights, database);
        }
        if (added <= 0) {
            appendOrientedBox(entity, it.properties.roomId, 0.12f, 0.08f, 0.18f, Vec3(1.0f, 0.75f, 0.15f));
            ++entityPlaceholderCount_;
        }
        ++entityMeshCount_;
    }
    for (std::size_t i = 0; i < level.characters.size(); ++i) {
        const Character& ch = level.characters[i];
        const Mat4x3 roomX = roomTransform(level, ch.properties.roomId);
        const Mat4x3 entity = combine(roomX, ch.properties.objectToRoom);
        const Kf2File* kf = 0;
        if (database) {
            const SkinDef* def = database->findSkin(ch.characterName);
            if (def != 0 && !def->lods.empty()) {
                kf = database->model(def->lods[0].resolvedExport);
            }
        }
        int added = 0;
        if (kf != 0) {
            // Skinned characters are posed every frame in beginAnimated /
            // appendAnimatedCharacter so the bind-pose T-pose is not baked.
            added = 1;
        }
        if (added <= 0) {
            Mat4x3 body = entity;
            body.rows[3] = transformPoint(entity, Vec3(0.0f, 0.9f, 0.0f));
            appendOrientedBox(body, ch.properties.roomId, 0.22f, 0.9f, 0.22f, Vec3(0.95f, 0.25f, 0.2f));
            ++entityPlaceholderCount_;
        }
        ++entityMeshCount_;
    }
    if (database && !database->worldSpherePath.empty()) {
        const Kf2File* sphere = database->model(database->worldSpherePath);
        if (sphere != 0) {
            Mat4x3 identity;
            appendKf2File(*sphere, identity, -1, lights, database);
        }
    }
    for (std::size_t i = 0; i < level.pointLights.size(); ++i) {
        const PointLight& pl = level.pointLights[i];
        const Mat4x3 roomX = roomTransform(level, pl.properties.roomId);
        const Mat4x3 entity = combine(roomX, pl.properties.objectToRoom);
        appendOrientedBox(entity, pl.properties.roomId, 0.08f, 0.08f, 0.08f,
                          Vec3(pl.r > 0.05f ? pl.r : 1.0f, pl.g > 0.05f ? pl.g : 0.9f,
                               pl.b > 0.05f ? pl.b : 0.4f));
    }
    for (std::size_t i = 0; i < level.staticLights.size(); ++i) {
        const StaticLight& sl = level.staticLights[i];
        const Mat4x3 roomX = roomTransform(level, sl.properties.roomId);
        const Mat4x3 entity = combine(roomX, sl.properties.objectToRoom);
        appendOrientedBox(entity, sl.properties.roomId, 0.1f, 0.1f, 0.1f, Vec3(sl.r, sl.g, sl.b));
    }
    for (std::size_t i = 0; i < level.fsms.size(); ++i) {
        const Fsm& fsm = level.fsms[i];
        const Mat4x3 roomX = roomTransform(level, fsm.properties.roomId);
        const Mat4x3 entity = combine(roomX, fsm.properties.objectToRoom);
        appendOrientedBox(entity, fsm.properties.roomId, 0.15f, 0.15f, 0.15f, Vec3(0.3f, 0.95f, 0.45f));
    }

    uploadBatches();
    buildHelpers(level);

    for (std::size_t i = 0; i < level.waypoints.size(); ++i) {
        const Waypoint& wp = level.waypoints[i];
        if (wp.type == 0) {
            continue;
        }
        SpawnPoint sp;
        sp.name = wp.sharedName;
        const Mat4x3 roomX = roomTransform(level, wp.properties.roomId);
        sp.position = worldPoint(roomX, wp.properties.objectToRoom);
        sp.position.y += 1.6f;
        sp.yaw = yawFromTransform(roomX, wp.properties.objectToRoom);
        sp.roomId = wp.properties.roomId;
        spawns_.push_back(sp);
    }
    return true;
}

Renderer::GpuMesh& Renderer::batchFor(const BatchKey& key) {
    std::vector<BatchKey>& keys = recordingAnimated_ ? animKeys_ : cpuKeys_;
    std::vector<GpuMesh>& batches = recordingAnimated_ ? animCpu_ : cpuBatches_;
    for (std::size_t b = 0; b < keys.size(); ++b) {
        if (keys[b] == key) {
            return batches[b];
        }
    }
    keys.push_back(key);
    batches.push_back(GpuMesh());
    return batches.back();
}

Vec3 Renderer::shadeVertex(const Vec3& worldPos, const Vec3& worldNrm,
                           const std::vector<WorldLight>& lights) const {
    Vec3 lit(0.18f, 0.18f, 0.20f);
    const Vec3 n = normalize(worldNrm);
    for (std::size_t i = 0; i < lights.size(); ++i) {
        const WorldLight& L = lights[i];
        Vec3 toL = L.position - worldPos;
        const float dist = length(toL);
        if (dist < 1.0e-4f) {
            continue;
        }
        toL = toL * (1.0f / dist);
        const float fall = L.falloff > 0.05f ? L.falloff : 4.0f;
        const float att = L.intensity / (1.0f + (dist / fall) * (dist / fall));
        const float ndotl = clamp(dot(n, toL), 0.0f, 1.0f);
        const float w = att * (0.35f + 0.65f * ndotl);
        lit.x += L.color.x * w;
        lit.y += L.color.y * w;
        lit.z += L.color.z * w;
    }
    lit.x = clamp(lit.x, 0.12f, 1.15f);
    lit.y = clamp(lit.y, 0.12f, 1.15f);
    lit.z = clamp(lit.z, 0.12f, 1.15f);
    return lit;
}

GLuint Renderer::textureFromFile(const std::string& path) {
    const std::string key = lowerCopy(path);
    std::map<std::string, GLuint>::const_iterator it = textureByPath_.find(key);
    if (it != textureByPath_.end()) {
        return it->second;
    }
    Image img;
    if (!loadImageFile(path, img, 0) || img.empty()) {
        textureByPath_[key] = greyTex_;
        return greyTex_;
    }
    const GLuint tex = uploadTexture(&img.pixels[0], img.width, img.height, true, false);
    textureByPath_[key] = tex;
    return tex;
}

void Renderer::appendMesh(const std::vector<Vec3>& vertices, const std::vector<Vec3>& normals,
                         const std::vector<TextureVertex>& texVerts, const std::vector<Polygon>& polygons,
                         const Mat4x3& transform, int roomId, bool dynamic, const Level& level,
                         const Database* database, const std::vector<GLuint>& materialTextures,
                         const std::vector<GLuint>& lightmapTextures,
                         const std::vector<RadiositySample>& radiosity,
                         const std::vector<WorldLight>& lights) {
    std::map<int, Vec3> radio;
    float radioMax = 0.0f;
    for (std::size_t i = 0; i < radiosity.size(); ++i) {
        radio[radiosity[i].key] = radiosity[i].value;
        radioMax = std::max(radioMax, std::max(radiosity[i].value.x,
                                               std::max(radiosity[i].value.y, radiosity[i].value.z)));
    }
    const float radioScale = radioMax > 2.0f ? (1.0f / 255.0f) : 1.0f;

    for (std::size_t p = 0; p < polygons.size(); ++p) {
        const Polygon& poly = polygons[p];
        if (poly.vertexCount < 3) {
            continue;
        }
        GLuint diff = whiteTex_;
        GLuint lm = greyTex_;
        bool alpha = false;
        bool service = false;
        bool writesZ = true;
        int detailOffset = 0;
        const Material* mat = 0;
        if (poly.materialId >= 0 && static_cast<std::size_t>(poly.materialId) < materialTextures.size()) {
            diff = materialTextures[static_cast<std::size_t>(poly.materialId)];
            mat = level.findMaterialByIndex(poly.materialId);
            if (mat) {
                alpha = mat->alphaTest;
                if (database) {
                    const MaterialCategory* cat = database->findMaterial(mat->category);
                    if (cat) {
                        service = !cat->drawPolygons;
                        writesZ = cat->writesZBuffer;
                        detailOffset = cat->detailOffset;
                    }
                }
            }
        }
        if (alpha && detailOffset < 1) {
            detailOffset = 1;
        }
        if (poly.lightmapId >= 0 && static_cast<std::size_t>(poly.lightmapId) < lightmapTextures.size()) {
            lm = lightmapTextures[static_cast<std::size_t>(poly.lightmapId)];
        }

        BatchKey key;
        key.diffuse = diff;
        key.lightmap = lm;
        key.roomId = roomId;
        key.alphaTest = alpha;
        key.dynamic = dynamic;
        key.service = service;
        key.writesZ = writesZ;
        key.vertexLit = false;
        key.detailOffset = detailOffset;
        GpuMesh& gpu = batchFor(key);
        const unsigned int base = static_cast<unsigned int>(gpu.vertices.size() / 13);

        for (int i = 0; i < poly.vertexCount; ++i) {
            const int tvi = poly.textureVertexStart + i;
            if (tvi < 0 || static_cast<std::size_t>(tvi) >= texVerts.size()) {
                continue;
            }
            const TextureVertex& tv = texVerts[static_cast<std::size_t>(tvi)];
            if (tv.vertexIndex < 0 || static_cast<std::size_t>(tv.vertexIndex) >= vertices.size()) {
                continue;
            }
            const Vec3 pos = mirrorX(transformPoint(transform, vertices[static_cast<std::size_t>(tv.vertexIndex)]));
            Vec3 nrm(0, 1, 0);
            if (static_cast<std::size_t>(tv.vertexIndex) < normals.size()) {
                nrm = mirrorX(transformVector(transform, normals[static_cast<std::size_t>(tv.vertexIndex)]));
            }
            Vec3 col(0.7f, 0.7f, 0.7f);
            std::map<int, Vec3>::const_iterator it = radio.find(tv.vertexIndex);
            if (it == radio.end()) {
                it = radio.find(tvi);
            }
            if (it != radio.end()) {
                col = it->second * radioScale;
            } else {
                col = shadeVertex(pos, nrm, lights);
            }
            gpu.vertices.push_back(pos.x);
            gpu.vertices.push_back(pos.y);
            gpu.vertices.push_back(pos.z);
            gpu.vertices.push_back(nrm.x);
            gpu.vertices.push_back(nrm.y);
            gpu.vertices.push_back(nrm.z);
            gpu.vertices.push_back(tv.uv.x);
            gpu.vertices.push_back(tv.uv.y);
            gpu.vertices.push_back(tv.lightmapUv.x);
            gpu.vertices.push_back(tv.lightmapUv.y);
            gpu.vertices.push_back(col.x);
            gpu.vertices.push_back(col.y);
            gpu.vertices.push_back(col.z);
        }

        const unsigned int emitted = static_cast<unsigned int>(gpu.vertices.size() / 13) - base;
        if (emitted < 3) {
            continue;
        }
        for (unsigned int i = 1; i + 1 < emitted; ++i) {
            gpu.indices.push_back(base + 0);
            gpu.indices.push_back(base + i + 1);
            gpu.indices.push_back(base + i);
            ++triangleCount_;
        }
    }
}

void Renderer::appendKf2Mesh(const Kf2DrawMesh& mesh, const Mat4x3& world, int roomId,
                            const std::vector<WorldLight>& lights, const Database* database,
                            const std::string& modelDir) {
    for (std::size_t p = 0; p < mesh.parts.size(); ++p) {
        const Kf2DrawPart& part = mesh.parts[p];
        if (part.vertices.size() < 3) {
            continue;
        }
        GLuint diff = greyTex_;
        if (!part.textureFiles.empty()) {
            const std::string hit =
                locateKf2Texture(part.textureFiles[0], modelDir, mesh.textureDirs, database);
            if (!hit.empty()) {
                diff = textureFromFile(hit);
            }
        }
        BatchKey key;
        key.diffuse = diff;
        key.lightmap = whiteTex_;
        key.roomId = roomId;
        key.alphaTest = part.textureFiles.size() > 1;
        key.dynamic = recordingAnimated_;
        key.service = false;
        key.writesZ = true;
        key.vertexLit = true;
        key.detailOffset = 0;
        GpuMesh& gpu = batchFor(key);
        const unsigned int base = static_cast<unsigned int>(gpu.vertices.size() / 13);
        for (std::size_t v = 0; v < part.vertices.size(); ++v) {
            const Vec3 pos = mirrorX(transformPoint(world, part.vertices[v].position));
            const Vec3 nrm = mirrorX(transformVector(world, part.vertices[v].normal));
            Vec3 col = shadeVertex(pos, nrm, lights);
            col.x = clamp(col.x * part.diffuseColor.x, 0.05f, 1.0f);
            col.y = clamp(col.y * part.diffuseColor.y, 0.05f, 1.0f);
            col.z = clamp(col.z * part.diffuseColor.z, 0.05f, 1.0f);
            gpu.vertices.push_back(pos.x);
            gpu.vertices.push_back(pos.y);
            gpu.vertices.push_back(pos.z);
            gpu.vertices.push_back(nrm.x);
            gpu.vertices.push_back(nrm.y);
            gpu.vertices.push_back(nrm.z);
            gpu.vertices.push_back(part.vertices[v].uv.x);
            gpu.vertices.push_back(part.vertices[v].uv.y);
            gpu.vertices.push_back(0.0f);
            gpu.vertices.push_back(0.0f);
            gpu.vertices.push_back(col.x);
            gpu.vertices.push_back(col.y);
            gpu.vertices.push_back(col.z);
        }
        const unsigned int emitted = static_cast<unsigned int>(gpu.vertices.size() / 13) - base;
        for (unsigned int i = 0; i + 2 < emitted; i += 3) {
            gpu.indices.push_back(base + i + 0);
            gpu.indices.push_back(base + i + 2);
            gpu.indices.push_back(base + i + 1);
            if (!recordingAnimated_) {
                ++triangleCount_;
                ++entityTriangleCount_;
            }
        }
    }
}

int Renderer::appendKf2File(const Kf2File& kf, const Mat4x3& entity, int roomId,
                            const std::vector<WorldLight>& lights, const Database* database) {
    std::vector<Kf2DrawMesh> draws;
    kf2BuildDrawMeshes(kf, draws);
    if (draws.empty()) {
        return 0;
    }
    std::vector<std::string> nodeNames;
    std::vector<Mat4x3> nodeWorlds;
    kf2NodeWorldTransforms(kf, &nodeNames, &nodeWorlds);
    const std::string modelDir = parentDir(kf.sourcePath);
    const unsigned int before = entityTriangleCount_;
    for (std::size_t m = 0; m < draws.size(); ++m) {
        Mat4x3 local;
        bool found = false;
        for (std::size_t n = 0; n < nodeNames.size(); ++n) {
            if (nodeNames[n] == draws[m].nodeName) {
                local = nodeWorlds[n];
                found = true;
                break;
            }
        }
        Mat4x3 world = entity;
        if (!draws[m].modelSpace && found) {
            world = combine(entity, local);
        }
        appendKf2Mesh(draws[m], world, roomId, lights, database, modelDir);
    }
    return static_cast<int>(entityTriangleCount_ - before);
}

void Renderer::destroyAnimatedGpu() {
    for (std::size_t i = 0; i < animBatches_.size(); ++i) {
        if (animBatches_[i].vao) {
            glDeleteVertexArrays(1, &animBatches_[i].vao);
        }
        if (animBatches_[i].vbo) {
            glDeleteBuffers(1, &animBatches_[i].vbo);
        }
        if (animBatches_[i].ebo) {
            glDeleteBuffers(1, &animBatches_[i].ebo);
        }
    }
    animBatches_.clear();
}

void Renderer::beginAnimated() {
    recordingAnimated_ = true;
    destroyAnimatedGpu();
    animCpu_.clear();
    animKeys_.clear();
}

int Renderer::appendAnimatedCharacter(const Kf2File& mesh, const Kf2File* skin, const Kf2File* bindAnim,
                                      const Kf2File* playAnim, float timeSeconds, const Mat4x3& entity,
                                      int roomId) {
    recordingAnimated_ = true;
    std::vector<Kf2DrawMesh> draws;
    kf2BuildSkinnedDrawMeshes(mesh, skin, bindAnim, playAnim, timeSeconds, draws);
    if (draws.empty()) {
        return appendKf2File(mesh, entity, roomId, lights_, database_);
    }
    const std::string modelDir = parentDir(mesh.sourcePath);
    const unsigned int before = entityTriangleCount_;
    for (std::size_t m = 0; m < draws.size(); ++m) {
        Mat4x3 world = entity;
        if (!draws[m].modelSpace) {
            world = combine(entity, draws[m].objectToParent);
        }
        appendKf2Mesh(draws[m], world, roomId, lights_, database_, modelDir);
    }
    return static_cast<int>(entityTriangleCount_ - before);
}

void Renderer::uploadAnimated() {
    recordingAnimated_ = false;
    destroyAnimatedGpu();
    for (std::size_t i = 0; i < animCpu_.size(); ++i) {
        GpuMesh& mesh = animCpu_[i];
        if (mesh.indices.empty()) {
            continue;
        }
        DrawBatch batch;
        batch.diffuse = animKeys_[i].diffuse;
        batch.lightmap = animKeys_[i].lightmap;
        batch.roomId = animKeys_[i].roomId;
        batch.alphaTest = animKeys_[i].alphaTest;
        batch.dynamic = true;
        batch.service = false;
        batch.writesZ = true;
        batch.vertexLit = true;
        batch.detailOffset = 0;
        batch.indexCount = static_cast<int>(mesh.indices.size());
        glGenVertexArrays(1, &batch.vao);
        glGenBuffers(1, &batch.vbo);
        glGenBuffers(1, &batch.ebo);
        glBindVertexArray(batch.vao);
        glBindBuffer(GL_ARRAY_BUFFER, batch.vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(float)),
                     &mesh.vertices[0], GL_STREAM_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, batch.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(unsigned int)),
                     &mesh.indices[0], GL_STREAM_DRAW);
        const GLsizei stride = 13 * sizeof(float);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, 0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(3 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(6 * sizeof(float)));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(8 * sizeof(float)));
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(10 * sizeof(float)));
        glBindVertexArray(0);
        animBatches_.push_back(batch);
    }
}

void Renderer::appendOrientedBox(const Mat4x3& entity, int roomId, float hx, float hy, float hz,
                                 const Vec3& color) {
    const Vec3 corners[8] = {
        Vec3(-hx, -hy, -hz), Vec3(hx, -hy, -hz), Vec3(hx, hy, -hz), Vec3(-hx, hy, -hz),
        Vec3(-hx, -hy, hz),  Vec3(hx, -hy, hz),  Vec3(hx, hy, hz),  Vec3(-hx, hy, hz),
    };
    Vec3 w[8];
    for (int i = 0; i < 8; ++i) {
        w[i] = mirrorX(transformPoint(entity, corners[i]));
    }
    const int faces[6][4] = {
        {0, 1, 2, 3}, {5, 4, 7, 6}, {4, 0, 3, 7}, {1, 5, 6, 2}, {4, 5, 1, 0}, {3, 2, 6, 7},
    };
    BatchKey key;
    key.diffuse = whiteTex_;
    key.lightmap = whiteTex_;
    key.roomId = roomId;
    key.alphaTest = false;
    key.dynamic = false;
    key.service = false;
    key.writesZ = true;
    key.vertexLit = true;
    key.detailOffset = 0;
    GpuMesh& gpu = batchFor(key);
    for (int f = 0; f < 6; ++f) {
        const Vec3 a = w[faces[f][0]];
        const Vec3 b = w[faces[f][1]];
        const Vec3 c = w[faces[f][2]];
        const Vec3 d = w[faces[f][3]];
        const Vec3 n = normalize(cross(b - a, c - a));
        const Vec3 tri[6] = {a, b, c, a, c, d};
        const unsigned int base = static_cast<unsigned int>(gpu.vertices.size() / 13);
        for (int v = 0; v < 6; ++v) {
            gpu.vertices.push_back(tri[v].x);
            gpu.vertices.push_back(tri[v].y);
            gpu.vertices.push_back(tri[v].z);
            gpu.vertices.push_back(n.x);
            gpu.vertices.push_back(n.y);
            gpu.vertices.push_back(n.z);
            gpu.vertices.push_back(0.0f);
            gpu.vertices.push_back(0.0f);
            gpu.vertices.push_back(0.0f);
            gpu.vertices.push_back(0.0f);
            gpu.vertices.push_back(color.x);
            gpu.vertices.push_back(color.y);
            gpu.vertices.push_back(color.z);
        }
        gpu.indices.push_back(base + 0);
        gpu.indices.push_back(base + 1);
        gpu.indices.push_back(base + 2);
        gpu.indices.push_back(base + 3);
        gpu.indices.push_back(base + 4);
        gpu.indices.push_back(base + 5);
        triangleCount_ += 2;
        entityTriangleCount_ += 2;
    }
}

void Renderer::uploadBatches() {
    for (std::size_t i = 0; i < batches_.size(); ++i) {
        glDeleteVertexArrays(1, &batches_[i].vao);
        glDeleteBuffers(1, &batches_[i].vbo);
        glDeleteBuffers(1, &batches_[i].ebo);
    }
    batches_.clear();
    batches_.reserve(cpuBatches_.size());
    for (std::size_t i = 0; i < cpuBatches_.size(); ++i) {
        GpuMesh& mesh = cpuBatches_[i];
        if (mesh.indices.empty()) {
            continue;
        }
        DrawBatch batch;
        batch.diffuse = cpuKeys_[i].diffuse;
        batch.lightmap = cpuKeys_[i].lightmap;
        batch.roomId = cpuKeys_[i].roomId;
        batch.alphaTest = cpuKeys_[i].alphaTest;
        batch.dynamic = cpuKeys_[i].dynamic;
        batch.service = cpuKeys_[i].service;
        batch.writesZ = cpuKeys_[i].writesZ;
        batch.vertexLit = cpuKeys_[i].vertexLit;
        batch.detailOffset = cpuKeys_[i].detailOffset;
        batch.indexCount = static_cast<int>(mesh.indices.size());
        glGenVertexArrays(1, &batch.vao);
        glGenBuffers(1, &batch.vbo);
        glGenBuffers(1, &batch.ebo);
        glBindVertexArray(batch.vao);
        glBindBuffer(GL_ARRAY_BUFFER, batch.vbo);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(float)),
                     &mesh.vertices[0], GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, batch.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(unsigned int)), &mesh.indices[0],
                     GL_STATIC_DRAW);
        const GLsizei stride = 13 * sizeof(float);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, 0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(3 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(6 * sizeof(float)));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(8 * sizeof(float)));
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(10 * sizeof(float)));
        glBindVertexArray(0);
        batches_.push_back(batch);
    }
    // Opaque, then DetailOffset overlays, then alpha-tested decals, then service.
    for (std::size_t i = 0; i < batches_.size(); ++i) {
        for (std::size_t j = i + 1; j < batches_.size(); ++j) {
            const DrawBatch& a = batches_[i];
            const DrawBatch& b = batches_[j];
            const int ka = (a.service ? 8 : 0) + (a.alphaTest ? 4 : 0) + (a.detailOffset > 0 ? 2 : 0);
            const int kb = (b.service ? 8 : 0) + (b.alphaTest ? 4 : 0) + (b.detailOffset > 0 ? 2 : 0);
            if (kb < ka) {
                DrawBatch tmp = batches_[i];
                batches_[i] = batches_[j];
                batches_[j] = tmp;
            }
        }
    }
    cpuBatches_.clear();
    cpuKeys_.clear();
}

void Renderer::buildHelpers(const Level& level) {
    std::vector<LineVertex> lines;
    for (std::size_t i = 0; i < level.waypoints.size(); ++i) {
        const Waypoint& wp = level.waypoints[i];
        const Mat4x3 roomX = roomTransform(level, wp.properties.roomId);
        const Mat4x3 world = combine(roomX, wp.properties.objectToRoom);
        const Vec3 p = mirrorX(transformPoint(world, Vec3(0, 0, 0)));
        const Vec3 x = normalize(mirrorX(transformVector(world, Vec3(1, 0, 0))));
        const Vec3 y = normalize(mirrorX(transformVector(world, Vec3(0, 1, 0))));
        const Vec3 z = normalize(mirrorX(transformVector(world, Vec3(0, 0, 1))));
        addAxes(lines, p, x, y, z, 0.6f);
    }
    for (std::size_t i = 0; i < level.triggers.size(); ++i) {
        const Trigger& tr = level.triggers[i];
        const Mat4x3 roomX = roomTransform(level, tr.properties.roomId);
        const Vec3 p = worldPoint(roomX, tr.properties.objectToRoom);
        addCircle(lines, p, tr.radius > 0.05f ? tr.radius : 0.4f, 0.2f, 0.9f, 1.0f);
    }
    for (std::size_t i = 0; i < level.characters.size(); ++i) {
        const Character& ch = level.characters[i];
        const Mat4x3 roomX = roomTransform(level, ch.properties.roomId);
        addDiamond(lines, worldPoint(roomX, ch.properties.objectToRoom) + Vec3(0, 0.9f, 0), 0.35f, 1, 0.2f,
                   0.2f);
    }
    for (std::size_t i = 0; i < level.fsms.size(); ++i) {
        const Fsm& fsm = level.fsms[i];
        const Mat4x3 roomX = roomTransform(level, fsm.properties.roomId);
        addDiamond(lines, worldPoint(roomX, fsm.properties.objectToRoom), 0.25f, 0.3f, 0.95f, 0.4f);
    }
    for (std::size_t i = 0; i < level.items.size(); ++i) {
        const LevelItem& it = level.items[i];
        const Mat4x3 roomX = roomTransform(level, it.properties.roomId);
        addDiamond(lines, worldPoint(roomX, it.properties.objectToRoom), 0.2f, 1, 0.3f, 0.9f);
    }
    for (std::size_t i = 0; i < level.pointLights.size(); ++i) {
        const PointLight& pl = level.pointLights[i];
        const Mat4x3 roomX = roomTransform(level, pl.properties.roomId);
        addDiamond(lines, worldPoint(roomX, pl.properties.objectToRoom), 0.15f, pl.r, pl.g, pl.b);
    }
    for (std::size_t i = 0; i < level.exits.size(); ++i) {
        const Exit& ex = level.exits[i];
        const Mat4x3 roomX = roomTransform(level, ex.roomId);
        if (ex.vertices.size() < 2) {
            continue;
        }
        std::vector<Vec3> pts;
        pts.reserve(ex.vertices.size());
        for (std::size_t v = 0; v < ex.vertices.size(); ++v) {
            pts.push_back(mirrorX(transformPoint(combine(roomX, ex.transform), ex.vertices[v])));
        }
        for (std::size_t v = 0; v < pts.size(); ++v) {
            addLine(lines, pts[v], pts[(v + 1) % pts.size()], 0.3f, 0.6f, 1.0f);
        }
    }

    if (lineVao_) {
        glDeleteVertexArrays(1, &lineVao_);
        lineVao_ = 0;
    }
    if (lineVbo_) {
        glDeleteBuffers(1, &lineVbo_);
        lineVbo_ = 0;
    }
    lineCount_ = static_cast<int>(lines.size());
    if (lines.empty()) {
        return;
    }
    glGenVertexArrays(1, &lineVao_);
    glGenBuffers(1, &lineVbo_);
    glBindVertexArray(lineVao_);
    glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(lines.size() * sizeof(LineVertex)), &lines[0],
                 GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex), 0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(LineVertex),
                          reinterpret_cast<void*>(3 * sizeof(float)));
    glBindVertexArray(0);
}

void Renderer::resize(int width, int height) {
    width_ = width < 1 ? 1 : width;
    height_ = height < 1 ? 1 : height;
    glViewport(0, 0, width_, height_);
}

void Renderer::cycleShading() {
    shading_ = static_cast<ShadingMode>((static_cast<int>(shading_) + 1) % 4);
}

void Renderer::cycleRoom(int delta, int roomCount) {
    if (roomCount <= 0) {
        isolatedRoom_ = -1;
        return;
    }
    if (isolatedRoom_ < 0) {
        isolatedRoom_ = delta > 0 ? 0 : roomCount - 1;
        return;
    }
    isolatedRoom_ += delta;
    if (isolatedRoom_ < 0 || isolatedRoom_ >= roomCount) {
        isolatedRoom_ = -1;
    }
}

void Renderer::drawBatches(bool alphaPass) {
    for (int pass = 0; pass < 2; ++pass) {
    const std::vector<DrawBatch>& list = pass == 0 ? batches_ : animBatches_;
    for (std::size_t i = 0; i < list.size(); ++i) {
        const DrawBatch& b = list[i];
        if (b.alphaTest != alphaPass) {
            continue;
        }
        if (b.dynamic && !showDynamic_) {
            continue;
        }
        if (b.service && !showService_) {
            continue;
        }
        if (isolatedRoom_ >= 0 && b.roomId >= 0 && b.roomId != isolatedRoom_) {
            continue;
        }
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, b.diffuse);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, b.lightmap);
        glUniform1i(glGetUniformLocation(meshProgram_, "uAlphaTest"), b.alphaTest ? 1 : 0);
        glUniform1i(glGetUniformLocation(meshProgram_, "uVertexLit"), b.vertexLit ? 1 : 0);
        glUniform1f(glGetUniformLocation(meshProgram_, "uAlphaRef"), 15.0f / 255.0f);
        if (b.alphaTest || b.detailOffset > 0) {
            glEnable(GL_POLYGON_OFFSET_FILL);
            const float units = -1.0f * static_cast<float>(1 + b.detailOffset);
            glPolygonOffset(units, units);
        } else {
            glDisable(GL_POLYGON_OFFSET_FILL);
        }
        glDepthMask(b.writesZ ? GL_TRUE : GL_FALSE);
        if (b.alphaTest || b.vertexLit) {
            glDisable(GL_CULL_FACE);
        } else {
            glEnable(GL_CULL_FACE);
        }
        glBindVertexArray(b.vao);
        glDrawElements(GL_TRIANGLES, b.indexCount, GL_UNSIGNED_INT, 0);
    }
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_POLYGON_OFFSET_FILL);
}

void Renderer::render(const Mat4& view, const Vec3& cameraPos) {
    (void)cameraPos;
    const float aspect = static_cast<float>(width_) / static_cast<float>(height_);
    const Mat4 proj = perspectiveRH(toRadians(70.0f), aspect, 0.05f, 400.0f);
    const Mat4 vp = multiply(proj, view);

    glViewport(0, 0, width_, height_);
    glClearColor(0.02f, 0.02f, 0.03f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glPolygonMode(GL_FRONT_AND_BACK, wireframe_ ? GL_LINE : GL_FILL);

    glUseProgram(meshProgram_);
    glUniformMatrix4fv(glGetUniformLocation(meshProgram_, "uViewProj"), 1, GL_FALSE, vp.m);
    glUniform1i(glGetUniformLocation(meshProgram_, "uDiffuse"), 0);
    glUniform1i(glGetUniformLocation(meshProgram_, "uLightmap"), 1);
    glUniform1i(glGetUniformLocation(meshProgram_, "uMode"), static_cast<int>(shading_));
    glUniform1f(glGetUniformLocation(meshProgram_, "uLmScale"), 2.0f);
    glUniform1i(glGetUniformLocation(meshProgram_, "uVertexLit"), 0);
    glUniform1f(glGetUniformLocation(meshProgram_, "uAlphaRef"), 0.45f);
    if (recordingAnimated_) {
        uploadAnimated();
    }
    drawBatches(false);
    drawBatches(true);
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glBindVertexArray(0);

    if (showHelpers_ && lineCount_ > 0) {
        glUseProgram(lineProgram_);
        glUniformMatrix4fv(glGetUniformLocation(lineProgram_, "uViewProj"), 1, GL_FALSE, vp.m);
        glBindVertexArray(lineVao_);
        glDrawArrays(GL_LINES, 0, lineCount_);
        glBindVertexArray(0);
    }
}

void Renderer::drawHudText(int x, int y, const char* text, float r, float g, float b) {
    if (text == 0 || width_ <= 0 || height_ <= 0) {
        return;
    }
    const float gw = 8.0f;
    const float gh = 12.0f;
    const float invW = 2.0f / static_cast<float>(width_);
    const float invH = 2.0f / static_cast<float>(height_);
    int cx = x;
    for (const char* p = text; *p; ++p) {
        if (*p == '\n') {
            cx = x;
            y += static_cast<int>(gh + 2);
            continue;
        }
        int ch = static_cast<unsigned char>(*p);
        if (ch < 32 || ch > 127) {
            ch = '?';
        }
        ch -= 32;
        const float u0 = static_cast<float>(ch % 16) / 16.0f;
        const float v0 = static_cast<float>(ch / 16) / 6.0f;
        const float u1 = u0 + 1.0f / 16.0f;
        const float v1 = v0 + 1.0f / 6.0f;
        const float x0 = static_cast<float>(cx) * invW - 1.0f;
        const float y0 = 1.0f - static_cast<float>(y) * invH;
        const float x1 = static_cast<float>(cx + static_cast<int>(gw)) * invW - 1.0f;
        const float y1 = 1.0f - static_cast<float>(y + static_cast<int>(gh)) * invH;
        const float quad[6][7] = {
            {x0, y0, u0, v0, r, g, b}, {x0, y1, u0, v1, r, g, b}, {x1, y0, u1, v0, r, g, b},
            {x1, y0, u1, v0, r, g, b}, {x0, y1, u0, v1, r, g, b}, {x1, y1, u1, v1, r, g, b},
        };
        for (int i = 0; i < 6; ++i) {
            for (int k = 0; k < 7; ++k) {
                hudVerts_.push_back(quad[i][k]);
            }
        }
        cx += static_cast<int>(gw);
    }
}

void Renderer::presentHud() { flushHud(); }

void Renderer::flushHud() {
    if (!showHud_ || hudVerts_.empty()) {
        hudVerts_.clear();
        return;
    }
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDepthMask(GL_TRUE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(fontProgram_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fontTex_);
    glUniform1i(glGetUniformLocation(fontProgram_, "uFont"), 0);
    glBindVertexArray(hudVao_);
    glBindBuffer(GL_ARRAY_BUFFER, hudVbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(hudVerts_.size() * sizeof(float)), &hudVerts_[0],
                 GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(hudVerts_.size() / 7));
    glBindVertexArray(0);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    hudVerts_.clear();
}

}  // namespace maxfx
