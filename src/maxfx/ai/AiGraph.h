#ifndef MAXFX_AI_AIGRAPH_H
#define MAXFX_AI_AIGRAPH_H

#include "maxfx/core/Math.h"
#include "maxfx/ldb/Ldb.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace maxfx {

// The pre-calculated AI movement network (`.ai` file next to each .ldb,
// written by X_GlobalAIObject::save). Full on-disk spec: docs/AI_FORMAT.md.
//
// The file stores node positions in room-local space. On load we resolve
// every node into WORLD space (same space as LDB entity transforms):
//   room grid node:  roomMatrix(room) * position
//   exit-hull node:  roomMatrix(dstRoom) * arrayTransform(dstRoom) * position
// so pathfinding works on plain world-space points.

struct AiNode {
    int id;            // global node id (58..13505 in the sample)
    int arrayIndex;    // owning array (exit hull or room grid)
    int roomIndex;     // the room the array lives in
    bool isExit;       // true for exit-hull nodes
    Vec3 world;        // resolved world position (LDB space)
    float safeArea;    // 0..1 openness heuristic (calculateSafeNodeArea)
    int exitListIndex; // reachability table row (not needed for plain A*)
    std::vector<int> links;      // global node ids of the forward links
    std::vector<float> linkDist; // per-direction walkable distance (metres)
};

struct AiRoomNetwork {
    int roomIndex;
    std::vector<int> nodeIds;  // grid nodes of this room (world-resolved)
    // (exitArrayID, destinationRoomID) pairs, kept for completeness.
    std::vector<std::pair<int, int> > exits;
};

struct AiGraph {
    bool loaded;
    std::string sourcePath;
    std::vector<AiNode> nodes;         // load-order indexed
    std::map<int, int> idToIndex;      // global node id -> nodes[] index
    std::vector<AiRoomNetwork> rooms;  // per-room networks

    AiGraph() : loaded(false) {}

    std::size_t nodeCount() const { return nodes.size(); }

    // Closest graph node to a world point (optional same-room preference).
    // Returns -1 when the graph is empty or nothing is within `maxDist`.
    int nearestNode(const Vec3& worldPos, int roomHint = -1, float maxDist = 6.0f) const;

    // A* over the forward links. Appends world-space waypoints (excluding
    // `from`, including the final node) to `out`; returns false when no
    // route exists.
    bool findPath(const Vec3& from, const Vec3& to, std::vector<Vec3>* out) const;

private:
    friend AiGraph loadAiFile(const std::string& path, const Level& level);
    struct ArrayHeader {
        int roomIndex;
        bool isExit;
    };
    std::vector<ArrayHeader> arrayHeaders_;
};

// Parse `<level>.ai`. `level` supplies the room transforms used to resolve
// node world positions. Returns an unloaded graph when the file is missing
// or malformed (the engine likewise continues without a network).
AiGraph loadAiFile(const std::string& path, const Level& level);

}  // namespace maxfx

#endif  // MAXFX_AI_AIGRAPH_H
