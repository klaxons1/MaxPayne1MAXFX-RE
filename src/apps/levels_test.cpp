// Self-test for the R_Script loader and levels.txt parser. No extra deps.
// Built as `levels-test` from the Makefile.

#include "maxfx/char/Character.h"
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
    testCollisionAndMath();
    testKf2AnimationAndSkinAi();
    testGameMessagesAndCatalog();
    testSoundOmniAndFsmCues();
    testWavPlaceholder();

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
