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
    float maxShootHz;
    float damage;
    float spread;
    float castLength;
    float sphereRadius;
    std::string projectileName;
    // [Attributes] WeaponID / SlotIndex / InventoryID (resolved #defines).
    // The HUD [ActiveWeapon] blocks and weaponpriority.txt key by WeaponID.
    int weaponId;
    int slotIndex;
    int inventoryId;
    // From the WEAPONANIM_SHOOT* message lists: the fire sound and the
    // muzzle-flash particle system (X_Character shooting plays these with
    // every shot).
    std::string shootSoundCategory;
    std::string shootSoundName;
    std::string muzzleEffect;

    WeaponDef()
        : clipSize(18),
          pocketSize(162),
          shootHz(4.0f),
          maxShootHz(0.0f),
          damage(5.0f),
          spread(100.0f),
          castLength(100.0f),
          sphereRadius(0.01f),
          weaponId(-1),
          slotIndex(-1),
          inventoryId(-1) {}
};

struct ProjectileDef {
    std::string name;
    float damage;
    float speed;
    bool damagesCharacter;
    // [Message] String lists from the projectile script. X_Projectile runs
    // them when it hits something: D_CreateDecal, PS_StartEffect,
    // A_Play3DSound, LI_CreateLevelItem ...
    std::vector<std::string> messages;

    ProjectileDef() : damage(5.0f), speed(80.0f), damagesCharacter(true) {}
};

struct GraphicNovelPageDef {
    std::string id;
    std::string exportData;
    std::string resolvedKf2;
    std::string initSound;
    bool newChapter;
    bool cine;

    GraphicNovelPageDef() : newChapter(false), cine(false) {}
};

struct GraphicNovelChapter {
    std::string id;
    std::string title;
    std::vector<int> pageIndices;
};

struct GameCatalog {
    std::map<std::string, WeaponDef> weapons;
    std::map<std::string, ProjectileDef> projectiles;
    std::vector<GraphicNovelPageDef> pages;
    std::vector<GraphicNovelChapter> chapters;
    // data/weaponpriority.txt [CycleWeapons] — the player's weapon cycle
    // order (weapon names, resolved through WeaponID).
    std::vector<std::string> cycleOrder;

    const WeaponDef* findWeapon(const std::string& name) const;
    const ProjectileDef* findProjectile(const std::string& name) const;
    const GraphicNovelPageDef* findPage(const std::string& id) const;
};

std::string graphicNovelChapterKey(const std::string& pageId);
std::string graphicNovelChapterTitle(const std::string& chapterId);
std::string graphicNovelPageLabel(const std::string& pageId);
void buildGraphicNovelChapters(const std::vector<GraphicNovelPageDef>& pages,
                               std::vector<GraphicNovelChapter>* out);

GameCatalog loadGameCatalog(const std::string& dbRoot);

}  // namespace maxfx

#endif  // MAXFX_GAME_CATALOG_H
