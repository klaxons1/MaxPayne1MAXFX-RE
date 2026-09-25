#include "maxfx/ldb/LdbReader.h"

#include "maxfx/core/Stream.h"

#include <fstream>
#include <sstream>

namespace maxfx {

static std::string formatOffset(std::size_t pos) {
    std::ostringstream oss;
    oss << "0x" << std::hex << pos;
    return oss.str();
}

LdbReader::LdbReader(TaggedReader& in) : in_(in) {}

Level LdbReader::loadFromFile(const std::string& path) {
    std::ifstream file(path.c_str(), std::ios::binary | std::ios::ate);
    if (!file) {
        throw ReadError("failed to open " + path, 0);
    }
    const std::streamoff end = file.tellg();
    if (end < 0) {
        throw ReadError("failed to size " + path, 0);
    }
    file.seekg(0, std::ios::beg);
    std::vector<unsigned char> bytes(static_cast<std::size_t>(end));
    if (end > 0) {
        file.read(reinterpret_cast<char*>(&bytes[0]), end);
        if (!file) {
            throw ReadError("failed to read " + path, 0);
        }
    }
    return loadFromMemory(bytes.empty() ? 0 : &bytes[0], bytes.size(), path);
}

Level LdbReader::loadFromMemory(const void* data, std::size_t size, const std::string& sourceName) {
    TaggedReader reader(static_cast<const std::uint8_t*>(data), size);
    LdbReader parser(reader);
    try {
        parser.parseAll();
    } catch (const ReadError& err) {
        std::ostringstream oss;
        oss << sourceName << ": " << err.what();
        throw ReadError(oss.str(), err.position());
    }
    if (!reader.eof()) {
        std::ostringstream oss;
        oss << sourceName << ": trailing " << reader.remaining()
            << " bytes after LDB parse (offset " << formatOffset(reader.position()) << ")";
        throw ReadError(oss.str(), reader.position());
    }
    return parser.level_;
}

void LdbReader::parseAll() {
    parseBsp();
    parseMaterials();
    parseExits();
    parseStaticMeshes();
    parseStaticLights();
    parseWaypoints();
    parseFsms();
    parseCharacters();
    parseTriggers();
    parseDynamicMeshes();
    parseItems();
    parsePointLights();
    parseRooms();
}

void LdbReader::parseBsp() {
    const int vertexCount = in_.readVectorHeader();
    level_.bsp.vertices.reserve(static_cast<std::size_t>(vertexCount));
    for (int i = 0; i < vertexCount; ++i) {
        level_.bsp.vertices.push_back(in_.readVec3());
    }

    const int polyCount = in_.readVectorHeader();
    level_.bsp.polygons.reserve(static_cast<std::size_t>(polyCount));
    for (int i = 0; i < polyCount; ++i) {
        BspPolygon poly;
        poly.vertexStart = in_.readInt();
        poly.vertexCount = in_.readInt();
        poly.polygonId = in_.readInt();
        poly.groupId = in_.readInt();
        poly.normal = in_.readVec3();
        poly.pivot = in_.readVec3();
        level_.bsp.polygons.push_back(poly);
    }

    const int nodeCount = in_.readVectorHeader();
    level_.bsp.nodes.reserve(static_cast<std::size_t>(nodeCount));
    for (int i = 0; i < nodeCount; ++i) {
        BspNode node;
        node.orientation = in_.readVec3();
        node.position = in_.readVec3();
        node.front.polygonIndexStart = in_.readInt();
        node.front.polygonCount = in_.readInt();
        node.front.nextNode = in_.readInt();
        node.back.polygonIndexStart = in_.readInt();
        node.back.polygonCount = in_.readInt();
        node.back.nextNode = in_.readInt();
        level_.bsp.nodes.push_back(node);
    }

    const int indexCount = in_.readVectorHeader();
    level_.bsp.polygonIndices.reserve(static_cast<std::size_t>(indexCount));
    for (int i = 0; i < indexCount; ++i) {
        level_.bsp.polygonIndices.push_back(in_.readInt());
    }
}

EmbeddedImage LdbReader::readEmbeddedImage(bool hasFileName) {
    EmbeddedImage img;
    if (hasFileName) {
        img.fileName = in_.readString();
        img.fileType = static_cast<int>(in_.readUInt());
        const int size = in_.readInt();
        if (size < 0) {
            throw ReadError("negative texture size", in_.position());
        }
        img.data = in_.readBytes(static_cast<std::size_t>(size));
    } else {
        img.id = in_.readInt();
        img.fileType = static_cast<int>(in_.readUInt());
        const int size = in_.readInt();
        if (size < 0) {
            throw ReadError("negative lightmap size", in_.position());
        }
        img.data = in_.readBytes(static_cast<std::size_t>(size));
    }
    return img;
}

void LdbReader::parseMaterials() {
    level_.version = in_.readInt();
    if (level_.version != kLdbVersionPC) {
        // Keep going: the Android decompile only checks this constant, and
        // community tools treat v32 as the sole PC layout. A mismatch is
        // almost certainly a different product (Max Payne 2 uses another
        // chunking scheme) so fail fast.
        std::ostringstream oss;
        oss << "unsupported LDB version " << level_.version << " (expected "
            << kLdbVersionPC << ")";
        throw ReadError(oss.str(), in_.position());
    }

    const int textureCount = in_.readInt();
    level_.textures.reserve(static_cast<std::size_t>(textureCount));
    for (int i = 0; i < textureCount; ++i) {
        level_.textures.push_back(readEmbeddedImage(true));
        level_.textures.back().id = i;
    }

    const int usedCount = in_.readMapHeader();
    level_.materials.reserve(static_cast<std::size_t>(usedCount));
    for (int i = 0; i < usedCount; ++i) {
        Material mat;
        mat.id = in_.readInt();
        in_.expectPairTag();
        mat.category = in_.readString();
        mat.name = in_.readString();
        level_.materials.push_back(mat);
    }

    // Second map is the MAX-ED editor order (category, name) -> id.
    const int sortedCount = in_.readMapHeader();
    for (int i = 0; i < sortedCount; ++i) {
        in_.expectPairTag();
        in_.readString();
        in_.readString();
        in_.readInt();
    }

    const int categoryCount = in_.readInt();
    for (int c = 0; c < categoryCount; ++c) {
        const std::string category = in_.readString();
        const int matCount = in_.readInt();
        for (int m = 0; m < matCount; ++m) {
            const std::string name = in_.readString();
            const std::string diffuse = in_.readString();
            const std::string alpha = in_.readString();
            const bool alphaTest = in_.readBool();
            const bool adult = in_.readBool();

            Material* found = 0;
            for (std::size_t i = 0; i < level_.materials.size(); ++i) {
                if (level_.materials[i].category == category && level_.materials[i].name == name) {
                    found = &level_.materials[i];
                    break;
                }
            }
            if (found == 0) {
                continue;  // unused material definition
            }
            found->diffuseTexture = level_.findTextureByFileName(diffuse);
            found->alphaTexture = level_.findTextureByFileName(alpha);
            found->alphaTest = alphaTest;
            found->adultContent = adult;
        }
    }

    const int lightmapCount = in_.readInt();
    level_.lightmaps.reserve(static_cast<std::size_t>(lightmapCount));
    for (int i = 0; i < lightmapCount; ++i) {
        level_.lightmaps.push_back(readEmbeddedImage(false));
    }
}

void LdbReader::parseExits() {
    const int count = in_.readInt();
    level_.exits.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        Exit exit;
        exit.name = in_.readString();
        const unsigned int vertexCount = in_.readUInt();
        exit.vertices.reserve(vertexCount);
        for (unsigned int v = 0; v < vertexCount; ++v) {
            exit.vertices.push_back(in_.readVec3());
        }
        exit.normal = in_.readVec3();
        exit.transform = in_.readMat4x3();
        exit.roomId = in_.readInt();
        exit.destinationRoomId = in_.readInt();
        exit.destinationExitName = in_.readString();
        const int regionCount = in_.readVectorHeader();
        exit.convexRegions.reserve(static_cast<std::size_t>(regionCount));
        for (int r = 0; r < regionCount; ++r) {
            ExitConvexRegion region;
            const int pointCount = in_.readVectorHeader();
            region.points.reserve(static_cast<std::size_t>(pointCount));
            for (int p = 0; p < pointCount; ++p) {
                region.points.push_back(in_.readInt());
            }
            exit.convexRegions.push_back(region);
        }
        level_.exits.push_back(exit);
    }
}

TextureVertex LdbReader::readTextureVertex() {
    TextureVertex tv;
    tv.vertexIndex = in_.readInt();
    tv.uv = in_.readVec2();
    tv.lightmapUv = in_.readVec2();
    tv.flags = in_.readUInt();
    tv.smooth = in_.readBool();
    return tv;
}

Polygon LdbReader::readPolygon() {
    Polygon poly;
    poly.id = in_.readInt();
    poly.textureVertexStart = in_.readInt();
    poly.vertexCount = in_.readInt();
    poly.normal = in_.readVec3();
    poly.engineMaterialType = in_.readUInt();
    poly.materialId = in_.readInt();
    poly.lightmapId = in_.readInt();
    poly.maxEdgeLength = in_.readFloat();
    poly.maxAngle = in_.readFloat();
    poly.smoothingGroup = in_.readInt();
    return poly;
}

std::vector<RadiositySample> LdbReader::readRadiosityMap() {
    std::vector<RadiositySample> samples;
    const int count = in_.readMapHeader();
    samples.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        RadiositySample sample;
        sample.key = in_.readInt();
        sample.value = in_.readVec3();
        samples.push_back(sample);
    }
    return samples;
}

void LdbReader::parseStaticMeshes() {
    const int tvCount = in_.readVectorHeader();
    level_.staticTextureVertices.reserve(static_cast<std::size_t>(tvCount));
    for (int i = 0; i < tvCount; ++i) {
        level_.staticTextureVertices.push_back(readTextureVertex());
    }

    const int meshCount = in_.readInt();
    level_.staticMeshes.reserve(static_cast<std::size_t>(meshCount));
    for (int i = 0; i < meshCount; ++i) {
        StaticMesh mesh;
        mesh.groupId = in_.readInt();
        const unsigned int vertexCount = in_.readUInt();
        mesh.vertices.reserve(vertexCount);
        for (unsigned int v = 0; v < vertexCount; ++v) {
            mesh.vertices.push_back(in_.readVec3());
        }
        const int normalCount = in_.readVectorHeader();
        mesh.normals.reserve(static_cast<std::size_t>(normalCount));
        for (int n = 0; n < normalCount; ++n) {
            mesh.normals.push_back(in_.readVec3());
        }
        mesh.transform = in_.readMat4x3();
        const int polyCount = in_.readInt();
        mesh.polygons.reserve(static_cast<std::size_t>(polyCount));
        for (int p = 0; p < polyCount; ++p) {
            mesh.polygons.push_back(readPolygon());
        }
        mesh.radiosity = readRadiosityMap();
        level_.staticMeshes.push_back(mesh);
    }
}

EntityProperties LdbReader::readEntityProperties() {
    EntityProperties props;
    props.name = in_.readString();
    props.objectToRoom = in_.readMat4x3();
    props.objectToParent = in_.readMat4x3();
    props.roomId = in_.readInt();
    props.parentDynamicMesh = in_.readString();
    return props;
}

void LdbReader::parseStaticLights() {
    const int count = in_.readInt();
    level_.staticLights.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        StaticLight light;
        light.sharedName = in_.readString();
        light.properties = readEntityProperties();
        light.rotation = in_.readMat3();
        light.r = in_.readFloat();
        light.g = in_.readFloat();
        light.b = in_.readFloat();
        light.a = in_.readFloat();
        light.area = in_.readFloat();
        light.intensity = in_.readFloat();
        light.hotspotAngle = in_.readFloat();
        light.falloffAngle = in_.readFloat();
        light.falloffRange = in_.readFloat();
        light.colorMultiplier = in_.readFloat();
        level_.staticLights.push_back(light);
    }
}

void LdbReader::parseWaypoints() {
    const int count = in_.readInt();
    level_.waypoints.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        Waypoint wp;
        wp.sharedName = in_.readString();
        wp.properties = readEntityProperties();
        wp.type = in_.readInt();
        level_.waypoints.push_back(wp);
    }
}

FsmMessages LdbReader::readMessageList() {
    FsmMessages list;
    const int count = in_.readVectorHeader();
    list.messages.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        list.messages.push_back(in_.readString());
    }
    return list;
}

FsmEvent LdbReader::readEvent() {
    FsmEvent event;
    event.name = in_.readString();
    event.before = readMessageList();
    const int specificCount = in_.readVectorHeader();
    event.stateSpecific.reserve(static_cast<std::size_t>(specificCount));
    for (int i = 0; i < specificCount; ++i) {
        FsmStateSpecific spec;
        spec.stateName = in_.readString();
        spec.messages = readMessageList();
        event.stateSpecific.push_back(spec);
    }
    event.after = readMessageList();
    return event;
}

void LdbReader::parseFsms() {
    const int count = in_.readInt();
    level_.fsms.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        Fsm fsm;
        fsm.sharedName = in_.readString();
        fsm.properties = readEntityProperties();

        const int stateCount = in_.readVectorHeader();
        fsm.states.reserve(static_cast<std::size_t>(stateCount));
        for (int s = 0; s < stateCount; ++s) {
            fsm.states.push_back(in_.readString());
        }
        fsm.defaultState = in_.readString();

        fsm.startupBefore = readMessageList();

        // Startup "state specific" block on PC v32 is a flat string vector,
        // not the nested message structure used by the other event lists.
        // Matching the working community parser keeps us in sync with the file.
        const int startupSpecific = in_.readVectorHeader();
        fsm.startupStateSpecific.reserve(static_cast<std::size_t>(startupSpecific));
        for (int s = 0; s < startupSpecific; ++s) {
            fsm.startupStateSpecific.push_back(in_.readString());
        }

        fsm.startupAfter = readMessageList();

        const int switchCount = in_.readVectorHeader();
        fsm.stateSwitch.reserve(static_cast<std::size_t>(switchCount));
        for (int e = 0; e < switchCount; ++e) {
            fsm.stateSwitch.push_back(readEvent());
        }

        const int customCount = in_.readVectorHeader();
        fsm.customString.reserve(static_cast<std::size_t>(customCount));
        for (int e = 0; e < customCount; ++e) {
            fsm.customString.push_back(readEvent());
        }

        const int entityCount = in_.readVectorHeader();
        fsm.entityEvents.reserve(static_cast<std::size_t>(entityCount));
        for (int e = 0; e < entityCount; ++e) {
            fsm.entityEvents.push_back(readEvent());
        }

        level_.fsms.push_back(fsm);
    }
}

void LdbReader::parseCharacters() {
    const int count = in_.readInt();
    level_.characters.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        Character ch;
        ch.sharedName = in_.readString();
        ch.properties = readEntityProperties();
        ch.characterName = in_.readString();
        ch.onStartup = readMessageList();
        ch.onDeath = readMessageList();
        ch.onActivate = readMessageList();
        ch.onSpecial = readMessageList();
        level_.characters.push_back(ch);
    }
}

void LdbReader::parseTriggers() {
    const int count = in_.readInt();
    level_.triggers.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        Trigger tr;
        tr.sharedName = in_.readString();
        tr.properties = readEntityProperties();
        tr.radius = in_.readFloat();
        tr.type = in_.readInt();
        level_.triggers.push_back(tr);
    }
}

AnimationGraph LdbReader::readAnimationGraph() {
    AnimationGraph graph;
    graph.header = in_.readInt();
    graph.majorVersion = in_.readInt();
    graph.minorVersion = in_.readInt();
    const int samples = in_.readInt();
    graph.samples.reserve(static_cast<std::size_t>(samples));
    for (int i = 0; i < samples; ++i) {
        graph.samples.push_back(in_.readFloat());
    }
    return graph;
}

void LdbReader::parseDynamicMeshes() {
    const int tvCount = in_.readVectorHeader();
    level_.dynamicTextureVertices.reserve(static_cast<std::size_t>(tvCount));
    for (int i = 0; i < tvCount; ++i) {
        level_.dynamicTextureVertices.push_back(readTextureVertex());
    }

    const int meshCount = in_.readInt();
    level_.dynamicMeshes.reserve(static_cast<std::size_t>(meshCount));
    for (int i = 0; i < meshCount; ++i) {
        DynamicMesh mesh;
        mesh.name = in_.readString();
        const unsigned int vertexCount = in_.readUInt();
        mesh.vertices.reserve(vertexCount);
        for (unsigned int v = 0; v < vertexCount; ++v) {
            mesh.vertices.push_back(in_.readVec3());
        }
        const int normalCount = in_.readVectorHeader();
        mesh.normals.reserve(static_cast<std::size_t>(normalCount));
        for (int n = 0; n < normalCount; ++n) {
            mesh.normals.push_back(in_.readVec3());
        }
        mesh.transform = in_.readMat4x3();
        const int polyCount = in_.readInt();
        mesh.polygons.reserve(static_cast<std::size_t>(polyCount));
        for (int p = 0; p < polyCount; ++p) {
            mesh.polygons.push_back(readPolygon());
        }
        mesh.radiosity = readRadiosityMap();
        mesh.properties = readEntityProperties();

        const int animCount = in_.readInt();
        mesh.animations.reserve(static_cast<std::size_t>(animCount));
        for (int a = 0; a < animCount; ++a) {
            MeshAnimation anim;
            anim.name = in_.readString();
            anim.lengthSeconds = in_.readFloat();
            anim.startTransform = in_.readMat4x3();
            anim.endTransform = in_.readMat4x3();
            anim.leavingFirstKeyframe = readMessageList();
            anim.returningFirstKeyframe = readMessageList();
            anim.reachingSecondKeyframe = readMessageList();
            anim.translation = readAnimationGraph();
            anim.rotation = readAnimationGraph();
            mesh.animations.push_back(anim);
        }

        mesh.config.dynamicCollisions = in_.readBool();
        mesh.config.bulletCollisions = in_.readBool();
        mesh.config.lightMapped = in_.readBool();
        mesh.config.continuousUpdate = in_.readBool();
        mesh.config.pointLightAffected = in_.readBool();
        mesh.config.blockExplosions = in_.readBool();
        mesh.bsp[0] = in_.readInt();
        mesh.bsp[1] = in_.readInt();
        mesh.bsp[2] = in_.readInt();
        mesh.bsp[3] = in_.readInt();
        level_.dynamicMeshes.push_back(mesh);
    }
}

void LdbReader::parseItems() {
    const int count = in_.readInt();
    level_.items.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        LevelItem item;
        item.sharedName = in_.readString();
        item.properties = readEntityProperties();
        item.itemName = in_.readString();
        level_.items.push_back(item);
    }
}

void LdbReader::parsePointLights() {
    const int count = in_.readInt();
    level_.pointLights.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        PointLight light;
        light.id = in_.readInt();
        light.properties = readEntityProperties();
        light.r = in_.readFloat();
        light.g = in_.readFloat();
        light.b = in_.readFloat();
        light.a = in_.readFloat();
        light.falloff = in_.readFloat();
        light.intensity = in_.readFloat();
        level_.pointLights.push_back(light);
    }
}

void LdbReader::parseRooms() {
    const int count = in_.readInt();
    level_.rooms.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        Room room;
        room.id = in_.readInt();

        int n = in_.readVectorHeader();
        room.staticMeshes.reserve(static_cast<std::size_t>(n));
        for (int k = 0; k < n; ++k) {
            room.staticMeshes.push_back(in_.readInt());
        }

        n = in_.readVectorHeader();
        room.staticLights.reserve(static_cast<std::size_t>(n));
        for (int k = 0; k < n; ++k) {
            room.staticLights.push_back(in_.readString());
        }

        n = in_.readVectorHeader();
        room.exits.reserve(static_cast<std::size_t>(n));
        for (int k = 0; k < n; ++k) {
            room.exits.push_back(in_.readString());
        }

        n = in_.readVectorHeader();
        room.startPoints.reserve(static_cast<std::size_t>(n));
        for (int k = 0; k < n; ++k) {
            room.startPoints.push_back(in_.readString());
        }

        n = in_.readVectorHeader();
        room.fsms.reserve(static_cast<std::size_t>(n));
        for (int k = 0; k < n; ++k) {
            room.fsms.push_back(in_.readString());
        }

        n = in_.readVectorHeader();
        room.characters.reserve(static_cast<std::size_t>(n));
        for (int k = 0; k < n; ++k) {
            room.characters.push_back(in_.readString());
        }

        n = in_.readVectorHeader();
        room.triggers.reserve(static_cast<std::size_t>(n));
        for (int k = 0; k < n; ++k) {
            room.triggers.push_back(in_.readString());
        }

        n = in_.readVectorHeader();
        room.dynamicMeshes.reserve(static_cast<std::size_t>(n));
        for (int k = 0; k < n; ++k) {
            room.dynamicMeshes.push_back(in_.readString());
        }

        n = in_.readVectorHeader();
        room.items.reserve(static_cast<std::size_t>(n));
        for (int k = 0; k < n; ++k) {
            room.items.push_back(in_.readString());
        }

        n = in_.readVectorHeader();
        room.pointLights.reserve(static_cast<std::size_t>(n));
        for (int k = 0; k < n; ++k) {
            room.pointLights.push_back(in_.readInt());
        }

        room.name = in_.readString();
        room.aiNetDensity = in_.readFloat();
        room.bsp[0] = in_.readInt();
        room.bsp[1] = in_.readInt();
        room.bsp[2] = in_.readInt();
        room.bsp[3] = in_.readInt();
        level_.rooms.push_back(room);
    }
}

}  // namespace maxfx
