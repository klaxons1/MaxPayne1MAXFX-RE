#include "maxfx/game/Catalog.h"

#include "maxfx/core/Fs.h"
#include "maxfx/game/Message.h"
#include "maxfx/script/Script.h"

#include <cmath>
#include <cstdio>

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

const ScriptBlock* childNamed(const ScriptBlock& block, const char* name) {
    const std::string key = lowerCopy(name);
    for (std::size_t i = 0; i < block.children.size(); ++i) {
        if (block.children[i].name == key) {
            return &block.children[i];
        }
    }
    return 0;
}

void walkNamed(const ScriptBlock& block, const char* name, std::vector<const ScriptBlock*>* out) {
    const std::string key = lowerCopy(name);
    for (std::size_t i = 0; i < block.children.size(); ++i) {
        if (block.children[i].name == key) {
            out->push_back(&block.children[i]);
        }
        walkNamed(block.children[i], name, out);
    }
}

// [AnimationSet] walk: every [Animation] whose Index is a WEAPONANIM_SHOOT
// variant (1 shoot, 2 no-case, 3 no-effects per weaponanimid.h) contributes
// the [Message] list of its sibling [Properties] block.
void collectShootMessages(const ScriptBlock& parent, std::vector<const ScriptBlock*>* out) {
    for (std::size_t b = 0; b < parent.children.size(); ++b) {
        const ScriptBlock& blk = parent.children[b];
        if (blk.name == "animation") {
            const int idx = parseIntCatch(assignmentOf(blk, "index"), -1);
            const bool isShoot = idx == 1 || idx == 2 || idx == 3;
            if (isShoot) {
                for (std::size_t s = b + 1; s < parent.children.size(); ++s) {
                    const ScriptBlock& sib = parent.children[s];
                    if (sib.name == "animation") {
                        break;
                    }
                    if (sib.name == "properties") {
                        walkNamed(sib, "message", out);
                    }
                }
            }
        }
        collectShootMessages(blk, out);
    }
}

std::string nativeFromScript(const std::string& p) {
    std::string s = p;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\') {
            s[i] = '/';
        }
    }
    return nativeSeparators(s);
}

bool truthy(const std::string& v) {
    const std::string s = lowerCopy(v);
    return s == "1" || s == "true" || s == "yes";
}

void loadWeapons(const std::string& dir, GameCatalog* cat) {
    if (!isDirectory(dir)) {
        return;
    }
    const std::vector<std::string> files = listFilesWithExtension(dir, ".txt");
    for (std::size_t i = 0; i < files.size(); ++i) {
        try {
            const Script sc = Script::loadFile(files[i], false);
            WeaponDef w;
            w.name = lowerCopy(fileStem(files[i]));
            w.sourcePath = files[i];
            const ScriptBlock* attrs = childNamed(sc.root(), "attributes");
            if (attrs) {
                w.clipSize = parseIntCatch(assignmentOf(*attrs, "clipsize"), w.clipSize);
                w.pocketSize = parseIntCatch(assignmentOf(*attrs, "pocketsize"), w.pocketSize);
                w.shootHz = parseFloatCatch(assignmentOf(*attrs, "defaultshootingfrequency"), w.shootHz);
                w.maxShootHz = parseFloatCatch(assignmentOf(*attrs, "maximumshootingfrequency"), w.maxShootHz);
                w.weaponId = parseIntCatch(assignmentOf(*attrs, "weaponid"), w.weaponId);
                w.slotIndex = parseIntCatch(assignmentOf(*attrs, "slotindex"), w.slotIndex);
                w.inventoryId = parseIntCatch(assignmentOf(*attrs, "inventoryid"), w.inventoryId);
                // shooting.h style: "<NAME>_BULLETDAMAGE" etc. live in the
                // includes; the script resolver already folded them.
                w.damage = parseFloatCatch(assignmentOf(*attrs, "bulletdamage"), w.damage);
                w.spread = parseFloatCatch(assignmentOf(*attrs, "bulletspread"), w.spread);
            }
            const ScriptBlock* cross = childNamed(sc.root(), "crosshairattributes");
            if (cross) {
                w.castLength = parseFloatCatch(assignmentOf(*cross, "castlength"), w.castLength);
                w.sphereRadius = parseFloatCatch(assignmentOf(*cross, "sphereradius"), w.sphereRadius);
            }
            // WEAPONANIM_* blocks carry the per-shot messages. In the
            // weapon scripts they are [Animation] Index = WEAPONANIM_SHOOT
            // (defines expand to numbers, weaponanimid.h) entries nested in
            // [AnimationSet], each with a SIBLING [Properties] block holding
            // the frame-timed [Message] list (projectile, fire sound, muzzle
            // flash) — the shoot variants fire with every trigger pull.
            std::vector<const ScriptBlock*> animMsgs;
            collectShootMessages(sc.root(), &animMsgs);
            for (std::size_t m = 0; m < animMsgs.size(); ++m) {
                const std::string str = assignmentOf(*animMsgs[m], "string");
                const std::vector<GameMessage> parsed = parseGameMessages(str);
                for (std::size_t k = 0; k < parsed.size(); ++k) {
                    if (methodIs(parsed[k], "p_createprojectile") && !parsed[k].args.empty()) {
                        const std::string pn = lowerCopy(parsed[k].args[0]);
                        if (pn.find("bullet_") == 0) {
                            w.projectileName = pn;
                        }
                    } else if (methodIs(parsed[k], "a_play3dsound") && parsed[k].args.size() >= 2 &&
                               w.shootSoundName.empty()) {
                        w.shootSoundCategory = lowerCopy(parsed[k].args[0]);
                        w.shootSoundName = lowerCopy(parsed[k].args[1]);
                    } else if (methodIs(parsed[k], "ps_starteffect") && !parsed[k].args.empty() &&
                               w.muzzleEffect.empty()) {
                        w.muzzleEffect = lowerCopy(parsed[k].args[0]);
                    }
                }
            }
            cat->weapons[w.name] = w;
        } catch (...) {
        }
    }
}

void loadProjectiles(const std::string& dir, GameCatalog* cat) {
    if (!isDirectory(dir)) {
        return;
    }
    const std::vector<std::string> files = listFilesWithExtension(dir, ".txt");
    for (std::size_t i = 0; i < files.size(); ++i) {
        try {
            const Script sc = Script::loadFile(files[i], false);
            ProjectileDef p;
            p.name = lowerCopy(fileStem(files[i]));
            const ScriptBlock* attrs = childNamed(sc.root(), "attributes");
            if (attrs) {
                p.damage = parseFloatCatch(assignmentOf(*attrs, "damage"), p.damage);
                p.speed = parseFloatCatch(assignmentOf(*attrs, "speed"), p.speed);
                const std::string dc = assignmentOf(*attrs, "damagescharacter");
                if (!dc.empty()) {
                    p.damagesCharacter = truthy(dc);
                }
            }
            std::vector<const ScriptBlock*> msgs;
            walkNamed(sc.root(), "message", &msgs);
            for (std::size_t m = 0; m < msgs.size(); ++m) {
                const std::string str = assignmentOf(*msgs[m], "string");
                if (!str.empty()) {
                    p.messages.push_back(str);
                }
            }
            cat->projectiles[p.name] = p;
        } catch (...) {
        }
    }
}

void loadPages(const std::string& dbRoot, GameCatalog* cat) {
    const std::string path = joinPath(joinPath(dbRoot, "graphicnovelpages"), "graphicnovelpages.txt");
    const std::string hit = existingPathIgnoreCase(path);
    if (hit.empty()) {
        return;
    }
    try {
        const Script sc = Script::loadFile(hit, false);
        const std::string pageDir = parentDir(hit);
        for (std::size_t i = 0; i < sc.root().children.size(); ++i) {
            const ScriptBlock& b = sc.root().children[i];
            GraphicNovelPageDef page;
            page.id = b.name;
            const ScriptBlock* geom = childNamed(b, "geometry");
            if (geom) {
                page.exportData = parseScriptString(assignmentOf(*geom, "exportdata"));
            }
            const ScriptBlock* gen = childNamed(b, "general");
            if (gen) {
                page.newChapter = truthy(assignmentOf(*gen, "newchapter"));
            }
            for (std::size_t c = 0; c < b.children.size(); ++c) {
                if (b.children[c].name == "oninitmessage" && page.initSound.empty()) {
                    page.initSound = assignmentOf(b.children[c], "string");
                }
            }
            const std::string exp = lowerCopy(page.exportData);
            const std::string pid = lowerCopy(page.id);
            page.cine = exp.find("cine") != std::string::npos || pid.find("cine") != std::string::npos;
            if (!page.exportData.empty()) {
                const std::string rel = nativeFromScript(page.exportData);
                std::vector<std::string> cands;
                cands.push_back(joinPath(pageDir, rel));
                cands.push_back(joinPath(joinPath(pageDir, fileStem(rel)), fileName(rel)));
                for (std::size_t c = 0; c < cands.size(); ++c) {
                    const std::string found = existingPathIgnoreCase(cands[c]);
                    if (!found.empty()) {
                        page.resolvedKf2 = found;
                        break;
                    }
                }
            }
            cat->pages.push_back(page);
        }
        buildGraphicNovelChapters(cat->pages, &cat->chapters);
    } catch (...) {
    }
}

}  // namespace

const WeaponDef* GameCatalog::findWeapon(const std::string& name) const {
    std::map<std::string, WeaponDef>::const_iterator it = weapons.find(lowerCopy(name));
    if (it == weapons.end()) {
        return 0;
    }
    return &it->second;
}

const ProjectileDef* GameCatalog::findProjectile(const std::string& name) const {
    std::map<std::string, ProjectileDef>::const_iterator it = projectiles.find(lowerCopy(name));
    if (it == projectiles.end()) {
        return 0;
    }
    return &it->second;
}

const GraphicNovelPageDef* GameCatalog::findPage(const std::string& name) const {
    const std::string key = lowerCopy(name);
    for (std::size_t i = 0; i < pages.size(); ++i) {
        if (pages[i].id == key) {
            return &pages[i];
        }
    }
    return 0;
}

std::string graphicNovelChapterKey(const std::string& pageId) {
    std::string s = lowerCopy(pageId);
    if (s.size() > 3 && s.compare(s.size() - 3, 3, "_ok") == 0) {
        s = s.substr(0, s.size() - 3);
    }
    const std::size_t us = s.rfind('_');
    if (us != std::string::npos && us > 0) {
        return s.substr(0, us);
    }
    return s;
}

std::string graphicNovelChapterTitle(const std::string& chapterId) {
    const std::string key = lowerCopy(chapterId);
    std::string part = "Graphic Novel";
    if (key.size() >= 2 && key[0] == 'p') {
        if (key[1] == '1') {
            part = "Part I";
        } else if (key[1] == '2') {
            part = "Part II";
        } else if (key[1] == '3') {
            part = "Part III";
        }
    }
    std::string rest;
    for (std::size_t i = 0; i < key.size(); ++i) {
        const char c = key[i];
        if (c >= 'a' && c <= 'z') {
            rest.push_back(static_cast<char>(c - 'a' + 'A'));
        } else {
            rest.push_back(c);
        }
    }
    if (rest.empty()) {
        return part;
    }
    return part + "  " + rest;
}

std::string graphicNovelPageLabel(const std::string& pageId) {
    std::string s = pageId;
    if (s.size() > 3 && s.compare(s.size() - 3, 3, "_ok") == 0) {
        s = s.substr(0, s.size() - 3);
    }
    const std::size_t us = s.rfind('_');
    if (us != std::string::npos && us + 1 < s.size()) {
        s = s.substr(us + 1);
    }
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] >= 'a' && s[i] <= 'z') {
            s[i] = static_cast<char>(s[i] - 'a' + 'A');
        }
    }
    return s;
}

void buildGraphicNovelChapters(const std::vector<GraphicNovelPageDef>& pages,
                               std::vector<GraphicNovelChapter>* out) {
    if (out == 0) {
        return;
    }
    out->clear();
    std::map<std::string, int> index;
    for (std::size_t i = 0; i < pages.size(); ++i) {
        if (pages[i].cine) {
            continue;
        }
        const std::string key = graphicNovelChapterKey(pages[i].id);
        std::map<std::string, int>::iterator it = index.find(key);
        if (it == index.end()) {
            GraphicNovelChapter ch;
            ch.id = key;
            ch.title = graphicNovelChapterTitle(key);
            index[key] = static_cast<int>(out->size());
            out->push_back(ch);
            it = index.find(key);
        }
        (*out)[static_cast<std::size_t>(it->second)].pageIndices.push_back(static_cast<int>(i));
    }
    if (out->empty() && !pages.empty()) {
        GraphicNovelChapter ch;
        ch.id = "all";
        ch.title = "Graphic Novel";
        for (std::size_t i = 0; i < pages.size(); ++i) {
            ch.pageIndices.push_back(static_cast<int>(i));
        }
        out->push_back(ch);
    }
}

// data/weaponpriority.txt: [BestWeapons] / [CycleWeapons] with
// "[weapon] ID = WEAPONID_X;" lines. The engine cycles [CycleWeapons];
// we keep the order as weapon names (WeaponID resolved through the weapon
// scripts, falling back to WEAPONID_<name>).
void loadWeaponPriority(const std::string& dbRoot, GameCatalog* cat) {
    std::string hit;
    const char* cands[] = {"../weaponpriority.txt", "weaponpriority.txt"};
    for (int i = 0; i < 2; ++i) {
        const std::string f = existingPathIgnoreCase(joinPath(dbRoot, cands[i]));
        if (!f.empty() && isFile(f)) {
            hit = f;
            break;
        }
    }
    if (hit.empty()) {
        return;
    }
    try {
        const Script sc = Script::loadFile(hit, false);
        std::vector<int> cycleIds;
        for (std::size_t b = 0; b < sc.root().children.size(); ++b) {
            const ScriptBlock& blk = sc.root().children[b];
            if (blk.name != "cycleweapons") {
                continue;
            }
            for (std::size_t c = 0; c < blk.children.size(); ++c) {
                const ScriptBlock& w = blk.children[c];
                if (w.name != "weapon") {
                    continue;
                }
                const std::string id = assignmentOf(w, "id");
                if (id.empty()) {
                    continue;
                }
                cycleIds.push_back(parseIntCatch(id, -1));
            }
        }
        if (cycleIds.empty()) {
            return;
        }
        // Map WeaponID -> weapon name through the parsed weapon defs.
        for (std::size_t i = 0; i < cycleIds.size(); ++i) {
            if (cycleIds[i] < 0) {
                continue;
            }
            for (std::map<std::string, WeaponDef>::iterator it = cat->weapons.begin();
                 it != cat->weapons.end(); ++it) {
                if (it->second.weaponId == cycleIds[i]) {
                    cat->cycleOrder.push_back(it->first);
                    break;
                }
            }
        }
    } catch (...) {
    }
}

// Fallback for weapon scripts whose #include of weaponid.h / shooting.h did
// not resolve: read "#define <NAME> <value>" from the .h files next to the
// database and fill the missing WeaponID / SlotIndex.
void loadWeaponIdDefines(const std::string& dbRoot, GameCatalog* cat) {
    if (dbRoot.empty() || cat->weapons.empty()) {
        return;
    }
    std::map<std::string, int> defines;
    const std::vector<std::string> files = listFilesWithExtension(dbRoot, ".h");
    for (std::size_t i = 0; i < files.size(); ++i) {
        std::FILE* f = std::fopen(files[i].c_str(), "rb");
        if (f == 0) {
            continue;
        }
        char line[512];
        while (std::fgets(line, sizeof(line), f) != 0) {
            const std::string s = line;
            if (s.compare(0, 7, "#define") != 0) {
                continue;
            }
            std::size_t p = 7;
            while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) {
                ++p;
            }
            std::string name;
            while (p < s.size() && s[p] != ' ' && s[p] != '\t' && s[p] != '\r' && s[p] != '\n') {
                name.push_back(s[p]);
                ++p;
            }
            while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) {
                ++p;
            }
            std::string value;
            while (p < s.size() && s[p] != ' ' && s[p] != '\t' && s[p] != '\r' && s[p] != '\n') {
                value.push_back(s[p]);
                ++p;
            }
            if (!name.empty() && !value.empty()) {
                defines[lowerCopy(name)] = parseIntCatch(value, 0);
            }
        }
        std::fclose(f);
    }
    if (defines.empty()) {
        return;
    }
    for (std::map<std::string, WeaponDef>::iterator it = cat->weapons.begin(); it != cat->weapons.end();
         ++it) {
        WeaponDef& w = it->second;
        if (w.weaponId < 0) {
            std::map<std::string, int>::const_iterator d = defines.find("weaponid_" + w.name);
            if (d != defines.end()) {
                w.weaponId = d->second;
            }
        }
        if (w.slotIndex < 0) {
            std::map<std::string, int>::const_iterator d = defines.find("slotid_" + w.name);
            if (d != defines.end()) {
                w.slotIndex = d->second;
            }
        }
    }
}

GameCatalog loadGameCatalog(const std::string& dbRoot) {
    GameCatalog cat;
    if (dbRoot.empty()) {
        return cat;
    }
    loadWeapons(joinPath(dbRoot, "weapons"), &cat);
    loadProjectiles(joinPath(dbRoot, "projectiles"), &cat);
    loadPages(dbRoot, &cat);
    loadWeaponIdDefines(dbRoot, &cat);
    loadWeaponPriority(dbRoot, &cat);
    for (std::map<std::string, WeaponDef>::iterator it = cat.weapons.begin(); it != cat.weapons.end();
         ++it) {
        if (it->second.projectileName.empty()) {
            it->second.projectileName = std::string("bullet_") + it->first;
        }
        const ProjectileDef* pr = cat.findProjectile(it->second.projectileName);
        if (pr) {
            it->second.damage = pr->damage;
        }
    }
    return cat;
}

}  // namespace maxfx
