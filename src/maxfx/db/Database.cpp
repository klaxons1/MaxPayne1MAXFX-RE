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

std::string stripDotDotPrefix(const std::string& rel) {
    std::string rest = nativeFromScriptPath(rel);
    for (;;) {
        if (rest.size() >= 3 && rest[0] == '.' && rest[1] == '.' && (rest[2] == '/' || rest[2] == '\\')) {
            rest = rest.substr(3);
            continue;
        }
        if (rest.size() >= 2 && rest[0] == '.' && (rest[1] == '/' || rest[1] == '\\')) {
            rest = rest.substr(2);
            continue;
        }
        break;
    }
    return rest;
}

std::string firstExisting(const std::vector<std::string>& candidates) {
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (candidates[i].empty()) {
            continue;
        }
        const std::string hit = existingPathIgnoreCase(candidates[i]);
        if (!hit.empty() && isFile(hit)) {
            return hit;
        }
    }
    return std::string();
}

// Scripts live next to their mesh folders in the full RAS, but the extracted
// tree often flattens `level_items/beretta.txt` while the KF2 stays under
// `level_items/ammo_beretta/` or `weapons/beretta/`. Try every layout.
std::string resolveModelPath(const std::string& dbRoot, const std::string& scriptDir,
                             const std::string& stem, const std::string& rel) {
    if (rel.empty()) {
        return std::string();
    }
    const std::string native = nativeFromScriptPath(rel);
    const std::string base = fileName(native);
    const std::string stripped = stripDotDotPrefix(native);
    std::vector<std::string> cands;
    cands.push_back(resolveRelative(scriptDir, native));
    cands.push_back(joinPath(scriptDir, native));
    if (!stem.empty()) {
        cands.push_back(joinPath(joinPath(scriptDir, stem), base));
    }
    if (!dbRoot.empty()) {
        cands.push_back(joinPath(dbRoot, stripped));
        cands.push_back(joinPath(dbRoot, base));
        if (!stem.empty()) {
            cands.push_back(joinPath(joinPath(dbRoot, stem), base));
            cands.push_back(joinPath(joinPath(joinPath(dbRoot, "level_items"), stem), base));
            cands.push_back(joinPath(joinPath(joinPath(dbRoot, "weapons"), stem), base));
            cands.push_back(joinPath(joinPath(joinPath(dbRoot, "skins"), stem), base));
        }
        cands.push_back(joinPath(joinPath(dbRoot, "weapons"), stripped));
        cands.push_back(joinPath(joinPath(dbRoot, "level_items"), stripped));
        cands.push_back(joinPath(joinPath(dbRoot, "skins"), stripped));
    }
    const std::string hit = firstExisting(cands);
    if (!hit.empty()) {
        return hit;
    }
    return resolveRelative(scriptDir, native);
}

void collectGeometry(const ScriptBlock& block, const std::string& dbRoot, const std::string& scriptDir,
                     const std::string& stem, std::vector<GeometryRef>* out) {
    if (block.name == "geometry") {
        GeometryRef g;
        g.exportData = assignmentOf(block, "exportdata");
        g.skinData = assignmentOf(block, "skindata");
        if (!g.exportData.empty()) {
            g.resolvedExport = resolveModelPath(dbRoot, scriptDir, stem, g.exportData);
        }
        if (!g.skinData.empty()) {
            g.resolvedSkin = resolveModelPath(dbRoot, scriptDir, stem, g.skinData);
        }
        if (!g.exportData.empty() || !g.skinData.empty()) {
            out->push_back(g);
        }
    }
    for (std::size_t i = 0; i < block.children.size(); ++i) {
        collectGeometry(block.children[i], dbRoot, scriptDir, stem, out);
    }
}

void fillSoundFields(const ScriptBlock& block, SoundDef* def) {
    const std::string fn = assignmentOf(block, "filename");
    if (!fn.empty()) {
        def->filename = nativeFromScriptPath(parseScriptString(fn));
    }
    const std::string vol = assignmentOf(block, "volume");
    if (!vol.empty()) {
        def->volume = parseFloatDefault(block, "volume", def->volume);
    }
    const std::string pitch = assignmentOf(block, "pitch");
    if (!pitch.empty()) {
        def->pitch = parseIntDefault(block, "pitch", def->pitch);
    }
    if (!assignmentOf(block, "looping").empty()) {
        def->looping = parseBoolDefault(block, "looping", def->looping);
    }
    if (!assignmentOf(block, "3dsound").empty()) {
        def->is3d = parseBoolDefault(block, "3dsound", def->is3d);
    }
    if (!assignmentOf(block, "streamed").empty()) {
        def->streamed = parseBoolDefault(block, "streamed", def->streamed);
    }
    if (!assignmentOf(block, "hotspot").empty()) {
        def->hotspot = parseFloatDefault(block, "hotspot", def->hotspot);
    }
    if (!assignmentOf(block, "falloff").empty()) {
        def->falloff = parseFloatDefault(block, "falloff", def->falloff);
    }
    for (std::size_t i = 0; i < block.children.size(); ++i) {
        fillSoundFields(block.children[i], def);
    }
}

std::string resolveSoundPath(const std::string& dbRoot, const std::string& subdir,
                             const std::string& filename) {
    if (filename.empty()) {
        return std::string();
    }
    std::vector<std::string> cands;
    cands.push_back(joinPath(joinPath(dbRoot, subdir), filename));
    cands.push_back(joinPath(dbRoot, filename));
    cands.push_back(joinPath(joinPath(dbRoot, "sounds"), filename));
    cands.push_back(joinPath(joinPath(dbRoot, "music"), filename));
    return firstExisting(cands);
}

void collectTopSounds(const Script& script, const std::string& dbRoot, const std::string& category,
                      const char* subdir, std::vector<SoundDef>* out) {
    for (std::size_t b = 0; b < script.root().children.size(); ++b) {
        const ScriptBlock& block = script.root().children[b];
        SoundDef def;
        def.name = block.name;
        def.category = category;
        fillSoundFields(block, &def);
        if (def.filename.empty()) {
            continue;
        }
        def.resolvedPath = resolveSoundPath(dbRoot, subdir, def.filename);
        out->push_back(def);
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

const SoundDef* Database::findSound(const std::string& name) const {
    const std::string key = lowerCopy(name);
    for (std::size_t i = 0; i < sounds.size(); ++i) {
        if (sounds[i].name == key) {
            return &sounds[i];
        }
    }
    for (std::size_t i = 0; i < music.size(); ++i) {
        if (music[i].name == key) {
            return &music[i];
        }
    }
    return 0;
}

const SoundDef* Database::findSound(const std::string& category, const std::string& name) const {
    const std::string cat = lowerCopy(category);
    const std::string key = lowerCopy(name);
    for (std::size_t i = 0; i < sounds.size(); ++i) {
        if (sounds[i].name == key && sounds[i].category == cat) {
            return &sounds[i];
        }
    }
    if (cat == "music") {
        return findMusic(name);
    }
    return findSound(name);
}

const SoundDef* Database::findMusic(const std::string& name) const {
    const std::string key = lowerCopy(name);
    for (std::size_t i = 0; i < music.size(); ++i) {
        if (music[i].name == key) {
            return &music[i];
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
                collectGeometry(script.root(), root, dir, skin.name, &skin.lods);
                fillCharacterConfig(skin.character, script.root(), files[i], root);
                if (skin.character.skeleton.empty()) {
                    skin.character.skeleton = skin.skeleton;
                }
                if (skin.skeleton.empty()) {
                    skin.skeleton = skin.character.skeleton;
                }
                db.skins.push_back(skin);
            } else if (isUnder(files[i], "level_items")) {
                ItemDef item;
                item.name = stemName(files[i]);
                item.sourcePath = files[i];
                collectGeometry(script.root(), root, dir, item.name, &item.lods);
                db.items.push_back(item);
            } else if (isUnder(files[i], "decals")) {
                collectDecals(script.root(), &db.decals);
            } else if (isUnder(files[i], "/sounds/") || isUnder(files[i], "\\sounds\\") ||
                       isUnder(files[i], "/sounds\\") || isUnder(files[i], "\\sounds/")) {
                collectTopSounds(script, root, stemName(files[i]), "sounds", &db.sounds);
            } else if (isUnder(files[i], "/music/") || isUnder(files[i], "\\music\\") ||
                       isUnder(files[i], "/music\\") || isUnder(files[i], "\\music/")) {
                collectTopSounds(script, root, stemName(files[i]), "music", &db.music);
            }
        } catch (const std::exception& ex) {
            entry.error = ex.what();
            ++db.failedScripts;
        }
        db.scripts.push_back(entry);
    }
    for (std::size_t s = 0; s < db.skins.size(); ++s) {
        SkinDef& skin = db.skins[s];
        if (skin.skeleton.empty()) {
            continue;
        }
        const std::string skelPath =
            existingPathIgnoreCase(joinPath(joinPath(root, "skeletons"), skin.skeleton + ".txt"));
        if (skelPath.empty() || !isFile(skelPath)) {
            continue;
        }
        try {
            const Script skel = Script::loadFile(skelPath, false);
            mergeSkeletonConfig(skin.character, skel.root(), skelPath, root);
        } catch (...) {
        }
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

void DatabaseReader::loadWorldSphere(Database& db, const std::string& name) {
    db.worldSphereName = name;
    db.worldSpherePath.clear();
    if (name.empty() || db.root.empty()) {
        return;
    }
    const std::string stem = fileStem(nativeFromScriptPath(name));
    std::vector<std::string> cands;
    const std::string folder = joinPath(db.root, "worldspheres");
    cands.push_back(joinPath(folder, std::string("bg_") + stem + ".kf2"));
    cands.push_back(joinPath(folder, stem + ".kf2"));
    cands.push_back(joinPath(folder, nativeFromScriptPath(name)));
    const std::string hit = firstExisting(cands);
    if (hit.empty()) {
        return;
    }
    if (cacheModel(db, hit) != 0) {
        db.worldSpherePath = hit;
    }
}

}  // namespace maxfx
