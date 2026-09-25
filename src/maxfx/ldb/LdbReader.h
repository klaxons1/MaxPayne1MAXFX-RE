#ifndef MAXFX_LDB_LDBREADER_H
#define MAXFX_LDB_LDBREADER_H

#include "maxfx/ldb/Ldb.h"

#include <string>

namespace maxfx {

class TaggedReader;

// Parses a PC Max Payne 1 LDB (file version 32).
// Android's X_LevelDBLevel::load expects the same version; if a future dump
// uses a different layout the reader will throw rather than silently desync.
class LdbReader {
public:
    static Level loadFromFile(const std::string& path);
    static Level loadFromMemory(const void* data, std::size_t size, const std::string& sourceName);

private:
    explicit LdbReader(TaggedReader& in);

    void parseAll();
    void parseBsp();
    void parseMaterials();
    void parseExits();
    void parseStaticMeshes();
    void parseStaticLights();
    void parseWaypoints();
    void parseFsms();
    void parseCharacters();
    void parseTriggers();
    void parseDynamicMeshes();
    void parseItems();
    void parsePointLights();
    void parseRooms();

    EntityProperties readEntityProperties();
    FsmMessages readMessageList();
    FsmEvent readEvent();
    TextureVertex readTextureVertex();
    Polygon readPolygon();
    std::vector<RadiositySample> readRadiosityMap();
    AnimationGraph readAnimationGraph();
    EmbeddedImage readEmbeddedImage(bool hasFileName);

    TaggedReader& in_;
    Level level_;
};

}  // namespace maxfx

#endif  // MAXFX_LDB_LDBREADER_H
