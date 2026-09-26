#include "maxfx/db/Database.h"

#include "maxfx/core/Fs.h"
#include "maxfx/script/Script.h"

#include <cctype>

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

bool parseBoolDefault(const ScriptBlock& block, const char* lvalue, bool fallback) {
    const std::string v = assignmentOf(block, lvalue);
    if (v.empty()) {
        return fallback;
    }
    try {
        return parseScriptInt(v, lvalue, block.line, "") != 0;
    } catch (...) {
        return fallback;
    }
}

int parseIntDefault(const ScriptBlock& block, const char* lvalue, int fallback) {
    const std::string v = assignmentOf(block, lvalue);
    if (v.empty()) {
        return fallback;
    }
    try {
        return parseScriptInt(v, lvalue, block.line, "");
    } catch (...) {
        return fallback;
    }
}

float parseFloatDefault(const ScriptBlock& block, const char* lvalue, float fallback) {
    const std::string v = assignmentOf(block, lvalue);
    if (v.empty()) {
        return fallback;
    }
    try {
        return parseScriptFloat(v, lvalue, block.line, "");
    } catch (...) {
        return fallback;
    }
}

std::string nativeFromScriptPath(const std::string& p) {
    std::string s = p;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\') {
            s[i] = '/';
        }
    }
    return nativeSeparators(s);
}

std::string resolveRelative(const std::string& baseDir, const std::string& rel) {
    if (rel.empty()) {
        return std::string();
    }
    const std::string n = nativeFromScriptPath(rel);
    std::string cur = baseDir;
    std::string rest = n;
    while (rest.size() >= 3 && rest[0] == '.' && rest[1] == '.' && (rest[2] == '/' || rest[2] == '\\')) {
        cur = parentDir(cur);
        rest = rest.substr(3);
    }
    const std::string joined = joinPath(cur, rest);
    const std::string hit = existingPathIgnoreCase(joined);
    return hit.empty() ? joined : hit;
}

void collectGeometry(const ScriptBlock& block, const std::string& scriptDir, std::vector<GeometryRef>* out) {
    if (block.name == "geometry") {
        GeometryRef g;
        g.exportData = assignmentOf(block, "exportdata");
        g.skinData = assignmentOf(block, "skindata");
        if (!g.exportData.empty()) {
            g.resolvedExport = resolveRelative(scriptDir, g.exportData);
        }
        if (!g.skinData.empty()) {
            g.resolvedSkin = resolveRelative(scriptDir, g.skinData);
        }
        if (!g.exportData.empty() || !g.skinData.empty()) {
            out->push_back(g);
        }
    }
    for (std::size_t i = 0; i < block.children.size(); ++i) {
        collectGeometry(block.children[i], scriptDir, out);
    }
}

void collectDecals(const ScriptBlock& block, std::vector<DecalMaterialDef>* out) {
    const std::string fn = assignmentOf(block, "filename");
    const std::string an = assignmentOf(block, "alphafilename");
    if (!fn.empty()) {
        DecalMaterialDef d;
        d.name = block.name;
        d.filename = fn;
        d.alphaFilename = an;
        d.minRadius = parseFloatDefault(block, "minimumradius", 0.0f);
        d.maxRadius = parseFloatDefault(block, "maximumradius", 0.0f);
        out->push_back(d);
    }
    for (std::size_t i = 0; i < block.children.size(); ++i) {
        collectDecals(block.children[i], out);
    }
}

bool looksLikeMaterialsFile(const std::string& path) {
    return lowerCopy(fileName(path)) == "materials.txt";
}

bool isUnder(const std::string& path, const char* folder) {
    const std::string n = lowerCopy(path);
    const std::string needle = std::string(folder);
    return n.find(needle) != std::string::npos;
}

std::string stemName(const std::string& path) { return lowerCopy(fileStem(path)); }

bool hasMaterialsTxt(const std::string& dir) {
    return isFile(joinPath(dir, "materials.txt"));
}

std::string candidateDatabase(const std::string& start) {
    if (start.empty()) {
        return std::string();
    }
    std::string cur = start;
    if (isFile(cur)) {
        cur = parentDir(cur);
    }
    for (int i = 0; i < 8 && !cur.empty(); ++i) {
        if (hasMaterialsTxt(cur)) {
            return cur;
        }
        const std::string db = joinPath(cur, "database");
        if (hasMaterialsTxt(db)) {
            return db;
        }
        const std::string nested = joinPath(joinPath(cur, "data"), "database");
        if (hasMaterialsTxt(nested)) {
            return nested;
        }
        const std::string docs = joinPath(joinPath(cur, "docs"), "database");
        if (hasMaterialsTxt(docs)) {
            return docs;
        }
        cur = parentDir(cur);
    }
    return std::string();
}

Kf2File* cacheModel(Database& db, const std::string& path) {
    if (path.empty() || !isFile(path)) {
        return 0;
    }
    const std::string key = lowerCopy(path);
    std::map<std::string, Kf2File>::iterator it = db.models.find(key);
    if (it != db.models.end()) {
        return &it->second;
    }
    try {
        Kf2File file = Kf2Reader::loadFromFile(path);
        std::pair<std::map<std::string, Kf2File>::iterator, bool> ins =
            db.models.insert(std::make_pair(key, file));
        ++db.loadedModels;
        return &ins.first->second;
    } catch (...) {
        return 0;
    }
}

}  // namespace

const MaterialCategory* Database::findMaterial(const std::string& category) const {
    const std::string key = lowerCopy(category);
    for (std::size_t i = 0; i < materials.size(); ++i) {
        if (lowerCopy(materials[i].name) == key) {
            return &materials[i];
        }
    }
    return 0;
}

const SkinDef* Database::findSkin(const std::string& name) const {
    const std::string key = lowerCopy(name);
    for (std::size_t i = 0; i < skins.size(); ++i) {
        if (lowerCopy(skins[i].name) == key) {
            return &skins[i];
        }
    }
    return 0;
}

const ItemDef* Database::findItem(const std::string& name) const {
    const std::string key = lowerCopy(name);
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (lowerCopy(items[i].name) == key) {
            return &items[i];
        }
    }
    return 0;
}

const Kf2File* Database::model(const std::string& resolvedPath) const {
    const std::string key = lowerCopy(resolvedPath);
    std::map<std::string, Kf2File>::const_iterator it = models.find(key);
    if (it == models.end()) {
        return 0;
    }
    return &it->second;
}

Kf2File* Database::model(const std::string& resolvedPath) {
    const std::string key = lowerCopy(resolvedPath);
    std::map<std::string, Kf2File>::iterator it = models.find(key);
    if (it == models.end()) {
        return 0;
    }
    return &it->second;
}

std::string DatabaseReader::locateRoot(const std::string& hint) {
    std::vector<std::string> roots;
    if (!hint.empty()) {
        roots.push_back(hint);
    }
    const std::string exe = executableDir();
    if (!exe.empty()) {
        roots.push_back(joinPath(exe, "data"));
        roots.push_back(exe);
        const std::string parent = parentDir(exe);
        if (!parent.empty()) {
            roots.push_back(joinPath(parent, "data"));
            roots.push_back(parent);
        }
    }
    roots.push_back(std::string("."));
    roots.push_back(std::string("data"));
    roots.push_back(std::string("docs"));
    roots.push_back(joinPath("docs", "database"));
    for (std::size_t i = 0; i < roots.size(); ++i) {
        const std::string hit = candidateDatabase(roots[i]);
        if (!hit.empty()) {
            return hit;
        }
    }
    return std::string();
}

Database DatabaseReader::load(const std::string& root) {
    Database db;
    db.root = root;
    if (root.empty() || !isDirectory(root)) {
        return db;
    }
    const std::vector<std::string> files = listFilesWithExtensionRecursive(root, ".txt");
    for (std::size_t i = 0; i < files.size(); ++i) {
        ScriptCatalogEntry entry;
        entry.path = files[i];
        entry.relative = files[i];
        if (files[i].size() > root.size()) {
            std::size_t off = root.size();
            if (files[i][off] == '/' || files[i][off] == '\\') {
                ++off;
            }
            entry.relative = files[i].substr(off);
        }
        try {
            const Script script = Script::loadFile(files[i], false);
            entry.topBlocks = static_cast<int>(script.root().children.size());
            ++db.parsedScripts;

            const std::string dir = parentDir(files[i]);
            if (looksLikeMaterialsFile(files[i])) {
                for (std::size_t b = 0; b < script.root().children.size(); ++b) {
                    const ScriptBlock& block = script.root().children[b];
                    MaterialCategory cat;
                    cat.name = block.name;
                    cat.drawPolygons = parseBoolDefault(block, "drawpolygons", true);
                    cat.detailOffset = parseIntDefault(block, "detailoffset", 0);
                    cat.sortPriority = parseIntDefault(block, "sortpriority", 0);
                    cat.writesZBuffer = parseBoolDefault(block, "writeszbuffer", true);
                    cat.alphaReference = parseIntDefault(block, "alphareference", 15);
                    cat.blendedAlphaTest = parseBoolDefault(block, "blendedalphatest", true);
                    cat.skyBox = parseBoolDefault(block, "skybox", false);
                    db.materials.push_back(cat);
                }
            } else if (isUnder(files[i], "skins") && !isUnder(files[i], "shared")) {
                SkinDef skin;
                skin.name = stemName(files[i]);
                skin.sourcePath = files[i];
                skin.skeleton = assignmentOf(script.root(), "skeleton");
                // [Properties] Skeleton = ...
                for (std::size_t b = 0; b < script.root().children.size(); ++b) {
                    if (script.root().children[b].name == "properties") {
                        const std::string sk = assignmentOf(script.root().children[b], "skeleton");
                        if (!sk.empty()) {
                            skin.skeleton = sk;
                        }
                    }
                }
                collectGeometry(script.root(), dir, &skin.lods);
                if (!skin.lods.empty()) {
                    db.skins.push_back(skin);
                }
            } else if (isUnder(files[i], "level_items")) {
                ItemDef item;
                item.name = stemName(files[i]);
                item.sourcePath = files[i];
                collectGeometry(script.root(), dir, &item.lods);
                if (!item.lods.empty()) {
                    db.items.push_back(item);
                }
            } else if (isUnder(files[i], "decals")) {
                collectDecals(script.root(), &db.decals);
            }
        } catch (const std::exception& ex) {
            entry.error = ex.what();
            ++db.failedScripts;
        }
        db.scripts.push_back(entry);
    }
    return db;
}

void DatabaseReader::loadModels(Database& db, const std::vector<std::string>& skinNames,
                                const std::vector<std::string>& itemNames) {
    for (std::size_t i = 0; i < skinNames.size(); ++i) {
        const SkinDef* skin = db.findSkin(skinNames[i]);
        if (skin == 0 || skin->lods.empty()) {
            continue;
        }
        cacheModel(db, skin->lods[0].resolvedExport);
        cacheModel(db, skin->lods[0].resolvedSkin);
    }
    for (std::size_t i = 0; i < itemNames.size(); ++i) {
        const ItemDef* item = db.findItem(itemNames[i]);
        if (item == 0 || item->lods.empty()) {
            continue;
        }
        cacheModel(db, item->lods[0].resolvedExport);
    }
}

}  // namespace maxfx
