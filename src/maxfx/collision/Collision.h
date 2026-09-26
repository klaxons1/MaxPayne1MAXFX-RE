// World collision for characters and the camera.
//
// Official Max Payne 1 stores a tagged BSP (`Level::bsp`) next to the room
// list, but those vertices are a vis / partition mesh — on Part1_Level1 they
// sit metres away from spawned NPCs. Walkable floors and walls are the room
// **static meshes** (the same triangles the renderer draws).
// `X_Character::collideObjects` (Android decompile) only dispatches
// object-object pairs; the capsule vs world test is those room meshes.
//
// Queries use a uniform grid so a level with tens of thousands of triangles
// is not O(N) per character per frame.
//
// Coordinates stay in MAX-FX / LDB space (Y up, no viewer X-mirror). The
// viewer unmirrors the camera before querying.
#ifndef MAXFX_COLLISION_COLLISION_H
#define MAXFX_COLLISION_COLLISION_H

#include "maxfx/core/Math.h"
#include "maxfx/ldb/Ldb.h"

#include <map>
#include <vector>

namespace maxfx {

struct CollisionHit {
    bool hit;
    float t;
    Vec3 point;
    Vec3 normal;
    int roomId;
    int polygonId;

    CollisionHit()
        : hit(false), t(1.0e30f), point(), normal(0.0f, 1.0f, 0.0f), roomId(-1), polygonId(-1) {}
};

class CollisionWorld {
public:
    CollisionWorld();
    void clear();
    void addTriangle(const Vec3& a, const Vec3& b, const Vec3& c, int roomId, int polygonId);
    void addBsp(const BspTree& bsp);
    void addStaticMesh(const StaticMesh& mesh, const std::vector<TextureVertex>& texVerts);
    // Room static meshes in world space. This is what characters stand on.
    void addLevelGeometry(const Level& level);

    CollisionHit raycast(const Vec3& origin, const Vec3& dir, float maxDist) const;
    CollisionHit sphereOverlap(const Vec3& center, float radius) const;

    // Iterative slide. `radius` is a sphere; characters pass the capsule
    // centre (feet + (top+bottom)*0.5).
    Vec3 moveSphere(const Vec3& start, const Vec3& delta, float radius) const;

    std::size_t triangleCount() const { return tris_.size(); }

private:
    struct Tri {
        Vec3 a;
        Vec3 b;
        Vec3 c;
        Vec3 n;
        int roomId;
        int polygonId;
    };

    void markDirty();
    void ensureGrid() const;
    long long packCell(int x, int y, int z) const;
    void cellOf(const Vec3& p, int* x, int* y, int* z) const;
    void gatherCell(int x, int y, int z, std::vector<int>* out) const;

    std::vector<Tri> tris_;
    float cellSize_;
    mutable bool gridDirty_;
    mutable std::map<long long, std::vector<int> > cells_;
    mutable std::vector<unsigned int> stamp_;
    mutable unsigned int stampGen_;
};

// Room's first static mesh transform, matching the viewer.
Mat4x3 roomWorldTransform(const Level& level, int roomId);

}  // namespace maxfx

#endif  // MAXFX_COLLISION_COLLISION_H
