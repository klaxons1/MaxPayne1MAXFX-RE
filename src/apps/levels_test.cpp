// Self-test for the R_Script loader and levels.txt parser. No extra deps.
// Built as `levels-test` from the Makefile.

#include "maxfx/core/Fs.h"
#include "maxfx/db/Database.h"
#include "maxfx/image/Image.h"
#include "maxfx/kf2/Kf2.h"
#include "maxfx/levels/Levels.h"
#include "maxfx/script/Script.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

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
    const maxfx::Database db = maxfx::DatabaseReader::load(root);
    check(db.parsedScripts > 10, "parsed many scripts");
    check(!db.materials.empty(), "materials.txt categories");
    const maxfx::MaterialCategory* graffiti = db.findMaterial("Graffiti");
    check(graffiti != 0 && graffiti->detailOffset == 1, "Graffiti DetailOffset");
    const maxfx::MaterialCategory* ai = db.findMaterial("AI_Node_Collision_NoDraw");
    check(ai != 0 && ai->drawPolygons == false, "AI node is service geometry");
    check(!db.items.empty() || !db.skins.empty(), "skins or level_items parsed");
}

int main() {
    testLevelsSnippet();
    testErrors();
    testInclude("/tmp");
    testUnbracedBlocks();
    testPcxAlpha();
    testKf2Beretta();
    testDatabase();

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
