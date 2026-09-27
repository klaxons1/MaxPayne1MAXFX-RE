// Weapon HUD definitions from data/hud/hud.txt (PC layout).
//
// The Android port rewrote its HUD for touch controls, so this follows the PC
// script structure (see MaxPayne_HUDMode in the decompile for the block names:
// Properties / Print / PrintTip / Health / SlowMotion / Weapons / Painkiller /
// ActiveWeapon / Inventory / Objectives / TimedMode):
//
//   [ActiveWeapon]
//   {
//     [WeaponSpecific]
//     id = WEAPONID_DESERTEAGLE;
//     LowAmmunition = LOWAMMO_ALWAYS_VISIBLE;
//     UnlimitedAmmoText = "-";
//     [Sprite]
//     ReferencePoint = DOWNRIGHT;
//     Position = WEAPON_POSITION;
//     Width = WEAPON_WIDTH;
//     Height = WEAPON_HEIGHT;
//     Filename = "bitmaps\weapons\deserteagle.jpg";
//     AlphaFilename = "bitmaps\weapons\deserteagle_alpha.pcx";
//     DefaultVertexAlpha = WEAPON_ALPHA;
//   }
//
// Sprite images are relative to the hud folder (data/hud); the ammo counters
// are [Weapons] { [AmmoInPocketText] / [AmmoInClipsText] }.
#ifndef MAXFX_GAME_HUD_H
#define MAXFX_GAME_HUD_H

#include <string>
#include <vector>

namespace maxfx {

struct HudSprite {
    std::string filename;      // relative to the hud folder ("" = none)
    std::string alphaFilename;
    float width;
    float height;
    float position[2];  // hud space, authored for 640x480
    std::string referencePoint;  // CENTER / DOWNRIGHT / UPLEFT ... ("" = upleft)
    float alpha;        // DefaultVertexAlpha

    HudSprite() : width(0.0f), height(0.0f), alpha(1.0f) {
        position[0] = 0.0f;
        position[1] = 0.0f;
    }
    bool valid() const { return !filename.empty(); }
};

struct HudWeaponSprite {
    int weaponId;  // [WeaponSpecific] id (WEAPONID_* define, resolved)
    HudSprite sprite;
    std::string unlimitedAmmoText;
    float lowAmmunition;  // LOWAMMO_* define (0 = never shown early)

    HudWeaponSprite() : weaponId(-1), lowAmmunition(0.0f) {}
};

struct HudText {
    float position[2];
    bool valid;

    HudText() : valid(false) {
        position[0] = 0.0f;
        position[1] = 0.0f;
    }
};

struct HudDef {
    bool valid;  // hud.txt found and parsed
    // [Health]
    HudSprite healthSprite;
    HudSprite healthBackground;
    HudText healthText;
    // [SlowMotion] (bullet time hourglass)
    HudSprite slowmoSprite;
    HudSprite slowmoBackground;
    // [Weapons]
    HudText ammoInPocketText;
    HudText ammoInClipsText;
    // [Painkiller]
    HudSprite painkillerSprite;
    HudText painkillerAmountText;
    // [ActiveWeapon] blocks (keyed by WeaponID).
    std::vector<HudWeaponSprite> weapons;

    HudDef() : valid(false) {}
};

// `dataRoot` is the folder containing database/ and hud/ (for the shipped
// layout: the Max Payne data folder). Missing files return an invalid def and
// the caller falls back to the built-in HUD.
HudDef loadHudDef(const std::string& dataRoot);

const HudWeaponSprite* findHudWeapon(const HudDef& hud, int weaponId);

// "DOWNRIGHT" etc. -> which sprite corner sits at `position`. 0 = upleft,
// 1 = center, 2 = downright (unknown names default to upleft like the
// original's ReferencePoint::UPLEFT).
int hudReferencePoint(const std::string& name);

}  // namespace maxfx

#endif  // MAXFX_GAME_HUD_H
