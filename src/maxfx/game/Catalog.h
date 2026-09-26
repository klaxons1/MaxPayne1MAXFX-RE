// Parsed gameplay configs from the shared database (PC scripts).
//
// weapons/*.txt        [Attributes] + [CrosshairAttributes]
// projectiles/*.txt    [Attributes] Damage / Speed
// graphicnovelpages.txt  [id] { [geometry] ExportData; [general] NewChapter; }
#ifndef MAXFX_GAME_CATALOG_H
#define MAXFX_GAME_CATALOG_H

#include <map>
#include <string>
#include <vector>

namespace maxfx {

struct WeaponDef {
    std::string name;
    std::string sourcePath;
    int clipSize;
    int pocketSize;
    float shootHz;
    float damage;
    float spread;
    float castLength;
    float sphereRadius;
    std::string projectileName;

    WeaponDef()
        : clipSize(18),
          pocketSize(162),
          shootHz(4.0f),
          damage(5.0f),
          spread(100.0f),
          castLength(100.0f),
          sphereRadius(0.01f) {}
};

struct ProjectileDef {
    std::string name;
    float damage;
    float speed;
    bool damagesCharacter;

    ProjectileDef() : damage(5.0f), speed(80.0f), damagesCharacter(true) {}
};

struct GraphicNovelPageDef {
    std::string id;
    std::string exportData;
    std::string resolvedKf2;
    std::string initSound;
    bool newChapter;

    GraphicNovelPageDef() : newChapter(false) {}
};

struct GameCatalog {
    std::map<std::string, WeaponDef> weapons;
    std::map<std::string, ProjectileDef> projectiles;
    std::vector<GraphicNovelPageDef> pages;

    const WeaponDef* findWeapon(const std::string& name) const;
    const ProjectileDef* findProjectile(const std::string& name) const;
};

GameCatalog loadGameCatalog(const std::string& dbRoot);

}  // namespace maxfx

#endif  // MAXFX_GAME_CATALOG_H
