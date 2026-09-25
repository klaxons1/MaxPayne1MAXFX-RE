// In-memory representation of a Max Payne 1 / MAX-FX level database (*.ldb).
//
// The on-disk layout is versioned. PC Max Payne 1 and the Android port both
// expect version 32 (see X_LevelDBLevel::load in libMaxPayne.so:
// "X_LevelDB Wrong version, expected %d, found %d!", 32).
//
// Block order (version 32):
//   BSP, version, materials/textures, lightmaps, exits, static meshes,
//   static lights, waypoints, FSMs, characters, triggers, dynamic meshes,
//   items, point lights, rooms.
#ifndef MAXFX_LDB_LDB_H
#define MAXFX_LDB_LDB_H

#include "maxfx/core/Math.h"

#include <string>
#include <vector>

namespace maxfx {

enum { kLdbVersionPC = 32 };

enum TextureFileType {
    kTexTga = 0x00,
    kTexScx = 0x02,
    kTexPcx = 0x03,
    kTexJpg = 0x04,
    kTexDds = 0x05
};

enum WaypointType {
    kWaypoint = 0,
    kStartpoint = 1,
    kJumppoint = 2
};

enum TriggerType {
    kTriggerActionButton = 0,
    kTriggerPlayerCollide = 1,
    kTriggerProjectileCollide = 2,
    kTriggerCharacterCollide = 3,
    kTriggerLookAt = 4
};

inline const char* textureFileTypeName(int type) {
    switch (type) {
        case kTexTga:
            return "tga";
        case kTexScx:
            return "scx";
        case kTexPcx:
            return "pcx";
        case kTexJpg:
            return "jpg";
        case kTexDds:
            return "dds";
        default:
            return "unknown";
    }
}

inline const char* triggerTypeName(int type) {
    switch (type) {
        case kTriggerActionButton:
            return "action_button";
        case kTriggerPlayerCollide:
            return "player_collide";
        case kTriggerProjectileCollide:
            return "projectile_collide";
        case kTriggerCharacterCollide:
            return "character_collide";
        case kTriggerLookAt:
            return "look_at";
        default:
            return "unknown";
    }
}

struct EmbeddedImage {
    std::string fileName;  // empty for lightmaps (they are keyed by id)
    int id;
    int fileType;
    std::vector<unsigned char> data;

    EmbeddedImage() : id(-1), fileType(0) {}
};

struct Material {
    int id;
    std::string category;
    std::string name;
    int diffuseTexture;  // index into Level::textures, or -1
    int alphaTexture;
    bool alphaTest;
    bool adultContent;

    Material()
        : id(-1),
          diffuseTexture(-1),
          alphaTexture(-1),
          alphaTest(false),
          adultContent(false) {}
};

struct BspPolygon {
    int vertexStart;
    int vertexCount;
    int polygonId;
    int groupId;
    Vec3 normal;
    Vec3 pivot;

    BspPolygon() : vertexStart(0), vertexCount(0), polygonId(0), groupId(0) {}
};

struct BspHalfSpace {
    int polygonIndexStart;
    int polygonCount;
    int nextNode;

    BspHalfSpace() : polygonIndexStart(0), polygonCount(0), nextNode(0) {}
};

struct BspNode {
    Vec3 orientation;
    Vec3 position;
    BspHalfSpace front;
    BspHalfSpace back;
};

struct BspTree {
    std::vector<Vec3> vertices;
    std::vector<BspPolygon> polygons;
    std::vector<BspNode> nodes;
    std::vector<int> polygonIndices;
};

struct ExitConvexRegion {
    std::vector<int> points;
};

struct Exit {
    std::string name;
    std::vector<Vec3> vertices;
    Vec3 normal;
    Mat4x3 transform;
    int roomId;
    int destinationRoomId;
    std::string destinationExitName;
    std::vector<ExitConvexRegion> convexRegions;

    Exit() : roomId(-1), destinationRoomId(-1) {}
};

struct TextureVertex {
    int vertexIndex;
    Vec2 uv;
    Vec2 lightmapUv;
    unsigned int flags;
    bool smooth;

    TextureVertex() : vertexIndex(0), flags(0), smooth(false) {}
};

struct Polygon {
    int id;
    int textureVertexStart;
    int vertexCount;
    Vec3 normal;
    unsigned int engineMaterialType;
    int materialId;
    int lightmapId;  // -1 = none
    float maxEdgeLength;
    float maxAngle;
    int smoothingGroup;

    Polygon()
        : id(0),
          textureVertexStart(0),
          vertexCount(0),
          engineMaterialType(0),
          materialId(-1),
          lightmapId(-1),
          maxEdgeLength(0.0f),
          maxAngle(0.0f),
          smoothingGroup(0) {}
};

struct RadiositySample {
    int key;
    Vec3 value;

    RadiositySample() : key(0) {}
};

struct StaticMesh {
    int groupId;
    std::vector<Vec3> vertices;
    std::vector<Vec3> normals;
    Mat4x3 transform;
    std::vector<Polygon> polygons;
    std::vector<RadiositySample> radiosity;

    StaticMesh() : groupId(-1) {}
};

struct EntityProperties {
    std::string name;
    Mat4x3 objectToRoom;
    Mat4x3 objectToParent;
    int roomId;
    std::string parentDynamicMesh;

    EntityProperties() : roomId(-1) {}
};

struct StaticLight {
    std::string sharedName;
    EntityProperties properties;
    Mat3 rotation;
    float r, g, b, a;
    float area;
    float intensity;
    float hotspotAngle;
    float falloffAngle;
    float falloffRange;
    float colorMultiplier;

    StaticLight()
        : r(1.0f),
          g(1.0f),
          b(1.0f),
          a(1.0f),
          area(0.0f),
          intensity(1.0f),
          hotspotAngle(0.0f),
          falloffAngle(0.0f),
          falloffRange(0.0f),
          colorMultiplier(1.0f) {}
};

struct Waypoint {
    std::string sharedName;
    EntityProperties properties;
    int type;

    Waypoint() : type(0) {}
};

struct FsmMessages {
    std::vector<std::string> messages;
};

struct FsmStateSpecific {
    std::string stateName;
    FsmMessages messages;
};

struct FsmEvent {
    std::string name;
    FsmMessages before;
    std::vector<FsmStateSpecific> stateSpecific;
    FsmMessages after;
};

struct Fsm {
    std::string sharedName;
    EntityProperties properties;
    std::vector<std::string> states;
    std::string defaultState;
    FsmMessages startupBefore;
    std::vector<std::string> startupStateSpecific;  // stored as raw strings (PC v32)
    FsmMessages startupAfter;
    std::vector<FsmEvent> stateSwitch;
    std::vector<FsmEvent> customString;
    std::vector<FsmEvent> entityEvents;
};

struct Character {
    std::string sharedName;
    EntityProperties properties;
    std::string characterName;
    FsmMessages onStartup;
    FsmMessages onDeath;
    FsmMessages onActivate;
    FsmMessages onSpecial;
};

struct Trigger {
    std::string sharedName;
    EntityProperties properties;
    float radius;
    int type;

    Trigger() : radius(0.0f), type(0) {}
};

struct AnimationGraph {
    int header;
    int majorVersion;
    int minorVersion;
    std::vector<float> samples;

    AnimationGraph() : header(0), majorVersion(0), minorVersion(0) {}
};

struct MeshAnimation {
    std::string name;
    float lengthSeconds;
    Mat4x3 startTransform;
    Mat4x3 endTransform;
    FsmMessages leavingFirstKeyframe;
    FsmMessages returningFirstKeyframe;
    FsmMessages reachingSecondKeyframe;
    AnimationGraph translation;
    AnimationGraph rotation;

    MeshAnimation() : lengthSeconds(0.0f) {}
};

struct DynamicMeshConfig {
    bool dynamicCollisions;
    bool bulletCollisions;
    bool lightMapped;
    bool continuousUpdate;
    bool pointLightAffected;
    bool blockExplosions;

    DynamicMeshConfig()
        : dynamicCollisions(false),
          bulletCollisions(false),
          lightMapped(false),
          continuousUpdate(false),
          pointLightAffected(false),
          blockExplosions(false) {}
};

struct DynamicMesh {
    std::string name;
    std::vector<Vec3> vertices;
    std::vector<Vec3> normals;
    Mat4x3 transform;
    std::vector<Polygon> polygons;
    std::vector<RadiositySample> radiosity;
    EntityProperties properties;
    std::vector<MeshAnimation> animations;
    DynamicMeshConfig config;
    int bsp[4];

    DynamicMesh() { bsp[0] = bsp[1] = bsp[2] = bsp[3] = 0; }
};

struct LevelItem {
    std::string sharedName;
    EntityProperties properties;
    std::string itemName;
};

struct PointLight {
    int id;
    EntityProperties properties;
    float r, g, b, a;
    float falloff;
    float intensity;

    PointLight()
        : id(-1), r(1.0f), g(1.0f), b(1.0f), a(1.0f), falloff(0.0f), intensity(1.0f) {}
};

struct Room {
    int id;
    std::string name;
    std::vector<int> staticMeshes;
    std::vector<std::string> staticLights;
    std::vector<std::string> exits;
    std::vector<std::string> startPoints;
    std::vector<std::string> fsms;
    std::vector<std::string> characters;
    std::vector<std::string> triggers;
    std::vector<std::string> dynamicMeshes;
    std::vector<std::string> items;
    std::vector<int> pointLights;
    float aiNetDensity;
    int bsp[4];

    Room() : id(-1), aiNetDensity(0.0f) { bsp[0] = bsp[1] = bsp[2] = bsp[3] = 0; }
};

struct Level {
    int version;
    BspTree bsp;
    std::vector<EmbeddedImage> textures;
    std::vector<Material> materials;
    std::vector<EmbeddedImage> lightmaps;
    std::vector<Exit> exits;
    std::vector<TextureVertex> staticTextureVertices;
    std::vector<StaticMesh> staticMeshes;
    std::vector<StaticLight> staticLights;
    std::vector<Waypoint> waypoints;
    std::vector<Fsm> fsms;
    std::vector<Character> characters;
    std::vector<Trigger> triggers;
    std::vector<TextureVertex> dynamicTextureVertices;
    std::vector<DynamicMesh> dynamicMeshes;
    std::vector<LevelItem> items;
    std::vector<PointLight> pointLights;
    std::vector<Room> rooms;

    Level() : version(0) {}

    const StaticMesh* findStaticMesh(int groupId) const {
        for (std::size_t i = 0; i < staticMeshes.size(); ++i) {
            if (staticMeshes[i].groupId == groupId) {
                return &staticMeshes[i];
            }
        }
        return 0;
    }

    const DynamicMesh* findDynamicMesh(const std::string& name) const {
        for (std::size_t i = 0; i < dynamicMeshes.size(); ++i) {
            if (dynamicMeshes[i].name == name) {
                return &dynamicMeshes[i];
            }
        }
        return 0;
    }

    const Room* findRoom(int id) const {
        for (std::size_t i = 0; i < rooms.size(); ++i) {
            if (rooms[i].id == id) {
                return &rooms[i];
            }
        }
        return 0;
    }

    const EmbeddedImage* findLightmap(int id) const {
        if (id < 0) {
            return 0;
        }
        for (std::size_t i = 0; i < lightmaps.size(); ++i) {
            if (lightmaps[i].id == id) {
                return &lightmaps[i];
            }
        }
        return 0;
    }

    int findTextureByFileName(const std::string& fileName) const {
        for (std::size_t i = 0; i < textures.size(); ++i) {
            if (textures[i].fileName == fileName) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    const Material* findMaterialByIndex(int id) const {
        for (std::size_t i = 0; i < materials.size(); ++i) {
            if (materials[i].id == id) {
                return &materials[i];
            }
        }
        return 0;
    }
};

}  // namespace maxfx

#endif  // MAXFX_LDB_LDB_H
