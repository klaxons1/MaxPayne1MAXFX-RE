#include "maxfx/game/Catalog.h"

#include "maxfx/core/Fs.h"
#include "maxfx/game/Message.h"
#include "maxfx/script/Script.h"

#include <cmath>

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
            }
            const ScriptBlock* cross = childNamed(sc.root(), "crosshairattributes");
            if (cross) {
                w.castLength = parseFloatCatch(assignmentOf(*cross, "castlength"), w.castLength);
                w.sphereRadius = parseFloatCatch(assignmentOf(*cross, "sphereradius"), w.sphereRadius);
            }
            std::vector<const ScriptBlock*> msgs;
            walkNamed(sc.root(), "message", &msgs);
            for (std::size_t m = 0; m < msgs.size(); ++m) {
                const std::string str = assignmentOf(*msgs[m], "string");
                const std::vector<GameMessage> parsed = parseGameMessages(str);
                for (std::size_t k = 0; k < parsed.size(); ++k) {
                    if (methodIs(parsed[k], "p_createprojectile") && !parsed[k].args.empty()) {
                        const std::string pn = lowerCopy(parsed[k].args[0]);
                        if (pn.find("bullet_") == 0 || pn.find("case_") == std::string::npos) {
                            if (pn.find("bullet_") == 0) {
                                w.projectileName = pn;
                            }
                        }
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
            const ScriptBlock* init = childNamed(b, "oninitmessage");
            if (init) {
                page.initSound = assignmentOf(*init, "string");
            }
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

GameCatalog loadGameCatalog(const std::string& dbRoot) {
    GameCatalog cat;
    if (dbRoot.empty()) {
        return cat;
    }
    loadWeapons(joinPath(dbRoot, "weapons"), &cat);
    loadProjectiles(joinPath(dbRoot, "projectiles"), &cat);
    loadPages(dbRoot, &cat);
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
