#include "maxfx/kf2/Kf2.h"

#include "maxfx/core/Fs.h"
#include "maxfx/core/Stream.h"

#include <cmath>
#include <map>

namespace maxfx {
namespace {

struct ChunkHeader {
    unsigned int id;
    unsigned int version;
    unsigned int size;
    std::size_t payloadStart;
    std::size_t payloadEnd;
};

bool tryReadChunk(TaggedReader& in, ChunkHeader& out) {
    if (in.remaining() < 13) {
        return false;
    }
    if (in.peekTag() != kKf2ChunkTag) {
        return false;
    }
    in.readRawU8();
    out.id = in.readRawU32();
    out.version = in.readRawU32();
    out.size = in.readRawU32();
    out.payloadStart = in.position();
    out.payloadEnd = out.payloadStart + static_cast<std::size_t>(out.size);
    if (out.payloadEnd > in.size()) {
        out.payloadEnd = in.size();
    }
    return true;
}

// Chunk `size` includes the next sibling's 13-byte header on PC Max Payne 1
// files, so it cannot be used to skip. Known chunks are consumed field by
// field. Unknown chunks skip tagged values until the next 0x0C header.
void skipTaggedValue(TaggedReader& in) {
    if (in.eof()) {
        return;
    }
    const std::uint8_t tag = in.peekTag();
    if (tag == kKf2ChunkTag) {
        return;
    }
    switch (tag) {
        case kTagLong:
        case kTagULong:
        case kTagInt32:
        case kTagUInt32:
            in.readInt();
            break;
        case kTagShort:
        case kTagUShort:
        case kTagInt16:
        case kTagUInt16:
            in.readInt();
            break;
        case kTagChar:
        case kTagSChar:
        case kTagUChar:
        case kTagInt8:
        case kTagUInt8:
        case kTagUInt24:
        case kTagInt24:
            in.readInt();
            break;
        case kTagFloat32:
        case kTagFloat16:
        case kTagDouble:
            in.readFloat();
            break;
        case kTagBool:
            in.readBool();
            break;
        case kTagString:
            in.readString();
            break;
        case kTagVec2:
            in.readVec2();
            break;
        case kTagVec3:
            in.readVec3();
            break;
        case kTagVec4:
            in.readVec4();
            break;
        case kTagMat3:
            in.readMat3();
            break;
        case kTagMat4x3:
            in.readMat4x3();
            break;
        default:
            in.readRawU8();
            break;
    }
}

void skipUnknownChunk(TaggedReader& in, const ChunkHeader& header) {
    (void)header;
    int guard = 0;
    while (!in.eof() && in.peekTag() != kKf2ChunkTag && guard++ < 100000) {
        skipTaggedValue(in);
    }
}

bool isTopLevelChunkId(unsigned int id) {
    switch (id) {
        case kKf2MaterialList:
        case kKf2Mesh:
        case kKf2Skin:
        case kKf2KeyframeAnimation:
        case kKf2Camera:
        case kKf2Environment:
        case kKf2Helper:
        case kKf2PointLight:
        case kKf2DirectionalLight:
        case kKf2SpotLight:
        case kKf2PointLightAnimation:
        case kKf2DirectionalLightAnimation:
        case kKf2SpotLightAnimation:
            return true;
        default:
            return false;
    }
}

bool readKf2Bool(TaggedReader& in) {
    const std::uint8_t tag = in.peekTag();
    if (tag == kTagBool) {
        return in.readBool();
    }
    return in.readInt() != 0;
}

Vec3 readUntaggedVec3(TaggedReader& in) {
    Vec3 v;
    v.x = in.readRawF32();
    v.y = in.readRawF32();
    v.z = in.readRawF32();
    return v;
}

Kf2Texture parseTexture(TaggedReader& in, const ChunkHeader& header) {
    if (header.id != kKf2Texture) {
        throw ReadError("expected texture sub-chunk", in.position());
    }
    Kf2Texture tex;
    tex.version = static_cast<int>(header.version);
    tex.name = in.readString();
    tex.mipMaps = in.readInt();
    tex.filtering = in.readInt();
    const int n = in.readInt();
    tex.files.reserve(static_cast<std::size_t>(n < 0 ? 0 : n));
    for (int i = 0; i < n; ++i) {
        tex.files.push_back(in.readString());
    }
    // Working community importer only reads the playback block when there is
    // more than one frame. The spec says `> 0`; MP1 files match the importer.
    if (n > 1) {
        readKf2Bool(in);
        readKf2Bool(in);
        in.readInt();
        in.readInt();
        in.readInt();
    }
    return tex;
}

bool readOptionalTexture(TaggedReader& in, bool present, Kf2Texture& out) {
    if (!present) {
        return true;
    }
    ChunkHeader header;
    if (!tryReadChunk(in, header)) {
        return false;
    }
    out = parseTexture(in, header);
    return true;
}

Kf2Material parseMaterial(TaggedReader& in, const ChunkHeader& header) {
    Kf2Material mat;
    mat.version = static_cast<int>(header.version);
    mat.name = in.readString();
    mat.twoSided = readKf2Bool(in);
    mat.fogging = readKf2Bool(in);
    mat.diffuseCombined = readKf2Bool(in);
    mat.invisibleGeometry = readKf2Bool(in);
    mat.hasVertexAlpha = readKf2Bool(in);
    mat.diffuseColorType = in.readInt();
    mat.specularColorType = in.readInt();
    mat.litType = in.readInt();
    mat.ambient.x = in.readFloat();
    mat.ambient.y = in.readFloat();
    mat.ambient.z = in.readFloat();
    mat.ambient.w = in.readFloat();
    mat.diffuse.x = in.readFloat();
    mat.diffuse.y = in.readFloat();
    mat.diffuse.z = in.readFloat();
    mat.diffuse.w = in.readFloat();
    mat.specular.x = in.readFloat();
    mat.specular.y = in.readFloat();
    mat.specular.z = in.readFloat();
    mat.specular.w = in.readFloat();
    mat.vertexAlpha = in.readFloat();
    mat.specularExponent = in.readFloat();
    mat.diffuseTextureType = in.readInt();
    mat.reflectionTextureType = in.readInt();
    mat.embossFactor = in.readFloat();

    mat.hasDiffuseTexture = readKf2Bool(in);
    readOptionalTexture(in, mat.hasDiffuseTexture, mat.diffuseTexture);
    mat.hasReflectionTexture = readKf2Bool(in);
    readOptionalTexture(in, mat.hasReflectionTexture, mat.reflectionTexture);
    mat.hasBumpTexture = readKf2Bool(in);
    readOptionalTexture(in, mat.hasBumpTexture, mat.bumpTexture);
    mat.hasOpacityTexture = readKf2Bool(in);
    // Community Python importer accidentally tests has_bump here. The spec and
    // the PC files use has_opacity_texture.
    readOptionalTexture(in, mat.hasOpacityTexture, mat.opacityTexture);

    if (header.version > 0) {
        mat.hasMaskTexture = readKf2Bool(in);
        readOptionalTexture(in, mat.hasMaskTexture, mat.maskTexture);
        in.readInt();  // mask_texture_type
        mat.hasLit = readKf2Bool(in);
    }
    if (header.version > 1) {
        readKf2Bool(in);
        readKf2Bool(in);
        in.readInt();
    }
    return mat;
}

Kf2MaterialList parseMaterialList(TaggedReader& in, const ChunkHeader& header) {
    Kf2MaterialList list;
    list.version = static_cast<int>(header.version);
    list.textureDirs = in.readString();
    const int n = in.readInt();
    list.materials.reserve(static_cast<std::size_t>(n < 0 ? 0 : n));
    for (int i = 0; i < n; ++i) {
        ChunkHeader matHeader;
        if (!tryReadChunk(in, matHeader) || matHeader.id != kKf2Material) {
            throw ReadError("expected material sub-chunk", in.position());
        }
        list.materials.push_back(parseMaterial(in, matHeader));
    }
    return list;
}

Kf2Node parseNode(TaggedReader& in, const ChunkHeader& header) {
    Kf2Node node;
    node.version = static_cast<int>(header.version);
    node.name = in.readString();
    node.parentName = in.readString();
    node.objectToParent = in.readMat4x3();
    node.hasParent = readKf2Bool(in);
    if (header.version > 0) {
        node.userDefined = in.readString();
    }
    return node;
}

Kf2Geometry parseGeometry(TaggedReader& in, const ChunkHeader& header) {
    Kf2Geometry geo;
    geo.version = static_cast<int>(header.version);
    const int n = in.readInt();
    geo.vertices.reserve(static_cast<std::size_t>(n < 0 ? 0 : n));
    if (header.version == 0) {
        for (int i = 0; i < n; ++i) {
            geo.vertices.push_back(in.readVec3());
        }
        geo.verticesPerPrimitive.push_back(n);
    } else {
        for (int i = 0; i < n; ++i) {
            geo.vertices.push_back(readUntaggedVec3(in));
        }
        geo.normals.reserve(static_cast<std::size_t>(n < 0 ? 0 : n));
        for (int i = 0; i < n; ++i) {
            geo.normals.push_back(readUntaggedVec3(in));
        }
        const int prims = in.readInt();
        geo.verticesPerPrimitive.reserve(static_cast<std::size_t>(prims < 0 ? 0 : prims));
        for (int i = 0; i < prims; ++i) {
            geo.verticesPerPrimitive.push_back(in.readInt());
        }
    }
    return geo;
}

void parsePolygonChunk(TaggedReader& in, const ChunkHeader& header, std::vector<int>& indices) {
    (void)header;
    const int n = in.readInt();
    for (int i = 0; i < n; ++i) {
        indices.push_back(in.readInt());
    }
}

Kf2Polygons parsePolygons(TaggedReader& in, const ChunkHeader& header) {
    Kf2Polygons polys;
    polys.version = static_cast<int>(header.version);
    const int n = in.readInt();
    if (header.version == 0) {
        for (int i = 0; i < n; ++i) {
            ChunkHeader polyHeader;
            if (!tryReadChunk(in, polyHeader) || polyHeader.id != kKf2Polygon) {
                throw ReadError("expected polygon sub-chunk", in.position());
            }
            parsePolygonChunk(in, polyHeader, polys.indices);
        }
        polys.polygonsPerPrimitive.push_back(static_cast<int>(polys.indices.size() / 3));
        in.readInt();
        in.readInt();
    } else {
        polys.indices.reserve(static_cast<std::size_t>(n < 0 ? 0 : n));
        for (int i = 0; i < n; ++i) {
            polys.indices.push_back(static_cast<int>(in.readRawU16()));
        }
        const int prims = in.readInt();
        polys.polygonsPerPrimitive.reserve(static_cast<std::size_t>(prims < 0 ? 0 : prims));
        for (int i = 0; i < prims; ++i) {
            polys.polygonsPerPrimitive.push_back(in.readInt());
        }
    }
    return polys;
}

Kf2PolygonMaterial parsePolygonMaterial(TaggedReader& in, const ChunkHeader& header) {
    Kf2PolygonMaterial pm;
    pm.version = static_cast<int>(header.version);
    const int n = in.readInt();
    pm.names.reserve(static_cast<std::size_t>(n < 0 ? 0 : n));
    for (int i = 0; i < n; ++i) {
        pm.names.push_back(in.readString());
    }
    if (header.version == 0) {
        const int np = in.readInt();
        pm.materialIndexForPolygon.reserve(static_cast<std::size_t>(np < 0 ? 0 : np));
        for (int i = 0; i < np; ++i) {
            pm.materialIndexForPolygon.push_back(in.readInt());
        }
    }
    return pm;
}

Kf2UvMapping parseUvMapping(TaggedReader& in, const ChunkHeader& header) {
    Kf2UvMapping uv;
    uv.version = static_cast<int>(header.version);
    uv.layer = in.readInt();
    if (header.version == 0) {
        const int np = in.readInt();
        for (int i = 0; i < np; ++i) {
            ChunkHeader polyHeader;
            if (!tryReadChunk(in, polyHeader) || polyHeader.id != kKf2Polygon) {
                throw ReadError("expected uv polygon sub-chunk", in.position());
            }
            parsePolygonChunk(in, polyHeader, uv.uvIndices);
        }
        in.readInt();
        in.readInt();
    }
    const int nc = in.readInt();
    uv.coordinates.reserve(static_cast<std::size_t>(nc < 0 ? 0 : nc));
    for (int i = 0; i < nc; ++i) {
        if (header.version == 0) {
            uv.coordinates.push_back(in.readVec3());
        } else {
            uv.coordinates.push_back(readUntaggedVec3(in));
        }
    }
    if (header.version == 1) {
        const int prims = in.readInt();
        uv.coordinatesPerPrimitive.reserve(static_cast<std::size_t>(prims < 0 ? 0 : prims));
        for (int i = 0; i < prims; ++i) {
            uv.coordinatesPerPrimitive.push_back(in.readInt());
        }
    }
    return uv;
}

void rebaseMeshV2(Kf2Mesh& mesh) {
    if (mesh.version != 2 || !mesh.hasGeometry || !mesh.hasPolygons) {
        return;
    }
    const std::vector<int>& perPrim = mesh.geometry.verticesPerPrimitive;
    if (perPrim.empty()) {
        return;
    }
    std::vector<int> vertexStart(perPrim.size() + 1, 0);
    for (std::size_t i = 0; i < perPrim.size(); ++i) {
        vertexStart[i + 1] = vertexStart[i] + perPrim[i];
    }
    std::vector<int> newIndices;
    newIndices.reserve(mesh.polygons.indices.size());
    std::vector<int> matForPoly;
    int cursor = 0;
    for (std::size_t p = 0; p < mesh.polygons.polygonsPerPrimitive.size(); ++p) {
        const int nFaces = mesh.polygons.polygonsPerPrimitive[p];
        for (int f = 0; f < nFaces; ++f) {
            if (cursor + 2 >= static_cast<int>(mesh.polygons.indices.size())) {
                break;
            }
            const int base = (p + 1 < vertexStart.size()) ? vertexStart[p] : 0;
            newIndices.push_back(mesh.polygons.indices[static_cast<std::size_t>(cursor + 0)] + base);
            newIndices.push_back(mesh.polygons.indices[static_cast<std::size_t>(cursor + 1)] + base);
            newIndices.push_back(mesh.polygons.indices[static_cast<std::size_t>(cursor + 2)] + base);
            matForPoly.push_back(static_cast<int>(p));
            cursor += 3;
        }
    }
    mesh.polygons.indices.swap(newIndices);
    if (mesh.hasPolygonMaterial) {
        mesh.polygonMaterial.materialIndexForPolygon.swap(matForPoly);
    }
}

Kf2Mesh parseMesh(TaggedReader& in, const ChunkHeader& header) {
    Kf2Mesh mesh;
    mesh.version = static_cast<int>(header.version);
    (void)header;
    while (!in.eof() && in.peekTag() == kKf2ChunkTag) {
        ChunkHeader sub;
        if (!tryReadChunk(in, sub)) {
            break;
        }
        if (isTopLevelChunkId(sub.id)) {
            in.seek(sub.payloadStart - 13);
            break;
        }
        switch (sub.id) {
            case kKf2Node:
                mesh.node = parseNode(in, sub);
                mesh.hasNode = true;
                break;
            case kKf2Geometry:
                mesh.geometry = parseGeometry(in, sub);
                mesh.hasGeometry = true;
                break;
            case kKf2Polygons:
                mesh.polygons = parsePolygons(in, sub);
                mesh.hasPolygons = true;
                break;
            case kKf2PolygonMaterial:
                mesh.polygonMaterial = parsePolygonMaterial(in, sub);
                mesh.hasPolygonMaterial = true;
                break;
            case kKf2UvMapping:
                mesh.uvMapping.push_back(parseUvMapping(in, sub));
                break;
            case kKf2ReferenceToData:
                mesh.referenceToData = in.readString();
                break;
            case kKf2Smoothing: {
                const int n = in.readInt();
                mesh.smoothingGroups.reserve(static_cast<std::size_t>(n < 0 ? 0 : n));
                for (int i = 0; i < n; ++i) {
                    mesh.smoothingGroups.push_back(in.readUInt());
                }
                break;
            }
            default:
                // Next top-level chunk leaked into the mesh payload: rewind.
                in.seek(sub.payloadStart - 13);
                rebaseMeshV2(mesh);
                return mesh;
        }
    }
    rebaseMeshV2(mesh);
    return mesh;
}

Kf2Skin parseSkin(TaggedReader& in, const ChunkHeader& header) {
    Kf2Skin skin;
    skin.version = static_cast<int>(header.version);
    if (header.version == 0) {
        std::map<int, Kf2SkinVertex> byVertex;
        for (int pass = 0; pass < 3; ++pass) {
            const int n = in.readInt();
            for (int i = 0; i < n; ++i) {
                const int vertexIndex = in.readInt();
                const int boneIndex = in.readInt();
                const float weight = in.readFloat();
                Kf2SkinVertex& sv = byVertex[vertexIndex];
                sv.vertexIndex = vertexIndex;
                sv.bones.push_back(boneIndex);
                sv.weights.push_back(weight);
            }
        }
        int maxIndex = -1;
        for (std::map<int, Kf2SkinVertex>::iterator it = byVertex.begin(); it != byVertex.end();
             ++it) {
            if (it->first > maxIndex) {
                maxIndex = it->first;
            }
        }
        if (maxIndex >= 0) {
            skin.vertices.assign(static_cast<std::size_t>(maxIndex + 1), Kf2SkinVertex());
            for (std::map<int, Kf2SkinVertex>::iterator it = byVertex.begin(); it != byVertex.end();
                 ++it) {
                skin.vertices[static_cast<std::size_t>(it->first)] = it->second;
            }
        }
        const int nSkin = in.readInt();
        for (int i = 0; i < nSkin; ++i) {
            skin.skinObjectNames.push_back(in.readString());
        }
        const int nSkel = in.readInt();
        for (int i = 0; i < nSkel; ++i) {
            skin.skeletonObjectNames.push_back(in.readString());
        }
        // Trailing per-vertex extras vary by exporter; stop on a non-int tag
        // rather than dropping the whole skin chunk.
        try {
            if (!in.eof() && in.peekTag() != kKf2ChunkTag) {
                const int extra = in.readInt();
                for (int i = 0; i < extra; ++i) {
                    if (in.eof() || in.peekTag() == kKf2ChunkTag) {
                        break;
                    }
                    in.readInt();
                }
            }
        } catch (const ReadError&) {
        }
    } else {
        // Version 1 stores several tagged vectors with a leading type tag we
        // do not need. Consume by remaining payload size at the end.
        in.readRawU8();
        const int nSkin = in.readInt();
        for (int i = 0; i < nSkin; ++i) {
            skin.skinObjectNames.push_back(in.readString());
        }
        in.readRawU8();
        const int nSkel = in.readInt();
        for (int i = 0; i < nSkel; ++i) {
            skin.skeletonObjectNames.push_back(in.readString());
        }
        in.readRawU8();
        const int nIdx = in.readInt();
        std::vector<int> vertexIndices;
        vertexIndices.reserve(static_cast<std::size_t>(nIdx < 0 ? 0 : nIdx));
        for (int i = 0; i < nIdx; ++i) {
            vertexIndices.push_back(in.readInt());
        }
        in.readRawU8();
        const int nBonesNum = in.readInt();
        std::vector<int> bonesNum;
        for (int i = 0; i < nBonesNum; ++i) {
            bonesNum.push_back(in.readInt());
        }
        in.readRawU8();
        const int nBones = in.readInt();
        std::vector<int> bones;
        for (int i = 0; i < nBones; ++i) {
            bones.push_back(in.readInt());
        }
        in.readRawU8();
        const int nW = in.readInt();
        std::vector<float> weights;
        for (int i = 0; i < nW; ++i) {
            weights.push_back(in.readFloat());
        }
        in.readRawU8();
        const int nPer = in.readInt();
        for (int i = 0; i < nPer; ++i) {
            in.readInt();
        }
        in.readRawU8();
        const int nStart = in.readInt();
        for (int i = 0; i < nStart; ++i) {
            in.readInt();
        }
        const int nVert = static_cast<int>(vertexIndices.size());
        for (int i = 0; i < nVert; ++i) {
            Kf2SkinVertex sv;
            sv.vertexIndex = i;
            const int offset = vertexIndices[static_cast<std::size_t>(i)];
            const int nb = (i < static_cast<int>(bonesNum.size())) ? bonesNum[static_cast<std::size_t>(i)] : 0;
            for (int b = 0; b < nb; ++b) {
                const int idx = offset + b;
                if (idx >= 0 && idx < static_cast<int>(bones.size())) {
                    sv.bones.push_back(bones[static_cast<std::size_t>(idx)]);
                }
                if (idx >= 0 && idx < static_cast<int>(weights.size())) {
                    sv.weights.push_back(weights[static_cast<std::size_t>(idx)]);
                }
            }
            skin.vertices.push_back(sv);
        }
    }
    return skin;
}

Kf2NodeAnimation parseKeyframeAnimation(TaggedReader& in, const ChunkHeader& header) {
    Kf2NodeAnimation anim;
    anim.version = static_cast<int>(header.version);
    if (in.peekTag() == kKf2ChunkTag) {
        ChunkHeader sub;
        sub.id = 0;
        sub.version = 0;
        sub.size = 0;
        if (tryReadChunk(in, sub)) {
            if (sub.id == kKf2Animation) {
                anim.targetName = in.readString();
                anim.frameRate = in.readInt();
                anim.looping = readKf2Bool(in);
            } else {
                skipUnknownChunk(in, sub);
            }
        }
    }
    if (in.eof() || in.peekTag() == kKf2ChunkTag) {
        return anim;
    }
    anim.parentName = in.readString();
    if (in.eof() || in.peekTag() == kKf2ChunkTag) {
        return anim;
    }
    anim.loopInterpolation = readKf2Bool(in);
    anim.totalKeyframeCount = in.readInt();
    const int keyCount = in.readInt();
    anim.keys.reserve(static_cast<std::size_t>(keyCount < 0 ? 0 : keyCount));
    for (int i = 0; i < keyCount; ++i) {
        Kf2AnimKey key;
        key.frame = in.readInt();
        key.objectToParent = in.readMat4x3();
        anim.keys.push_back(key);
    }
    if (header.version > 0 && !in.eof() && in.peekTag() != kKf2ChunkTag) {
        const int visCount = in.readInt();
        anim.visibility.reserve(static_cast<std::size_t>(visCount < 0 ? 0 : visCount));
        for (int i = 0; i < visCount; ++i) {
            Kf2VisibilityKey v;
            v.frame = in.readInt();
            v.visibility = in.readFloat();
            anim.visibility.push_back(v);
        }
    }
    if (header.version > 1 && !in.eof() && in.peekTag() != kKf2ChunkTag) {
        anim.loopToFrame = in.readInt();
    }
    if (header.version > 2 && !in.eof() && in.peekTag() != kKf2ChunkTag) {
        anim.interpolationMethod = in.readInt();
    }
    if (header.version > 3 && !in.eof() && in.peekTag() != kKf2ChunkTag) {
        anim.maintainMatrixScaling = readKf2Bool(in);
    }
    if (header.version <= 4 && anim.totalKeyframeCount >= 0) {
        anim.totalKeyframeCount += 1;
    }
    return anim;
}

const Kf2Material* findMaterial(const Kf2File& file, const std::string& name) {
    const std::string key = lowerCopy(name);
    for (std::size_t l = 0; l < file.materialLists.size(); ++l) {
        const std::vector<Kf2Material>& mats = file.materialLists[l].materials;
        for (std::size_t m = 0; m < mats.size(); ++m) {
            if (lowerCopy(mats[m].name) == key) {
                return &mats[m];
            }
        }
    }
    return 0;
}

std::string lowerAscii(const std::string& s) { return lowerCopy(s); }

const Kf2Mesh* findMeshByName(const Kf2File& file, const std::string& name) {
    const std::string key = lowerAscii(name);
    for (std::size_t i = 0; i < file.meshes.size(); ++i) {
        if (file.meshes[i].hasNode && lowerAscii(file.meshes[i].node.name) == key) {
            return &file.meshes[i];
        }
    }
    return 0;
}

void appendTriangles(const Kf2Mesh& mesh, const Kf2File& file, Kf2DrawMesh& out) {
    if (!mesh.hasGeometry || !mesh.hasPolygons) {
        return;
    }
    const std::vector<Vec3>& verts = mesh.geometry.vertices;
    const std::vector<Vec3>& nrms = mesh.geometry.normals;
    const Kf2UvMapping* uv = mesh.uvMapping.empty() ? 0 : &mesh.uvMapping[0];
    const int triCount = static_cast<int>(mesh.polygons.indices.size() / 3);
    for (int t = 0; t < triCount; ++t) {
        std::string matName;
        if (mesh.hasPolygonMaterial) {
            int mi = 0;
            if (t < static_cast<int>(mesh.polygonMaterial.materialIndexForPolygon.size())) {
                mi = mesh.polygonMaterial.materialIndexForPolygon[static_cast<std::size_t>(t)];
            } else if (!mesh.polygonMaterial.names.empty()) {
                mi = 0;
            }
            if (mi >= 0 && mi < static_cast<int>(mesh.polygonMaterial.names.size())) {
                matName = mesh.polygonMaterial.names[static_cast<std::size_t>(mi)];
            }
        }
        Kf2DrawPart* part = 0;
        for (std::size_t p = 0; p < out.parts.size(); ++p) {
            if (out.parts[p].materialName == matName) {
                part = &out.parts[p];
                break;
            }
        }
        if (part == 0) {
            Kf2DrawPart np;
            np.materialName = matName;
            np.twoSided = false;
            np.invisible = false;
            np.diffuseColor = Vec4(1, 1, 1, 1);
            const Kf2Material* mat = findMaterial(file, matName);
            if (mat) {
                np.twoSided = mat->twoSided;
                np.invisible = mat->invisibleGeometry;
                np.diffuseColor = mat->diffuse;
                np.textureFiles = mat->diffuseTexture.files;
                if (mat->hasOpacityTexture) {
                    np.opacityFiles = mat->opacityTexture.files;
                }
            }
            out.parts.push_back(np);
            part = &out.parts.back();
        }
        for (int k = 0; k < 3; ++k) {
            const int vi = mesh.polygons.indices[static_cast<std::size_t>(t * 3 + k)];
            Kf2DrawVertex dv;
            if (vi >= 0 && vi < static_cast<int>(verts.size())) {
                dv.position = verts[static_cast<std::size_t>(vi)];
            }
            if (vi >= 0 && vi < static_cast<int>(nrms.size())) {
                dv.normal = nrms[static_cast<std::size_t>(vi)];
            } else {
                dv.normal = Vec3(0, 1, 0);
            }
            int uvi = vi;
            if (uv && t * 3 + k < static_cast<int>(uv->uvIndices.size())) {
                uvi = uv->uvIndices[static_cast<std::size_t>(t * 3 + k)];
            }
            if (uv && uvi >= 0 && uvi < static_cast<int>(uv->coordinates.size())) {
                dv.uv.x = uv->coordinates[static_cast<std::size_t>(uvi)].x;
                dv.uv.y = uv->coordinates[static_cast<std::size_t>(uvi)].y;
            }
            part->vertices.push_back(dv);
        }
    }
}

}  // namespace

Kf2File Kf2Reader::loadFromMemory(const std::uint8_t* data, std::size_t size,
                                  const std::string& sourceName) {
    Kf2File file;
    file.sourcePath = sourceName;
    if (data == 0 || size == 0) {
        throw ReadError("empty KF2 stream", 0);
    }
    TaggedReader in(data, size);
    while (!in.eof()) {
        if (in.peekTag() != kKf2ChunkTag) {
            // Trailing padding / unparsed bytes.
            break;
        }
        ChunkHeader header;
        if (!tryReadChunk(in, header)) {
            break;
        }
        try {
            switch (header.id) {
                case kKf2MaterialList:
                    file.materialLists.push_back(parseMaterialList(in, header));
                    break;
                case kKf2Mesh:
                    file.meshes.push_back(parseMesh(in, header));
                    break;
                case kKf2Skin:
                    file.skins.push_back(parseSkin(in, header));
                    break;
                case kKf2KeyframeAnimation:
                    file.animations.push_back(parseKeyframeAnimation(in, header));
                    break;
                default:
                    ++file.skippedChunks;
                    skipUnknownChunk(in, header);
                    break;
            }
        } catch (const ReadError&) {
            ++file.unknownChunks;
            skipUnknownChunk(in, header);
        }
    }
    return file;
}

Kf2File Kf2Reader::loadFromFile(const std::string& path) {
    const std::vector<unsigned char> bytes = readFileBytes(path);
    if (bytes.empty()) {
        throw ReadError("empty KF2 file", 0);
    }
    Kf2File file = loadFromMemory(&bytes[0], bytes.size(), path);
    file.sourcePath = path;
    return file;
}

void kf2NodeWorldTransforms(const Kf2File& file, std::vector<std::string>* names,
                            std::vector<Mat4x3>* worlds) {
    names->clear();
    worlds->clear();
    std::map<std::string, Mat4x3> local;
    std::map<std::string, std::string> parent;
    for (std::size_t i = 0; i < file.meshes.size(); ++i) {
        if (!file.meshes[i].hasNode) {
            continue;
        }
        const Kf2Node& n = file.meshes[i].node;
        local[n.name] = n.objectToParent;
        if (n.hasParent) {
            parent[n.name] = n.parentName;
        }
        names->push_back(n.name);
    }
    for (std::size_t i = 0; i < names->size(); ++i) {
        const std::string& name = (*names)[i];
        Mat4x3 world = local[name];
        std::string walk = name;
        int guard = 0;
        while (parent.find(walk) != parent.end() && guard++ < 64) {
            walk = parent[walk];
            std::map<std::string, Mat4x3>::const_iterator it = local.find(walk);
            if (it == local.end()) {
                break;
            }
            world = combine(it->second, world);
        }
        worlds->push_back(world);
    }
}

void kf2BuildDrawMeshes(const Kf2File& file, std::vector<Kf2DrawMesh>& out) {
    out.clear();
    std::string dirs;
    if (!file.materialLists.empty()) {
        dirs = file.materialLists[0].textureDirs;
    }
    for (std::size_t i = 0; i < file.meshes.size(); ++i) {
        const Kf2Mesh* src = &file.meshes[i];
        Kf2Mesh resolved = *src;
        if (!resolved.hasGeometry && !resolved.referenceToData.empty()) {
            const Kf2Mesh* ref = findMeshByName(file, resolved.referenceToData);
            if (ref) {
                resolved.geometry = ref->geometry;
                resolved.hasGeometry = ref->hasGeometry;
                resolved.polygons = ref->polygons;
                resolved.hasPolygons = ref->hasPolygons;
                resolved.polygonMaterial = ref->polygonMaterial;
                resolved.hasPolygonMaterial = ref->hasPolygonMaterial;
                resolved.uvMapping = ref->uvMapping;
            }
        }
        Kf2DrawMesh dm;
        dm.textureDirs = dirs;
        if (resolved.hasNode) {
            dm.nodeName = resolved.node.name;
            dm.parentName = resolved.node.parentName;
            dm.objectToParent = resolved.node.objectToParent;
        }
        appendTriangles(resolved, file, dm);
        if (!dm.parts.empty()) {
            out.push_back(dm);
        }
    }
}

float kf2AnimationDuration(const Kf2File& file) {
    float best = 0.0f;
    for (std::size_t i = 0; i < file.animations.size(); ++i) {
        const Kf2NodeAnimation& a = file.animations[i];
        const float fps = a.frameRate > 0 ? static_cast<float>(a.frameRate) : 30.0f;
        float frames = static_cast<float>(a.totalKeyframeCount);
        if (!a.keys.empty()) {
            const float last = static_cast<float>(a.keys.back().frame);
            if (last + 1.0f > frames) {
                frames = last + 1.0f;
            }
        }
        const float d = frames / fps;
        if (d > best) {
            best = d;
        }
    }
    return best;
}

Mat4x3 sampleChannel(const Kf2NodeAnimation& a, float timeSeconds) {
    if (a.keys.empty()) {
        return Mat4x3();
    }
    const float fps = a.frameRate > 0 ? static_cast<float>(a.frameRate) : 30.0f;
    float duration = static_cast<float>(a.totalKeyframeCount) / fps;
    if (duration < 1.0e-4f) {
        duration = (static_cast<float>(a.keys.back().frame) + 1.0f) / fps;
    }
    if (duration < 1.0e-4f) {
        return a.keys[0].objectToParent;
    }
    float t = timeSeconds;
    if (a.looping) {
        t = t - duration * std::floor(t / duration);
        if (t < 0.0f) {
            t += duration;
        }
    } else if (t > duration) {
        t = duration;
    }
    if (t < 0.0f) {
        t = 0.0f;
    }
    const float frame = t * fps;
    if (a.keys.size() == 1 || frame <= static_cast<float>(a.keys.front().frame)) {
        return a.keys.front().objectToParent;
    }
    if (frame >= static_cast<float>(a.keys.back().frame)) {
        return a.keys.back().objectToParent;
    }
    for (std::size_t i = 0; i + 1 < a.keys.size(); ++i) {
        const float f0 = static_cast<float>(a.keys[i].frame);
        const float f1 = static_cast<float>(a.keys[i + 1].frame);
        if (frame > f1) {
            continue;
        }
        if (f1 <= f0 + 1.0e-4f || a.interpolationMethod == 0) {
            return a.keys[i].objectToParent;
        }
        const float u = (frame - f0) / (f1 - f0);
        return lerpMat(a.keys[i].objectToParent, a.keys[i + 1].objectToParent, u);
    }
    return a.keys.back().objectToParent;
}

void kf2SampleAnimation(const Kf2File& file, float timeSeconds, std::vector<std::string>* names,
                        std::vector<Mat4x3>* locals) {
    names->clear();
    locals->clear();
    names->reserve(file.animations.size());
    locals->reserve(file.animations.size());
    for (std::size_t i = 0; i < file.animations.size(); ++i) {
        names->push_back(file.animations[i].targetName);
        locals->push_back(sampleChannel(file.animations[i], timeSeconds));
    }
}

int findNameIndex(const std::vector<std::string>& names, const std::string& key) {
    const std::string k = lowerAscii(key);
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (lowerAscii(names[i]) == k) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void worldsFromLocals(const std::vector<std::string>& names, const std::vector<Mat4x3>& locals,
                      const std::vector<std::string>& parents, std::vector<Mat4x3>* worlds) {
    worlds->assign(locals.begin(), locals.end());
    for (std::size_t i = 0; i < names.size(); ++i) {
        Mat4x3 world = locals[i];
        std::string walk = names[i];
        int guard = 0;
        while (guard++ < 64) {
            const int idx = findNameIndex(names, walk);
            if (idx < 0 || parents[static_cast<std::size_t>(idx)].empty()) {
                break;
            }
            walk = parents[static_cast<std::size_t>(idx)];
            const int p = findNameIndex(names, walk);
            if (p < 0) {
                break;
            }
            world = combine(locals[static_cast<std::size_t>(p)], world);
        }
        (*worlds)[i] = world;
    }
}

void kf2BuildSkeletonWorlds(const Kf2File& anim, float timeSeconds, const Kf2File* bind,
                            std::vector<std::string>* names, std::vector<Mat4x3>* worlds) {
    names->clear();
    worlds->clear();
    std::vector<std::string> localsNames;
    std::vector<Mat4x3> locals;
    std::vector<std::string> parents;
    if (bind != 0) {
        kf2SampleAnimation(*bind, 0.0f, &localsNames, &locals);
        parents.assign(localsNames.size(), std::string());
        for (std::size_t i = 0; i < bind->animations.size(); ++i) {
            const int idx = findNameIndex(localsNames, bind->animations[i].targetName);
            if (idx >= 0) {
                parents[static_cast<std::size_t>(idx)] = bind->animations[i].parentName;
            }
        }
    }
    std::vector<std::string> playNames;
    std::vector<Mat4x3> playLocals;
    kf2SampleAnimation(anim, timeSeconds, &playNames, &playLocals);
    for (std::size_t i = 0; i < playNames.size(); ++i) {
        const int idx = findNameIndex(localsNames, playNames[i]);
        if (idx >= 0) {
            locals[static_cast<std::size_t>(idx)] = playLocals[i];
        } else {
            localsNames.push_back(playNames[i]);
            locals.push_back(playLocals[i]);
            std::string parent;
            for (std::size_t a = 0; a < anim.animations.size(); ++a) {
                if (lowerAscii(anim.animations[a].targetName) == lowerAscii(playNames[i])) {
                    parent = anim.animations[a].parentName;
                    break;
                }
            }
            parents.push_back(parent);
        }
    }
    if (localsNames.empty()) {
        return;
    }
    worldsFromLocals(localsNames, locals, parents, worlds);
    *names = localsNames;
}

const Kf2Skin* pickSkin(const Kf2File& meshFile, const Kf2File* skinFile) {
    if (skinFile != 0 && !skinFile->skins.empty()) {
        return &skinFile->skins[0];
    }
    if (!meshFile.skins.empty()) {
        return &meshFile.skins[0];
    }
    return 0;
}

Mat4x3 lookupWorld(const std::vector<std::string>& names, const std::vector<Mat4x3>& worlds,
                   const std::string& key, bool* found) {
    const int idx = findNameIndex(names, key);
    if (idx >= 0) {
        if (found) {
            *found = true;
        }
        return worlds[static_cast<std::size_t>(idx)];
    }
    if (found) {
        *found = false;
    }
    return Mat4x3();
}

void poseGeometry(Kf2Mesh& mesh, const Kf2Skin* skin, const std::vector<std::string>& bindNames,
                  const std::vector<Mat4x3>& bindWorlds, const std::vector<std::string>& playNames,
                  const std::vector<Mat4x3>& playWorlds) {
    if (!mesh.hasGeometry) {
        return;
    }
    bool dummy = false;
    const Mat4x3 meshBind = mesh.hasNode
                                ? lookupWorld(bindNames, bindWorlds, mesh.node.name, &dummy)
                                : Mat4x3();
    const Mat4x3 meshPlay = mesh.hasNode
                                ? lookupWorld(playNames, playWorlds, mesh.node.name, &dummy)
                                : Mat4x3();
    std::vector<Vec3>& verts = mesh.geometry.vertices;
    std::vector<Vec3>& nrms = mesh.geometry.normals;
    for (std::size_t i = 0; i < verts.size(); ++i) {
        const Vec3 vLocal = verts[i];
        const Vec3 nLocal = i < nrms.size() ? nrms[i] : Vec3(0.0f, 1.0f, 0.0f);
        Vec3 posed = transformPoint(meshPlay, vLocal);
        Vec3 posedN = transformVector(meshPlay, nLocal);
        if (skin != 0 && i < skin->vertices.size() && !skin->vertices[i].bones.empty()) {
            const Kf2SkinVertex& sv = skin->vertices[i];
            const Vec3 vBind = transformPoint(meshBind, vLocal);
            const Vec3 nBind = transformVector(meshBind, nLocal);
            Vec3 acc(0.0f, 0.0f, 0.0f);
            Vec3 accN(0.0f, 0.0f, 0.0f);
            float wsum = 0.0f;
            for (std::size_t b = 0; b < sv.bones.size(); ++b) {
                const int bi = sv.bones[b];
                if (bi < 0 || bi >= static_cast<int>(skin->skeletonObjectNames.size())) {
                    continue;
                }
                const std::string& bone = skin->skeletonObjectNames[static_cast<std::size_t>(bi)];
                bool hasBind = false;
                bool hasPlay = false;
                const Mat4x3 bw = lookupWorld(bindNames, bindWorlds, bone, &hasBind);
                const Mat4x3 pw = lookupWorld(playNames, playWorlds, bone, &hasPlay);
                if (!hasBind || !hasPlay) {
                    continue;
                }
                const float w = b < sv.weights.size() ? sv.weights[b] : 1.0f;
                const Mat4x3 invB = inverseRigid(bw);
                acc += transformPoint(pw, transformPoint(invB, vBind)) * w;
                accN += transformVector(pw, transformVector(invB, nBind)) * w;
                wsum += w;
            }
            if (wsum > 1.0e-5f) {
                posed = acc * (1.0f / wsum);
                posedN = accN * (1.0f / wsum);
            }
        }
        verts[i] = posed;
        if (i < nrms.size()) {
            nrms[i] = normalize(posedN);
        }
    }
    if (mesh.hasNode) {
        mesh.node.objectToParent = Mat4x3();
        mesh.node.hasParent = false;
        mesh.node.parentName.clear();
    }
}

void kf2BuildSkinnedDrawMeshes(const Kf2File& meshFile, const Kf2File* skinFile,
                               const Kf2File* bindAnim, const Kf2File* playAnim, float timeSeconds,
                               std::vector<Kf2DrawMesh>& out) {
    const Kf2File* play = playAnim != 0 ? playAnim : bindAnim;
    std::vector<std::string> bindNames;
    std::vector<Mat4x3> bindWorlds;
    std::vector<std::string> playNames;
    std::vector<Mat4x3> playWorlds;
    if (bindAnim != 0 && !bindAnim->animations.empty()) {
        kf2BuildSkeletonWorlds(*bindAnim, 0.0f, 0, &bindNames, &bindWorlds);
    } else {
        kf2NodeWorldTransforms(meshFile, &bindNames, &bindWorlds);
    }
    if (play != 0 && !play->animations.empty()) {
        kf2BuildSkeletonWorlds(*play, timeSeconds, bindAnim, &playNames, &playWorlds);
    } else {
        playNames = bindNames;
        playWorlds = bindWorlds;
    }

    const Kf2Skin* skin = pickSkin(meshFile, skinFile);
    if (skin == 0) {
        kf2BuildDrawMeshes(meshFile, out);
        return;
    }
    Kf2File posed = meshFile;
    for (std::size_t i = 0; i < posed.meshes.size(); ++i) {
        poseGeometry(posed.meshes[i], skin, bindNames, bindWorlds, playNames, playWorlds);
    }
    kf2BuildDrawMeshes(posed, out);
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i].modelSpace = true;
        out[i].objectToParent = Mat4x3();
        out[i].parentName.clear();
    }
}

}  // namespace maxfx
