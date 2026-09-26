#include "maxfx/kf2/Kf2.h"

#include "maxfx/core/Fs.h"
#include "maxfx/core/Stream.h"

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
        for (std::map<int, Kf2SkinVertex>::iterator it = byVertex.begin(); it != byVertex.end();
             ++it) {
            skin.vertices.push_back(it->second);
        }
        const int nSkin = in.readInt();
        for (int i = 0; i < nSkin; ++i) {
            skin.skinObjectNames.push_back(in.readString());
        }
        const int nSkel = in.readInt();
        for (int i = 0; i < nSkel; ++i) {
            skin.skeletonObjectNames.push_back(in.readString());
        }
        const int extra = in.readInt();
        for (int i = 0; i < extra; ++i) {
            in.readInt();
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

}  // namespace maxfx
