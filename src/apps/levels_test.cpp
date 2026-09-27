// Self-test for the R_Script loader and levels.txt parser. No extra deps.
// Built as `levels-test` from the Makefile.

#include "maxfx/char/Character.h"
#include "maxfx/ai/AiGraph.h"
#include "maxfx/collision/Collision.h"
#include "maxfx/core/Fs.h"
#include "maxfx/core/Math.h"
#include "maxfx/db/Database.h"
#include "maxfx/game/Catalog.h"
#include "maxfx/game/Message.h"
#include "maxfx/game/Runtime.h"
#include "maxfx/image/Image.h"
#include "maxfx/kf2/Kf2.h"
#include "maxfx/ldb/Ldb.h"
#include "maxfx/ldb/LdbReader.h"
#include "maxfx/levels/Levels.h"
#include "maxfx/script/Script.h"
#include "maxfx/sound/Sound.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

using maxfx::LevelDatabase;
using maxfx::LevelsReader;
using maxfx::Script;
using maxfx::ScriptError;

static int gFails = 0;
static int gPass = 0;

static void check(bool cond, const char* msg) {
    if (cond) {
        ++gPass;
        return;
    }
    ++gFails;
    std::fprintf(stderr, "FAIL: %s\n", msg);
}

static void checkEq(const std::string& a, const std::string& b, const char* msg) {
    if (a == b) {
        ++gPass;
        return;
    }
    ++gFails;
    std::fprintf(stderr, "FAIL: %s (got \"%s\", want \"%s\")\n", msg, a.c_str(), b.c_str());
}

static void checkNear(float a, float b, const char* msg) {
    if (std::fabs(a - b) < 1.0e-4f) {
        ++gPass;
        return;
    }
    ++gFails;
    std::fprintf(stderr, "FAIL: %s (got %g, want %g)\n", msg, a, b);
}

static const char* kSnippet =
    "#define TRUE 1\n"
    "#define FALSE 0\n"
    "#define GRAVITY_VALUE -981\n"
    "#define TIMEBONUS 5\n"
    "\n"
    "// comment\n"
    "/* block\n"
    "   comment */\n"
    "[Part1_Level1]\n"
    "{\n"
    "    [Properties]\n"
    "    {\n"
    "        LevelName = \"Roscoe Street Station\";\n"
    "        Directory = part1;\n"
    "        Level = Part1_Level1.ldb;\n"
    "        Gravity = ( 0, GRAVITY_VALUE , 0 );\n"
    "        AmbientColor = ( 140, 135, 110 );\n"
    "        DebrisProjectileCountInLevel = 400;\n"
    "        DebrisProjectileCountPerRoom = 50;\n"
    "        PlayerSkinName = \"max_payne\";\n"
    "        LoadingScreen = \"..\\\\autosave\\\\P0L0.jpg\";\n"
    "        WorldSphere = \"intro\";\n"
    "        EnableAI = FALSE;\n"
    "        PlayerStartingPlace = \"::startroom::Jumppoint 00\";\n"
    "        Fogging = TRUE;\n"
    "        FogColor = ( 0, 0, 0 );\n"
    "        FogStart = 2000.0; // on the fly\n"
    "        FogEnd = 5000.0;\n"
    "        StartupLevel = true;\n"
    "        ExitLevel = false;\n"
    "        AINodeCastHeight = 0.0;\n"
    "    }\n"
    "    [Difficulty]\n"
    "    {\n"
    "        MaximumHealth = 300%;\n"
    "        MinimumHealth = 0%;\n"
    "        MinimumDeaths = 0;\n"
    "        MaximumDeaths = 100;\n"
    "        MinimumTime = 0;\n"
    "        MaximumTime = 3600;\n"
    "        HardcoreHealth = 300%;\n"
    "        HardcoreDeaths = 0;\n"
    "        HardcoreTime = 0;\n"
    "    }\n"
    "    [Timedmode]\n"
    "    {\n"
    "        PlayTime = 60;\n"
    "        EnemyTimeBonus = TIMEBONUS;\n"
    "    }\n"
    "}\n"
    "[part1_level2]\n"
    "{\n"
    "    [Properties]\n"
    "    {\n"
    "        LevelName = \"The Next Map\";\n"
    "        Directory = part1;\n"
    "        Level = Part1_Level2.ldb;\n"
    "        Gravity = ( 0, -981, 0 );\n"
    "        AmbientColor = ( 1, 2, 3 );\n"
    "        DebrisProjectileCountInLevel = 1;\n"
    "        DebrisProjectileCountPerRoom = 1;\n"
    "        PlayerSkinName = \"max_payne\";\n"
    "        LoadingScreen = \"x.jpg\";\n"
    "        WorldSphere = \"\";\n"
    "        EnableAI = TRUE;\n"
    "        PlayerStartingPlace = \"::room::jp\";\n"
    "        Fogging = FALSE;\n"
    "        FogColor = ( 8, 8, 8 );\n"
    "        FogStart = 1;\n"
    "        FogEnd = 2;\n"
    "        StartupLevel = FALSE;\n"
    "        ExitLevel = TRUE;\n"
    "        ExtraAndroidKey = 42;\n"
    "    }\n"
    "    [Difficulty]\n"
    "    {\n"
    "        MaximumHealth = 100;\n"
    "        MinimumHealth = 0;\n"
    "        MinimumDeaths = 0;\n"
    "        MaximumDeaths = 1;\n"
    "        MinimumTime = 0;\n"
    "        MaximumTime = 1;\n"
    "        HardcoreHealth = 1;\n"
    "        HardcoreDeaths = 0;\n"
    "        HardcoreTime = 0;\n"
    "    }\n"
    "    [Timedmode]\n"
    "    {\n"
    "        PlayTime = 10;\n"
    "        EnemyTimeBonus = 1;\n"
    "    }\n"
    "}\n";

static void testLevelsSnippet() {
    Script script = Script::parseText(kSnippet, "memory:levels.txt", std::string());
    check(script.count() == 2, "two top-level blocks");
    check(script.count("Part1_Level1") == 1, "count by name is case-insensitive");
    checkEq(script.blockName(), "", "root is unnamed");

    const LevelDatabase db = LevelsReader::parseScript(script, "/tmp/levels");
    check(db.levels.size() == 2, "two levels");

    const maxfx::LevelInfo* a = db.findById("PART1_LEVEL1");
    check(a != 0, "findById is case-insensitive");
    if (a == 0) {
        return;
    }
    checkEq(a->id, "part1_level1", "id stored lower-cased");
    checkEq(a->levelName, "Roscoe Street Station", "LevelName keeps string case/spaces");
    checkEq(a->directory, "part1", "Directory");
    checkEq(a->filename, "Part1_Level1.ldb", "Level rvalue keeps file case");
    checkNear(a->gravity.x, 0.0f, "gravity.x");
    checkNear(a->gravity.y, -981.0f, "gravity.y from #define");
    checkNear(a->gravity.z, 0.0f, "gravity.z");
    checkNear(a->upVector.x, 0.0f, "up.x");
    checkNear(a->upVector.y, 1.0f, "up.y = normalise(-gravity)");
    checkNear(a->upVector.z, 0.0f, "up.z");
    checkNear(a->ambientColor.x, 140.0f, "ambient.x");
    checkEq(a->playerStartingPlace, "::startroom::Jumppoint 00", "starting place");
    check(a->aiEnabled == false, "EnableAI FALSE");
    check(a->foggingEnabled == true, "Fogging TRUE");
    check(a->startupLevel == true, "StartupLevel true");
    check(a->exitLevel == false, "ExitLevel false");
    checkNear(a->difficulty.maximumHealth, 300.0f, "MaximumHealth strips %");
    checkNear(a->timedMode.enemyTimeBonus, 5.0f, "EnemyTimeBonus from #define");
    checkEq(a->relativeLdbPath(), "part1/Part1_Level1.ldb", "relative ldb path");
    checkNear(a->postProcessMultiply.x, 1.0f, "PP multiply default");
    checkNear(a->postProcessAdd.x, 0.0f, "PP add default");

    const maxfx::LevelInfo* b = db.findById("part1_level2");
    check(b != 0, "second level");
    if (b != 0) {
        check(b->exitLevel == true, "ExitLevel TRUE");
        check(b->aiEnabled == true, "EnableAI TRUE");
        check(b->extraProperties.size() == 1, "unknown Properties key kept");
        if (!b->extraProperties.empty()) {
            checkEq(b->extraProperties[0].lvalue, "extraandroidkey", "extra key lower-cased");
            checkEq(b->extraProperties[0].rvalue, "42", "extra value");
        }
        checkEq(b->worldSphereName, "", "empty WorldSphere");
    }
}

static void testErrors() {
    bool threw = false;
    try {
        Script::parseText("[foo]\n{\n    bar = 1;\n    bar = 2;\n}\n", "dup.txt", "");
    } catch (const ScriptError&) {
        threw = true;
    }
    check(threw, "duplicate lvalue throws");

    threw = false;
    try {
        Script::parseText("[foo]\n{\n    bar = 1\n}\n", "nosemi.txt", "");
    } catch (const ScriptError&) {
        threw = true;
    }
    check(threw, "missing semicolon throws");

    threw = false;
    try {
        Script script = Script::parseText("[foo]\n{\n    [Properties]\n    {\n"
                                          "        LevelName = \"x\";\n"
                                          "    }\n}\n",
                                          "incomplete.txt", "");
        LevelsReader::parseScript(script, "");
    } catch (const ScriptError&) {
        threw = true;
    }
    check(threw, "incomplete Properties / missing Difficulty throws");

    threw = false;
    try {
        Script::parseText("foo = 1;\n", "notag.txt", "");
    } catch (const ScriptError&) {
        threw = true;
    }
    check(threw, "assignment without tag throws");
}

static void testInclude(const std::string& tmpDir) {
    const std::string inc = maxfx::joinPath(tmpDir, "globaldefines.h");
    const std::string mainPath = maxfx::joinPath(tmpDir, "mini.txt");
    std::FILE* f = std::fopen(inc.c_str(), "wb");
    check(f != 0, "open include for write");
    if (f == 0) {
        return;
    }
    std::fputs("#define TRUE 1\n#define FALSE 0\n#define G -1\n", f);
    std::fclose(f);

    f = std::fopen(mainPath.c_str(), "wb");
    check(f != 0, "open mini.txt for write");
    if (f == 0) {
        return;
    }
    std::fputs("#include \"globaldefines.h\"\n"
               "[x]\n{\n"
               "  [Properties]\n{\n"
               "    LevelName = \"n\";\n"
               "    Directory = .;\n"
               "    Level = a.ldb;\n"
               "    Gravity = ( 0, G, 0 );\n"
               "    AmbientColor = ( 1, 1, 1 );\n"
               "    DebrisProjectileCountInLevel = 1;\n"
               "    DebrisProjectileCountPerRoom = 1;\n"
               "    PlayerSkinName = \"p\";\n"
               "    LoadingScreen = \"l\";\n"
               "    WorldSphere = \"w\";\n"
               "    EnableAI = FALSE;\n"
               "    PlayerStartingPlace = \"::r::j\";\n"
               "    Fogging = TRUE;\n"
               "    FogColor = ( 0, 0, 0 );\n"
               "    FogStart = 1;\n"
               "    FogEnd = 2;\n"
               "    StartupLevel = FALSE;\n"
               "    ExitLevel = FALSE;\n"
               "  }\n"
               "  [Difficulty]\n{\n"
               "    MaximumHealth = 1; MinimumHealth = 0;\n"
               "    MinimumDeaths = 0; MaximumDeaths = 1;\n"
               "    MinimumTime = 0; MaximumTime = 1;\n"
               "    HardcoreHealth = 1; HardcoreDeaths = 0; HardcoreTime = 0;\n"
               "  }\n"
               "  [Timedmode]\n{ PlayTime = 1; EnemyTimeBonus = 1; }\n"
               "}\n",
               f);
    std::fclose(f);

    const LevelDatabase db = LevelsReader::loadFile(mainPath);
    check(db.levels.size() == 1, "include file parsed one level");
    if (!db.levels.empty()) {
        checkNear(db.levels[0].gravity.y, -1.0f, "define from #include");
        check(db.levels[0].foggingEnabled == true, "TRUE from include");
        checkEq(db.levels[0].relativeLdbPath(), "a.ldb", "Directory = .");
    }
}

static void testUnbracedBlocks() {
    const char* text =
        "[Cardboard]\n"
        "DrawPolygons = TRUE;\n"
        "DetailOffset = 1;\n"
        "[Geometry] ExportData = foo.kf2; SkinData = foo.skd;\n";
    const Script s = Script::parseText(text, "unbraced.txt");
    check(s.root().children.size() == 2, "two unbraced top blocks");
    checkEq(s.root().children[0].name, "cardboard", "cardboard tag");
    checkEq(s.root().children[0].assignments[0].lvalue, "drawpolygons", "drawpolygons lvalue");
    checkEq(s.root().children[1].name, "geometry", "geometry tag");
    check(s.root().children[1].assignments.size() == 2, "geometry two assignments");
}

static void testNestedQuotesAnd3DSound() {
    const char* text =
        "[OnPickup]\n"
        "{\n"
        "  [Message] string = \"activator->C_PickupWeapon( \"beretta\" );\";\n"
        "}\n"
        "[Shoot]\n"
        "{\n"
        "  [Sound]\n"
        "  Filename = weapons\\\\shoot_empty.wav;\n"
        "  3DSound = YES;\n"
        "  Looping = NO;\n"
        "}\n";
    const Script s = Script::parseText(text, "soundlike.txt");
    check(s.root().children.size() == 2, "pickup + shoot blocks");
    check(s.root().children[0].children.size() == 1, "message child");
    if (!s.root().children[0].children.empty()) {
        check(s.root().children[0].children[0].assignments.size() >= 1, "message string");
        if (!s.root().children[0].children[0].assignments.empty()) {
            const std::string& v = s.root().children[0].children[0].assignments[0].rvalue;
            check(v.find("beretta") != std::string::npos, "nested quote keeps beretta");
        }
    }
    check(s.root().children[1].children.size() == 1, "sound child");
    if (!s.root().children[1].children.empty()) {
        bool has3d = false;
        for (std::size_t i = 0; i < s.root().children[1].children[0].assignments.size(); ++i) {
            if (s.root().children[1].children[0].assignments[i].lvalue == "3dsound") {
                has3d = true;
            }
        }
        check(has3d, "3DSound lvalue");
    }

    const char* extraSemi =
        "[LOD]\n{\n  [Geometry] ExportData = foo.kf2;;\n  Distance = 1;\n}\n";
    const Script semi = Script::parseText(extraSemi, "semi.txt");
    check(semi.root().children.size() == 1, "double semicolon still parses");
}

static void testPcxAlpha() {
    maxfx::Image img;
    std::string err;
    if (!maxfx::isFile("docs/baseballbat_alpha.pcx")) {
        std::fprintf(stderr, "skip PCX alpha (docs/baseballbat_alpha.pcx missing)\\n");
        return;
    }
    check(maxfx::loadImageFile("docs/baseballbat_alpha.pcx", img, &err), "load baseballbat_alpha.pcx");
    check(img.width == 128 && img.height == 32, "alpha pcx size");
    check(!img.empty() && img.channels == 4, "alpha pcx rgba");
    bool anyTrans = false;
    bool anyOpaque = false;
    for (int i = 0; i < img.width * img.height; ++i) {
        const unsigned char a = img.pixels[static_cast<std::size_t>(i * 4 + 3)];
        if (a < 250) {
            anyTrans = true;
        }
        if (a > 5) {
            anyOpaque = true;
        }
    }
    check(anyTrans, "alpha pcx has transparent texels");
    check(anyOpaque, "alpha pcx has opaque texels");

    // Official glass/water pair colour and alpha at different resolutions.
    maxfx::Image color;
    color.width = 4;
    color.height = 4;
    color.channels = 4;
    color.pixels.assign(4 * 4 * 4, 200);
    for (int i = 0; i < 16; ++i) {
        color.pixels[static_cast<std::size_t>(i * 4 + 3)] = 255;
    }
    maxfx::Image mask;
    mask.width = 1;
    mask.height = 1;
    mask.channels = 4;
    mask.pixels.resize(4);
    mask.pixels[0] = 64;
    mask.pixels[1] = 64;
    mask.pixels[2] = 64;
    mask.pixels[3] = 255;
    check(maxfx::applyAlphaMap(color, mask), "resample 1x1 alpha onto 4x4");
    check(color.pixels[3] == 64, "stretched alpha 50%");
    check(color.pixels[15] == 64, "stretched alpha corner");

    // 8x8 colour vs 2x2 50% tile, same pairing as water / alpha_50.pcx.
    maxfx::Image water;
    water.width = 8;
    water.height = 8;
    water.channels = 4;
    water.pixels.assign(8 * 8 * 4, 180);
    for (int i = 0; i < 64; ++i) {
        water.pixels[static_cast<std::size_t>(i * 4 + 3)] = 255;
    }
    maxfx::Image a50;
    a50.width = 2;
    a50.height = 2;
    a50.channels = 4;
    a50.pixels.assign(2 * 2 * 4, 128);
    for (int i = 0; i < 4; ++i) {
        a50.pixels[static_cast<std::size_t>(i * 4 + 3)] = 128;
    }
    check(maxfx::applyAlphaMap(water, a50), "water 8x8 vs 2x2 50% tile");
    check(water.pixels[3] == 128, "water opacity 128");
}

static void testKf2Beretta() {
    const char* path = "docs/database/weapons/beretta/beretta_levelitem.kf2";
    if (!maxfx::isFile(path)) {
        std::fprintf(stderr, "skip KF2 (beretta_levelitem.kf2 missing)\\n");
        return;
    }
    const maxfx::Kf2File kf = maxfx::Kf2Reader::loadFromFile(path);
    check(!kf.materialLists.empty(), "beretta material list");
    check(!kf.meshes.empty(), "beretta mesh");
    if (!kf.meshes.empty()) {
        check(kf.meshes[0].hasGeometry, "beretta geometry");
        check(kf.meshes[0].geometry.vertices.size() == 339, "beretta 339 verts");
        check(kf.meshes[0].polygons.indices.size() == 1008, "beretta 1008 indices");
    }
    std::vector<maxfx::Kf2DrawMesh> draws;
    maxfx::kf2BuildDrawMeshes(kf, draws);
    check(!draws.empty(), "beretta draw mesh");
    unsigned tris = 0;
    for (std::size_t i = 0; i < draws.size(); ++i) {
        for (std::size_t p = 0; p < draws[i].parts.size(); ++p) {
            tris += static_cast<unsigned>(draws[i].parts[p].vertices.size() / 3);
        }
    }
    check(tris == 336, "beretta 336 triangles");

    const char* kfs = "docs/database/skins/balder_alex/Alex_Balder_L0.kfs";
    if (maxfx::isFile(kfs)) {
        const maxfx::Kf2File skin = maxfx::Kf2Reader::loadFromFile(kfs);
        check(!skin.meshes.empty(), "alex kfs has meshes");
        check(!skin.materialLists.empty(), "alex kfs material list");
    }
}

static void testDatabase() {
    const std::string root = maxfx::DatabaseReader::locateRoot("docs");
    if (root.empty()) {
        std::fprintf(stderr, "skip database (docs/database missing)\\n");
        return;
    }
    maxfx::Database db = maxfx::DatabaseReader::load(root);
    check(db.parsedScripts > 10, "parsed many scripts");
    check(!db.materials.empty(), "materials.txt categories");
    const maxfx::MaterialCategory* graffiti = db.findMaterial("Graffiti");
    check(graffiti != 0 && graffiti->detailOffset == 1, "Graffiti DetailOffset");
    const maxfx::MaterialCategory* ai = db.findMaterial("AI_Node_Collision_NoDraw");
    check(ai != 0 && ai->drawPolygons == false, "AI node is service geometry");
    check(!db.items.empty() || !db.skins.empty(), "skins or level_items parsed");
    check(db.findItem("beretta") != 0, "beretta item def");
    if (db.findItem("beretta") != 0 && !db.findItem("beretta")->lods.empty()) {
        check(maxfx::isFile(db.findItem("beretta")->lods[0].resolvedExport), "beretta kf2 resolved");
    }
    check(db.findSkin("C1_All_Mickey") != 0, "C1_All_Mickey skin def");
    check(!db.music.empty(), "music.txt entries");
    check(db.findMusic("max_payne") != 0, "max_payne music cue");
    if (db.findMusic("max_payne") != 0) {
        check(db.findMusic("max_payne")->filename.find("max_theme") != std::string::npos,
              "max_payne wavs/max_theme.wav path");
        check(db.findMusic("max_payne")->filename.find("wavs") != std::string::npos,
              "max_payne keeps wavs\\\\ prefix");
    }
    check(!db.sounds.empty(), "sound scripts parsed");
    maxfx::DatabaseReader::loadWorldSphere(db, "intro");
    check(!db.worldSpherePath.empty() && maxfx::isFile(db.worldSpherePath), "worldsphere intro kf2");
    check(db.worldSphereFiles.find("intro") != db.worldSphereFiles.end() ||
              db.worldSphereFiles.find("bronx") != db.worldSphereFiles.end(),
          "worldspheres.txt catalogued");
    maxfx::DatabaseReader::loadWorldSphere(db, "bronx");
    check(!db.worldSpherePath.empty() && maxfx::isFile(db.worldSpherePath), "worldsphere bronx kf2");
}

// Real Max Payne model (docs/database/skins/max_payne): the full skinning
// chain end to end on shipped data — SKD weights, name-matched bone palette,
// exact bind-pose identity and a sane animated pose. This is the fixture the
// Android 1.0 decompile skinning verdict (docs/ANIMATION.md) was verified on.
static void testRealSkinning() {
    using namespace maxfx;
    const std::string root = DatabaseReader::locateRoot("docs");
    if (root.empty() || !isFile("docs/database/skins/max_payne/Payne_Max_L0.KFS")) {
        std::fprintf(stderr, "skip real skinning (max payne model missing)\n");
        return;
    }
    Database db = DatabaseReader::load(root);
    const SkinDef* def = db.findSkin("max_payne");
    check(def != 0 && !def->lods.empty(), "max_payne skin def");
    if (def == 0 || def->lods.empty()) {
        return;
    }
    const Kf2File* mesh = db.loadModel(def->lods[0].resolvedExport);
    const Kf2File* skinFile = db.loadModel(def->lods[0].resolvedSkin);
    check(mesh != 0 && skinFile != 0, "KFS + SKD load");
    if (mesh == 0 || skinFile == 0 || skinFile->skins.empty()) {
        return;
    }
    const Kf2Skin& skin = skinFile->skins[0];
    check(!mesh->meshes.empty() && mesh->meshes[0].hasGeometry, "mesh geometry");
    if (mesh->meshes.empty() || !mesh->meshes[0].hasGeometry) {
        return;
    }
    // SKD vertex slots align with the geometry vertices (per-primitive order).
    check(skin.vertices.size() == mesh->meshes[0].geometry.vertices.size(),
          "skin vertices align with geometry");
    // Weights: every vertex sums to 1, at most 4 bones (DirectX limit the
    // engine enforces in KF_SkinMeshCallback::allocatePrimitive).
    float maxBones = 0.0f;
    bool weightsOk = true;
    for (std::size_t i = 0; i < skin.vertices.size(); ++i) {
        const Kf2SkinVertex& sv = skin.vertices[i];
        float sum = 0.0f;
        for (std::size_t b = 0; b < sv.bones.size(); ++b) {
            sum += b < sv.weights.size() ? sv.weights[b] : 1.0f;
            const int bi = sv.bones[b];
            if (bi < 0 || static_cast<std::size_t>(bi) >= skin.skeletonObjectNames.size()) {
                weightsOk = false;
            }
        }
        if (std::fabs(sum - 1.0f) > 0.01f) {
            weightsOk = false;
        }
        maxBones = std::max(maxBones, static_cast<float>(sv.bones.size()));
    }
    check(weightsOk, "skin weights sum to 1, bone indices in range");
    check(maxBones <= 4.0f, "at most 4 bones per vertex");
    // The pose clip (bind) covers every skin bone by name.
    const CharacterAnimClip* poseClip = findAnimClip(def->character, kCharAnimPose);
    check(poseClip != 0 && isFile(poseClip->resolvedPath), "CHARANIM_POSE clip");
    const Kf2File* poseK = poseClip != 0 ? db.loadModel(poseClip->resolvedPath) : 0;
    const Kf2File* standK = db.loadModel(
        findAnimClip(def->character, kCharAnimStand) != 0
            ? findAnimClip(def->character, kCharAnimStand)->resolvedPath
            : std::string());
    check(poseK != 0 && standK != 0, "pose + stand clips load");
    if (poseK == 0 || standK == 0) {
        return;
    }
    std::vector<std::string> poseNames;
    std::vector<Mat4x3> poseWorlds;
    kf2BuildSkeletonWorlds(*poseK, 0.0f, 0, &poseNames, &poseWorlds);
    std::size_t matched = 0;
    for (std::size_t i = 0; i < skin.skeletonObjectNames.size(); ++i) {
        for (std::size_t j = 0; j < poseNames.size(); ++j) {
            if (poseNames[j] == skin.skeletonObjectNames[i]) {
                ++matched;
                break;
            }
        }
    }
    check(matched == skin.skeletonObjectNames.size(),
          "every skin bone matches a pose channel by name");
    // Bind-pose identity: playing the bind clip as both bind and play must
    // reproduce the node-transformed mesh exactly (the defining property of
    // the inverse-bind chain).
    std::vector<Kf2DrawMesh> skinned;
    kf2BuildSkinnedDrawMeshes(*mesh, skinFile, poseK, poseK, 0.0f, skinned);
    std::vector<Kf2DrawMesh> raw;
    kf2BuildDrawMeshes(*mesh, raw);
    std::vector<std::string> nodeNames;
    std::vector<Mat4x3> nodeWorlds;
    kf2NodeWorldTransforms(*mesh, &nodeNames, &nodeWorlds);
    Mat4x3 nodeWorld;
    if (!nodeWorlds.empty()) {
        nodeWorld = nodeWorlds[0];
    }
    std::size_t compared = 0;
    float maxDiff = 0.0f;
    for (std::size_t d = 0; d < skinned.size() && d < raw.size(); ++d) {
        for (std::size_t p = 0; p < skinned[d].parts.size(); ++p) {
            for (std::size_t v = 0; v < skinned[d].parts[p].vertices.size(); ++v) {
                const Vec3 a = skinned[d].parts[p].vertices[v].position;
                const Vec3 b = transformPoint(nodeWorld, raw[d].parts[p].vertices[v].position);
                maxDiff = std::max(maxDiff, length(a - b));
                ++compared;
            }
        }
    }
    check(compared > 4000, "compared real draw vertices");
    checkNear(maxDiff, 0.0f, "bind-pose identity on the real model");
    // Animated pose: standing character, no NaN, real volume in every axis.
    for (int t = 0; t < 3; ++t) {
        std::vector<Kf2DrawMesh> anim;
        kf2BuildSkinnedDrawMeshes(*mesh, skinFile, poseK, standK, 0.3f * t, anim);
        Vec3 mn(1e9f, 1e9f, 1e9f), mx(-1e9f, -1e9f, -1e9f);
        bool nan = false;
        for (std::size_t d = 0; d < anim.size(); ++d) {
            for (std::size_t p = 0; p < anim[d].parts.size(); ++p) {
                for (std::size_t v = 0; v < anim[d].parts[p].vertices.size(); ++v) {
                    const Vec3 a = anim[d].parts[p].vertices[v].position;
                    if (a.x != a.x || a.y != a.y || a.z != a.z) {
                        nan = true;
                        continue;
                    }
                    mn.x = std::min(mn.x, a.x); mn.y = std::min(mn.y, a.y); mn.z = std::min(mn.z, a.z);
                    mx.x = std::max(mx.x, a.x); mx.y = std::max(mx.y, a.y); mx.z = std::max(mx.z, a.z);
                }
            }
        }
        check(!nan, "animated pose has no NaN");
        check(mx.y - mn.y > 1.5f && mx.y < 2.2f, "standing height ~1.8 m");
        check(mx.x - mn.x > 0.3f && mx.z - mn.z > 0.15f, "animated pose keeps volume");
    }
    // The KFS materials' diffuse textures resolve next to the model
    // (textures/ directory, the first candidate in the viewer's chain).
    std::size_t texFound = 0, texTotal = 0;
    for (std::size_t ml = 0; ml < mesh->materialLists.size(); ++ml) {
        for (std::size_t m = 0; m < mesh->materialLists[ml].materials.size(); ++m) {
            const Kf2Material& mat = mesh->materialLists[ml].materials[m];
            for (std::size_t t = 0; t < mat.diffuseTexture.files.size(); ++t) {
                ++texTotal;
                const std::string cand = joinPath(
                    joinPath(parentDir(def->lods[0].resolvedExport), "textures"),
                    fileName(mat.diffuseTexture.files[t]));
                if (isFile(existingPathIgnoreCase(cand))) {
                    ++texFound;
                }
            }
        }
    }
    check(texTotal > 0 && texFound == texTotal, "skin diffuse textures resolve");
}

static void testCollisionAndMath() {
    maxfx::CollisionWorld world;
    world.addTriangle(maxfx::Vec3(0, 0, 0), maxfx::Vec3(2, 0, 0), maxfx::Vec3(0, 0, 2), 0, 1);
    const maxfx::CollisionHit hit = world.raycast(maxfx::Vec3(0.2f, 1.0f, 0.2f),
                                                  maxfx::Vec3(0.0f, -1.0f, 0.0f), 5.0f);
    check(hit.hit, "ray hits floor triangle");
    checkNear(hit.point.y, 0.0f, "floor y");
    const maxfx::Vec3 moved =
        world.moveSphere(maxfx::Vec3(0.3f, 0.5f, 0.3f), maxfx::Vec3(0.0f, -1.0f, 0.0f), 0.25f);
    check(moved.y > 0.2f, "sphere rests on floor");
    maxfx::Mat4x3 m = maxfx::makeEntity(maxfx::Vec3(1, 2, 3), 0.4f);
    maxfx::Mat4x3 inv = maxfx::inverseRigid(m);
    maxfx::Vec3 p(0.5f, 0.25f, -0.1f);
    maxfx::Vec3 back = maxfx::transformPoint(inv, maxfx::transformPoint(m, p));
    checkNear(back.x, p.x, "inverseRigid x");
    checkNear(back.y, p.y, "inverseRigid y");
    checkNear(back.z, p.z, "inverseRigid z");

    maxfx::CollisionWorld gridWorld;
    for (int i = 0; i < 40; ++i) {
        const float x = 20.0f + static_cast<float>(i);
        gridWorld.addTriangle(maxfx::Vec3(x, 5, 0), maxfx::Vec3(x + 1, 5, 0),
                              maxfx::Vec3(x, 5, 1), 0, 10 + i);
    }
    gridWorld.addTriangle(maxfx::Vec3(0, 0, 0), maxfx::Vec3(4, 0, 0), maxfx::Vec3(0, 0, 4), 1, 99);
    const maxfx::CollisionHit gridHit =
        gridWorld.raycast(maxfx::Vec3(0.4f, 2.0f, 0.4f), maxfx::Vec3(0.0f, -1.0f, 0.0f), 5.0f);
    check(gridHit.hit, "grid ray hits nearby floor among far triangles");
    checkNear(gridHit.point.y, 0.0f, "grid floor y");

    maxfx::StaticMesh sm;
    sm.vertices.push_back(maxfx::Vec3(0, 1, 0));
    sm.vertices.push_back(maxfx::Vec3(3, 1, 0));
    sm.vertices.push_back(maxfx::Vec3(0, 1, 3));
    maxfx::Polygon sp;
    sp.textureVertexStart = 0;
    sp.vertexCount = 3;
    sp.id = 7;
    sm.polygons.push_back(sp);
    std::vector<maxfx::TextureVertex> tvs(3);
    tvs[0].vertexIndex = 0;
    tvs[1].vertexIndex = 1;
    tvs[2].vertexIndex = 2;
    maxfx::CollisionWorld meshWorld;
    meshWorld.addStaticMesh(sm, tvs);
    const maxfx::CollisionHit meshHit =
        meshWorld.raycast(maxfx::Vec3(0.3f, 3.0f, 0.3f), maxfx::Vec3(0.0f, -1.0f, 0.0f), 5.0f);
    check(meshHit.hit, "static mesh floor hit");
    checkNear(meshHit.point.y, 1.0f, "static mesh floor y");

    maxfx::CharacterConfig cfg;
    maxfx::CharacterActor actor;
    actor.spawn(maxfx::Vec3(1.0f, 4.0f, 1.0f), 0.0f, 0, &cfg, "dummy");
    maxfx::CollisionWorld empty;
    actor.update(0.05f, maxfx::Vec3(8.0f, 4.0f, 8.0f), empty, 0);
    checkNear(actor.position.y, 4.0f, "missing collision does not drop the actor");
}

// Hand-built keyframe channels that encode the Android decompile semantics
// (LinearlyOptimizedContainer<M_Matrix4x3>::getItem +
// KF_KeyframeAnimation::animateFrameWithLastFrame). These run without any
// game data and pin the sampler so it cannot silently drift from the engine.
static maxfx::Mat4x3 sampleChannelAt(const maxfx::Kf2File& file, float timeSeconds) {
    std::vector<std::string> names;
    std::vector<maxfx::Mat4x3> locals;
    maxfx::kf2SampleAnimation(file, timeSeconds, &names, &locals);
    if (locals.size() != 1) {
        return maxfx::Mat4x3();
    }
    return locals[0];
}

// Key with a Y rotation and translation (0, frame, 0) so the sampled
// translation directly reports the wrapped frame number.
static maxfx::Kf2AnimKey frameKey(int frame, float yawRadians) {
    maxfx::Kf2AnimKey key;
    key.frame = frame;
    const float c = std::cos(yawRadians);
    const float s = std::sin(yawRadians);
    key.objectToParent.rows[0] = maxfx::Vec3(c, 0.0f, -s);
    key.objectToParent.rows[1] = maxfx::Vec3(0.0f, 1.0f, 0.0f);
    key.objectToParent.rows[2] = maxfx::Vec3(s, 0.0f, c);
    key.objectToParent.rows[3] = maxfx::Vec3(0.0f, static_cast<float>(frame), 0.0f);
    return key;
}

static void testKf2EngineSampling() {
    // --- 1. Plain component lerp (interpolationMethod 0) -----------------
    // The engine lerps all 12 floats and does NOT re-orthonormalize: a 0-90
    // degree blend at t=0.5 shrinks the rotated rows to cos(45 deg).
    {
        maxfx::Kf2File file;
        maxfx::Kf2NodeAnimation ch;
        ch.targetName = "Bone";
        ch.frameRate = 10;
        ch.looping = false;
        ch.loopInterpolation = false;
        ch.totalKeyframeCount = 11;
        ch.interpolationMethod = 0;
        ch.keys.push_back(frameKey(0, 0.0f));
        ch.keys.push_back(frameKey(10, 1.5707964f));
        file.animations.push_back(ch);
        checkNear(maxfx::kf2AnimationDuration(file), 1.1f, "duration is total/fps");
        const maxfx::Mat4x3 mid = sampleChannelAt(file, 0.5f);
        checkNear(mid.translation().y, 5.0f, "lerp translation");
        checkNear(maxfx::length(mid.rows[0]), 0.70710678f, "method 0 shrinks rows mid-blend");
        const maxfx::Mat4x3 start = sampleChannelAt(file, 0.0f);
        checkNear(maxfx::length(start.rows[0]), 1.0f, "exact key keeps row length");
        // Non-looping hold: frame >= total-1 clamps to the last key.
        const maxfx::Mat4x3 held = sampleChannelAt(file, 1.5f);
        checkNear(held.translation().y, 10.0f, "non-looping holds last key");
        checkNear(held.rows[0].x, 0.0f, "held key rotation");
        checkNear(held.rows[0].z, -1.0f, "held key rotation z");
    }

    // --- 2. Loop wrap honours LoopToFrame ---------------------------------
    // looping, total 21, loopToFrame 5: frames past total-1 wrap into
    // [5, 21), so the intro [0, 5) plays exactly once. Translations carry
    // the frame number, so y == wrapped frame.
    {
        maxfx::Kf2File file;
        maxfx::Kf2NodeAnimation ch;
        ch.targetName = "Bone";
        ch.frameRate = 10;
        ch.looping = true;
        ch.loopInterpolation = false;
        ch.totalKeyframeCount = 21;
        ch.loopToFrame = 5;
        ch.interpolationMethod = 0;
        ch.keys.push_back(frameKey(0, 0.0f));
        ch.keys.push_back(frameKey(5, 0.0f));
        ch.keys.push_back(frameKey(10, 0.0f));
        ch.keys.push_back(frameKey(20, 0.0f));
        file.animations.push_back(ch);
        checkNear(sampleChannelAt(file, 1.9f).translation().y, 19.0f, "pre-wrap frame");
        // frame 21.5 -> 5 + fmod(16.5, 16) = 5.5 (not fmod(21.5, 21) = 0.5)
        checkNear(sampleChannelAt(file, 2.15f).translation().y, 5.5f, "wrap lands in [loopToFrame, total)");
        // frame 30.0 -> 5 + fmod(25, 16) = 14
        checkNear(sampleChannelAt(file, 3.0f).translation().y, 14.0f, "second wrap cycle");
    }

    // --- 3. Loop seam blend (UseLoopInterpolation) -------------------------
    // During the final frame [total-1, total) the engine blends the last key
    // towards the loop-start key instead of holding and snapping.
    {
        maxfx::Kf2File file;
        maxfx::Kf2NodeAnimation ch;
        ch.targetName = "Bone";
        ch.frameRate = 10;
        ch.looping = true;
        ch.loopInterpolation = true;
        ch.totalKeyframeCount = 11;
        ch.loopToFrame = 0;
        ch.interpolationMethod = 0;
        ch.keys.push_back(frameKey(0, 0.0f));
        ch.keys.push_back(frameKey(10, 1.5707964f));
        file.animations.push_back(ch);
        // frame 10.5 -> seam blend keys[1] -> keys[0] with t = 0.5
        const maxfx::Mat4x3 seam = sampleChannelAt(file, 1.05f);
        checkNear(seam.translation().y, 5.0f, "seam blend midpoint");
        // frame 11.1 wraps to 0.1 -> regular segment, u = 0.01
        checkNear(sampleChannelAt(file, 1.11f).translation().y, 0.1f, "post-wrap frame");
        // frame 10.0 exactly -> last key, no blend
        checkNear(sampleChannelAt(file, 1.0f).translation().y, 10.0f, "seam start is exact key");
    }

    // --- 4. Seam blend picks the key at/after LoopToFrame ------------------
    {
        maxfx::Kf2File file;
        maxfx::Kf2NodeAnimation ch;
        ch.targetName = "Bone";
        ch.frameRate = 1;
        ch.looping = true;
        ch.loopInterpolation = true;
        ch.totalKeyframeCount = 13;
        ch.loopToFrame = 4;
        ch.interpolationMethod = 0;
        ch.keys.push_back(frameKey(0, 0.0f));
        ch.keys.push_back(frameKey(4, 0.0f));
        ch.keys.push_back(frameKey(8, 0.0f));
        ch.keys.push_back(frameKey(12, 0.0f));
        file.animations.push_back(ch);
        // frame 12.5: seam blend keys[3] (frame 12) -> keys[1] (frame 4)
        checkNear(sampleChannelAt(file, 12.5f).translation().y, 8.0f, "seam blends to loop key");
        // frame 13.2 wraps to 4 + fmod(9.2, 9) = 4.2
        checkNear(sampleChannelAt(file, 13.2f).translation().y, 4.2f, "wrap into loop segment");
    }

    // --- 5. interpolationMethod 2 orthonormalizes --------------------------
    {
        maxfx::Kf2File file;
        maxfx::Kf2NodeAnimation ch;
        ch.targetName = "Bone";
        ch.frameRate = 10;
        ch.looping = false;
        ch.loopInterpolation = false;
        ch.totalKeyframeCount = 11;
        ch.interpolationMethod = 2;
        ch.keys.push_back(frameKey(0, 0.0f));
        ch.keys.push_back(frameKey(10, 1.5707964f));
        file.animations.push_back(ch);
        const maxfx::Mat4x3 mid = sampleChannelAt(file, 0.5f);
        checkNear(maxfx::length(mid.rows[0]), 1.0f, "method 2 keeps unit rows");
        checkNear(mid.rows[0].x, 0.70710678f, "method 2 half rotation x");
        checkNear(mid.rows[0].z, -0.70710678f, "method 2 half rotation z");
        checkNear(mid.translation().y, 5.0f, "method 2 translation untouched");
    }

    // --- 6. maintainMatrixScaling re-applies row lengths -------------------
    {
        maxfx::Kf2File file;
        maxfx::Kf2NodeAnimation ch;
        ch.targetName = "Bone";
        ch.frameRate = 10;
        ch.looping = false;
        ch.loopInterpolation = false;
        ch.totalKeyframeCount = 11;
        ch.interpolationMethod = 1;
        ch.maintainMatrixScaling = true;
        maxfx::Kf2AnimKey a = frameKey(0, 0.0f);
        maxfx::Kf2AnimKey b = frameKey(10, 1.5707964f);
        for (int r = 0; r < 3; ++r) {
            a.objectToParent.rows[r] = a.objectToParent.rows[r] * 2.0f;
            b.objectToParent.rows[r] = b.objectToParent.rows[r] * 2.0f;
        }
        ch.keys.push_back(a);
        ch.keys.push_back(b);
        file.animations.push_back(ch);
        // Exact key: normalize then re-scale restores the original length 2.
        checkNear(maxfx::length(sampleChannelAt(file, 0.0f).rows[0]), 2.0f,
                  "method 1 + maintain keeps exact key scale");
        // Mid blend: lengths lerped in frame space stay 2 for uniform scale.
        checkNear(maxfx::length(sampleChannelAt(file, 0.5f).rows[0]), 2.0f,
                  "method 1 + maintain keeps blended scale");
    }

    // --- 7. Frames before the first key hold it ---------------------------
    {
        maxfx::Kf2File file;
        maxfx::Kf2NodeAnimation ch;
        ch.targetName = "Bone";
        ch.frameRate = 10;
        ch.looping = true;
        ch.loopInterpolation = false;
        ch.totalKeyframeCount = 31;
        ch.loopToFrame = 0;
        ch.interpolationMethod = 0;
        ch.keys.push_back(frameKey(5, 0.0f));
        ch.keys.push_back(frameKey(30, 0.0f));
        file.animations.push_back(ch);
        checkNear(sampleChannelAt(file, 0.2f).translation().y, 5.0f, "frame before first key holds it");
        checkNear(sampleChannelAt(file, 2.0f).translation().y, 20.0f, "segment lerp");
    }
}

static void testKf2AnimationAndSkinAi() {
    const std::string pose = "docs/database/skeletons/default_skeleton/anim/Widepose.kf2";
    const std::string walk = "docs/database/skeletons/default_skeleton/anim/Walk.kf2";
    if (!maxfx::isFile(pose) || !maxfx::isFile(walk)) {
        std::fprintf(stderr, "skip kf2 animation (clips missing)\n");
        return;
    }
    const maxfx::Kf2File poseFile = maxfx::Kf2Reader::loadFromFile(pose);
    check(!poseFile.animations.empty(), "widepose has bone channels");
    bool hasPelvis = false;
    for (std::size_t i = 0; i < poseFile.animations.size(); ++i) {
        if (poseFile.animations[i].targetName == "Pelvis") {
            hasPelvis = true;
            check(!poseFile.animations[i].keys.empty(), "pelvis has a key");
        }
    }
    check(hasPelvis, "widepose pelvis channel");
    const maxfx::Kf2File walkFile = maxfx::Kf2Reader::loadFromFile(walk);
    check(walkFile.animations.size() >= poseFile.animations.size() / 2, "walk has many channels");
    check(maxfx::kf2AnimationDuration(walkFile) > 0.1f, "walk duration");
    std::vector<std::string> names;
    std::vector<maxfx::Mat4x3> worlds;
    maxfx::kf2BuildSkeletonWorlds(walkFile, 0.1f, &poseFile, &names, &worlds);
    check(!names.empty() && names.size() == worlds.size(), "sampled skeleton worlds");

    const std::string root = maxfx::DatabaseReader::locateRoot("docs");
    if (root.empty()) {
        return;
    }
    maxfx::Database db = maxfx::DatabaseReader::load(root);
    const maxfx::SkinDef* mickey = db.findSkin("C1_All_Mickey");
    check(mickey != 0, "mickey skin");
    if (mickey != 0) {
        checkNear(mickey->character.capsule.radius, 0.31f, "mickey capsule radius");
        checkNear(mickey->character.ai.visualPerceivingRadius, 50.0f, "mickey visual radius");
        check(mickey->character.maxHealth > 1.0f, "mickey health");
        check(maxfx::findAnimClip(mickey->character, maxfx::kCharAnimStand) != 0 ||
                  maxfx::findAnimClip(mickey->character, maxfx::kCharAnimWalk) != 0,
              "mickey has stand or walk clip");
        const maxfx::CharacterAnimClip* walkClip =
            maxfx::findAnimClip(mickey->character, maxfx::kCharAnimWalk);
        if (walkClip != 0) {
            check(maxfx::isFile(walkClip->resolvedPath), "walk clip resolves to kf2");
        }
        std::vector<std::string> names;
        names.push_back("C1_All_Mickey");
        maxfx::DatabaseReader::loadModels(db, names, std::vector<std::string>());
        const maxfx::CharacterAnimClip* poseClip =
            maxfx::findAnimClip(mickey->character, maxfx::kCharAnimPose);
        if (walkClip != 0 && maxfx::isFile(walkClip->resolvedPath)) {
            check(db.model(walkClip->resolvedPath) != 0, "walk clip is cached for skinning");
        }
        if (poseClip != 0 && maxfx::isFile(poseClip->resolvedPath)) {
            check(db.model(poseClip->resolvedPath) != 0, "pose clip is cached for skinning");
        }
    }

    const std::string kfs = "docs/database/skins/balder_alex/Alex_Balder_L0.kfs";
    const std::string skd = "docs/database/skins/balder_alex/ALEX_BALDER_L0.SKD";
    if (maxfx::isFile(kfs) && maxfx::isFile(skd)) {
        const maxfx::Kf2File mesh = maxfx::Kf2Reader::loadFromFile(kfs);
        const maxfx::Kf2File skin = maxfx::Kf2Reader::loadFromFile(skd);
        check(!skin.skins.empty(), "alex skd has skin chunk");
        std::vector<maxfx::Kf2DrawMesh> posed;
        maxfx::kf2BuildSkinnedDrawMeshes(mesh, &skin, &poseFile, &poseFile, 0.0f, posed);
        check(!posed.empty(), "skinned draw meshes");
        check(posed[0].modelSpace, "skinned mesh is model-space");
        float minY = 1.0e30f, maxY = -1.0e30f;
        for (std::size_t m = 0; m < posed.size(); ++m) {
            for (std::size_t p = 0; p < posed[m].parts.size(); ++p) {
                const std::vector<maxfx::Kf2DrawVertex>& vs = posed[m].parts[p].vertices;
                for (std::size_t i = 0; i < vs.size(); ++i) {
                    minY = std::min(minY, vs[i].position.y);
                    maxY = std::max(maxY, vs[i].position.y);
                }
            }
        }
        check(maxY - minY > 1.2f, "skinned pose is upright (Y extent)");
        check(maxY > 1.0f, "skinned pose head above 1m");
        posed.clear();
        maxfx::kf2BuildSkinnedDrawMeshes(mesh, &skin, &poseFile, &walkFile, 0.2f, posed);
        check(!posed.empty(), "skinned walk meshes");
        float wminY = 1.0e30f, wmaxY = -1.0e30f, wminX = 1.0e30f, wmaxX = -1.0e30f;
        for (std::size_t m = 0; m < posed.size(); ++m) {
            for (std::size_t p = 0; p < posed[m].parts.size(); ++p) {
                const std::vector<maxfx::Kf2DrawVertex>& vs = posed[m].parts[p].vertices;
                for (std::size_t i = 0; i < vs.size(); ++i) {
                    wminY = std::min(wminY, vs[i].position.y);
                    wmaxY = std::max(wmaxY, vs[i].position.y);
                    wminX = std::min(wminX, vs[i].position.x);
                    wmaxX = std::max(wmaxX, vs[i].position.x);
                }
            }
        }
        check(wmaxY - wminY > 1.2f, "skinned walk is upright (Y extent)");
        check(wmaxX - wminX < 1.6f, "skinned walk is not rubber-hose wide");

        const std::string stand = "docs/database/skeletons/default_skeleton/anim/Mobster_Stand.kf2";
        if (maxfx::isFile(stand)) {
            const maxfx::Kf2File standFile = maxfx::Kf2Reader::loadFromFile(stand);
            posed.clear();
            maxfx::kf2BuildSkinnedDrawMeshes(mesh, &skin, &poseFile, &standFile, 1.0f, posed);
            float sminY = 1.0e30f, smaxY = -1.0e30f, sminX = 1.0e30f, smaxX = -1.0e30f;
            float sminZ = 1.0e30f, smaxZ = -1.0e30f;
            for (std::size_t m = 0; m < posed.size(); ++m) {
                for (std::size_t p = 0; p < posed[m].parts.size(); ++p) {
                    const std::vector<maxfx::Kf2DrawVertex>& vs = posed[m].parts[p].vertices;
                    for (std::size_t i = 0; i < vs.size(); ++i) {
                        sminY = std::min(sminY, vs[i].position.y);
                        smaxY = std::max(smaxY, vs[i].position.y);
                        sminX = std::min(sminX, vs[i].position.x);
                        smaxX = std::max(smaxX, vs[i].position.x);
                        sminZ = std::min(sminZ, vs[i].position.z);
                        smaxZ = std::max(smaxZ, vs[i].position.z);
                    }
                }
            }
            check(smaxY - sminY > 1.2f, "skinned stand is upright");
            check(smaxX - sminX < 1.2f, "skinned stand arms down, not T-pose");
            const float cx = 0.5f * (sminX + smaxX);
            check(std::fabs(cx) < 0.25f, "in-place root lock keeps stand on X origin");
        }
    }
}

static void testGameMessagesAndCatalog() {
    const std::vector<maxfx::GameMessage> jump =
        maxfx::parseGameMessages("this->C_Jump( 7.5  );");
    check(jump.size() == 1, "parse C_Jump");
    check(maxfx::methodIs(jump[0], "c_jump"), "C_Jump method");
    if (!jump.empty() && !jump[0].args.empty()) {
        check(jump[0].args[0].find("7") != std::string::npos, "C_Jump arg");
    }
    const std::vector<maxfx::GameMessage> many = maxfx::parseGameMessages(
        "this->c_pickupweapon ( beretta ); this->c_pickupammo ( beretta, 20); "
        "maxpayne_gamemode->gm_setplayercontrols(true);");
    check(many.size() == 3, "three oninit messages");
    bool sawGun = false, sawAmmo = false, sawCtrl = false;
    for (std::size_t i = 0; i < many.size(); ++i) {
        if (maxfx::methodIs(many[i], "c_pickupweapon")) {
            sawGun = true;
        }
        if (maxfx::methodIs(many[i], "c_pickupammo")) {
            sawAmmo = true;
        }
        if (maxfx::methodIs(many[i], "gm_setplayercontrols")) {
            sawCtrl = true;
        }
    }
    check(sawGun && sawAmmo && sawCtrl, "OnInit C_PickupWeapon / C_PickupAmmo / GM_SetPlayerControls");

    const std::string root = maxfx::DatabaseReader::locateRoot("docs");
    if (root.empty()) {
        return;
    }
    const maxfx::GameCatalog cat = maxfx::loadGameCatalog(root);
    check(!cat.weapons.empty() || !maxfx::isDirectory(maxfx::joinPath(root, "weapons")),
          "weapon catalog");
    const maxfx::WeaponDef* beretta = cat.findWeapon("beretta");
    if (beretta) {
        check(beretta->clipSize == 18, "beretta clip 18");
        check(beretta->castLength > 1.0f, "beretta cast length");
    }
    const maxfx::ProjectileDef* bullet = cat.findProjectile("bullet_beretta");
    if (bullet) {
        check(bullet->damage == 5.0f, "beretta bullet damage 5");
        check(bullet->damagesCharacter, "beretta damages character");
    }
    check(!cat.pages.empty(), "graphic novel pages parsed");
    check(!cat.chapters.empty(), "graphic novel chapters grouped");
    bool cineInReader = false;
    for (std::size_t c = 0; c < cat.chapters.size(); ++c) {
        for (std::size_t p = 0; p < cat.chapters[c].pageIndices.size(); ++p) {
            const int pi = cat.chapters[c].pageIndices[p];
            if (pi >= 0 && static_cast<std::size_t>(pi) < cat.pages.size() && cat.pages[static_cast<std::size_t>(pi)].cine) {
                cineInReader = true;
            }
        }
    }
    check(!cineInReader, "chapter list skips cine pages");
    checkEq(maxfx::graphicNovelChapterKey("p1l0a_005_ok"), "p1l0a", "chapter key strips _ok");
    checkEq(maxfx::graphicNovelPageLabel("p1l0a_003"), "003", "page label is panel number");

    maxfx::GameRuntime rt;
    rt.loadCatalog(root);
    maxfx::CollisionWorld emptyWorld;
    rt.player.grounded = true;
    rt.tickPlayer(0.05f, true, false, false, false, true, false, emptyWorld);
    check(rt.player.velocity.y > 1.0f, "C_Jump / space applies jump velocity");
    check(std::strcmp(maxfx::triggerTypeName(maxfx::kTriggerActionButton), "action_button") == 0,
          "action_button name");
}

static void testSoundOmniAndFsmCues() {
    checkNear(maxfx::soundOmniGain(0.5f, 1.0f, 10.0f), 1.0f, "inside hotspot full gain");
    checkNear(maxfx::soundOmniGain(1.0f, 1.0f, 10.0f), 1.0f, "at hotspot full gain");
    checkNear(maxfx::soundOmniGain(10.0f, 1.0f, 10.0f), 0.0f, "at falloff silent");
    checkNear(maxfx::soundOmniGain(20.0f, 1.0f, 10.0f), 0.0f, "past falloff silent");
    checkNear(maxfx::soundOmniGain(5.5f, 1.0f, 10.0f), 0.5f, "mid falloff half gain");

    const std::vector<maxfx::GameMessage> msgs = maxfx::parseGameMessages(
        "this->A_Play3DSound( ambient, ac_fan_loop, \"\" ); this->A_PlaySound( story, P1L0a_001 );");
    check(msgs.size() == 2, "A_Play3DSound + A_PlaySound parse");
    check(maxfx::methodIs(msgs[0], "a_play3dsound"), "method a_play3dsound");
    check(msgs[0].args.size() >= 2, "3d sound category+name");
    if (msgs[0].args.size() >= 2) {
        checkEq(msgs[0].args[0], "ambient", "3d category");
        checkEq(msgs[0].args[1], "ac_fan_loop", "3d cue name");
    }

    if (!maxfx::isFile("docs/Part1_Level1.ldb")) {
        std::fprintf(stderr, "skip fsm 3d cues (Part1_Level1.ldb missing)\n");
        return;
    }
    try {
        const maxfx::Level level = maxfx::LdbReader::loadFromFile("docs/Part1_Level1.ldb");
        std::vector<maxfx::SoundCueRequest> cues;
        maxfx::collectLevelSoundCues(level, &cues);
        int n3d = 0;
        for (std::size_t i = 0; i < cues.size(); ++i) {
            if (cues[i].is3d) {
                ++n3d;
            }
        }
        check(n3d >= 11, "Part1_Level1 FSM startup has 11 A_Play3DSound");
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "FAIL: Part1_Level1.ldb cues: %s\n", ex.what());
        ++gFails;
    }
}

// X_LevelRuntimeTrigger semantics against the shipped sample level
// (docs/Part1_Level1.ldb): one-shot latch, own-FSM-only T_Activate dispatch,
// T_Enable re-arm, use-button types, and the level intro comic queue.
static void testTriggerSemantics() {
    if (!maxfx::isFile("docs/Part1_Level1.ldb")) {
        std::fprintf(stderr, "skip trigger semantics (Part1_Level1.ldb missing)\n");
        return;
    }
    maxfx::Level level;
    try {
        level = maxfx::LdbReader::loadFromFile("docs/Part1_Level1.ldb");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: trigger level load: %s\n", e.what());
        ++gFails;
        return;
    }
    // Every trigger must wire to the FSM named like it minus ".TRIGGER"
    // (X_LevelDBTrigger::getFSM).
    {
        int wired = 0;
        for (std::size_t i = 0; i < level.triggers.size(); ++i) {
            std::string want = level.triggers[i].sharedName;
            const std::string suf = ".TRIGGER";
            if (want.size() > suf.size() &&
                want.compare(want.size() - suf.size(), suf.size(), suf) == 0) {
                want = want.substr(0, want.size() - suf.size());
            }
            std::string wl = want;
            for (std::size_t c = 0; c < wl.size(); ++c) {
                wl[c] = static_cast<char>(std::tolower(static_cast<unsigned char>(wl[c])));
            }
            for (std::size_t f = 0; f < level.fsms.size(); ++f) {
                std::string fl = level.fsms[f].sharedName;
                for (std::size_t c = 0; c < fl.size(); ++c) {
                    fl[c] = static_cast<char>(std::tolower(static_cast<unsigned char>(fl[c])));
                }
                if (fl == wl) {
                    ++wired;
                    break;
                }
            }
        }
        check(wired == static_cast<int>(level.triggers.size()),
              "every trigger wires to its own FSM");
    }

    maxfx::GameRuntime game;
    game.resetLevel(level, 0, maxfx::Vec3(0, 0, 0), 0.0f);
    // Fabricate the intro pages (::startroom::fsm_start picks P1L1a_001..004).
    const char* introIds[4] = {"P1L1a_001", "P1L1a_002", "P1L1a_003", "P1L1a_004"};
    for (int i = 0; i < 4; ++i) {
        maxfx::GraphicNovelPageDef pg;
        pg.id = introIds[i];
        game.catalog.pages.push_back(pg);
    }

    // --- Level startup: the intro comic queues once ------------------------
    std::vector<maxfx::CharacterActor> noActors;
    game.startLevel(level, noActors);
    check(game.mode == maxfx::kModeGraphicNovel, "level startup opens the intro comic");
    check(game.comicIndex == 0, "intro comic starts at the first picked note");
    check(game.pendingNotes.size() == 3, "three intro pages remain queued");
    check(game.nextPendingNote() == 1, "queue pops in pickup order");
    check(game.nextPendingNote() == 2, "queue pops in pickup order (2)");
    check(game.nextPendingNote() == 3, "queue pops in pickup order (3)");
    check(game.nextPendingNote() == -1, "queue exhausted");
    game.mode = maxfx::kModePlaying;

    // Locate the sample triggers used below.
    int controlDesk = -1, buttonLookat = -1, cameraTrigger = -1, trainOn = -1, trainOff = -1,
        unrelated = -1;
    for (std::size_t i = 0; i < level.triggers.size(); ++i) {
        const std::string& n = level.triggers[i].sharedName;
        if (n == "::2nd_rail::control_desk::trigger.TRIGGER") controlDesk = static_cast<int>(i);
        if (n == "::2nd_rail::button_lookat.TRIGGER") buttonLookat = static_cast<int>(i);
        if (n == "::2nd_rail::camera_trigger.TRIGGER") cameraTrigger = static_cast<int>(i);
        if (n == "::bridget01::p5_train_on.TRIGGER") trainOn = static_cast<int>(i);
        if (n == "::bridget01::p5_train_off.TRIGGER") trainOff = static_cast<int>(i);
        if (n == "::c::pt1.TRIGGER") unrelated = static_cast<int>(i);
    }
    check(controlDesk >= 0 && buttonLookat >= 0 && cameraTrigger >= 0 && trainOn >= 0 &&
              trainOff >= 0 && unrelated >= 0,
          "sample triggers found");

    // --- One-shot latch (no self-disable in this T_Activate) ----------------
    if (buttonLookat >= 0 && unrelated >= 0) {
        check(game.activateTrigger(buttonLookat, level, noActors), "first T_Activate fires");
        check(game.triggers[static_cast<std::size_t>(buttonLookat)].activated, "latch state set");
        check(!game.activateTrigger(buttonLookat, level, noActors),
              "latch blocks the second fire");
        // Its T_Activate runs "::2nd_rail::button_lookat->FSM_Switch(2)" in
        // state "1" — the runtime state must have switched.
        for (std::size_t f = 0; f < level.fsms.size(); ++f) {
            if (level.fsms[f].sharedName == "::2nd_rail::button_lookat") {
                check(f < game.fsmStates.size() && game.fsmStates[f].state == "2",
                      "T_Activate switches the FSM state");
            }
        }
        // No broadcast: an unrelated trigger must stay armed and untouched.
        check(game.triggers[static_cast<std::size_t>(unrelated)].enabled &&
                  !game.triggers[static_cast<std::size_t>(unrelated)].activated,
              "unrelated trigger untouched");
    }

    // --- Authored one-shot: self T_Enable(false) + targeted disables --------
    if (controlDesk >= 0 && buttonLookat >= 0 && unrelated >= 0) {
        const bool wasEnabled =
            game.triggers[static_cast<std::size_t>(buttonLookat)].enabled;
        check(game.activateTrigger(controlDesk, level, noActors), "control desk fires");
        check(!game.triggers[static_cast<std::size_t>(controlDesk)].enabled,
              "self T_Enable(false) disables the control desk trigger");
        check(!game.activateTrigger(controlDesk, level, noActors),
              "disabled trigger refuses to fire");
        if (wasEnabled) {
            check(!game.triggers[static_cast<std::size_t>(buttonLookat)].enabled,
                  "T_Activate disables the targeted look-at trigger");
        }
        // T_Enable(true) re-arms (X_LevelRuntimeTrigger::receive resets +1613).
        maxfx::GameMessage enable;
        enable.target = "::2nd_rail::button_lookat.TRIGGER";
        enable.method = "t_enable";
        enable.args.push_back("true");
        game.dispatch(enable, level, noActors, -1);
        check(game.triggers[static_cast<std::size_t>(buttonLookat)].enabled &&
                  !game.triggers[static_cast<std::size_t>(buttonLookat)].activated,
              "T_Enable(true) re-arms the latch");
        check(game.triggers[static_cast<std::size_t>(unrelated)].enabled,
              "unrelated trigger still enabled");
    }

    // --- Type 0 fires only on Use ------------------------------------------
    if (cameraTrigger >= 0) {
        const maxfx::Trigger& tr = level.triggers[static_cast<std::size_t>(cameraTrigger)];
        const maxfx::Mat4x3 wx = maxfx::combine(maxfx::roomWorldTransform(level, tr.properties.roomId),
                                                tr.properties.objectToRoom);
        game.player.position = maxfx::transformPoint(wx, maxfx::Vec3(0, 0, 0));
        game.tickTriggers(0.016f, level, false, maxfx::Vec3(0, 0, 1), noActors);
        check(!game.triggers[static_cast<std::size_t>(cameraTrigger)].activated,
              "action trigger waits for Use");
        game.tickTriggers(0.016f, level, true, maxfx::Vec3(0, 0, 1), noActors);
        check(game.triggers[static_cast<std::size_t>(cameraTrigger)].activated,
              "action trigger fires on Use");
        game.tickTriggers(0.016f, level, true, maxfx::Vec3(0, 0, 1), noActors);
        // No observable counter; the latch flag staying set is the contract.
        check(game.triggers[static_cast<std::size_t>(cameraTrigger)].activated,
              "action trigger stays latched");
    }

    // --- Type 1 fires on player entry, exactly once -------------------------
    if (trainOn >= 0) {
        const maxfx::Trigger& tr = level.triggers[static_cast<std::size_t>(trainOn)];
        const maxfx::Mat4x3 wx = maxfx::combine(maxfx::roomWorldTransform(level, tr.properties.roomId),
                                                tr.properties.objectToRoom);
        const maxfx::Vec3 center = maxfx::transformPoint(wx, maxfx::Vec3(0, 0, 0));
        game.player.position = center + maxfx::Vec3(5.0f, 0.0f, 0.0f);
        game.tickTriggers(0.016f, level, false, maxfx::Vec3(0, 0, 1), noActors);
        check(!game.triggers[static_cast<std::size_t>(trainOn)].activated,
              "collide trigger armed outside the sphere");
        game.player.position = center;
        game.tickTriggers(0.016f, level, false, maxfx::Vec3(0, 0, 1), noActors);
        check(!game.triggers[static_cast<std::size_t>(trainOn)].enabled,
              "collide trigger fires on entry (self-disables)");
        if (trainOff >= 0) {
            check(game.triggers[static_cast<std::size_t>(trainOff)].enabled,
                  "train_on T_Activate re-enables train_off");
        }
        game.player.position = center + maxfx::Vec3(5.0f, 0.0f, 0.0f);
        game.tickTriggers(0.016f, level, false, maxfx::Vec3(0, 0, 1), noActors);
        game.player.position = center;
        check(!game.activateTrigger(trainOn, level, noActors),
              "collide trigger fires only once");
    }

    // --- Engine pitch formula ------------------------------------------------
    checkNear(maxfx::soundPitchMultiplier(0, 44100), 1.0f, "no script pitch = native rate");
    checkNear(maxfx::soundPitchMultiplier(44100, 44100), 1.0f, "44100/44100 = 1x");
    checkNear(maxfx::soundPitchMultiplier(22050, 44100), 0.5f, "22050/44100 = half speed");
    checkNear(maxfx::soundPitchMultiplier(44100, 22050), 2.0f, "44100/22050 = double speed");
}

// Cinematics: camera-path parsing, clip frame hooks and the runtime
// cutscene state machine (frame-message edges, fades, [Exit] dispatch).
static void testCinematics(const std::string& tmpDir) {
    using namespace maxfx;
    // --- camerapaths.txt parsing ---
    const std::string dbRoot = joinPath(tmpDir, "cine_db");
    const std::string cpDir = joinPath(dbRoot, "camerapaths");
    check(isDirectory(cpDir) || std::system((std::string("mkdir -p ") + cpDir).c_str()) == 0,
          "mkdir camerapaths");
    const std::string cpPath = joinPath(cpDir, "camerapaths.txt");
    std::FILE* f = std::fopen(cpPath.c_str(), "wb");
    check(f != 0, "open camerapaths.txt for write");
    if (f != 0) {
        std::fputs("[intro_fly]\n{\n"
                   "  [Attributes]\n  {\n"
                   "    FadeInTime = 0.5;\n"
                   "    FadeOutTime = 0.25;\n"
                   "    Abortable = TRUE;\n"
                   "  }\n"
                   "  [AnimationSet]\n  {\n"
                   "    [Animation] Filename = \"..\\\\camerapaths\\\\intro.kf2\";\n"
                   "  }\n"
                   "  [Exit]\n  {\n"
                   "    [Message] String = \"this->C_SetHealth( 0.25 );\";\n"
                   "  }\n"
                   "}\n"
                   "[pause]\n{\n"
                   "  [AnimationSet]\n  {\n"
                   "    [Animation] Filename = \"pause.kf2\";\n"
                   "  }\n"
                   "}\n",
                   f);
        std::fclose(f);
    }
    CameraPathCatalog cats;
    cats.load(dbRoot);
    check(cats.size() == 2, "two camera paths parsed");
    const CameraPathDef* fly = cats.find("Intro_Fly");
    check(fly != 0, "camera path lookup case-insensitive");
    if (fly != 0) {
        checkNear(fly->fadeInTime, 0.5f, "FadeInTime");
        checkNear(fly->fadeOutTime, 0.25f, "FadeOutTime");
        check(fly->abortable, "Abortable");
        check(fly->exitMessages.size() == 1 && fly->exitMessages[0].find("SetHealth") != std::string::npos,
              "exit message");
        check(!fly->animationFile.empty(), "animation filename kept");
    }
    const CameraPathDef* pause = cats.find("pause");
    check(pause != 0 && !pause->abortable, "second path defaults");

    // --- runtime cutscene state machine ---
    GameRuntime rt;
    rt.cameraPaths = cats;
    Level level;  // empty level is enough: dispatch only touches state
    std::vector<CharacterActor> noActors;

    // CAM_AnimateAbsolute(block, room) via dispatch.
    std::vector<GameMessage> msgs =
        parseGameMessages("this->CAM_AnimateAbsolute( intro_fly, ::startroom );");
    check(msgs.size() == 1, "parse CAM_AnimateAbsolute");
    if (!msgs.empty()) {
        rt.dispatch(msgs[0], level, noActors, -1);
        check(rt.cine.cameraActive, "camera path activated");
        check(rt.cine.cameraMode == 1, "absolute mode");
        checkEq(rt.cine.cameraPath, "intro_fly", "path name");
        checkEq(rt.cine.cameraRoom, "startroom", "room arg strips ::");
    }

    // Start a cinematic clip with frame hooks; drive the edges.
    std::vector<ClipFrameMessage> hooks;
    ClipFrameMessage h0;
    h0.frame = 0;
    h0.text = "MaxPayne_GameMode->GM_EnableWideScreen( true, 0 );";
    hooks.push_back(h0);
    ClipFrameMessage h5;
    h5.frame = 5;
    h5.text = "MaxPayne_HudMode->MPHM_FadeToColor(0x00000000,0x000000ff,0.5);";
    hooks.push_back(h5);
    ClipFrameMessage h8;
    h8.frame = 8;
    h8.text = "this->C_EnableCinematicMode( false );";
    hooks.push_back(h8);
    rt.startCinematic(297, 0.4f, 30, hooks, "");
    check(rt.cine.active && rt.cine.clipIndex == 297, "cinematic started");
    // 0.05s at 30fps -> frame 1: hook 0 fired only.
    rt.tickCinematic(0.05f, level, noActors);
    check(rt.cine.widescreen, "frame 0 hook ran (widescreen)");
    checkNear(rt.cine.fadeDuration, 0.0f, "fade not started yet");
    // 0.2s -> frame 6: fade hook ran.
    rt.tickCinematic(0.15f, level, noActors);
    checkNear(rt.cine.fadeDuration, 0.5f, "frame 5 hook ran (fade)");
    checkNear(rt.cine.fadeTo[3], 1.0f, "fade target alpha ff");
    check(rt.cine.fadeElapsed > 0.0f, "fade progresses");
    // 0.25s more -> past frame 8 (0.28s) and past duration 0.4s: clip done.
    rt.tickCinematic(0.25f, level, noActors);
    check(!rt.cine.cinematicMode, "frame 8 hook ran (cinematic mode off)");
    check(!rt.cine.active, "clip ended at duration");
    // Camera path still running (no duration resolved): exit fires at 2s.
    rt.cine.cameraDuration = 1.0f;
    rt.cine.cameraTime = 0.9f;
    rt.tickCinematic(0.2f, level, noActors);
    check(!rt.cine.cameraActive, "camera path ended");
    // The [Exit] message ran (C_SetHealth scales by max health).
    checkNear(rt.player.health, 0.25f * rt.player.maxHealth, "camera path [Exit] message dispatched");

    // MPHM_FadeToColor arg parsing (hex colours).
    rt.cine = CinematicState();
    msgs = parseGameMessages("this->MPHM_FadeToColor(0x10203040,0x50607080,1.5);");
    check(msgs.size() == 1, "parse MPHM_FadeToColor");
    if (!msgs.empty()) {
        rt.dispatch(msgs[0], level, noActors, -1);
        checkNear(rt.cine.fadeFrom[0], 0x10 / 255.0f, "fade from r");
        checkNear(rt.cine.fadeFrom[3], 0x40 / 255.0f, "fade from a");
        checkNear(rt.cine.fadeTo[2], 0x70 / 255.0f, "fade to b");
        checkNear(rt.cine.fadeDuration, 1.5f, "fade time");
    }

    // stopCinematic only stops the clip; the presentation clears once the
    // whole cutscene (clip + camera path) is over.
    rt.startCinematic(1, 1.0f, 30, hooks, "");
    rt.stopCinematic();
    check(!rt.cine.active, "stopCinematic stops the clip");
    rt.tickCinematic(0.016f, level, noActors);
    check(!rt.cine.widescreen && rt.cine.hudVisible, "presentation cleared when fully over");

    // A finished fade-to-black does not outlive the cutscene.
    rt.cine = CinematicState();
    rt.startCameraPath("intro_fly", 1, "");
    check(rt.cine.cameraActive, "second path start");
    rt.cine.cameraDuration = 0.2f;
    msgs = parseGameMessages("this->MPHM_FadeToColor(0x00000000,0x000000ff,0.1);");
    rt.dispatch(msgs[0], level, noActors, -1);
    checkNear(rt.cine.fadeDuration, 0.1f, "fade running");
    rt.tickCinematic(0.05f, level, noActors);
    check(rt.cine.cameraActive, "path still flying");
    rt.tickCinematic(0.3f, level, noActors);
    check(!rt.cine.cameraActive && rt.cine.fadeDuration <= 0.0f,
          "fade cleared when the cutscene is over");

    // Esc-style full abort clears everything at once.
    rt.startCinematic(1, 1.0f, 30, hooks, "");
    rt.startCameraPath("intro_fly", 1, "");
    msgs = parseGameMessages("MaxPayne_GameMode->GM_EnableWideScreen( true, 0 );");
    rt.dispatch(msgs[0], level, noActors, -1);
    msgs = parseGameMessages("this->MPHM_FadeToColor(0x00000000,0x000000ff,1.0);");
    rt.dispatch(msgs[0], level, noActors, -1);
    check(rt.cine.widescreen && rt.cine.fadeDuration > 0.0f, "cutscene presentation on");
    rt.abortCinematic();
    check(!rt.cine.active && !rt.cine.cameraActive && !rt.cine.widescreen &&
              rt.cine.hudVisible && rt.cine.fadeDuration <= 0.0f,
          "abortCinematic clears everything");

    // --- [Animation] + sibling [Properties] pairing (real skin format) ---
    // R_ScriptLoader keeps the unbraced [Animation] assignments and the
    // braced [Properties]{...} as consecutive SIBLING blocks;
    // X_SharedDBAnimationContainer::construct pairs animation i with the
    // block at getBlockIndex("animation", i) + 1 when it is "properties".
    {
        const std::string skinText =
            "[Configuration]\n{\n"
            "  [Properties]\n  {\n"
            "    MaximumHealth = 60;\n"
            "  }\n"
            "  [Modifiers]\n  {\n"
            "    [Animation] Index = 999; Filename = \"cinematics\\demo.kf2\";\n"
            "    [Properties]\n    {\n"
            "      [Movement]\n      {\n"
            "        Filename = \"cinematics\\demo_mov.kf2\";\n"
            "      }\n"
            "      [Message] Frame = 5; String = \"this->GM_ChangeGameSpeed( 0.01, 0.25 );\";\n"
            "      [Message] Frame = 30; String = \"this->C_EnableCinematicMode( false );\";\n"
            "    }\n"
            "    [Animation] Index = 1000; Filename = \"anim\\plain.kf2\";\n"
            "  }\n"
            "}\n";
        Script script = Script::parseText(skinText, "demo_skin.txt");
        CharacterConfig cfg;
        fillCharacterConfig(cfg, script.root(), "demo_skin.txt", "");
        const CharacterAnimClip* demo = findAnimClip(cfg, 999);
        check(demo != 0, "sibling-properties clip parsed");
        if (demo != 0) {
            check(demo->frameMessages.size() == 2, "hooks come from the sibling [Properties]");
            check(demo->movementFile.find("demo_mov.kf2") != std::string::npos,
                  "movement file comes from the sibling [Properties]");
            checkNear(demo->frameMessages[0].frame, 5.0f, "hook frame");
        }
        const CharacterAnimClip* plain = findAnimClip(cfg, 1000);
        check(plain != 0 && plain->frameMessages.empty(),
              "clip without a following [Properties] has no hooks");
    }

    // --- game speed (GM_ChangeGameSpeed / GM_EnableBulletTime) ---
    {
        GameRuntime gs;
        checkNear(gs.gameSpeed, 1.0f, "game speed starts normal");
        std::vector<GameMessage> m = parseGameMessages("this->GM_ChangeGameSpeed( 0.5, 2.0 );");
        gs.dispatch(m[0], level, noActors, -1);
        checkNear(gs.gameSpeedTarget, 0.5f, "GM_ChangeGameSpeed target");
        gs.tickGameSpeed(1.0f);  // half of the 2s ramp
        checkNear(gs.gameSpeed, 0.75f, "game speed ramps linearly");
        gs.tickGameSpeed(1.0f);
        checkNear(gs.gameSpeed, 0.5f, "game speed reaches the target");
        m = parseGameMessages("this->GM_ChangeGameSpeed( 0.01, 0 );");
        gs.dispatch(m[0], level, noActors, -1);
        checkNear(gs.gameSpeed, 0.01f, "zero transition snaps");
        m = parseGameMessages("this->GM_EnableBulletTime( true );");
        gs.dispatch(m[0], level, noActors, -1);
        check(gs.bulletTime && gs.gameSpeedTarget < 1.0f, "bullet time slows the game");
        m = parseGameMessages("this->GM_EnableBulletTime( false );");
        gs.dispatch(m[0], level, noActors, -1);
        check(!gs.bulletTime && gs.gameSpeedTarget == 1.0f, "bullet time restores the target");
        gs.gameSpeed = 0.3f;
        gs.resetLevel(level, 0, Vec3(), 0.0f);
        checkNear(gs.gameSpeed, 1.0f, "level reset clears game speed");
    }

    // --- cinematic mode releases controls when the clip ends ---
    {
        GameRuntime cm;
        cm.player.controlsEnabled = true;
        std::vector<ClipFrameMessage> hooks;
        hooks.push_back(ClipFrameMessage());
        hooks.back().frame = 0;
        hooks.back().text = "this->C_EnableCinematicMode( true );";
        cm.startCinematic(1, 0.5f, 30, hooks, "");
        cm.tickCinematic(0.1f, level, noActors);
        check(!cm.player.controlsEnabled && cm.cine.cinematicMode,
              "C_EnableCinematicMode hook disables controls");
        cm.stopCinematic();
        check(cm.player.controlsEnabled && !cm.cine.cinematicMode,
              "clip end restores controls");
        // C_EnableCinematicMode( false ) alone also re-enables them.
        cm.startCinematic(1, 0.5f, 30, hooks, "");
        cm.tickCinematic(0.1f, level, noActors);
        std::vector<GameMessage> off = parseGameMessages("this->C_EnableCinematicMode( false );");
        cm.dispatch(off[0], level, noActors, -1);
        check(cm.player.controlsEnabled, "C_EnableCinematicMode( false ) re-enables controls");
        cm.stopCinematic();
    }

    // Abortable flag drives user aborts (movement) of camera paths.
    rt.cine = CinematicState();
    rt.startCameraPath("intro_fly", 1, "");
    check(rt.cameraPathAbortable(), "intro_fly is abortable");
    rt.startCameraPath("pause", 1, "");
    check(!rt.cameraPathAbortable(), "pause is not abortable");
    rt.abortCameraPath(level, noActors);
    check(!rt.cine.cameraActive, "abortCameraPath ends the path");
}

// Dynamic-object poses (doors), noclip descend and the cinematic duration
// fallback.
static void testDynamicObjectsAndNoclip() {
    using namespace maxfx;
    GameRuntime rt;
    Level level;

    // --- dynamicMeshPose: MeshAnimation start -> end interpolation ---
    MeshAnimation anim;
    anim.lengthSeconds = 2.0f;
    anim.startTransform = makeEntity(Vec3(0.0f, 0.0f, 0.0f), 0.0f);
    anim.endTransform = makeEntity(Vec3(2.0f, 0.0f, 0.0f), toRadians(90.0f));
    const Mat4x3 p0 = rt.dynamicMeshPose(anim, 0.0f);
    checkNear(p0.rows[3].x, 0.0f, "door pose t=0 at start");
    const Mat4x3 p1 = rt.dynamicMeshPose(anim, 1.0f);
    checkNear(p1.rows[3].x, 2.0f, "door pose t=1 at end");
    // 90-degree yaw at t=1: X axis rotates onto Z.
    checkNear(std::fabs(p1.rows[0].z), 1.0f, "door pose t=1 rotation");
    const Mat4x3 pH = rt.dynamicMeshPose(anim, 0.5f);
    checkNear(pH.rows[3].x, 1.0f, "door pose half-way translation");
    // Rotated pose stays orthonormal (fixCrossAnimation-style).
    const Vec3 rx = normalize(pH.rows[0]);
    const Vec3 rz = normalize(pH.rows[2]);
    checkNear(std::fabs(dot(rx, rz)), 0.0f, "door pose orthonormal");

    // --- tickDoors still drives the state machine ---
    DynamicMesh dm;
    dm.name = "door_test";
    dm.animations.push_back(anim);
    level.dynamicMeshes.push_back(dm);
    rt.resetLevel(level, 0, Vec3(), 0.0f);
    check(rt.doors.size() == 1, "one door state");
    std::vector<GameMessage> open = parseGameMessages("X.DO->DO_Animate( door_test );");
    std::vector<CharacterActor> noActors;
    // The dispatch target carries the mesh name; fall back to the first idle.
    rt.dispatch(open[0], level, noActors, -1);
    check(rt.doors[0].dir == 1, "DO_Animate starts opening");
    rt.tickDoors(1.0f, level);
    check(rt.doors[0].t > 0.4f, "door half open after half the clip");
    rt.tickDoors(1.5f, level);
    check(rt.doors[0].open && rt.doors[0].dir == 0, "door open at clip end");

    // --- noclip: N toggles, Ctrl descends ---
    CollisionWorld emptyWorld;
    rt.player.noclip = true;
    rt.player.position = Vec3(0.0f, 10.0f, 0.0f);
    rt.tickPlayer(0.5f, false, false, false, false, false, false, emptyWorld, true);
    check(rt.player.position.y < 10.0f, "noclip descend lowers the player");
    const float yAfterDescend = rt.player.position.y;
    rt.tickPlayer(0.5f, false, false, false, false, true, false, emptyWorld, false);
    check(rt.player.position.y > yAfterDescend, "noclip jump raises the player");

    // --- cinematic duration fallback (no clip KF2 available) ---
    std::vector<ClipFrameMessage> hooks;
    ClipFrameMessage h;
    h.frame = 60;
    h.text = "this->C_SetHealth( 1.0 );";
    hooks.push_back(h);
    rt.startCinematic(1, 0.0f, 30, hooks, "");
    check(rt.cine.duration > 2.5f && rt.cine.duration < 3.5f, "duration from last hook + 1s");
    check(rt.cine.active, "cinematic active");
    for (int i = 0; i < 80 && rt.cine.active; ++i) {
        rt.tickCinematic(0.05f, level, noActors);
    }
    check(!rt.cine.active, "cinematic terminates without a KF2");
}

// --- enemy combat AI (.ai network + X_Character shooting) ---
static void testEnemyCombatAi() {
    const std::string root = maxfx::DatabaseReader::locateRoot("docs");
    if (root.empty()) {
        return;
    }
    maxfx::Level level;
    try {
        level = maxfx::LdbReader::loadFromFile("docs/Part1_Level1.ldb");
    } catch (const std::exception&) {
        return;  // sample level not shipped in this checkout
    }

    // 1. X_GlobalAIObject parse: 37 exit arrays + 38 room networks.
    maxfx::AiGraph graph = maxfx::loadAiFile("docs/Part1_Level1.ai", level);
    check(graph.loaded, "ai graph loads");
    check(graph.nodeCount() > 4000, "ai node count (sample)");
    int links = 0;
    for (std::size_t i = 0; i < graph.nodeCount(); ++i) {
        links += static_cast<int>(graph.nodes[i].links.size());
    }
    check(links > 14000, "ai link count (sample)");
    check(graph.rooms.size() == 38, "ai room network count");
    // A* detours around geometry: the waypoint route must be longer than
    // the straight line but still connect the two nodes.
    {
        std::vector<maxfx::Vec3> path;
        const bool ok = graph.findPath(graph.nodes[200].world, graph.nodes[261].world, &path);
        check(ok && path.size() >= 4, "ai A* finds multi-hop path");
        float len = 0.0f;
        for (std::size_t i = 1; i < path.size(); ++i) {
            len += maxfx::length(path[i] - path[i - 1]);
        }
        const float straight = maxfx::length(graph.nodes[261].world - graph.nodes[200].world);
        check(len > straight * 1.05f, "ai A* path detours around walls");
    }

    // 2. Full runtime with the level's characters.
    maxfx::Database db = maxfx::DatabaseReader::load(root);
    {
        std::vector<std::string> skins;
        for (std::size_t i = 0; i < level.characters.size(); ++i) {
            skins.push_back(level.characters[i].characterName);
        }
        maxfx::DatabaseReader::loadModels(db, skins, std::vector<std::string>());
    }
    maxfx::GameRuntime rt;
    rt.loadCatalog(root);
    rt.resetLevel(level, 0, maxfx::Vec3(0.0f, 0.0f, 0.0f), 0.0f);
    rt.loadLevelAi("docs/Part1_Level1.ai", level);
    check(rt.aiGraph.loaded, "runtime loads level ai graph");

    maxfx::CollisionWorld world;
    world.addLevelGeometry(level);

    std::vector<maxfx::CharacterActor> actors;
    int mickey = -1;
    int finale1 = -1;
    for (std::size_t i = 0; i < level.characters.size(); ++i) {
        const maxfx::Character& ch = level.characters[i];
        const maxfx::Mat4x3 roomX = maxfx::roomWorldTransform(level, ch.properties.roomId);
        const maxfx::Mat4x3 wx = maxfx::combine(roomX, ch.properties.objectToRoom);
        const maxfx::Vec3 z = wx.zAxis();
        const maxfx::SkinDef* def = db.findSkin(ch.characterName);
        maxfx::CharacterActor actor;
        actor.spawn(wx.translation(), std::atan2(z.x, z.z), ch.properties.roomId,
                    def != 0 ? &def->character : 0, ch.characterName, ch.sharedName);
        if (def != 0) {
            rt.applyCharacterMessages(actor, def->character.onInitMessages);
        }
        rt.applyCharacterStartup(actor, ch.onStartup);
        if (ch.sharedName == "::gate::enemy") {
            mickey = static_cast<int>(actors.size());
        }
        if (ch.sharedName == "::teleport::e1") {
            finale1 = static_cast<int>(actors.size());
        }
        actors.push_back(actor);
    }
    check(mickey >= 0, "gate enemy spawned");
    check(finale1 >= 0, "finale enemy spawned");
    rt.startLevel(level, actors);  // FSM startup lists (trigger latches, ...)
    check(actors[static_cast<std::size_t>(mickey)].fireInterval > 0.0f,
          "skin OnInit arms the gate enemy (beretta)");
    check(actors[static_cast<std::size_t>(finale1)].fireInterval < 0.0f,
          "C_RemoveAllWeapons(empty) disarms the finale enemy");
    check(actors[static_cast<std::size_t>(finale1)].aiNonReactive,
          "c_setstatemachine(nonreactive) applied");

    // 3. Mickey fights: put the player where the gate enemy can see him
    // (his booth has solid walls; sight lines run through the opening).
    maxfx::CharacterActor& mic = actors[static_cast<std::size_t>(mickey)];
    const maxfx::Vec3 eye =
        mic.position + maxfx::Vec3(0.0f, mic.config->capsule.top * 0.85f, 0.0f);
    maxfx::Vec3 playerPos = mic.position;
    bool spotFound = false;
    // Prefer spots inside his sight cone (AimingSpeedCone) so the AI
    // activates immediately; fall back to any clear line.
    for (int pass = 0; pass < 2 && !spotFound; ++pass) {
        for (float off = 0.0f; off < 3.15f && !spotFound; off += 0.1f) {
            for (int side = (off < 0.01f ? 0 : -1); side <= 1 && !spotFound; side += 2) {
                const float a = mic.yaw + off * static_cast<float>(side == 0 ? 0 : side);
                for (float d = 4.0f; d <= 16.0f && !spotFound; d += 2.0f) {
                    const maxfx::Vec3 cand(eye.x + std::sin(a) * d, eye.y - 0.06f,
                                           eye.z + std::cos(a) * d);
                    const maxfx::Vec3 los = (cand + maxfx::Vec3(0.0f, 1.6f, 0.0f)) - eye;
                    const maxfx::CollisionHit hit = world.raycast(eye, los, 1.0f);
                    if ((!hit.hit || hit.t >= maxfx::length(los) * 0.95f) &&
                        (pass == 1 || std::fabs(off) < 0.25f)) {
                        playerPos = cand;
                        spotFound = true;
                    }
                }
            }
        }
    }
    check(spotFound, "clear sight line to the gate enemy exists");
    rt.player.position = playerPos;
    rt.player.controlsEnabled = true;
    rt.charActivated.assign(actors.size(), 0);

    int fired = 0;
    int shootSounds = 0;
    bool sawShootAnim = false;
    const bool hasShootClip =
        maxfx::findAnimClip(*mic.config, maxfx::weaponShootAnimIndex("beretta")) != 0;
    const float startHealth = rt.player.health;
    for (int frame = 0; frame < 300; ++frame) {  // 10 s at 30 fps
        // The player keeps firing (the gate fight): PerceivingGroupOne
        // hears gunfire through walls at the skin's GroupOne radius.
        if (frame % 15 == 0) {
            rt.notePlayerShot();
        }
        maxfx::AiUpdateContext ctx;
        ctx.graph = &rt.aiGraph;
        ctx.playerAlive = rt.player.health > 0.0f;
        ctx.playerShotRecently = rt.playerShotRecently();
        ctx.playerPosition = playerPos;
        std::vector<maxfx::AiFireEvent> fires;
        for (std::size_t i = 0; i < actors.size(); ++i) {
            actors[i].update(1.0f / 30.0f, playerPos, world, &actors, &ctx, &fires,
                             static_cast<int>(i));
        }
        for (std::size_t i = 0; i < fires.size(); ++i) {
            ++fired;
            rt.enemyFire(fires[i], level, world, actors);
        }
        for (std::size_t i = 0; i < actors.size(); ++i) {
            if (actors[i].aiActive) {
                rt.activateCharacter(static_cast<int>(i), level, actors);
            }
        }
        // The fire sound + muzzle flash are the WEAPONANIM_SHOOT messages.
        for (std::size_t i = 0; i < rt.pendingSounds.size(); ++i) {
            if (rt.pendingSounds[i].name.find("shoot_beretta") != std::string::npos) {
                ++shootSounds;
            }
        }
        rt.pendingSounds.clear();
        if (hasShootClip &&
            mic.animIndex == maxfx::weaponShootAnimIndex("beretta")) {
            sawShootAnim = true;  // CHARANIM_SHOOTBERETTA plays while firing
        }
    }
    if (hasShootClip) {
        check(sawShootAnim, "enemy plays the weapon shoot clip");
    }
    check(fired > 5, "gate enemy fires at the player");
    check(shootSounds == fired, "every shot plays the weapon sound");
    check(rt.player.health < startHealth, "enemy fire damages the player");
    check(mic.aiActive, "gate enemy AI activated");

    check(maxfx::weaponShootAnimIndex("beretta") == 313 &&
              maxfx::weaponShootAnimIndex("deserteagle") == 315 &&
              maxfx::weaponShootAnimIndex("ingram") == 318 &&
              maxfx::weaponShootAnimIndex("sawedshotgun") == 316 &&
              maxfx::weaponShootAnimIndex("nonexistent") == 313,
          "CHARANIM_SHOOT<weapon> mapping");

    // 4. Non-reactive enemies never fire even at point blank.
    {
        maxfx::CharacterActor& e1 = actors[static_cast<std::size_t>(finale1)];
        const maxfx::Vec3 p2 = e1.position + maxfx::Vec3(2.0f, 0.0f, 0.0f);
        int finaleFired = 0;
        for (int frame = 0; frame < 120; ++frame) {
            maxfx::AiUpdateContext ctx;
            ctx.graph = &rt.aiGraph;
            ctx.playerPosition = p2;
            std::vector<maxfx::AiFireEvent> fires;
            for (std::size_t i = 0; i < actors.size(); ++i) {
                actors[i].update(1.0f / 30.0f, p2, world, &actors, &ctx, &fires,
                                 static_cast<int>(i));
            }
            finaleFired += static_cast<int>(fires.size());
        }
        check(finaleFired == 0, "nonreactive enemy does not fire");
    }

    // 5. Death messages: e1's onDeath sends ::p5::death_counter fsm_send(add1),
    //    which makes the survivor e2 C_GoToAndShoot + standandshootstatic.
    {
        maxfx::CharacterActor& e1 = actors[static_cast<std::size_t>(finale1)];
        maxfx::CharacterActor& e2 = actors[static_cast<std::size_t>(finale1 + 1)];
        check(e2.entityName == "::teleport::e2", "finale pair adjacency");
        check(e1.activity != maxfx::kCharDead, "finale enemy starts alive");
        rt.applyCharacterDamage(finale1, 10000.0f, level, actors);
        check(e1.activity == maxfx::kCharDead, "damage kills the finale enemy");
        check(e2.aiActive && !e2.aiNonReactive,
              "onDeath -> death_counter add1 -> survivor standandshootstatic");
        check(e2.hasScriptGoal, "survivor C_GoToAndShoot waypoint goal");
    }

    // 6. Character-entity messages: combat state machine switch + weapon.
    {
        maxfx::CharacterActor& e1 = actors[static_cast<std::size_t>(finale1)];
        const std::vector<maxfx::GameMessage> msgs = maxfx::parseGameMessages(
            "::teleport::e1->C_SetStateMachine(mobstercombat);"
            "::teleport::e1->C_PickupWeapon(deserteagle);");
        rt.currentActivatorActor = -1;
        for (std::size_t i = 0; i < msgs.size(); ++i) {
            rt.dispatch(msgs[i], level, actors, -1);
        }
        check(e1.aiActive && !e1.aiNonReactive, "C_SetStateMachine(mobstercombat) activates");
        check(e1.weaponName == "deserteagle", "C_PickupWeapon routed to the character");
        check(e1.fireInterval > 0.0f, "deserteagle fire interval set");
    }
}

static void testWavPlaceholder() {
    if (!maxfx::isFile("docs/database/sounds/placeholder.wav")) {
        std::fprintf(stderr, "skip wav (placeholder.wav missing)\\n");
        return;
    }
    maxfx::WavFile wav;
    std::string err;
    check(maxfx::loadWavFile("docs/database/sounds/placeholder.wav", wav, &err), "load placeholder.wav");
    check(wav.channels == 2 && wav.bitsPerSample == 8, "placeholder wav format");
    check(wav.sampleRate == 44100, "placeholder wav rate");
}

int main() {
    testLevelsSnippet();
    testErrors();
    testInclude("/tmp");
    testUnbracedBlocks();
    testNestedQuotesAnd3DSound();
    testPcxAlpha();
    testKf2Beretta();
    testDatabase();
    testRealSkinning();
    testCollisionAndMath();
    testKf2EngineSampling();
    testKf2AnimationAndSkinAi();
    testGameMessagesAndCatalog();
    testSoundOmniAndFsmCues();
    testTriggerSemantics();
    testCinematics("/tmp");
    testDynamicObjectsAndNoclip();
    testWavPlaceholder();
    testEnemyCombatAi();

    // Official sample shipped in docs/.
    const std::string sample = maxfx::LevelsReader::locateLevelsTxt("docs");
    if (sample.empty()) {
        std::fprintf(stderr, "FAIL: docs/levels.txt not found\n");
        ++gFails;
    } else {
        try {
            const LevelDatabase db = LevelsReader::loadFile(sample);
            check(db.levels.size() >= 1, "docs/levels.txt has at least one level");
            if (!db.levels.empty()) {
                check(!db.levels[0].id.empty(), "sample id");
                check(!db.levels[0].filename.empty(), "sample filename");
            }
        } catch (const ScriptError& e) {
            std::fprintf(stderr, "FAIL: docs/levels.txt: %s\n", e.what());
            ++gFails;
        }
    }

    std::printf("levels-test: %d passed, %d failed\n", gPass, gFails);
    return gFails == 0 ? 0 : 1;
}
