// X_SharedDBLevel / X_SharedDBLevelContainer stand-in for data/database/levels/levels.txt.
//
// X_SharedDB::construct loads the container as ("Levels", "levels.txt"). Each
// top-level [id] block becomes one X_SharedDBLevel:
//
//   R_Script::blockCheck("properties")
//   lock("Properties"); parse(off_DB3F48); unlock();
//   lock("Difficulty"); parse(MaximumHealth...); unlock();
//   lock("Timedmode");  parse(PlayTime, EnemyTimeBonus); unlock();
//
// Field names follow the MaxEd SDK layout plus the Android post-process pair
// that X_SharedDBLevel::construct copies from BSS (PPMult / PPAdd). UpVector
// is not a script field — the engine sets it to normalise(-Gravity).
#ifndef MAXFX_LEVELS_LEVELS_H
#define MAXFX_LEVELS_LEVELS_H

#include "maxfx/core/Math.h"
#include "maxfx/script/Script.h"

#include <string>
#include <vector>

namespace maxfx {

struct LevelDifficulty {
    float maximumHealth;  // percent of player's maximum health
    float minimumHealth;
    int minimumDeaths;
    int maximumDeaths;
    float minimumTime;  // seconds
    float maximumTime;
    float hardcoreHealth;
    int hardcoreDeaths;
    float hardcoreTime;

    LevelDifficulty();
};

struct LevelTimedMode {
    float playTime;         // initial playtime in seconds
    float enemyTimeBonus;   // extra seconds per kill

    LevelTimedMode();
};

struct LevelInfo {
    // [id] tag — this is what gm_init( id ) / the console uses.
    std::string id;

    // [Properties]
    std::string levelName;
    std::string directory;
    std::string filename;  // "Level = foo.ldb"
    Vec3 gravity;
    Vec3 upVector;  // computed: normalise(-gravity)
    Vec3 ambientColor;
    Vec3 postProcessMultiply;  // Android extra, default (1,1,1)
    Vec3 postProcessAdd;       // Android extra, default (0,0,0)
    std::string worldSphereName;
    int debrisProjectileCountInLevel;
    int debrisProjectileCountPerRoom;
    std::string playerSkinName;
    std::string loadingScreen;
    bool aiEnabled;
    std::string playerStartingPlace;
    bool foggingEnabled;
    Vec3 fogColor;
    float fogStart;
    float fogEnd;
    bool startupLevel;
    bool exitLevel;
    float aiNodeCastHeight;

    LevelDifficulty difficulty;
    LevelTimedMode timedMode;

    // Assignments in [Properties] that are not in the known table
    // (so an official file with extra Android keys is not silently dropped).
    std::vector<ScriptAssignment> extraProperties;

    unsigned line;
    std::string sourceFile;

    LevelInfo();

    // dataDir/Directory/Level  (forward slashes), matching
    // X_SharedDBLevel::construct: dataDir + "/" + filename after assigning Directory.
    std::string relativeLdbPath() const;
};

struct LevelDatabase {
    std::string sourceFile;
    std::string dataDirectory;  // folder that contained levels.txt
    std::vector<LevelInfo> levels;

    const LevelInfo* findById(const std::string& id) const;
};

class LevelsReader {
public:
    // Parse an already-loaded R_Script whose cursor is at the file root.
    // `dataDirectory` is the folder X_SharedDB cd's into (".../database/levels").
    static LevelDatabase parseScript(Script& script, const std::string& dataDirectory);

    static LevelDatabase loadFile(const std::string& path);

    // Search typical drop-in locations for data/database/levels/levels.txt:
    // argv path, <exe>/data/database/levels, cwd, parent-of-exe.
    static std::string locateLevelsTxt(const std::string& hint = std::string());
};

}  // namespace maxfx

#endif  // MAXFX_LEVELS_LEVELS_H
