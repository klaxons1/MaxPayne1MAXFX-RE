#include "maxfx/levels/Levels.h"

#include "maxfx/core/Fs.h"

#include <cmath>

namespace maxfx {
namespace {

Vec3 vec3(float x, float y, float z) { return Vec3(x, y, z); }

Vec3 parseVec3(Script& script, const char* name) {
    const std::string rv = script.assignment(name);
    float v[3];
    parseScriptFloat3(rv, v, name, script.current().line, script.sourceName());
    return vec3(v[0], v[1], v[2]);
}

std::string requireString(Script& script, const char* name) {
    if (!script.hasAssignment(name)) {
        throw ScriptError(std::string("R_Script: Unknown variable \"") + name +
                              "\" in [" + script.blockName() + "]",
                          script.sourceName(), script.current().line);
    }
    return parseScriptString(script.assignment(name));
}

int requireInt(Script& script, const char* name) {
    if (!script.hasAssignment(name)) {
        throw ScriptError(std::string("R_Script: Unknown variable \"") + name +
                              "\" in [" + script.blockName() + "]",
                          script.sourceName(), script.current().line);
    }
    return parseScriptInt(script.assignment(name), name, script.current().line,
                          script.sourceName());
}

float requireFloat(Script& script, const char* name) {
    if (!script.hasAssignment(name)) {
        throw ScriptError(std::string("R_Script: Unknown variable \"") + name +
                              "\" in [" + script.blockName() + "]",
                          script.sourceName(), script.current().line);
    }
    return parseScriptFloat(script.assignment(name), name, script.current().line,
                            script.sourceName());
}

Vec3 requireVec3(Script& script, const char* name) {
    if (!script.hasAssignment(name)) {
        throw ScriptError(std::string("R_Script: Unknown variable \"") + name +
                              "\" in [" + script.blockName() + "]",
                          script.sourceName(), script.current().line);
    }
    return parseVec3(script, name);
}

bool requireBool(Script& script, const char* name) { return requireInt(script, name) != 0; }

float optionalFloat(Script& script, const char* name, float fallback) {
    if (!script.hasAssignment(name)) {
        return fallback;
    }
    return parseScriptFloat(script.assignment(name), name, script.current().line,
                            script.sourceName());
}

Vec3 optionalVec3(Script& script, const char* name, const Vec3& fallback) {
    if (!script.hasAssignment(name)) {
        return fallback;
    }
    return parseVec3(script, name);
}

std::string optionalString(Script& script, const char* name, const std::string& fallback) {
    if (!script.hasAssignment(name)) {
        return fallback;
    }
    return parseScriptString(script.assignment(name));
}

Vec3 normalisedOrZero(const Vec3& v) {
    const float len2 = v.x * v.x + v.y * v.y + v.z * v.z;
    if (len2 <= 0.0f) {
        return Vec3(0.0f, 0.0f, 0.0f);
    }
    const float inv = 1.0f / std::sqrt(len2);
    return Vec3(v.x * inv, v.y * inv, v.z * inv);
}

// Known [Properties] keys (lower-case). Anything else is kept in extraProperties.
bool isKnownProperty(const std::string& lvalue) {
    static const char* kNames[] = {
        "levelname",
        "directory",
        "level",
        "gravity",
        "ambientcolor",
        "postprocessmultiply",
        "postprocessadd",
        "colormultiply",
        "coloradd",
        "worldsphere",
        "debrisprojectilecountinlevel",
        "debrisprojectilecountperroom",
        "playerskinname",
        "loadingscreen",
        "enableai",
        "playerstartingplace",
        "fogging",
        "fogcolor",
        "fogstart",
        "fogend",
        "startuplevel",
        "exitlevel",
        "ainodecastheight",
        0};
    for (int i = 0; kNames[i] != 0; ++i) {
        if (lvalue == kNames[i]) {
            return true;
        }
    }
    return false;
}

void parseProperties(Script& script, LevelInfo& level) {
    script.lock("Properties", 0);

    level.levelName = requireString(script, "LevelName");
    level.directory = requireString(script, "Directory");
    level.filename = requireString(script, "Level");
    level.gravity = requireVec3(script, "Gravity");
    // X_SharedDBLevel::construct: up = normalise(-gravity).
    level.upVector = normalisedOrZero(Vec3(-level.gravity.x, -level.gravity.y, -level.gravity.z));
    level.ambientColor = requireVec3(script, "AmbientColor");

    // Android extra float3s sitting in BSS between AmbientColor and WorldSphere.
    // Official PC files omit them; defaults match initializeMembers (1,1,1) / (0,0,0).
    Vec3 ppMul = optionalVec3(script, "PostProcessMultiply", vec3(1.0f, 1.0f, 1.0f));
    if (script.hasAssignment("ColorMultiply")) {
        ppMul = parseVec3(script, "ColorMultiply");
    }
    Vec3 ppAdd = optionalVec3(script, "PostProcessAdd", vec3(0.0f, 0.0f, 0.0f));
    if (script.hasAssignment("ColorAdd")) {
        ppAdd = parseVec3(script, "ColorAdd");
    }
    level.postProcessMultiply = ppMul;
    level.postProcessAdd = ppAdd;

    level.worldSphereName = optionalString(script, "WorldSphere", std::string());
    level.debrisProjectileCountInLevel = requireInt(script, "DebrisProjectileCountInLevel");
    level.debrisProjectileCountPerRoom = requireInt(script, "DebrisProjectileCountPerRoom");
    level.playerSkinName = requireString(script, "PlayerSkinName");
    level.loadingScreen = requireString(script, "LoadingScreen");
    level.aiEnabled = requireBool(script, "EnableAI");
    level.playerStartingPlace = requireString(script, "PlayerStartingPlace");
    level.foggingEnabled = requireBool(script, "Fogging");
    level.fogColor = requireVec3(script, "FogColor");
    level.fogStart = requireFloat(script, "FogStart");
    level.fogEnd = requireFloat(script, "FogEnd");
    level.startupLevel = requireBool(script, "StartupLevel");
    level.exitLevel = requireBool(script, "ExitLevel");
    level.aiNodeCastHeight = optionalFloat(script, "AINodeCastHeight", 0.0f);

    for (std::size_t i = 0; i < script.current().assignments.size(); ++i) {
        const ScriptAssignment& a = script.current().assignments[i];
        if (!isKnownProperty(a.lvalue)) {
            level.extraProperties.push_back(a);
        }
    }

    script.unlock();
}

void parseDifficulty(Script& script, LevelDifficulty& d) {
    script.lock("Difficulty", 0);
    d.maximumHealth = requireFloat(script, "MaximumHealth");
    d.minimumHealth = requireFloat(script, "MinimumHealth");
    d.minimumDeaths = requireInt(script, "MinimumDeaths");
    d.maximumDeaths = requireInt(script, "MaximumDeaths");
    d.minimumTime = requireFloat(script, "MinimumTime");
    d.maximumTime = requireFloat(script, "MaximumTime");
    d.hardcoreHealth = requireFloat(script, "HardcoreHealth");
    d.hardcoreDeaths = requireInt(script, "HardcoreDeaths");
    d.hardcoreTime = requireFloat(script, "HardcoreTime");
    script.unlock();
}

void parseTimedMode(Script& script, LevelTimedMode& t) {
    script.lock("Timedmode", 0);
    t.playTime = requireFloat(script, "PlayTime");
    t.enemyTimeBonus = requireFloat(script, "EnemyTimeBonus");
    script.unlock();
}

LevelInfo parseOneLevel(Script& script) {
    LevelInfo level;
    level.id = script.blockName();
    level.line = script.current().line;
    level.sourceFile = script.sourceName();

    if (script.count("Properties") < 1) {
        throw ScriptError("R_Script: Named block \"properties\" not found in [" + level.id +
                              "]",
                          script.sourceName(), script.current().line);
    }
    parseProperties(script, level);

    if (script.count("Difficulty") < 1) {
        throw ScriptError("R_Script: Named block \"difficulty\" not found in [" + level.id +
                              "]",
                          script.sourceName(), script.current().line);
    }
    parseDifficulty(script, level.difficulty);

    if (script.count("Timedmode") < 1) {
        throw ScriptError("R_Script: Named block \"timedmode\" not found in [" + level.id +
                              "]",
                          script.sourceName(), script.current().line);
    }
    parseTimedMode(script, level.timedMode);
    return level;
}

std::string candidateLevelsTxt(const std::string& root) {
    if (root.empty()) {
        return std::string();
    }
    // Accept levels.txt itself, a folder that contains it, or a game data/ root.
    if (isFile(root)) {
        if (lowerCopy(fileName(root)) == "levels.txt") {
            return root;
        }
        std::string dir = parentDir(root);
        for (int up = 0; up < 4 && !dir.empty(); ++up) {
            const std::string cand = joinPath(dir, "levels.txt");
            if (isFile(cand)) {
                return cand;
            }
            dir = parentDir(dir);
        }
        return std::string();
    }
    const std::string direct = joinPath(root, "levels.txt");
    if (isFile(direct)) {
        return direct;
    }
    const std::string inLevels = joinPath(joinPath(root, "levels"), "levels.txt");
    if (isFile(inLevels)) {
        return inLevels;
    }
    const std::string inDb =
        joinPath(joinPath(joinPath(root, "database"), "levels"), "levels.txt");
    if (isFile(inDb)) {
        return inDb;
    }
    const std::string inData =
        joinPath(joinPath(joinPath(joinPath(root, "data"), "database"), "levels"),
                 "levels.txt");
    if (isFile(inData)) {
        return inData;
    }
    return std::string();
}

}  // namespace

LevelDifficulty::LevelDifficulty()
    : maximumHealth(0.0f),
      minimumHealth(0.0f),
      minimumDeaths(0),
      maximumDeaths(0),
      minimumTime(0.0f),
      maximumTime(0.0f),
      hardcoreHealth(0.0f),
      hardcoreDeaths(0),
      hardcoreTime(0.0f) {}

LevelTimedMode::LevelTimedMode() : playTime(0.0f), enemyTimeBonus(0.0f) {}

LevelInfo::LevelInfo()
    : gravity(0.0f, 0.0f, 0.0f),
      upVector(0.0f, 0.0f, 0.0f),
      ambientColor(0.0f, 0.0f, 0.0f),
      postProcessMultiply(1.0f, 1.0f, 1.0f),
      postProcessAdd(0.0f, 0.0f, 0.0f),
      debrisProjectileCountInLevel(0),
      debrisProjectileCountPerRoom(0),
      aiEnabled(false),
      foggingEnabled(false),
      fogColor(0.0f, 0.0f, 0.0f),
      fogStart(0.0f),
      fogEnd(0.0f),
      startupLevel(false),
      exitLevel(false),
      aiNodeCastHeight(0.0f),
      line(0) {}

std::string LevelInfo::relativeLdbPath() const {
    if (directory.empty() || directory == "." || directory == "./") {
        return filename;
    }
    std::string dir = directory;
    for (std::size_t i = 0; i < dir.size(); ++i) {
        if (dir[i] == '\\') {
            dir[i] = '/';
        }
    }
    if (!dir.empty() && dir[dir.size() - 1] == '/') {
        return dir + filename;
    }
    return dir + "/" + filename;
}

std::string LevelInfo::absoluteLdbPath(const std::string& levelsRoot) const {
    std::string dir = directory;
    for (std::size_t i = 0; i < dir.size(); ++i) {
        if (dir[i] == '\\' || dir[i] == '/') {
#ifdef _WIN32
            dir[i] = '\\';
#else
            dir[i] = '/';
#endif
        }
    }
    std::string root = levelsRoot.empty() ? std::string() : nativeSeparators(levelsRoot);
    if (dir.empty() || dir == "." || dir == "./" || dir == ".\\") {
        return nativeSeparators(joinPath(root, filename));
    }
    return nativeSeparators(joinPath(joinPath(root, dir), filename));
}

const LevelInfo* LevelDatabase::findById(const std::string& id) const {
    const std::string key = lowerCopy(id);
    for (std::size_t i = 0; i < levels.size(); ++i) {
        if (levels[i].id == key) {
            return &levels[i];
        }
    }
    return 0;
}

LevelDatabase LevelsReader::parseScript(Script& script, const std::string& dataDirectory) {
    LevelDatabase db;
    db.sourceFile = script.sourceName();
    db.dataDirectory = dataDirectory;
    const int n = script.count();
    if (n == 0) {
        throw ScriptError("X_SharedDB: There was nothing inside \"" + script.sourceName() +
                              "\", database incomplete",
                          script.sourceName(), 1);
    }
    for (int i = 0; i < n; ++i) {
        script.lock(i);
        db.levels.push_back(parseOneLevel(script));
        script.unlock();
    }
    return db;
}

LevelDatabase LevelsReader::loadFile(const std::string& path) {
    // Official levels.txt #includes globaldefines.h from the RAS; skip if absent.
    Script script = Script::loadFile(path, false);
    return parseScript(script, parentDir(path));
}

std::string LevelsReader::locateLevelsTxt(const std::string& hint) {
    std::vector<std::string> roots;
    if (!hint.empty()) {
        roots.push_back(hint);
    }
    const std::string exe = executableDir();
    if (!exe.empty()) {
        roots.push_back(joinPath(exe, "data"));
        roots.push_back(exe);
        const std::string parent = parentDir(exe);
        if (!parent.empty()) {
            roots.push_back(joinPath(parent, "data"));
            roots.push_back(parent);
        }
    }
    roots.push_back(std::string("."));
    roots.push_back(std::string("data"));
    roots.push_back(std::string("docs"));

    for (std::size_t i = 0; i < roots.size(); ++i) {
        const std::string hit = candidateLevelsTxt(roots[i]);
        if (!hit.empty()) {
            return hit;
        }
    }
    return std::string();
}

}  // namespace maxfx
