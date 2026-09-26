// Shared MAX-FX text database (`data/database`).
//
// Every `*.txt` under the database root is an R_Script file. This module
// walks them once and exposes the subsets the rest of the engine needs
// (material categories, skins, level items, decals) without the viewer
// having to know the on-disk layout.
#ifndef MAXFX_DB_DATABASE_H
#define MAXFX_DB_DATABASE_H

#include "maxfx/char/Character.h"
#include "maxfx/kf2/Kf2.h"

#include <map>
#include <string>
#include <vector>

namespace maxfx {

struct MaterialCategory {
    std::string name;
    bool drawPolygons;
    int detailOffset;   // 0 = base, 1..4 = ID_DETAIL*
    int sortPriority;   // 0 = base, 1..4 = ID_SORT*
    bool writesZBuffer;
    int alphaReference;
    bool blendedAlphaTest;
    bool skyBox;

    MaterialCategory()
        : drawPolygons(true),
          detailOffset(0),
          sortPriority(0),
          writesZBuffer(true),
          alphaReference(15),
          blendedAlphaTest(true),
          skyBox(false) {}
};

struct GeometryRef {
    std::string exportData;  // .kf2 / .kfs, relative to the script file
    std::string skinData;    // .skd
    std::string resolvedExport;
    std::string resolvedSkin;
};

struct SkinDef {
    std::string name;  // file stem, matches LDB Character::characterName
    std::string sourcePath;
    std::string skeleton;
    std::vector<GeometryRef> lods;
    CharacterConfig character;
};

struct ItemDef {
    std::string name;  // file stem, matches LDB LevelItem::itemName
    std::string sourcePath;
    std::vector<GeometryRef> lods;
};

struct DecalMaterialDef {
    std::string name;
    std::string filename;
    std::string alphaFilename;
    float minRadius;
    float maxRadius;

    DecalMaterialDef() : minRadius(0.0f), maxRadius(0.0f) {}
};

// One [name] entry from sounds/*.txt or music/music.txt.
struct SoundDef {
    std::string name;      // block tag, lower-cased
    std::string category;  // file stem (ambient, weapons, music, ...)
    std::string filename;  // script Filename, native separators
    std::string resolvedPath;
    float volume;
    int pitch;
    bool looping;
    bool is3d;
    bool streamed;
    float hotspot;
    float falloff;

    SoundDef()
        : volume(1.0f),
          pitch(22050),
          looping(false),
          is3d(false),
          streamed(false),
          hotspot(1.0f),
          falloff(10.0f) {}
};

struct ScriptCatalogEntry {
    std::string path;
    std::string relative;
    int topBlocks;
    std::string error;  // empty = parsed
};

struct Database {
    std::string root;

    std::vector<MaterialCategory> materials;
    std::vector<SkinDef> skins;
    std::vector<ItemDef> items;
    std::vector<DecalMaterialDef> decals;
    std::vector<SoundDef> sounds;
    std::vector<SoundDef> music;
    std::vector<ScriptCatalogEntry> scripts;

    std::string worldSphereName;
    std::string worldSpherePath;

    int parsedScripts;
    int failedScripts;
    int loadedModels;

    Database() : parsedScripts(0), failedScripts(0), loadedModels(0) {}

    const MaterialCategory* findMaterial(const std::string& category) const;
    const SkinDef* findSkin(const std::string& name) const;
    const ItemDef* findItem(const std::string& name) const;
    const SoundDef* findSound(const std::string& name) const;
    const SoundDef* findSound(const std::string& category, const std::string& name) const;
    const SoundDef* findMusic(const std::string& name) const;

    // Cached KF2/KFS/SKD. Missing files return null.
    const Kf2File* model(const std::string& resolvedPath) const;
    Kf2File* model(const std::string& resolvedPath);
    // Parse and cache on first use (CHARANIM clips, extra LODs).
    Kf2File* loadModel(const std::string& resolvedPath);

    std::map<std::string, Kf2File> models;  // key = lower-cased native path
};

class DatabaseReader {
public:
    // Walk parents of `hint` (an .ldb, a folder, or empty) looking for
    // materials.txt. Also checks data/database next to the executable and
    // the in-repo docs/database tree.
    static std::string locateRoot(const std::string& hint = std::string());

    // Parse every .txt under `root`. Model files are loaded lazily through
    // Database::model / loadModelsForLevel.
    static Database load(const std::string& root);

    // Resolve ExportData paths and parse the KF2/KFS referenced by the skins
    // and items that actually appear in `skinNames` / `itemNames`.
    static void loadModels(Database& db, const std::vector<std::string>& skinNames,
                           const std::vector<std::string>& itemNames);

    // `WorldSphere = "intro"` in levels.txt → worldspheres/bg_intro.kf2.
    static void loadWorldSphere(Database& db, const std::string& name);
};

}  // namespace maxfx

#endif  // MAXFX_DB_DATABASE_H
