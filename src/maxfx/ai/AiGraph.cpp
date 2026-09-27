#include "maxfx/ai/AiGraph.h"

#include "maxfx/core/Fs.h"
#include "maxfx/core/Stream.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace maxfx {

namespace {

// Room placement: the first static mesh's transform (the same anchor
// Renderer::roomMatrix / spawnActors use).
Mat4x3 roomMatrixOf(const Level& level, int roomId) {
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

struct RawNode {
    int id;
    Vec3 position;  // array-local
    float safeArea;
    int exitListIndex;
    std::vector<int> links;      // node ids (0 = empty slot)
    std::vector<float> linkDist;
};

struct RawArray {
    int roomIndex;
    bool isExit;
    std::vector<RawNode> nodes;
    // room -> transform into that room's space
    std::vector<std::pair<int, Mat4x3> > transforms;
    Vec3 hullMin;
    Vec3 hullMax;
};

}  // namespace

AiGraph loadAiFile(const std::string& path, const Level& level) {
    AiGraph graph;
    graph.sourcePath = path;
    const std::vector<std::uint8_t> data = readFileBytes(path);
    if (data.size() < 4) {
        return graph;  // engine: "AI network file not found" -> play without
    }
    TaggedReader in(&data[0], data.size());
    try {
        const int magic = in.readInt();
        if (magic != 123) {
            return graph;  // "AI network file is incompatible"
        }
        // --- exit-hull arrays ---
        const int exitArrayCount = in.readInt();
        std::vector<RawArray> arrays;
        arrays.reserve(static_cast<std::size_t>(exitArrayCount > 0 ? exitArrayCount : 0));
        for (int a = 0; a < exitArrayCount; ++a) {
            RawArray arr;
            arr.isExit = true;
            arr.roomIndex = 0;
            const int nodeCount = in.readInt();
            for (int n = 0; n < nodeCount; ++n) {
                RawNode node;
                node.id = in.readInt();
                in.readInt();  // linkCapacity (2 exit / 4 grid) — informational
                node.position = in.readVec3();
                for (int s = 0; s < 4; ++s) {  // ALWAYS four slots
                    const int linkId = in.readInt();
                    const float dist = in.readFloat();
                    if (linkId != 0) {
                        node.links.push_back(linkId);
                        node.linkDist.push_back(dist);
                    }
                }
                node.safeArea = in.readFloat();
                node.exitListIndex = in.readInt();
                const int reverseCount = in.readInt();
                for (int r = 0; r < reverseCount; ++r) {
                    in.readInt();
                }
                arr.nodes.push_back(node);
            }
            arr.roomIndex = in.readInt();
            in.readFloat();  // gridDensity (FLT_MAX = room has no grid)
            in.readFloat();  // rayEpsilon
            in.readFloat();  // maxLinkHeight
            in.readFloat();  // maxSafeWalk
            in.readVec3();   // gravity
            in.readFloat();  // capsuleTop
            in.readFloat();  // capsuleBottom
            in.readFloat();  // capsuleRadius
            const int transformCount = in.readInt();
            for (int t = 0; t < transformCount; ++t) {
                const int room = in.readInt();
                arr.transforms.push_back(std::make_pair(room, in.readMat4x3()));
            }
            arr.hullMin = in.readVec3();
            arr.hullMax = in.readVec3();
            const int exitListCount = in.readInt();
            for (int e = 0; e < exitListCount; ++e) {
                const int idCount = in.readInt();
                for (int i = 0; i < idCount; ++i) {
                    in.readInt();
                }
            }
            arrays.push_back(arr);
        }
        // --- per-room networks ---
        const int networkCount = in.readInt();
        std::vector<RawArray> networks;
        networks.reserve(static_cast<std::size_t>(networkCount > 0 ? networkCount : 0));
        for (int a = 0; a < networkCount; ++a) {
            RawArray arr;
            arr.isExit = false;
            const int nodeCount = in.readInt();
            for (int n = 0; n < nodeCount; ++n) {
                RawNode node;
                node.id = in.readInt();
                in.readInt();
                node.position = in.readVec3();
                for (int s = 0; s < 4; ++s) {
                    const int linkId = in.readInt();
                    const float dist = in.readFloat();
                    if (linkId != 0) {
                        node.links.push_back(linkId);
                        node.linkDist.push_back(dist);
                    }
                }
                node.safeArea = in.readFloat();
                node.exitListIndex = in.readInt();
                const int reverseCount = in.readInt();
                for (int r = 0; r < reverseCount; ++r) {
                    in.readInt();
                }
                arr.nodes.push_back(node);
            }
            arr.roomIndex = in.readInt();
            in.readFloat();
            in.readFloat();
            in.readFloat();
            in.readFloat();
            in.readVec3();
            in.readFloat();
            in.readFloat();
            in.readFloat();
            const int transformCount = in.readInt();
            for (int t = 0; t < transformCount; ++t) {
                const int room = in.readInt();
                arr.transforms.push_back(std::make_pair(room, in.readMat4x3()));
            }
            arr.hullMin = in.readVec3();
            arr.hullMax = in.readVec3();
            const int exitListCount = in.readInt();
            for (int e = 0; e < exitListCount; ++e) {
                const int idCount = in.readInt();
                for (int i = 0; i < idCount; ++i) {
                    in.readInt();
                }
            }
            // X_RoomMovementNetwork::save writes the array record (which
            // already contains roomIndex) and then the room index AGAIN,
            // followed by the exit list.
            AiRoomNetwork roomNet;
            roomNet.roomIndex = in.readInt();  // duplicated roomIndex
            const int exitCount = in.readInt();
            for (int e = 0; e < exitCount; ++e) {
                const int exitArrayID = in.readInt();
                const int destRoom = in.readInt();
                roomNet.exits.push_back(std::make_pair(exitArrayID, destRoom));
            }
            networks.push_back(arr);
            graph.rooms.push_back(roomNet);
        }
        // --- resolve world positions ---
        for (std::size_t a = 0; a < arrays.size(); ++a) {
            for (std::size_t n = 0; n < arrays[a].nodes.size(); ++n) {
                AiNode node;
                node.id = arrays[a].nodes[n].id;
                node.arrayIndex = static_cast<int>(a);
                node.roomIndex = arrays[a].roomIndex;
                node.isExit = arrays[a].isExit;
                node.safeArea = arrays[a].nodes[n].safeArea;
                node.exitListIndex = arrays[a].nodes[n].exitListIndex;
                node.links = arrays[a].nodes[n].links;
                node.linkDist = arrays[a].nodes[n].linkDist;
                if (arrays[a].isExit) {
                    // Exit hulls sit between rooms; a transform maps the
                    // hull into a destination room's local space.
                    Mat4x3 world;
                    bool placed = false;
                    for (std::size_t t = 0; t < arrays[a].transforms.size(); ++t) {
                        world = combine(roomMatrixOf(level, arrays[a].transforms[t].first),
                                        arrays[a].transforms[t].second);
                        placed = true;
                        break;  // first destination room is deterministic
                    }
                    if (!placed) {
                        world = roomMatrixOf(level, arrays[a].roomIndex);
                    }
                    node.world = transformPoint(world, arrays[a].nodes[n].position);
                } else {
                    const Mat4x3 world = roomMatrixOf(level, arrays[a].roomIndex);
                    node.world = transformPoint(world, arrays[a].nodes[n].position);
                }
                graph.idToIndex[node.id] = static_cast<int>(graph.nodes.size());
                graph.nodes.push_back(node);
            }
        }
        for (std::size_t a = 0; a < networks.size(); ++a) {
            AiRoomNetwork& roomNet = graph.rooms[a];
            for (std::size_t n = 0; n < networks[a].nodes.size(); ++n) {
                AiNode node;
                node.id = networks[a].nodes[n].id;
                node.arrayIndex = static_cast<int>(arrays.size() + a);
                node.roomIndex = networks[a].roomIndex;
                node.isExit = false;
                node.safeArea = networks[a].nodes[n].safeArea;
                node.exitListIndex = networks[a].nodes[n].exitListIndex;
                node.links = networks[a].nodes[n].links;
                node.linkDist = networks[a].nodes[n].linkDist;
                const Mat4x3 world = roomMatrixOf(level, networks[a].roomIndex);
                node.world = transformPoint(world, networks[a].nodes[n].position);
                graph.idToIndex[node.id] = static_cast<int>(graph.nodes.size());
                graph.nodes.push_back(node);
                roomNet.nodeIds.push_back(node.id);
            }
        }
        graph.loaded = !graph.nodes.empty();
    } catch (...) {
        // Truncated / shifted stream: the engine throws
        // X_GlobalAIObjectException and plays without a network; so do we.
        graph = AiGraph();
        graph.sourcePath = path;
    }
    return graph;
}

int AiGraph::nearestNode(const Vec3& worldPos, int roomHint, float maxDist) const {
    if (!loaded) {
        return -1;
    }
    int best = -1;
    float bestDist = maxDist * maxDist;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const Vec3 d = nodes[i].world - worldPos;
        const float distSq = d.x * d.x + d.y * d.y + d.z * d.z;
        const bool sameRoom = roomHint >= 0 && nodes[i].roomIndex == roomHint &&
                              !nodes[i].isExit;
        const float bias = sameRoom ? 0.0f : 2.25f;  // 1.5 m penalty for other rooms
        const float score = distSq + bias;
        if (score < bestDist) {
            bestDist = score;
            best = static_cast<int>(i);
        }
    }
    return best;
}

bool AiGraph::findPath(const Vec3& from, const Vec3& to, std::vector<Vec3>* out) const {
    out->clear();
    if (!loaded) {
        return false;
    }
    int start = nearestNode(from, -1, 8.0f);
    int goal = nearestNode(to, -1, 8.0f);
    if (start < 0 || goal < 0) {
        return false;
    }
    if (start == goal) {
        out->push_back(nodes[static_cast<std::size_t>(goal)].world);
        return true;
    }
    const std::size_t n = nodes.size();
    std::vector<float> g(n, 1.0e30f);
    std::vector<int> from_(n, -1);
    std::vector<char> closed(n, 0);
    std::vector<int> open;
    g[static_cast<std::size_t>(start)] = 0.0f;
    open.push_back(start);
    const Vec3 goalPos = nodes[static_cast<std::size_t>(goal)].world;
    while (!open.empty()) {
        // pick the open node with the lowest f = g + h
        std::size_t bestIdx = 0;
        float bestF = 1.0e30f;
        for (std::size_t i = 0; i < open.size(); ++i) {
            const int ni = open[i];
            const Vec3 d = nodes[static_cast<std::size_t>(ni)].world - goalPos;
            const float f = g[static_cast<std::size_t>(ni)] + std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            if (f < bestF) {
                bestF = f;
                bestIdx = i;
            }
        }
        const int current = open[bestIdx];
        open[bestIdx] = open.back();
        open.pop_back();
        if (current == goal) {
            std::vector<Vec3> reversed;
            for (int c = goal; c != -1; c = from_[static_cast<std::size_t>(c)]) {
                reversed.push_back(nodes[static_cast<std::size_t>(c)].world);
            }
            for (std::size_t i = reversed.size(); i-- > 0;) {
                out->push_back(reversed[i]);
            }
            out->push_back(to);
            return true;
        }
        closed[static_cast<std::size_t>(current)] = 1;
        const AiNode& node = nodes[static_cast<std::size_t>(current)];
        for (std::size_t l = 0; l < node.links.size(); ++l) {
            std::map<int, int>::const_iterator it = idToIndex.find(node.links[l]);
            if (it == idToIndex.end()) {
                continue;
            }
            const int next = it->second;
            if (closed[static_cast<std::size_t>(next)]) {
                continue;
            }
            const Vec3 d = nodes[static_cast<std::size_t>(next)].world - node.world;
            const float step = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            const float tentative = g[static_cast<std::size_t>(current)] + step;
            if (tentative < g[static_cast<std::size_t>(next)]) {
                g[static_cast<std::size_t>(next)] = tentative;
                from_[static_cast<std::size_t>(next)] = current;
                open.push_back(next);
            }
        }
    }
    return false;
}

}  // namespace maxfx
