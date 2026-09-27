#include "maxfx/game/CameraPaths.h"

#include "maxfx/core/Fs.h"
#include "maxfx/script/Script.h"

#include <cstddef>

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

float parseFloatOr(const std::string& v, float fallback) {
    if (v.empty()) {
        return fallback;
    }
    try {
        const float f = std::stof(v);
        return f;
    } catch (...) {
        return fallback;
    }
}

bool truthy(const std::string& v) {
    const std::string s = lowerCopy(v);
    return s == "true" || s == "1" || s == "yes";
}

std::string nativeFromScriptPath(const std::string& p) {
    std::string s = p;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\') {
            s[i] = '/';
        }
    }
    return s;
}

// The [Animation] Filename is relative to the script folder with possible
// ../ segments ("..\..\camerapaths\x.kf2"); also try the database root and
// the file's bare name next to the script.
std::string resolveAnimPath(const std::string& scriptDir, const std::string& dbRoot,
                            const std::string& rel) {
    if (rel.empty()) {
        return std::string();
    }
    const std::string n = nativeFromScriptPath(parseScriptString(rel));
    const std::string base = fileName(n);
    std::vector<std::string> cands;
    // ../ chain relative to the script folder.
    {
        std::string cur = scriptDir;
        std::string rest = n;
        bool ok = true;
        while (rest.size() >= 3 && rest[0] == '.' && rest[1] == '.' && rest[2] == '/') {
            cur = parentDir(cur);
            rest = rest.substr(3);
        }
        if (rest.size() >= 2 && rest[0] == '.' && rest[1] == '/') {
            rest = rest.substr(2);
        }
        if (ok) {
            cands.push_back(joinPath(cur, rest));
        }
    }
    cands.push_back(joinPath(dbRoot, n));
    cands.push_back(joinPath(dbRoot, base));
    cands.push_back(joinPath(scriptDir, base));
    for (std::size_t i = 0; i < cands.size(); ++i) {
        const std::string hit = existingPathIgnoreCase(cands[i]);
        if (!hit.empty() && isFile(hit)) {
            return hit;
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

void collectMessages(const ScriptBlock& block, std::vector<std::string>* out) {
    if (block.name == "message") {
        const std::string s = parseScriptString(assignmentOf(block, "string"));
        if (!s.empty()) {
            out->push_back(s);
        }
    }
    for (std::size_t i = 0; i < block.children.size(); ++i) {
        collectMessages(block.children[i], out);
    }
}

}  // namespace

void CameraPathCatalog::load(const std::string& dbRoot) {
    paths_.clear();
    if (dbRoot.empty()) {
        return;
    }
    std::string path = joinPath(joinPath(dbRoot, "camerapaths"), "camerapaths.txt");
    std::string hit = existingPathIgnoreCase(path);
    if (hit.empty() || !isFile(hit)) {
        path = joinPath(dbRoot, "camerapaths.txt");
        hit = existingPathIgnoreCase(path);
        if (hit.empty() || !isFile(hit)) {
            return;
        }
    }
    // Script's default constructor is private; retry without includes when
    // the include chain is missing (same pattern as Hud / Catalog).
    std::vector<Script> attempts;
    try {
        attempts.push_back(Script::loadFile(hit, true));
    } catch (...) {
        try {
            attempts.push_back(Script::loadFile(hit, false));
        } catch (...) {
            return;
        }
    }
    const Script& sc = attempts[0];
    const std::string scriptDir = parentDir(hit);
    for (std::size_t i = 0; i < sc.root().children.size(); ++i) {
        const ScriptBlock& b = sc.root().children[i];
        if (b.name.empty()) {
            continue;
        }
        CameraPathDef def;
        def.name = b.name;
        const ScriptBlock* attrs = childNamed(b, "attributes");
        if (attrs != 0) {
            def.fadeInTime = parseFloatOr(assignmentOf(*attrs, "fadeintime"), 0.0f);
            def.fadeOutTime = parseFloatOr(assignmentOf(*attrs, "fadeouttime"), 0.0f);
            def.abortable = truthy(assignmentOf(*attrs, "abortable"));
        }
        const ScriptBlock* animSet = childNamed(b, "animationset");
        if (animSet != 0) {
            // First [Animation] with a Filename wins (camera paths are
            // single-clip; the engine's AnimationContainer takes index 0).
            for (std::size_t a = 0; a < animSet->children.size(); ++a) {
                if (animSet->children[a].name != "animation") {
                    continue;
                }
                const std::string f = assignmentOf(animSet->children[a], "filename");
                if (f.empty()) {
                    continue;
                }
                def.animationFile = parseScriptString(f);
                def.resolvedAnimation = resolveAnimPath(scriptDir, dbRoot, f);
                break;
            }
        }
        const ScriptBlock* exit = childNamed(b, "exit");
        if (exit != 0) {
            collectMessages(*exit, &def.exitMessages);
        }
        paths_.push_back(def);
    }
}

const CameraPathDef* CameraPathCatalog::find(const std::string& name) const {
    const std::string key = lowerCopy(name);
    for (std::size_t i = 0; i < paths_.size(); ++i) {
        if (lowerCopy(paths_[i].name) == key) {
            return &paths_[i];
        }
    }
    return 0;
}

}  // namespace maxfx
