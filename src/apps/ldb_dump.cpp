// Command-line LDB inspector. Parses a Max Payne 1 level and prints a summary.
// Usage: ldb-dump [--json] path/to/level.ldb

#include "maxfx/core/Stream.h"
#include "maxfx/image/Image.h"
#include "maxfx/ldb/LdbReader.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

using maxfx::Level;
using maxfx::LdbReader;
using maxfx::ReadError;
using maxfx::textureFileTypeName;
using maxfx::triggerTypeName;

static void printSummary(const Level& level, const std::string& path) {
    std::printf("file            : %s\n", path.c_str());
    std::printf("version         : %d\n", level.version);
    std::printf("bsp vertices    : %zu\n", level.bsp.vertices.size());
    std::printf("bsp polygons    : %zu\n", level.bsp.polygons.size());
    std::printf("bsp nodes       : %zu\n", level.bsp.nodes.size());
    std::printf("bsp indices     : %zu\n", level.bsp.polygonIndices.size());
    std::printf("textures        : %zu\n", level.textures.size());
    std::printf("materials       : %zu\n", level.materials.size());
    std::printf("lightmaps       : %zu\n", level.lightmaps.size());
    std::printf("exits           : %zu\n", level.exits.size());
    std::printf("static texverts : %zu\n", level.staticTextureVertices.size());
    std::printf("static meshes   : %zu\n", level.staticMeshes.size());
    std::printf("static lights   : %zu\n", level.staticLights.size());
    std::printf("waypoints       : %zu\n", level.waypoints.size());
    std::printf("fsms            : %zu\n", level.fsms.size());
    std::printf("characters      : %zu\n", level.characters.size());
    std::printf("triggers        : %zu\n", level.triggers.size());
    std::printf("dynamic texverts: %zu\n", level.dynamicTextureVertices.size());
    std::printf("dynamic meshes  : %zu\n", level.dynamicMeshes.size());
    std::printf("items           : %zu\n", level.items.size());
    std::printf("point lights    : %zu\n", level.pointLights.size());
    std::printf("rooms           : %zu\n", level.rooms.size());

    int jpg = 0, tga = 0, pcx = 0, other = 0;
    std::size_t texBytes = 0;
    for (std::size_t i = 0; i < level.textures.size(); ++i) {
        texBytes += level.textures[i].data.size();
        switch (level.textures[i].fileType) {
            case maxfx::kTexJpg:
                ++jpg;
                break;
            case maxfx::kTexTga:
                ++tga;
                break;
            case maxfx::kTexPcx:
                ++pcx;
                break;
            default:
                ++other;
                break;
        }
    }
    std::printf("\ntexture types   : jpg=%d tga=%d pcx=%d other=%d  (%zu bytes)\n", jpg, tga, pcx,
                other, texBytes);

    std::size_t staticVerts = 0, staticPolys = 0, staticTris = 0;
    for (std::size_t i = 0; i < level.staticMeshes.size(); ++i) {
        staticVerts += level.staticMeshes[i].vertices.size();
        staticPolys += level.staticMeshes[i].polygons.size();
        for (std::size_t p = 0; p < level.staticMeshes[i].polygons.size(); ++p) {
            const int n = level.staticMeshes[i].polygons[p].vertexCount;
            if (n >= 3) {
                staticTris += static_cast<std::size_t>(n - 2);
            }
        }
    }
    std::size_t dynVerts = 0, dynPolys = 0, dynTris = 0, dynAnims = 0;
    for (std::size_t i = 0; i < level.dynamicMeshes.size(); ++i) {
        dynVerts += level.dynamicMeshes[i].vertices.size();
        dynPolys += level.dynamicMeshes[i].polygons.size();
        dynAnims += level.dynamicMeshes[i].animations.size();
        for (std::size_t p = 0; p < level.dynamicMeshes[i].polygons.size(); ++p) {
            const int n = level.dynamicMeshes[i].polygons[p].vertexCount;
            if (n >= 3) {
                dynTris += static_cast<std::size_t>(n - 2);
            }
        }
    }
    std::printf("static geometry : %zu verts, %zu polys, %zu tris (fan)\n", staticVerts, staticPolys,
                staticTris);
    std::printf("dynamic geometry: %zu verts, %zu polys, %zu tris, %zu animations\n", dynVerts,
                dynPolys, dynTris, dynAnims);

    std::printf("\nrooms:\n");
    for (std::size_t i = 0; i < level.rooms.size(); ++i) {
        const maxfx::Room& room = level.rooms[i];
        std::printf("  [%2d] %-32s  sm=%zu dyn=%zu char=%zu item=%zu trig=%zu exit=%zu\n", room.id,
                    room.name.c_str(), room.staticMeshes.size(), room.dynamicMeshes.size(),
                    room.characters.size(), room.items.size(), room.triggers.size(),
                    room.exits.size());
    }

    std::printf("\ncharacters:\n");
    for (std::size_t i = 0; i < level.characters.size(); ++i) {
        const maxfx::Character& ch = level.characters[i];
        std::printf("  %-40s  skin=%-20s room=%d\n", ch.sharedName.c_str(), ch.characterName.c_str(),
                    ch.properties.roomId);
    }

    std::printf("\nitems:\n");
    for (std::size_t i = 0; i < level.items.size(); ++i) {
        const maxfx::LevelItem& it = level.items[i];
        std::printf("  %-40s  class=%s room=%d\n", it.sharedName.c_str(), it.itemName.c_str(),
                    it.properties.roomId);
    }

    std::printf("\njumppoints / startpoints:\n");
    for (std::size_t i = 0; i < level.waypoints.size(); ++i) {
        const maxfx::Waypoint& wp = level.waypoints[i];
        if (wp.type == 0) {
            continue;
        }
        const maxfx::Vec3 t = wp.properties.objectToRoom.translation();
        std::printf("  type=%d room=%d  (%7.2f %7.2f %7.2f)  %s\n", wp.type, wp.properties.roomId, t.x,
                    t.y, t.z, wp.sharedName.c_str());
    }

    int decoded = 0, failed = 0;
    for (std::size_t i = 0; i < level.textures.size(); ++i) {
        maxfx::Image img;
        std::string err;
        if (maxfx::decodeEmbeddedImage(level.textures[i].fileType, level.textures[i].data.empty()
                                                                        ? 0
                                                                        : &level.textures[i].data[0],
                                       level.textures[i].data.size(), img, &err)) {
            ++decoded;
        } else {
            ++failed;
            if (failed <= 8) {
                std::printf("  texture decode failed (%s %s): %s\n",
                            textureFileTypeName(level.textures[i].fileType),
                            level.textures[i].fileName.c_str(), err.c_str());
            }
        }
    }
    int lmOk = 0, lmFail = 0;
    for (std::size_t i = 0; i < level.lightmaps.size(); ++i) {
        maxfx::Image img;
        std::string err;
        if (maxfx::decodeEmbeddedImage(level.lightmaps[i].fileType,
                                       level.lightmaps[i].data.empty() ? 0
                                                                       : &level.lightmaps[i].data[0],
                                       level.lightmaps[i].data.size(), img, &err)) {
            ++lmOk;
        } else {
            ++lmFail;
        }
    }
    std::printf("\ndecoded textures : %d ok, %d failed\n", decoded, failed);
    std::printf("decoded lightmaps: %d ok, %d failed\n", lmOk, lmFail);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <level.ldb>\n", argv[0]);
        return 1;
    }
    const char* path = argv[argc - 1];
    try {
        const Level level = LdbReader::loadFromFile(path);
        printSummary(level, path);
    } catch (const ReadError& err) {
        std::fprintf(stderr, "parse error at 0x%zx: %s\n", err.position(), err.what());
        return 2;
    } catch (const std::exception& err) {
        std::fprintf(stderr, "error: %s\n", err.what());
        return 3;
    }
    return 0;
}
