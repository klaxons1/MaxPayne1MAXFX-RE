#ifndef MAXFX_VIEWER_RENDERER_H
#define MAXFX_VIEWER_RENDERER_H

#include "maxfx/core/Math.h"
#include "maxfx/db/Database.h"
#include "maxfx/ldb/Ldb.h"
#include "viewer/GL.h"

#include <map>
#include <string>
#include <vector>

namespace maxfx {

enum ShadingMode {
    kShadeLit = 0,       // diffuse * lightmap * 2
    kShadeDiffuse = 1,   // albedo only
    kShadeLightmap = 2,  // lightmap only
    kShadeVertex = 3     // radiosity / point-light vertex colour
};

struct DrawBatch {
    GLuint vao;
    GLuint vbo;
    GLuint ebo;
    GLuint diffuse;
    GLuint lightmap;
    int indexCount;
    int roomId;
    bool alphaTest;
    bool blend;         // materials.txt BlendedAlphaTest
    bool dynamic;
    bool service;       // materials.txt DrawPolygons = FALSE
    bool writesZ;
    bool vertexLit;     // KF2 entities (no lightmap)
    bool followCamera;  // world sphere, translated in the VS
    int detailOffset;   // materials.txt DetailOffset, plus alpha decals
    int alphaRef;       // 0..255, materials.txt AlphaReference
};

struct LineVertex {
    float x, y, z;
    float r, g, b, a;
};

struct SpawnPoint {
    std::string name;
    Vec3 position;
    float yaw;
    int roomId;
};

class Renderer {
public:
    Renderer();
    ~Renderer();

    bool init(char* error, std::size_t errorSize);
    bool loadLevel(const Level& level, const Database* database, char* error, std::size_t errorSize);
    void clearLevel();
    void shutdown();

    void beginAnimated();
    int appendAnimatedCharacter(const Kf2File& mesh, const Kf2File* skin, const Kf2File* bindAnim,
                                const Kf2File* playAnim, float timeSeconds, const Mat4x3& entity,
                                int roomId);

    void resize(int width, int height);
    void render(const Mat4& view, const Vec3& cameraPos);

    void setShading(ShadingMode mode) { shading_ = mode; }
    ShadingMode shading() const { return shading_; }
    void cycleShading();

    void setWireframe(bool on) { wireframe_ = on; }
    bool wireframe() const { return wireframe_; }

    void setShowDynamic(bool on) { showDynamic_ = on; }
    bool showDynamic() const { return showDynamic_; }

    void setShowHelpers(bool on) { showHelpers_ = on; }
    bool showHelpers() const { return showHelpers_; }

    void setShowService(bool on) { showService_ = on; }
    bool showService() const { return showService_; }

    void setShowHud(bool on) { showHud_ = on; }
    bool showHud() const { return showHud_; }

    unsigned int entityMeshCount() const { return entityMeshCount_; }
    unsigned int entityTriangleCount() const { return entityTriangleCount_; }
    unsigned int entityPlaceholderCount() const { return entityPlaceholderCount_; }

    // -1 = all rooms
    void setIsolatedRoom(int roomId) { isolatedRoom_ = roomId; }
    int isolatedRoom() const { return isolatedRoom_; }
    void cycleRoom(int delta, int roomCount);

    unsigned int triangleCount() const { return triangleCount_; }
    unsigned int batchCount() const { return static_cast<unsigned int>(batches_.size()); }
    const std::vector<SpawnPoint>& spawns() const { return spawns_; }

    void drawHudText(int x, int y, const char* text, float r, float g, float b);
    void presentHud();

private:
    struct GpuMesh {
        std::vector<float> vertices;  // pos3 nrm3 uv2 lm2 color3
        std::vector<unsigned int> indices;
    };

    struct BatchKey {
        GLuint diffuse;
        GLuint lightmap;
        int roomId;
        bool alphaTest;
        bool blend;
        bool dynamic;
        bool service;
        bool writesZ;
        bool vertexLit;
        bool followCamera;
        int detailOffset;
        int alphaRef;

        bool operator==(const BatchKey& o) const {
            return diffuse == o.diffuse && lightmap == o.lightmap && roomId == o.roomId &&
                   alphaTest == o.alphaTest && blend == o.blend && dynamic == o.dynamic &&
                   service == o.service && writesZ == o.writesZ && vertexLit == o.vertexLit &&
                   followCamera == o.followCamera && detailOffset == o.detailOffset &&
                   alphaRef == o.alphaRef;
        }
    };

    struct WorldLight {
        Vec3 position;
        Vec3 color;
        float intensity;
        float falloff;
    };

    GLuint uploadTexture(const unsigned char* rgba, int w, int h, bool mipmaps, bool clamp);
    GLuint makeSolidTexture(unsigned char r, unsigned char g, unsigned char b);
    void appendMesh(const std::vector<Vec3>& vertices, const std::vector<Vec3>& normals,
                    const std::vector<TextureVertex>& texVerts, const std::vector<Polygon>& polygons,
                    const Mat4x3& transform, int roomId, bool dynamic, const Level& level,
                    const Database* database, const std::vector<GLuint>& materialTextures,
                    const std::vector<GLuint>& lightmapTextures,
                    const std::vector<RadiositySample>& radiosity,
                    const std::vector<WorldLight>& lights);
    int appendKf2File(const Kf2File& kf, const Mat4x3& entity, int roomId,
                      const std::vector<WorldLight>& lights, const Database* database);
    void appendKf2Mesh(const Kf2DrawMesh& mesh, const Mat4x3& world, int roomId,
                       const std::vector<WorldLight>& lights, const Database* database,
                       const std::string& modelDir);
    void appendOrientedBox(const Mat4x3& entity, int roomId, float hx, float hy, float hz,
                           const Vec3& color);
    GpuMesh& batchFor(const BatchKey& key);
    void destroyAnimatedGpu();
    void uploadAnimated();
    Vec3 shadeVertex(const Vec3& worldPos, const Vec3& worldNrm, const std::vector<WorldLight>& lights) const;
    GLuint textureFromFile(const std::string& path);
    GLuint textureFromColorAlpha(const std::string& colorPath, const std::string& alphaPath);
    void uploadBatches();
    void buildHelpers(const Level& level);
    void buildFont();
    void drawBatches(bool alphaPass, const Vec3& cameraPos);
    void flushHud();

    GLuint meshProgram_;
    GLuint lineProgram_;
    GLuint fontProgram_;
    GLuint whiteTex_;
    GLuint greyTex_;
    GLuint fontTex_;

    void clearLevelGpu();

    std::vector<GLuint> ownedTextures_;
    std::size_t persistentTextureCount_;
    std::vector<DrawBatch> batches_;
    std::vector<GpuMesh> cpuBatches_;
    std::vector<BatchKey> cpuKeys_;

    GLuint lineVao_;
    GLuint lineVbo_;
    int lineCount_;

    GLuint hudVao_;
    GLuint hudVbo_;
    std::vector<float> hudVerts_;

    ShadingMode shading_;
    bool wireframe_;
    bool showDynamic_;
    bool showHelpers_;
    bool showService_;
    bool showHud_;
    int isolatedRoom_;
    int width_;
    int height_;
    unsigned int triangleCount_;
    unsigned int entityMeshCount_;
    unsigned int entityTriangleCount_;
    unsigned int entityPlaceholderCount_;
    std::vector<SpawnPoint> spawns_;

    const Database* database_;
    std::vector<WorldLight> lights_;
    std::map<std::string, GLuint> textureByPath_;
    bool recordingAnimated_;
    bool recordingSky_;
    std::vector<GpuMesh> animCpu_;
    std::vector<BatchKey> animKeys_;
    std::vector<DrawBatch> animBatches_;
};

}  // namespace maxfx

#endif  // MAXFX_VIEWER_RENDERER_H
