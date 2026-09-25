// Command-line levels.txt inspector.
// Usage: levels-dump [--json] [path/to/levels.txt | path/to/data]
//
// With no path, searches <exe>/data/database/levels/levels.txt, cwd, docs/.

#include "maxfx/core/Fs.h"
#include "maxfx/levels/Levels.h"
#include "maxfx/script/Script.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using maxfx::LevelDatabase;
using maxfx::LevelInfo;
using maxfx::LevelsReader;
using maxfx::ScriptError;

static void printVec3(const char* name, const maxfx::Vec3& v) {
    std::printf("    %-32s (%g, %g, %g)\n", name, v.x, v.y, v.z);
}

static void printStr(const char* name, const std::string& v) {
    std::printf("    %-32s \"%s\"\n", name, v.c_str());
}

static void printInt(const char* name, int v) {
    std::printf("    %-32s %d\n", name, v);
}

static void printFloat(const char* name, float v) {
    std::printf("    %-32s %g\n", name, v);
}

static void printBool(const char* name, bool v) {
    std::printf("    %-32s %s\n", name, v ? "TRUE" : "FALSE");
}

static void printLevel(const LevelInfo& level, const std::string& dataDir) {
    std::printf("[%s]  (line %u)\n", level.id.c_str(), level.line);
    std::printf("  [Properties]\n");
    printStr("LevelName", level.levelName);
    printStr("Directory", level.directory);
    printStr("Level", level.filename);
    printVec3("Gravity", level.gravity);
    printVec3("UpVector (computed)", level.upVector);
    printVec3("AmbientColor", level.ambientColor);
    printVec3("PostProcessMultiply", level.postProcessMultiply);
    printVec3("PostProcessAdd", level.postProcessAdd);
    printStr("WorldSphere", level.worldSphereName);
    printInt("DebrisProjectileCountInLevel", level.debrisProjectileCountInLevel);
    printInt("DebrisProjectileCountPerRoom", level.debrisProjectileCountPerRoom);
    printStr("PlayerSkinName", level.playerSkinName);
    printStr("LoadingScreen", level.loadingScreen);
    printBool("EnableAI", level.aiEnabled);
    printStr("PlayerStartingPlace", level.playerStartingPlace);
    printBool("Fogging", level.foggingEnabled);
    printVec3("FogColor", level.fogColor);
    printFloat("FogStart", level.fogStart);
    printFloat("FogEnd", level.fogEnd);
    printBool("StartupLevel", level.startupLevel);
    printBool("ExitLevel", level.exitLevel);
    printFloat("AINodeCastHeight", level.aiNodeCastHeight);
    std::printf("    %-32s %s\n", "relative .ldb", level.relativeLdbPath().c_str());
    if (!dataDir.empty()) {
        std::printf("    %-32s %s\n", "resolved .ldb",
                    maxfx::joinPath(dataDir, level.relativeLdbPath()).c_str());
    }
    for (std::size_t i = 0; i < level.extraProperties.size(); ++i) {
        std::printf("    extra %-26s %s\n", level.extraProperties[i].lvalue.c_str(),
                    level.extraProperties[i].rvalue.c_str());
    }

    std::printf("  [Difficulty]\n");
    printFloat("MaximumHealth", level.difficulty.maximumHealth);
    printFloat("MinimumHealth", level.difficulty.minimumHealth);
    printInt("MinimumDeaths", level.difficulty.minimumDeaths);
    printInt("MaximumDeaths", level.difficulty.maximumDeaths);
    printFloat("MinimumTime", level.difficulty.minimumTime);
    printFloat("MaximumTime", level.difficulty.maximumTime);
    printFloat("HardcoreHealth", level.difficulty.hardcoreHealth);
    printInt("HardcoreDeaths", level.difficulty.hardcoreDeaths);
    printFloat("HardcoreTime", level.difficulty.hardcoreTime);

    std::printf("  [Timedmode]\n");
    printFloat("PlayTime", level.timedMode.playTime);
    printFloat("EnemyTimeBonus", level.timedMode.enemyTimeBonus);
    std::printf("\n");
}

static void printJson(const LevelDatabase& db) {
    std::printf("{\n  \"file\": \"");
    for (std::size_t i = 0; i < db.sourceFile.size(); ++i) {
        const char c = db.sourceFile[i];
        if (c == '\\' || c == '"') {
            std::putchar('\\');
        }
        std::putchar(c);
    }
    std::printf("\",\n  \"count\": %zu,\n  \"levels\": [\n", db.levels.size());
    for (std::size_t i = 0; i < db.levels.size(); ++i) {
        const LevelInfo& L = db.levels[i];
        std::printf("    {\"id\": \"%s\", \"levelName\": \"%s\", \"ldb\": \"%s\", "
                    "\"start\": \"%s\", \"startup\": %s}%s\n",
                    L.id.c_str(), L.levelName.c_str(), L.relativeLdbPath().c_str(),
                    L.playerStartingPlace.c_str(), L.startupLevel ? "true" : "false",
                    i + 1 == db.levels.size() ? "" : ",");
    }
    std::printf("  ]\n}\n");
}

int main(int argc, char** argv) {
    bool json = false;
    std::string hint;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--json") == 0) {
            json = true;
        } else if (argv[i][0] == '-') {
            std::fprintf(stderr, "usage: levels-dump [--json] [levels.txt | data dir]\n");
            return 2;
        } else {
            hint = argv[i];
        }
    }

    std::string path;
    if (!hint.empty() && maxfx::isFile(hint)) {
        path = hint;
    } else {
        path = LevelsReader::locateLevelsTxt(hint);
    }
    if (path.empty()) {
        std::fprintf(stderr,
                     "levels-dump: could not find levels.txt\n"
                     "  pass a path, or drop the file at data/database/levels/levels.txt\n");
        return 1;
    }

    try {
        const LevelDatabase db = LevelsReader::loadFile(path);
        if (json) {
            printJson(db);
            return 0;
        }
        std::printf("file            : %s\n", db.sourceFile.c_str());
        std::printf("data directory  : %s\n", db.dataDirectory.c_str());
        std::printf("levels          : %zu\n\n", db.levels.size());
        for (std::size_t i = 0; i < db.levels.size(); ++i) {
            printLevel(db.levels[i], db.dataDirectory);
        }
        return 0;
    } catch (const ScriptError& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "levels-dump: %s\n", e.what());
        return 1;
    }
}
