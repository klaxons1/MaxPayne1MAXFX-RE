#include "maxfx/collision/Collision.h"

#include <cmath>

namespace maxfx {
namespace {

const float kEpsilon = 1.0e-6f;

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

}  // namespace

void CollisionWorld::clear() { tris_.clear(); }

void CollisionWorld::addTriangle(const Vec3& a, const Vec3& b, const Vec3& c, int roomId,
                                 int polygonId) {
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

CollisionHit CollisionWorld::raycast(const Vec3& origin, const Vec3& dir, float maxDist) const {
    CollisionHit best;
    const float dirLen = length(dir);
    if (dirLen <= kEpsilon) {
        return best;
    }
    const Vec3 nd = dir * (1.0f / dirLen);
    const float maxT = maxDist * dirLen;
    for (std::size_t i = 0; i < tris_.size(); ++i) {
        const Tri& t = tris_[i];
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
    return best;
}

CollisionHit CollisionWorld::sphereOverlap(const Vec3& center, float radius) const {
    CollisionHit best;
    float bestPen = 0.0f;
    for (std::size_t i = 0; i < tris_.size(); ++i) {
        const Tri& t = tris_[i];
        const Vec3 c = closestOnTriangle(center, t.a, t.b, t.c);
        const Vec3 d = center - c;
        const float dist2 = dot(d, d);
        if (dist2 >= radius * radius) {
            continue;
        }
        const float dist = std::sqrt(dist2);
        const float pen = radius - dist;
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
