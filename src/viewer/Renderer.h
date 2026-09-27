#ifndef MAXFX_VIEWER_RENDERER_H
#define MAXFX_VIEWER_RENDERER_H

#include "maxfx/core/Math.h"
#include "maxfx/db/Database.h"
#include "maxfx/game/Effects.h"
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
    // Per-batch model matrix (uWorld). Identity for pre-transformed
    // batches; level dynamic meshes keep object-space geometry and only
    // update this matrix when the animated pose changes.
    Mat4 world;
    // World-space bounds for frustum culling (hasBounds false = always draw).
    Vec3 boundsMin;
    Vec3 boundsMax;
    bool hasBounds;

    DrawBatch() : hasBounds(false) {}
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
    // Room transform of a level room (identity when unknown) — shared with
    // the dynamic-mesh streaming below.
    static Mat4x3 roomMatrix(const Level& level, int roomId);
    // Stream one dynamic level mesh (doors, platforms, ...) with its
    // animated world transform. Call between beginAnimated() and render().
    // Per-frame pose update for an uploaded dynamic mesh (no re-upload).
    void setDynamicMeshWorld(std::size_t meshIndex, const Mat4x3& world);
    int appendAnimatedCharacter(const Kf2File& mesh, const Kf2File* skin, const Kf2File* bindAnim,
                                const Kf2File* playAnim, float timeSeconds, const Mat4x3& entity,
                                int roomId, bool lockRootToBind = true, const Kf2File* crossAnim = 0,
                                float crossTimeSeconds = 0.0f, float crossBlend = 0.0f);
    int appendOverlayKf2(const Kf2File& kf, const Mat4x3& entity);

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

    void setSkipWorld(bool on) { skipWorld_ = on; }
    bool skipWorld() const { return skipWorld_; }
    void setFovY(float degrees) { fovY_ = degrees; }
    float fovY() const { return fovY_; }

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
    void drawHudQuad(int x, int y, int w, int h, float r, float g, float b, float a);
    // Textured HUD sprite (hud.txt [Sprite]): pixel coords / size in the
    // current window, anchored per hudReferencePoint(). `colorPath` loads via
    // the shared image loader (composites a sibling _alpha file); when
    // `alphaPath` is set it is merged explicitly.
    void drawHudImage(const std::string& colorPath, const std::string& alphaPath, float x, float y,
                      float w, float h, int refPoint, float alpha);
    void presentHud();

    // World-space effects pass: bullet decals (textured quads projected on the
    // hit surface) and particle billboards. `decalFiles` maps the decal
    // material name to (color, alpha) image paths.
    void renderEffects(const Mat4& view, const Vec3& cameraPos, const std::vector<Decal>& decals,
                       const std::vector<ParticleEffectInstance>& effects,
                       const std::map<std::string, std::pair<std::string, std::string> >* decalFiles);

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
        int owner;  // dynamic-mesh index: one mesh never merges into another's batch

        BatchKey() : owner(-1) {}

        bool operator==(const BatchKey& o) const {
            return diffuse == o.diffuse && lightmap == o.lightmap && roomId == o.roomId &&
                   alphaTest == o.alphaTest && blend == o.blend && dynamic == o.dynamic &&
                   service == o.service && writesZ == o.writesZ && vertexLit == o.vertexLit &&
                   followCamera == o.followCamera && detailOffset == o.detailOffset &&
                   alphaRef == o.alphaRef && owner == o.owner;
        }
    };

    struct WorldLight {
        Vec3 position;
        Vec3 color;
        float intensity;
        float falloff;
    };

    // Cached uniform locations (glGetUniformLocation per batch per frame is
    // a measurable driver cost with hundreds of batches).
    struct MeshUniforms {
        GLint viewProj;
        GLint origin;
        GLint world;
        GLint mode;
        GLint lmScale;
        GLint alphaTest;
        GLint vertexLit;
        GLint alphaRef;
        GLint diffuse;
        GLint lightmap;

        MeshUniforms() : viewProj(-1), origin(-1), world(-1), mode(-1), lmScale(-1),
                         alphaTest(-1), vertexLit(-1), alphaRef(-1), diffuse(-1), lightmap(-1) {}
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
    // Level dynamic meshes: geometry uploaded ONCE in object space; the
    // animated pose only updates DrawBatch::world (uWorld uniform), instead
    // of re-transforming and re-uploading vertices every frame.
    void uploadDynamicLevelMeshes(const Level& level);
    void destroyDynamicGpu();
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
    GLuint hudImageProgram_;
    GLuint effectProgram_;
    GLuint effectVao_;
    GLuint effectVbo_;
    GLuint particleTex_;

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
    // GPU texture ids of the loaded level (materials by list index,
    // lightmaps by id) so dynamic meshes can stream without re-uploading.
    std::vector<GLuint> levelMaterialTextures_;
    std::vector<GLuint> levelLightmapTextures_;
    std::map<std::string, GLuint> textureByPath_;
    std::map<std::string, std::vector<Kf2DrawMesh> > restKf2Draws_;
    std::map<std::string, std::vector<Kf2DrawMesh> > posedKf2Draws_;
    bool recordingAnimated_;
    bool recordingSky_;
    bool recordingUnlit_;
    bool skipWorld_;
    float fovY_;
    std::vector<GpuMesh> animCpu_;
    std::vector<BatchKey> animKeys_;
    std::vector<DrawBatch> animBatches_;
    // Persistent animated-batch state: VBO capacity (floats) so a frame
    // re-fills with glBufferSubData instead of re-specifying the buffer,
    // and the last uploaded index list (character topology is stable while
    // posing, so the EBO upload is skipped until it changes).
    std::vector<std::size_t> animVboCapacity_;
    std::vector<std::vector<unsigned int> > animLastIndices_;

    bool recordingDynamic_;
    int dynOwner_;
    std::vector<GpuMesh> dynCpu_;
    std::vector<BatchKey> dynKeys_;
    std::vector<DrawBatch> dynBatches_;
    std::vector<std::vector<int> > dynMeshBatches_;  // meshIndex -> batch indices
    std::vector<std::pair<Vec3, Vec3> > dynObjBounds_;  // batch -> object-space AABB
    MeshUniforms u_;
    Mat4 frustumMatrix_;  // last frame's view-projection for batch culling
};

}  // namespace maxfx

#endif  // MAXFX_VIEWER_RENDERER_H
