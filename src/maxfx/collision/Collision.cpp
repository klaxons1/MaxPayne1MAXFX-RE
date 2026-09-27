#include "maxfx/collision/Collision.h"

#include <cmath>
#include <algorithm>

namespace maxfx {
namespace {

const float kEpsilon = 1.0e-6f;
const float kCellSize = 2.0f;

Vec3 triNormal(const Vec3& a, const Vec3& b, const Vec3& c) {
    const Vec3 n = cross(b - a, c - a);
    const float len = length(n);
    if (len <= kEpsilon) {
        return Vec3(0.0f, 1.0f, 0.0f);
    }
    return n * (1.0f / len);
}

bool rayTriangle(const Vec3& origin, const Vec3& dir, float maxDist, const Vec3& a, const Vec3& b,
                 const Vec3& c, float* tOut, Vec3* nOut) {
    const Vec3 ab = b - a;
    const Vec3 ac = c - a;
    const Vec3 pvec = cross(dir, ac);
    const float det = dot(ab, pvec);
    if (det > -kEpsilon && det < kEpsilon) {
        return false;
    }
    const float inv = 1.0f / det;
    const Vec3 tvec = origin - a;
    const float u = dot(tvec, pvec) * inv;
    if (u < 0.0f || u > 1.0f) {
        return false;
    }
    const Vec3 qvec = cross(tvec, ab);
    const float v = dot(dir, qvec) * inv;
    if (v < 0.0f || u + v > 1.0f) {
        return false;
    }
    const float t = dot(ac, qvec) * inv;
    if (t < kEpsilon || t > maxDist) {
        return false;
    }
    *tOut = t;
    *nOut = triNormal(a, b, c);
    if (dot(*nOut, dir) > 0.0f) {
        *nOut = *nOut * -1.0f;
    }
    return true;
}

Vec3 closestOnTriangle(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c) {
    const Vec3 ab = b - a;
    const Vec3 ac = c - a;
    const Vec3 ap = p - a;
    const float d1 = dot(ab, ap);
    const float d2 = dot(ac, ap);
    if (d1 <= 0.0f && d2 <= 0.0f) {
        return a;
    }
    const Vec3 bp = p - b;
    const float d3 = dot(ab, bp);
    const float d4 = dot(ac, bp);
    if (d3 >= 0.0f && d4 <= d3) {
        return b;
    }
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        const float v = d1 / (d1 - d3);
        return a + ab * v;
    }
    const Vec3 cp = p - c;
    const float d5 = dot(ab, cp);
    const float d6 = dot(ac, cp);
    if (d6 >= 0.0f && d5 <= d6) {
        return c;
    }
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        const float w = d2 / (d2 - d6);
        return a + ac * w;
    }
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return b + (c - b) * w;
    }
    const float denom = 1.0f / (va + vb + vc);
    const float v = vb * denom;
    const float w = vc * denom;
    return a + ab * v + ac * w;
}

int floorDiv(float v, float cell) {
    return static_cast<int>(std::floor(v / cell));
}

}  // namespace

CollisionWorld::CollisionWorld() : cellSize_(kCellSize), gridDirty_(false), stampGen_(1) {}

void CollisionWorld::clear() {
    tris_.clear();
    cells_.clear();
    stamp_.clear();
    stampGen_ = 1;
    gridDirty_ = false;
    cellSize_ = kCellSize;
}

void CollisionWorld::markDirty() {
    gridDirty_ = true;
}

long long CollisionWorld::packCell(int x, int y, int z) const {
    const int b = 1 << 18;
    return (static_cast<long long>(x + b) & 0x7FFFFLL) |
           ((static_cast<long long>(y + b) & 0x7FFFFLL) << 19) |
           ((static_cast<long long>(z + b) & 0x7FFFFLL) << 38);
}

void CollisionWorld::cellOf(const Vec3& p, int* x, int* y, int* z) const {
    const float s = cellSize_ > 0.01f ? cellSize_ : kCellSize;
    *x = floorDiv(p.x, s);
    *y = floorDiv(p.y, s);
    *z = floorDiv(p.z, s);
}

void CollisionWorld::gatherCell(int x, int y, int z, std::vector<int>* out) const {
    const std::map<long long, std::vector<int> >::const_iterator it = cells_.find(packCell(x, y, z));
    if (it == cells_.end()) {
        return;
    }
    for (std::size_t i = 0; i < it->second.size(); ++i) {
        out->push_back(it->second[i]);
    }
}

void CollisionWorld::ensureGrid() const {
    if (!gridDirty_) {
        return;
    }
    gridDirty_ = false;
    cells_.clear();
    if (tris_.empty()) {
        return;
    }
    const float s = cellSize_ > 0.01f ? cellSize_ : kCellSize;
    for (std::size_t i = 0; i < tris_.size(); ++i) {
        const Tri& t = tris_[i];
        const float mnX = std::min(t.a.x, std::min(t.b.x, t.c.x));
        const float mnY = std::min(t.a.y, std::min(t.b.y, t.c.y));
        const float mnZ = std::min(t.a.z, std::min(t.b.z, t.c.z));
        const float mxX = std::max(t.a.x, std::max(t.b.x, t.c.x));
        const float mxY = std::max(t.a.y, std::max(t.b.y, t.c.y));
        const float mxZ = std::max(t.a.z, std::max(t.b.z, t.c.z));
        const int x0 = floorDiv(mnX, s);
        const int y0 = floorDiv(mnY, s);
        const int z0 = floorDiv(mnZ, s);
        const int x1 = floorDiv(mxX, s);
        const int y1 = floorDiv(mxY, s);
        const int z1 = floorDiv(mxZ, s);
        for (int z = z0; z <= z1; ++z) {
            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    cells_[packCell(x, y, z)].push_back(static_cast<int>(i));
                }
            }
        }
    }
}

void CollisionWorld::addTriangle(const Vec3& a, const Vec3& b, const Vec3& c, int roomId,
                                 int polygonId) {
    if (cellSize_ < 0.01f) {
        cellSize_ = kCellSize;
    }
    const Vec3 n = triNormal(a, b, c);
    if (length(cross(b - a, c - a)) < 1.0e-8f) {
        return;
    }
    Tri t;
    t.a = a;
    t.b = b;
    t.c = c;
    t.n = n;
    t.roomId = roomId;
    t.polygonId = polygonId;
    tris_.push_back(t);
    markDirty();
}

void CollisionWorld::addBsp(const BspTree& bsp) {
    for (std::size_t p = 0; p < bsp.polygons.size(); ++p) {
        const BspPolygon& poly = bsp.polygons[p];
        if (poly.vertexCount < 3) {
            continue;
        }
        const int start = poly.vertexStart;
        if (start < 0 || start + poly.vertexCount > static_cast<int>(bsp.vertices.size())) {
            continue;
        }
        const Vec3& a = bsp.vertices[static_cast<std::size_t>(start)];
        for (int i = 1; i + 1 < poly.vertexCount; ++i) {
            const Vec3& b = bsp.vertices[static_cast<std::size_t>(start + i)];
            const Vec3& c = bsp.vertices[static_cast<std::size_t>(start + i + 1)];
            addTriangle(a, b, c, poly.groupId, poly.polygonId);
        }
    }
}

void CollisionWorld::addStaticMesh(const StaticMesh& mesh,
                                   const std::vector<TextureVertex>& texVerts) {
    for (std::size_t p = 0; p < mesh.polygons.size(); ++p) {
        const Polygon& poly = mesh.polygons[p];
        if (poly.vertexCount < 3) {
            continue;
        }
        std::vector<Vec3> pts;
        pts.reserve(static_cast<std::size_t>(poly.vertexCount));
        for (int i = 0; i < poly.vertexCount; ++i) {
            const int tvi = poly.textureVertexStart + i;
            if (tvi < 0 || static_cast<std::size_t>(tvi) >= texVerts.size()) {
                continue;
            }
            const int vi = texVerts[static_cast<std::size_t>(tvi)].vertexIndex;
            if (vi < 0 || static_cast<std::size_t>(vi) >= mesh.vertices.size()) {
                continue;
            }
            pts.push_back(transformPoint(mesh.transform, mesh.vertices[static_cast<std::size_t>(vi)]));
        }
        if (pts.size() < 3) {
            continue;
        }
        for (std::size_t i = 1; i + 1 < pts.size(); ++i) {
            addTriangle(pts[0], pts[i], pts[i + 1], mesh.groupId, poly.id);
        }
    }
}

void CollisionWorld::addDynamicMesh(const DynamicMesh& mesh, const std::vector<TextureVertex>& texVerts,
                                    const Mat4x3& world) {
    for (std::size_t p = 0; p < mesh.polygons.size(); ++p) {
        const Polygon& poly = mesh.polygons[p];
        if (poly.vertexCount < 3) {
            continue;
        }
        std::vector<Vec3> pts;
        pts.reserve(static_cast<std::size_t>(poly.vertexCount));
        for (int i = 0; i < poly.vertexCount; ++i) {
            const int tvi = poly.textureVertexStart + i;
            if (tvi < 0 || static_cast<std::size_t>(tvi) >= texVerts.size()) {
                continue;
            }
            const int vi = texVerts[static_cast<std::size_t>(tvi)].vertexIndex;
            if (vi < 0 || static_cast<std::size_t>(vi) >= mesh.vertices.size()) {
                continue;
            }
            pts.push_back(transformPoint(world, mesh.vertices[static_cast<std::size_t>(vi)]));
        }
        if (pts.size() < 3) {
            continue;
        }
        for (std::size_t i = 1; i + 1 < pts.size(); ++i) {
            addTriangle(pts[0], pts[i], pts[i + 1], mesh.properties.roomId, poly.id);
        }
    }
}

void CollisionWorld::addLevelGeometry(const Level& level) {
    for (std::size_t i = 0; i < level.staticMeshes.size(); ++i) {
        addStaticMesh(level.staticMeshes[i], level.staticTextureVertices);
    }
}

CollisionHit CollisionWorld::raycast(const Vec3& origin, const Vec3& dir, float maxDist) const {
    CollisionHit best;
    const float dirLen = length(dir);
    if (dirLen <= kEpsilon) {
        return best;
    }
    const Vec3 nd = dir * (1.0f / dirLen);
    const float maxT = maxDist * dirLen;
    ensureGrid();
    if (tris_.empty()) {
        return best;
    }

    const float s = cellSize_ > 0.01f ? cellSize_ : kCellSize;
    int ix = 0, iy = 0, iz = 0;
    cellOf(origin, &ix, &iy, &iz);
    const int stepX = nd.x > 0.0f ? 1 : (nd.x < 0.0f ? -1 : 0);
    const int stepY = nd.y > 0.0f ? 1 : (nd.y < 0.0f ? -1 : 0);
    const int stepZ = nd.z > 0.0f ? 1 : (nd.z < 0.0f ? -1 : 0);
    const float tDeltaX = stepX != 0 ? s / std::fabs(nd.x) : 1.0e30f;
    const float tDeltaY = stepY != 0 ? s / std::fabs(nd.y) : 1.0e30f;
    const float tDeltaZ = stepZ != 0 ? s / std::fabs(nd.z) : 1.0e30f;
    float tMaxX = 1.0e30f;
    float tMaxY = 1.0e30f;
    float tMaxZ = 1.0e30f;
    if (stepX > 0) {
        tMaxX = ((static_cast<float>(ix) + 1.0f) * s - origin.x) / nd.x;
    } else if (stepX < 0) {
        tMaxX = (origin.x - static_cast<float>(ix) * s) / -nd.x;
    }
    if (stepY > 0) {
        tMaxY = ((static_cast<float>(iy) + 1.0f) * s - origin.y) / nd.y;
    } else if (stepY < 0) {
        tMaxY = (origin.y - static_cast<float>(iy) * s) / -nd.y;
    }
    if (stepZ > 0) {
        tMaxZ = ((static_cast<float>(iz) + 1.0f) * s - origin.z) / nd.z;
    } else if (stepZ < 0) {
        tMaxZ = (origin.z - static_cast<float>(iz) * s) / -nd.z;
    }

    std::vector<int> cand;
    int steps = 0;
    float tEnter = 0.0f;
    while (tEnter <= maxT && steps < 512) {
        ++steps;
        cand.clear();
        gatherCell(ix, iy, iz, &cand);
        for (std::size_t i = 0; i < cand.size(); ++i) {
            const Tri& t = tris_[static_cast<std::size_t>(cand[i])];
            float hitT = 0.0f;
            Vec3 n;
            if (!rayTriangle(origin, nd, maxT, t.a, t.b, t.c, &hitT, &n)) {
                continue;
            }
            if (hitT < best.t) {
                best.hit = true;
                best.t = hitT;
                best.point = origin + nd * hitT;
                best.normal = n;
                best.roomId = t.roomId;
                best.polygonId = t.polygonId;
            }
        }
        const float tLeave = std::min(tMaxX, std::min(tMaxY, tMaxZ));
        if (best.hit && best.t <= tLeave + 1.0e-4f) {
            break;
        }
        if (tMaxX < tMaxY && tMaxX < tMaxZ) {
            ix += stepX;
            tEnter = tMaxX;
            tMaxX += tDeltaX;
        } else if (tMaxY < tMaxZ) {
            iy += stepY;
            tEnter = tMaxY;
            tMaxY += tDeltaY;
        } else {
            iz += stepZ;
            tEnter = tMaxZ;
            tMaxZ += tDeltaZ;
        }
        if (stepX == 0 && stepY == 0 && stepZ == 0) {
            break;
        }
    }
    return best;
}

CollisionHit CollisionWorld::sphereOverlap(const Vec3& center, float radius) const {
    CollisionHit best;
    float bestPen = 0.0f;
    ensureGrid();
    if (tris_.empty()) {
        return best;
    }
    const float s = cellSize_ > 0.01f ? cellSize_ : kCellSize;
    const float rad = radius < 0.0f ? 0.0f : radius;
    const int x0 = floorDiv(center.x - rad, s);
    const int y0 = floorDiv(center.y - rad, s);
    const int z0 = floorDiv(center.z - rad, s);
    const int x1 = floorDiv(center.x + rad, s);
    const int y1 = floorDiv(center.y + rad, s);
    const int z1 = floorDiv(center.z + rad, s);
    std::vector<int> cand;
    if (stamp_.size() < tris_.size()) {
        stamp_.assign(tris_.size(), 0);
    }
    ++stampGen_;
    if (stampGen_ == 0) {
        stamp_.assign(tris_.size(), 0);
        stampGen_ = 1;
    }
    for (int z = z0; z <= z1; ++z) {
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const std::map<long long, std::vector<int> >::const_iterator it =
                    cells_.find(packCell(x, y, z));
                if (it == cells_.end()) {
                    continue;
                }
                for (std::size_t i = 0; i < it->second.size(); ++i) {
                    const int idx = it->second[i];
                    if (idx < 0 || static_cast<std::size_t>(idx) >= stamp_.size()) {
                        continue;
                    }
                    if (stamp_[static_cast<std::size_t>(idx)] == stampGen_) {
                        continue;
                    }
                    stamp_[static_cast<std::size_t>(idx)] = stampGen_;
                    cand.push_back(idx);
                }
            }
        }
    }
    const float r2 = rad * rad;
    for (std::size_t i = 0; i < cand.size(); ++i) {
        const Tri& t = tris_[static_cast<std::size_t>(cand[i])];
        const Vec3 c = closestOnTriangle(center, t.a, t.b, t.c);
        const Vec3 d = center - c;
        const float dist2 = dot(d, d);
        if (dist2 >= r2) {
            continue;
        }
        const float dist = std::sqrt(dist2);
        const float pen = rad - dist;
        if (pen > bestPen) {
            bestPen = pen;
            best.hit = true;
            best.t = dist;
            best.point = c;
            if (dist > kEpsilon) {
                best.normal = d * (1.0f / dist);
            } else {
                best.normal = t.n;
            }
            best.roomId = t.roomId;
            best.polygonId = t.polygonId;
        }
    }
    return best;
}

Vec3 CollisionWorld::moveSphere(const Vec3& start, const Vec3& delta, float radius) const {
    const float rad = radius < 0.01f ? 0.01f : radius;
    Vec3 pos = start;
    const float maxDist = length(delta);
    if (maxDist > kEpsilon) {
        const Vec3 dir = delta * (1.0f / maxDist);
        const CollisionHit sweep = raycast(start, dir, maxDist + rad);
        if (sweep.hit) {
            float stop = sweep.t - rad;
            if (stop < 0.0f) {
                stop = 0.0f;
            }
            pos = start + dir * stop;
        } else {
            pos = start + delta;
        }
    }
    for (int iter = 0; iter < 5; ++iter) {
        const CollisionHit hit = sphereOverlap(pos, rad);
        if (!hit.hit) {
            break;
        }
        const float dist = length(pos - hit.point);
        const float push = rad - dist + 0.002f;
        pos += hit.normal * push;
    }
    return pos;
}

Mat4x3 roomWorldTransform(const Level& level, int roomId) {
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

}  // namespace maxfx
