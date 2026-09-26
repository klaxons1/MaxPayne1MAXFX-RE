// KF2 / KFS / SKD (MAX-FX model, character mesh, skin weights).
//
// On disk these are the same chunked tagged stream as LDB, with a 13-byte
// chunk header `{0x0C, u32 id, u32 version, u32 payloadSize}` wrapping each
// block. Payload fields use the shared TaggedReader.
//
// Layout taken from the community KF2 specification (m0nstr0) cross-checked
// against Android libMaxPayne.so `KF2::*` names. PC Max Payne 1 writes the
// same chunks; the Android port is only used for names.
#ifndef MAXFX_KF2_KF2_H
#define MAXFX_KF2_KF2_H

#include "maxfx/core/Math.h"

#include <cstdint>
#include <string>
#include <vector>

namespace maxfx {

enum Kf2ChunkId {
    kKf2Node = 0x00010000,
    kKf2Camera = 0x00010001,
    kKf2PointLight = 0x00010002,
    kKf2DirectionalLight = 0x00010003,
    kKf2SpotLight = 0x00010004,
    kKf2Mesh = 0x00010005,
    kKf2Geometry = 0x00010006,
    kKf2Polygons = 0x00010007,
    kKf2Polygon = 0x00010008,
    kKf2Smoothing = 0x0001000B,
    kKf2PolygonMaterial = 0x0001000C,
    kKf2UvMapping = 0x0001000E,
    kKf2MaterialList = 0x0001000F,
    kKf2Material = 0x00010010,
    kKf2Texture = 0x00010011,
    kKf2KeyframeAnimation = 0x00010012,
    kKf2Animation = 0x00010013,
    kKf2Skin = 0x00010014,
    kKf2Environment = 0x00010015,
    kKf2Helper = 0x00010016,
    kKf2PointLightAnimation = 0x00010017,
    kKf2DirectionalLightAnimation = 0x00010018,
    kKf2SpotLightAnimation = 0x00010019,
    kKf2ReferenceToData = 0x0001001A
};

enum { kKf2ChunkTag = 0x0C };

struct Kf2Texture {
    int version;
    std::string name;
    int mipMaps;
    int filtering;
    std::vector<std::string> files;

    Kf2Texture() : version(0), mipMaps(0), filtering(0) {}
};

struct Kf2Material {
    int version;
    std::string name;
    bool twoSided;
    bool fogging;
    bool diffuseCombined;
    bool invisibleGeometry;
    bool hasVertexAlpha;
    int diffuseColorType;
    int specularColorType;
    int litType;
    Vec4 ambient;
    Vec4 diffuse;
    Vec4 specular;
    float vertexAlpha;
    float specularExponent;
    int diffuseTextureType;
    int reflectionTextureType;
    float embossFactor;
    bool hasLit;
    Kf2Texture diffuseTexture;
    Kf2Texture reflectionTexture;
    Kf2Texture bumpTexture;
    Kf2Texture opacityTexture;
    Kf2Texture maskTexture;
    bool hasDiffuseTexture;
    bool hasReflectionTexture;
    bool hasBumpTexture;
    bool hasOpacityTexture;
    bool hasMaskTexture;

    Kf2Material()
        : version(0),
          twoSided(false),
          fogging(false),
          diffuseCombined(false),
          invisibleGeometry(false),
          hasVertexAlpha(false),
          diffuseColorType(0),
          specularColorType(0),
          litType(0),
          ambient(1, 1, 1, 1),
          diffuse(1, 1, 1, 1),
          specular(0, 0, 0, 1),
          vertexAlpha(1.0f),
          specularExponent(0.0f),
          diffuseTextureType(0),
          reflectionTextureType(0),
          embossFactor(0.0f),
          hasLit(false),
          hasDiffuseTexture(false),
          hasReflectionTexture(false),
          hasBumpTexture(false),
          hasOpacityTexture(false),
          hasMaskTexture(false) {}
};

struct Kf2MaterialList {
    int version;
    std::string textureDirs;  // semicolon-separated, relative to the model file
    std::vector<Kf2Material> materials;

    Kf2MaterialList() : version(0) {}
};

struct Kf2Node {
    int version;
    std::string name;
    std::string parentName;
    Mat4x3 objectToParent;
    bool hasParent;
    std::string userDefined;

    Kf2Node() : version(0), hasParent(false) {}
};

struct Kf2Geometry {
    int version;
    std::vector<Vec3> vertices;
    std::vector<Vec3> normals;
    std::vector<int> verticesPerPrimitive;

    Kf2Geometry() : version(0) {}
};

struct Kf2Polygons {
    int version;
    std::vector<int> indices;  // triangle list (3 indices per face)
    std::vector<int> polygonsPerPrimitive;

    Kf2Polygons() : version(0) {}
};

struct Kf2PolygonMaterial {
    int version;
    std::vector<std::string> names;
    std::vector<int> materialIndexForPolygon;  // one per triangle

    Kf2PolygonMaterial() : version(0) {}
};

struct Kf2UvMapping {
    int version;
    int layer;
    std::vector<Vec3> coordinates;
    std::vector<int> coordinatesPerPrimitive;
    std::vector<int> uvIndices;  // 3 per triangle, version 0 only

    Kf2UvMapping() : version(0), layer(0) {}
};

struct Kf2Mesh {
    int version;
    Kf2Node node;
    bool hasNode;
    Kf2Geometry geometry;
    bool hasGeometry;
    Kf2Polygons polygons;
    bool hasPolygons;
    Kf2PolygonMaterial polygonMaterial;
    bool hasPolygonMaterial;
    std::vector<Kf2UvMapping> uvMapping;
    std::string referenceToData;
    std::vector<unsigned int> smoothingGroups;

    Kf2Mesh()
        : version(0),
          hasNode(false),
          hasGeometry(false),
          hasPolygons(false),
          hasPolygonMaterial(false) {}
};

struct Kf2SkinVertex {
    int vertexIndex;
    std::vector<int> bones;
    std::vector<float> weights;

    Kf2SkinVertex() : vertexIndex(0) {}
};

struct Kf2Skin {
    int version;
    std::vector<std::string> skinObjectNames;
    std::vector<std::string> skeletonObjectNames;
    std::vector<Kf2SkinVertex> vertices;

    Kf2Skin() : version(0) {}
};

struct Kf2AnimKey {
    int frame;
    Mat4x3 objectToParent;

    Kf2AnimKey() : frame(0) {}
};

struct Kf2VisibilityKey {
    int frame;
    float visibility;

    Kf2VisibilityKey() : frame(0), visibility(1.0f) {}
};

// One bone channel. PC files emit one kKf2KeyframeAnimation chunk per node
// (see KeyframeAnimationChunk::operator>> in the Android decompile).
struct Kf2NodeAnimation {
    int version;
    std::string targetName;
    std::string parentName;
    int frameRate;
    bool looping;
    bool loopInterpolation;
    int totalKeyframeCount;
    int loopToFrame;
    int interpolationMethod;
    bool maintainMatrixScaling;
    std::vector<Kf2AnimKey> keys;
    std::vector<Kf2VisibilityKey> visibility;

    Kf2NodeAnimation()
        : version(0),
          frameRate(30),
          looping(false),
          loopInterpolation(false),
          totalKeyframeCount(0),
          loopToFrame(0),
          interpolationMethod(1),
          maintainMatrixScaling(false) {}
};

struct Kf2File {
    std::string sourcePath;
    std::vector<Kf2MaterialList> materialLists;
    std::vector<Kf2Mesh> meshes;
    std::vector<Kf2Skin> skins;
    std::vector<Kf2NodeAnimation> animations;
    int skippedChunks;
    int unknownChunks;

    Kf2File() : skippedChunks(0), unknownChunks(0) {}
};

// Flattened triangle mesh ready for a renderer or a future engine port.
struct Kf2DrawVertex {
    Vec3 position;
    Vec3 normal;
    Vec2 uv;
};

struct Kf2DrawPart {
    std::string materialName;
    bool twoSided;
    bool invisible;
    Vec4 diffuseColor;
    std::vector<std::string> textureFiles;  // relative names from the KF2 material
    std::vector<std::string> opacityFiles;  // separate opacity map when present
    std::vector<Kf2DrawVertex> vertices;    // triangle list
};

struct Kf2DrawMesh {
    std::string nodeName;
    std::string parentName;
    Mat4x3 objectToParent;
    std::string textureDirs;  // copied from the file's material list
    bool modelSpace;          // true after skinning / pose (renderer uses entity only)
    std::vector<Kf2DrawPart> parts;

    Kf2DrawMesh() : modelSpace(false) {}
};

class Kf2Reader {
public:
    static Kf2File loadFromFile(const std::string& path);
    static Kf2File loadFromMemory(const std::uint8_t* data, std::size_t size,
                                  const std::string& sourceName = std::string());
};

// Bind-pose world matrix for every named node (parent chain of objectToParent).
void kf2NodeWorldTransforms(const Kf2File& file, std::vector<std::string>* names,
                            std::vector<Mat4x3>* worlds);

// Triangle lists in node-local space. Meshes that only reference another
// object's geometry are expanded.
void kf2BuildDrawMeshes(const Kf2File& file, std::vector<Kf2DrawMesh>& out);

float kf2AnimationDuration(const Kf2File& file);

// Sample every bone channel at `timeSeconds` (loops when the clip says so).
void kf2SampleAnimation(const Kf2File& file, float timeSeconds, std::vector<std::string>* names,
                        std::vector<Mat4x3>* locals);

// Parent-chain worlds for an animation clip. Missing bones fall back to
// `bind` (typically CHARANIM_POSE).
void kf2BuildSkeletonWorlds(const Kf2File& anim, float timeSeconds, const Kf2File* bind,
                            std::vector<std::string>* names, std::vector<Mat4x3>* worlds);

// Linear-blend skin: KFS mesh + SKD weights + pose clip (bind) + current clip.
// Output vertices are in model space (`modelSpace = true`).
void kf2BuildSkinnedDrawMeshes(const Kf2File& meshFile, const Kf2File* skinFile,
                               const Kf2File* bindAnim, const Kf2File* playAnim, float timeSeconds,
                               std::vector<Kf2DrawMesh>& out);

inline const char* kf2ChunkName(unsigned int id) {
    switch (id) {
        case kKf2Node:
            return "node";
        case kKf2Mesh:
            return "mesh";
        case kKf2MaterialList:
            return "material_list";
        case kKf2Material:
            return "material";
        case kKf2Texture:
            return "texture";
        case kKf2Geometry:
            return "geometry";
        case kKf2Polygons:
            return "polygons";
        case kKf2Skin:
            return "skin";
        case kKf2KeyframeAnimation:
            return "keyframe_animation";
        case kKf2Environment:
            return "environment";
        default:
            return "chunk";
    }
}

}  // namespace maxfx

#endif  // MAXFX_KF2_KF2_H
