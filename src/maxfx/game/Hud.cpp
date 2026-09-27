#include "maxfx/game/Hud.h"

#include "maxfx/core/Fs.h"
#include "maxfx/script/Script.h"

#include <cstdlib>

namespace maxfx {
namespace {

std::string assignmentOf(const ScriptBlock& block, const char* lvalue) {
    const std::string key = lowerCopy(lvalue);
    for (std::size_t i = 0; i < block.assignments.size(); ++i) {
        if (block.assignments[i].lvalue == key) {
            return block.assignments[i].rvalue;
        }
    }
    return std::string();
}

const ScriptBlock* childNamed(const ScriptBlock& block, const char* name) {
    const std::string key = lowerCopy(name);
    for (std::size_t i = 0; i < block.children.size(); ++i) {
        if (block.children[i].name == key) {
            return &block.children[i];
        }
    }
    return 0;
}

float parseFloatCatch(const std::string& v, float fallback) {
    if (v.empty()) {
        return fallback;
    }
    try {
        return parseScriptFloat(v, "", 0, "");
    } catch (...) {
        return fallback;
    }
}

int parseIntCatch(const std::string& v, int fallback) {
    if (v.empty()) {
        return fallback;
    }
    try {
        return parseScriptInt(v, "", 0, "");
    } catch (...) {
        return fallback;
    }
}

// Position = ( x, y, 0 ); — parse the first two components.
void parsePosition(const ScriptBlock& block, float out[2]) {
    const std::string v = assignmentOf(block, "position");
    if (v.empty()) {
        return;
    }
    try {
        float xyz[3] = {out[0], out[1], 0.0f};
        parseScriptFloat3(v, xyz, "position", 0, "");
        out[0] = xyz[0];
        out[1] = xyz[1];
    } catch (...) {
        // A single scalar or an unresolved define.
        out[0] = parseFloatCatch(v, out[0]);
    }
}

void fillSprite(const ScriptBlock& block, HudSprite* sprite) {
    sprite->filename = parseScriptString(assignmentOf(block, "filename"));
    const std::string alpha = assignmentOf(block, "alphafilename");
    if (!alpha.empty()) {
        sprite->alphaFilename = parseScriptString(alpha);
    }
    sprite->width = parseFloatCatch(assignmentOf(block, "width"), sprite->width);
    sprite->height = parseFloatCatch(assignmentOf(block, "height"), sprite->height);
    sprite->alpha = parseFloatCatch(assignmentOf(block, "defaultvertexalpha"), sprite->alpha);
    sprite->referencePoint = lowerCopy(parseScriptString(assignmentOf(block, "referencepoint")));
    parsePosition(block, sprite->position);
}

void fillText(const ScriptBlock& block, HudText* text) {
    parsePosition(block, text->position);
    text->valid = true;
}

}  // namespace

int hudReferencePoint(const std::string& name) {
    if (name == "center") {
        return 1;
    }
    if (name == "downright" || name == "down right" || name == "bottomright") {
        return 2;
    }
    if (name == "downleft" || name == "down left" || name == "bottomleft") {
        return 3;
    }
    return 0;  // upleft
}

HudDef loadHudDef(const std::string& dataRoot) {
    HudDef hud;
    if (dataRoot.empty()) {
        return hud;
    }
    const std::string hudDir = joinPath(dataRoot, "hud");
    const std::string path = existingPathIgnoreCase(joinPath(hudDir, "hud.txt"));
    if (path.empty() || !isFile(path)) {
        return hud;
    }
    // Retry without includes when the #include paths do not resolve (a broken
    // include must not kill the HUD; the define constants just fall back).
    std::vector<Script> attempts;
    try {
        attempts.push_back(Script::loadFile(path, true));
    } catch (...) {
        try {
            attempts.push_back(Script::loadFile(path, false));
        } catch (...) {
            return hud;
        }
    }
    const ScriptBlock& root = attempts[0].root();

    // [Health] { [Sprite] [BackgroundSprite] [HealthSpecific]... }
    if (const ScriptBlock* health = childNamed(root, "health")) {
        if (const ScriptBlock* sp = childNamed(*health, "sprite")) {
            fillSprite(*sp, &hud.healthSprite);
        }
        if (const ScriptBlock* sp = childNamed(*health, "backgroundsprite")) {
            fillSprite(*sp, &hud.healthBackground);
        }
    }
    // [SlowMotion]
    if (const ScriptBlock* sm = childNamed(root, "slowmotion")) {
        if (const ScriptBlock* sp = childNamed(*sm, "sprite")) {
            fillSprite(*sp, &hud.slowmoSprite);
        }
        if (const ScriptBlock* sp = childNamed(*sm, "backgroundsprite")) {
            fillSprite(*sp, &hud.slowmoBackground);
        }
    }
    // [Weapons] { [AmmoInPocketText] [AmmoInClipsText] }
    if (const ScriptBlock* weapons = childNamed(root, "weapons")) {
        if (const ScriptBlock* t = childNamed(*weapons, "ammoinpockettext")) {
            fillText(*t, &hud.ammoInPocketText);
        }
        if (const ScriptBlock* t = childNamed(*weapons, "ammoinclipstext")) {
            fillText(*t, &hud.ammoInClipsText);
        }
    }
    // [Painkiller]
    if (const ScriptBlock* pk = childNamed(root, "painkiller")) {
        if (const ScriptBlock* sp = childNamed(*pk, "sprite")) {
            fillSprite(*sp, &hud.painkillerSprite);
        }
        if (const ScriptBlock* t = childNamed(*pk, "amounttext")) {
            fillText(*t, &hud.painkillerAmountText);
        }
    }
    // [ActiveWeapon] blocks (multiple, same tag name).
    for (std::size_t b = 0; b < root.children.size(); ++b) {
        const ScriptBlock& blk = root.children[b];
        if (blk.name != "activeweapon") {
            continue;
        }
        HudWeaponSprite w;
        if (const ScriptBlock* spec = childNamed(blk, "weaponspecific")) {
            w.weaponId = parseIntCatch(assignmentOf(*spec, "id"), -1);
            w.unlimitedAmmoText = parseScriptString(assignmentOf(*spec, "unlimitedammotext"));
            w.lowAmmunition = parseFloatCatch(assignmentOf(*spec, "lowammunition"), 0.0f);
        }
        if (const ScriptBlock* sp = childNamed(blk, "sprite")) {
            fillSprite(*sp, &w.sprite);
        }
        if (w.weaponId >= 0) {
            hud.weapons.push_back(w);
        }
    }
    hud.valid = hud.healthSprite.valid() || hud.slowmoSprite.valid() || !hud.weapons.empty() ||
                hud.ammoInClipsText.valid;
    return hud;
}

const HudWeaponSprite* findHudWeapon(const HudDef& hud, int weaponId) {
    if (weaponId < 0) {
        return 0;
    }
    for (std::size_t i = 0; i < hud.weapons.size(); ++i) {
        if (hud.weapons[i].weaponId == weaponId) {
            return &hud.weapons[i];
        }
    }
    return 0;
}

}  // namespace maxfx
