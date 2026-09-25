// Remedy R_Script / R_ScriptLoader stand-in.
//
// Max Payne 1 databases (levels.txt, hud.txt, weapons, ...) are text files
// with C-like preprocessor directives and a nested-block grammar:
//
//   #include <globaldefines.h>
//   #define GRAVITY_VALUE -981
//   [blockname]
//   {
//       lvalue = rvalue;
//       [child]
//       {
//           ...
//       }
//   }
//
// Behaviour is taken from Android libMaxPayne.so (R_ScriptLoader::preprocess,
// R_Script::lock / parse / count / getAssignment). Block and lvalue names are
// case-insensitive and stored lower-cased, matching the loader's strlwr pass.
// Quoted strings keep their original case and interior spaces.
#ifndef MAXFX_SCRIPT_SCRIPT_H
#define MAXFX_SCRIPT_SCRIPT_H

#include <stdexcept>
#include <string>
#include <vector>

namespace maxfx {

class ScriptError : public std::runtime_error {
public:
    ScriptError(const std::string& message, const std::string& file, unsigned line);
    const std::string& file() const { return file_; }
    unsigned line() const { return line_; }

private:
    std::string file_;
    unsigned line_;
};

struct ScriptAssignment {
    std::string lvalue;  // lower-cased
    std::string rvalue;  // quotes stripped, otherwise as written (defines expanded)
    unsigned line;
};

struct ScriptBlock {
    std::string name;  // lower-cased tag between [ ]
    unsigned line;
    std::vector<ScriptAssignment> assignments;
    std::vector<ScriptBlock> children;
};

// In-memory tree of one script file (plus its #includes).
class Script {
public:
    // Load `path`, recursively expanding #include relative to that file.
    static Script loadFile(const std::string& path);

    // Parse `text` as if it came from `sourceName`. #include is resolved
    // relative to `includeBaseDir` (empty = the process working directory).
    static Script parseText(const std::string& text,
                            const std::string& sourceName,
                            const std::string& includeBaseDir = std::string());

    const ScriptBlock& root() const { return root_; }
    const std::string& sourceName() const { return sourceName_; }

    // R_Script::count / lock / unlock / getBlockName / getAssignment.
    // The cursor starts at the implicit root (unnamed file-level block).
    int count() const;
    int count(const std::string& blockName) const;
    int countLvalues() const;
    int countLvalues(const std::string& lvalue) const;

    void lock(int index);
    void lock(const std::string& blockName, int nth = 0);
    void unlock();

    std::string blockName() const;
    const ScriptBlock& current() const;

    // Empty string if the lvalue is missing. `nth` selects among duplicates
    // (the engine throws DoubleVariable instead; we keep the first).
    std::string assignment(const std::string& lvalue, int nth = 0) const;
    bool hasAssignment(const std::string& lvalue) const;

    // Path of nested [tag] blocks from the file root to the cursor, as
    // R_ScriptLoader::generateContentStaticNesting: "[a]/[b]/[c]".
    std::string staticNesting() const;

private:
    Script() {}

    static Script fromExpanded(const std::string& expanded, const std::string& sourceName);
    const ScriptBlock& blockAt(const std::vector<int>& path) const;

    ScriptBlock root_;
    std::string sourceName_;
    std::vector<int> lockPath_;
};

// Helpers used by database parsers (R_Script::t_int / t_float / t_float3 /
// t_string / t_unsigned). Trailing '%' is accepted on numbers (MaximumHealth
// = 300%). TRUE/FALSE are accepted on integers in case the file did not
// #include globaldefines.h.
int parseScriptInt(const std::string& rvalue, const std::string& lvalue, unsigned line,
                   const std::string& file);
unsigned parseScriptUnsigned(const std::string& rvalue, const std::string& lvalue, unsigned line,
                             const std::string& file);
float parseScriptFloat(const std::string& rvalue, const std::string& lvalue, unsigned line,
                       const std::string& file);
void parseScriptFloat3(const std::string& rvalue, float out[3], const std::string& lvalue,
                       unsigned line, const std::string& file);
std::string parseScriptString(const std::string& rvalue);

bool scriptNamesEqual(const std::string& a, const std::string& b);

}  // namespace maxfx

#endif  // MAXFX_SCRIPT_SCRIPT_H
