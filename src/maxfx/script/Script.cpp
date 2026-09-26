#include "maxfx/script/Script.h"

#include "maxfx/core/Fs.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <sstream>

namespace maxfx {
namespace {

const int kDefineMaxChars = 159;  // R_ScriptLoaderException_DefineTooLong, 159

bool isWhiteSpace(unsigned char c) {
    // R_ScriptLoader::isWhiteSpace: bits of 0x100002E01 = NUL, TAB, LF, VT, CR, SPACE.
    if (c > 0x20u) {
        return false;
    }
    return ((0x100002E01ull >> c) & 1ull) != 0;
}

bool isEndOfLine(char c) { return c == '\n'; }

bool isIdentChar(unsigned char c) {
    return std::isalnum(c) != 0 || c == '_' || c == '.';
}

std::string lowerAscii(const std::string& s) {
    std::string out = s;
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(out[i])));
    }
    return out;
}

std::string trimCopy(const std::string& s) {
    std::size_t a = 0;
    while (a < s.size() && isWhiteSpace(static_cast<unsigned char>(s[a]))) {
        ++a;
    }
    std::size_t b = s.size();
    while (b > a && isWhiteSpace(static_cast<unsigned char>(s[b - 1]))) {
        --b;
    }
    return s.substr(a, b - a);
}

char matchingClose(char open) {
    switch (open) {
        case '(':
            return ')';
        case '[':
            return ']';
        case '{':
            return '}';
        default:
            return 0;
    }
}

std::string formatLine(const std::string& file, unsigned line, const std::string& msg) {
    std::ostringstream oss;
    oss << "R_ScriptLoader: " << msg << " in line " << line << " of \"" << file << "\"";
    return oss.str();
}

struct Bracket {
    char open;
    unsigned line;
};

struct Loader {
    std::map<std::string, std::string> defines;
    std::set<std::string> includeStack;
    bool requiredIncludes;

    Loader() : requiredIncludes(true) {
        // Official levels.txt #includes globaldefines.h; if that RAS file is
        // missing we still accept TRUE/FALSE/GRAVITY_VALUE used in every map.
        defines["true"] = "1";
        defines["false"] = "0";
        defines["gravity_value"] = "-981";
        defines["timebonus"] = "5";
    }

    std::string loadFile(const std::string& path) {
        const std::string full = path;
        if (includeStack.count(full) != 0) {
            throw ScriptError("R_ScriptLoader: Attempt to include file \"" + full +
                                  "\" again (recursion problem)!",
                              full, 0);
        }
        includeStack.insert(full);
        const std::string text = readFileText(full);
        std::string out = preprocess(text, full, parentDir(full));
        includeStack.erase(full);
        return out;
    }

    std::string preprocess(const std::string& text, const std::string& file,
                           const std::string& baseDir) {
        std::string out;
        out.reserve(text.size());
        std::vector<Bracket> brackets;
        unsigned line = 1;
        const std::size_t n = text.size();
        std::size_t i = 0;
        if (n >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB &&
            (unsigned char)text[2] == 0xBF) {
            i = 3;
        }

        while (i < n) {
            const char c = text[i];

            if (c == '"' ) {
                const unsigned startLine = line;
                out.push_back('"');
                ++i;
                bool closed = false;
                while (i < n) {
                    const char q = text[i++];
                    if (isEndOfLine(q)) {
                        ++line;
                    }
                    out.push_back(q);
                    if (q == '"') {
                        closed = true;
                        break;
                    }
                }
                if (!closed) {
                    throw ScriptError(
                        formatLine(file, startLine,
                                   "String marking \"...\" starting doesn't end!"),
                        file, startLine);
                }
                continue;
            }

            if (c == '/' && i + 1 < n && text[i + 1] == '/') {
                i += 2;
                while (i < n && !isEndOfLine(text[i])) {
                    ++i;
                }
                continue;
            }

            if (c == '/' && i + 1 < n && text[i + 1] == '*') {
                const unsigned startLine = line;
                i += 2;
                bool closed = false;
                while (i + 1 < n) {
                    if (isEndOfLine(text[i])) {
                        ++line;
                    }
                    if (text[i] == '*' && text[i + 1] == '/') {
                        i += 2;
                        closed = true;
                        break;
                    }
                    ++i;
                }
                if (!closed) {
                    throw ScriptError(
                        formatLine(file, startLine,
                                   "Block comment /*...*/ starting doesn't end!"),
                        file, startLine);
                }
                out.push_back(' ');
                continue;
            }

            if (c == '#') {
                // Directives are only recognised at the start of a token.
                std::size_t j = i;
                while (j < n && !isWhiteSpace(static_cast<unsigned char>(text[j])) &&
                       text[j] != '"' && text[j] != '\'' && text[j] != ';') {
                    ++j;
                }
                std::string dir = lowerAscii(text.substr(i, j - i));
                if (dir == "#include" || dir == "#define") {
                    if (!brackets.empty()) {
                        const char* kind =
                            dir == "#include" ? "#include" : "#define";
                        throw ScriptError(
                            formatLine(file, line,
                                       std::string(kind) +
                                           " directive can't be used inside a "
                                           "bracketed block (starts in line " +
                                           unsignedToString(brackets.back().line) +
                                           ")!"),
                            file, line);
                    }
                    if (dir == "#include") {
                        i = handleInclude(text, n, j, file, line, baseDir, out);
                    } else {
                        i = handleDefine(text, n, j, file, line);
                    }
                    continue;
                }
                if (dir.size() > 1) {
                    throw ScriptError(
                        formatLine(file, line, "Unknown preprocessor directive!"),
                        file, line);
                }
                // A lone '#' is passed through (should not appear in real files).
            }

            if (c == '(' || c == '[' || c == '{') {
                Bracket b;
                b.open = c;
                b.line = line;
                brackets.push_back(b);
                out.push_back(c);
                ++i;
                continue;
            }
            if (c == ')' || c == ']' || c == '}') {
                if (brackets.empty()) {
                    throw ScriptError(
                        formatLine(file, line,
                                   std::string("Closing bracket \"") + c +
                                       "\" found without opening bracket!"),
                        file, line);
                }
                const char expect = matchingClose(brackets.back().open);
                if (expect != c) {
                    throw ScriptError(
                        formatLine(file, line,
                                   std::string("Closing bracket \"") + c +
                                       "\" is incompatible with opening \"" +
                                       brackets.back().open + "\" from line " +
                                       unsignedToString(brackets.back().line) + "!"),
                        file, line);
                }
                brackets.pop_back();
                out.push_back(c);
                ++i;
                continue;
            }

            if (isEndOfLine(c)) {
                ++line;
                out.push_back('\n');
                ++i;
                continue;
            }

            // Identifier (possible #define replacement). R_ScriptLoader
            // lower-cases tags and lvalues at parse time; rvalues keep the
            // file's original case so Level = Part1_Level1.ldb still matches
            // the on-disk name. Define lookup is case-insensitive.
            if (std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_') {
                std::size_t j = i + 1;
                while (j < n && isIdentChar(static_cast<unsigned char>(text[j]))) {
                    ++j;
                }
                const std::string original = text.substr(i, j - i);
                const std::string ident = lowerAscii(original);
                if (defines.find(ident) != defines.end()) {
                    out += expandIdent(ident, file, line);
                } else {
                    out += original;
                }
                i = j;
                continue;
            }

            if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
                out.push_back(c);
                ++i;
                continue;
            }

            out.push_back(c);
            ++i;
        }

        if (!brackets.empty()) {
            throw ScriptError(
                formatLine(file, brackets.back().line,
                           std::string("File ends with opened bracket from line ") +
                               unsignedToString(brackets.back().line) + "!"),
                file, brackets.back().line);
        }
        return out;
    }

    static std::string unsignedToString(unsigned v) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%u", v);
        return std::string(buf);
    }

    std::string expandIdent(const std::string& ident, const std::string& file, unsigned line) {
        std::string cur = ident;
        std::set<std::string> seen;
        for (int depth = 0; depth < 32; ++depth) {
            const std::map<std::string, std::string>::const_iterator it = defines.find(cur);
            if (it == defines.end()) {
                return cur;
            }
            if (seen.count(cur) != 0) {
                throw ScriptError(
                    formatLine(file, line, "Recursive #define \"" + ident + "\""),
                    file, line);
            }
            seen.insert(cur);
            cur = it->second;
            // Defined values may be numbers / punctuation, not just idents.
            if (cur.empty() ||
                (std::isalpha(static_cast<unsigned char>(cur[0])) == 0 && cur[0] != '_')) {
                return cur;
            }
            // Keep expanding if the whole value is a single identifier.
            bool onlyIdent = true;
            for (std::size_t k = 0; k < cur.size(); ++k) {
                if (!isIdentChar(static_cast<unsigned char>(cur[k]))) {
                    onlyIdent = false;
                    break;
                }
            }
            if (!onlyIdent) {
                return cur;
            }
            cur = lowerAscii(cur);
        }
        throw ScriptError(formatLine(file, line, "Recursive #define \"" + ident + "\""), file,
                          line);
    }

    std::size_t skipWsAndComments(const std::string& text, std::size_t n, std::size_t i,
                                  unsigned& line) const {
        while (i < n) {
            const char c = text[i];
            if (isEndOfLine(c)) {
                ++line;
                ++i;
                continue;
            }
            if (isWhiteSpace(static_cast<unsigned char>(c))) {
                ++i;
                continue;
            }
            if (c == '/' && i + 1 < n && text[i + 1] == '/') {
                i += 2;
                while (i < n && !isEndOfLine(text[i])) {
                    ++i;
                }
                continue;
            }
            if (c == '/' && i + 1 < n && text[i + 1] == '*') {
                i += 2;
                while (i + 1 < n && !(text[i] == '*' && text[i + 1] == '/')) {
                    if (isEndOfLine(text[i])) {
                        ++line;
                    }
                    ++i;
                }
                if (i + 1 < n) {
                    i += 2;
                }
                continue;
            }
            break;
        }
        return i;
    }

    std::size_t handleInclude(const std::string& text, std::size_t n, std::size_t i,
                              const std::string& file, unsigned& line,
                              const std::string& baseDir, std::string& out) {
        i = skipWsAndComments(text, n, i, line);
        if (i >= n) {
            throw ScriptError(formatLine(file, line, "Syntax error in #include directive!"),
                              file, line);
        }
        const char open = text[i];
        char close = 0;
        if (open == '<') {
            close = '>';
        } else if (open == '"') {
            close = '"';
        } else {
            throw ScriptError(formatLine(file, line, "Syntax error in #include directive!"),
                              file, line);
        }
        ++i;
        std::string inc;
        while (i < n && text[i] != close) {
            if (isEndOfLine(text[i])) {
                throw ScriptError(formatLine(file, line, "Syntax error in #include directive!"),
                                  file, line);
            }
            inc.push_back(text[i]);
            ++i;
        }
        if (i >= n || text[i] != close || inc.empty()) {
            throw ScriptError(formatLine(file, line, "Syntax error in #include directive!"),
                              file, line);
        }
        ++i;
        // Optional trailing semicolon is ignored (some Remedy files use one).
        i = skipWsAndComments(text, n, i, line);
        if (i < n && text[i] == ';') {
            ++i;
        }

        std::string path = trimCopy(inc);
        for (std::size_t k = 0; k < path.size(); ++k) {
            if (path[k] == '\\') {
                path[k] = '/';
            }
        }
        const std::string resolved = resolveInclude(path, baseDir, file, line);
        if (resolved.empty()) {
            return i;
        }
        const std::string nested = loadFile(resolved);
        out += nested;
        if (!out.empty() && out[out.size() - 1] != '\n') {
            out.push_back('\n');
        }
        return i;
    }

    std::string resolveInclude(const std::string& path, const std::string& baseDir,
                               const std::string& file, unsigned line) {
        std::vector<std::string> candidates;
        if (!path.empty() && (path[0] == '/' ||
#ifdef _WIN32
                              (path.size() >= 2 && path[1] == ':')
#else
                              false
#endif
                                  )) {
            candidates.push_back(nativeSeparators(path));
        } else {
            if (!baseDir.empty()) {
                candidates.push_back(nativeSeparators(joinPath(baseDir, path)));
            }
            candidates.push_back(nativeSeparators(path));
            // Walk parents so <globalh.h> still resolves from data/.
            std::string walk = baseDir;
            const std::string leaf = fileName(path);
            for (int up = 0; up < 6 && !walk.empty(); ++up) {
                candidates.push_back(joinPath(walk, leaf));
                walk = parentDir(walk);
            }
        }
        for (std::size_t c = 0; c < candidates.size(); ++c) {
            if (isFile(candidates[c])) {
                return candidates[c];
            }
        }
        if (!requiredIncludes) {
            return std::string();
        }
        throw ScriptError(formatLine(file, line,
                                     "Can't open include file \"" + path + "\"!"),
                          file, line);
    }

    std::size_t handleDefine(const std::string& text, std::size_t n, std::size_t i,
                             const std::string& file, unsigned& line) {
        i = skipWsAndComments(text, n, i, line);
        if (i >= n ||
            (std::isalpha(static_cast<unsigned char>(text[i])) == 0 && text[i] != '_')) {
            throw ScriptError(formatLine(file, line, "Syntax error in #define directive!"),
                              file, line);
        }
        std::size_t j = i + 1;
        while (j < n && isIdentChar(static_cast<unsigned char>(text[j]))) {
            ++j;
        }
        const std::string name = lowerAscii(text.substr(i, j - i));
        i = j;
        if (i < n && !isWhiteSpace(static_cast<unsigned char>(text[i])) &&
            !isEndOfLine(text[i])) {
            throw ScriptError(formatLine(file, line, "Syntax error in #define directive!"),
                              file, line);
        }
        while (i < n && isWhiteSpace(static_cast<unsigned char>(text[i])) &&
               !isEndOfLine(text[i])) {
            ++i;
        }
        std::string value;
        while (i < n && !isEndOfLine(text[i])) {
            if (text[i] == '/' && i + 1 < n && text[i + 1] == '/') {
                break;
            }
            value.push_back(text[i]);
            ++i;
        }
        value = trimCopy(value);
        if (!value.empty() && value[value.size() - 1] == ';') {
            value.erase(value.size() - 1);
            value = trimCopy(value);
        }
        if (name.empty() || value.empty()) {
            throw ScriptError(formatLine(file, line, "Syntax error in #define directive!"),
                              file, line);
        }
        if (static_cast<int>(name.size()) > kDefineMaxChars ||
            static_cast<int>(value.size()) >= kDefineMaxChars + 1) {
            throw ScriptError(
                formatLine(file, line,
                           std::string("#define can't assign #define's longer than ") +
                               unsignedToString(static_cast<unsigned>(kDefineMaxChars)) +
                               " characters!"),
                file, line);
        }
        defines[name] = value;
        return i;
    }
};

struct Parser {
    const std::string& text;
    const std::string& file;
    std::size_t i;
    unsigned line;

    Parser(const std::string& text_, const std::string& file_)
        : text(text_), file(file_), i(0), line(1) {}

    void skipWs() {
        while (i < text.size()) {
            const char c = text[i];
            if (isEndOfLine(c)) {
                ++line;
                ++i;
                continue;
            }
            if (isWhiteSpace(static_cast<unsigned char>(c))) {
                ++i;
                continue;
            }
            break;
        }
    }

    bool eof() {
        skipWs();
        return i >= text.size();
    }

    char peek() {
        skipWs();
        return i < text.size() ? text[i] : 0;
    }

    void expect(char c, const char* what) {
        skipWs();
        if (i >= text.size() || text[i] != c) {
            throw ScriptError(formatLine(file, line, std::string("expected ") + what), file,
                              line);
        }
        ++i;
    }

    std::string parseTagName() {
        skipWs();
        if (i >= text.size() || text[i] != '[') {
            throw ScriptError(formatLine(file, line, "No opening bracket '[' for tag"), file,
                              line);
        }
        const unsigned startLine = line;
        ++i;
        std::string name;
        while (i < text.size() && text[i] != ']') {
            if (isEndOfLine(text[i])) {
                throw ScriptError(
                    formatLine(file, startLine, "Tag without closing bracket"), file,
                    startLine);
            }
            name.push_back(text[i]);
            ++i;
        }
        if (i >= text.size() || text[i] != ']') {
            throw ScriptError(formatLine(file, startLine, "Tag without closing bracket"),
                              file, startLine);
        }
        ++i;
        name = lowerAscii(trimCopy(name));
        if (name.empty()) {
            throw ScriptError(formatLine(file, startLine, "Empty tag"), file, startLine);
        }
        return name;
    }

    std::string parseIdent(const char* what) {
        skipWs();
        if (i >= text.size() ||
            (std::isalpha(static_cast<unsigned char>(text[i])) == 0 && text[i] != '_')) {
            throw ScriptError(formatLine(file, line, std::string("No ") + what), file, line);
        }
        const std::size_t start = i;
        ++i;
        while (i < text.size() && isIdentChar(static_cast<unsigned char>(text[i]))) {
            ++i;
        }
        return text.substr(start, i - start);
    }

    std::string parseRvalue() {
        skipWs();
        if (i >= text.size() || text[i] == ';') {
            throw ScriptError(formatLine(file, line, "No rvalue"), file, line);
        }
        if (text[i] == '"') {
            const unsigned startLine = line;
            ++i;
            std::string s;
            while (i < text.size() && text[i] != '"') {
                if (isEndOfLine(text[i])) {
                    throw ScriptError(
                        formatLine(file, startLine,
                                   "String marking \"...\" starting doesn't end!"),
                        file, startLine);
                }
                s.push_back(text[i]);
                ++i;
            }
            if (i >= text.size() || text[i] != '"') {
                throw ScriptError(
                    formatLine(file, startLine, "String marking \"...\" starting doesn't end!"),
                    file, startLine);
            }
            ++i;
            skipWs();
            if (i >= text.size() || text[i] != ';') {
                throw ScriptError(formatLine(file, line, "No semicolon"), file, line);
            }
            ++i;
            return s;
        }

        std::string s;
        int paren = 0;
        const unsigned startLine = line;
        while (i < text.size()) {
            const char c = text[i];
            if (c == '"') {
                // Unquoted rvalue shouldn't start a new string in the middle;
                // keep it as data until ';' so sscanf still sees the text.
                s.push_back(c);
                ++i;
                continue;
            }
            if (c == '(') {
                ++paren;
                s.push_back(c);
                ++i;
                continue;
            }
            if (c == ')') {
                if (paren > 0) {
                    --paren;
                }
                s.push_back(c);
                ++i;
                continue;
            }
            if (c == ';' && paren == 0) {
                ++i;
                return trimCopy(s);
            }
            if (isEndOfLine(c) && paren == 0) {
                // Statement must end with ';'. A newline here is an error.
                throw ScriptError(formatLine(file, startLine, "No semicolon"), file,
                                  startLine);
            }
            if (isEndOfLine(c)) {
                ++line;
            }
            s.push_back(c);
            ++i;
        }
        throw ScriptError(formatLine(file, startLine, "No semicolon"), file, startLine);
    }

    void parseContent(ScriptBlock& block) {
        for (;;) {
            skipWs();
            if (i >= text.size()) {
                throw ScriptError(formatLine(file, block.line, "No closing bracket '}'"),
                                  file, block.line);
            }
            if (text[i] == '}') {
                ++i;
                return;
            }
            if (text[i] == '[') {
                parseBlock(block);
                continue;
            }
            if (text[i] == '{') {
                throw ScriptError(formatLine(file, line, "Nesting without tag"), file, line);
            }
            parseAssignment(block);
        }
    }

    void parseAssignment(ScriptBlock& block) {
        const unsigned startLine = line;
        const std::string lvalue = parseIdent("lvalue");
        skipWs();
        if (i < text.size() &&
            (std::isalpha(static_cast<unsigned char>(text[i])) != 0 || text[i] == '_')) {
            throw ScriptError(formatLine(file, startLine, "Lvalue spaces"), file, startLine);
        }
        if (i >= text.size() || text[i] != '=') {
            throw ScriptError(formatLine(file, startLine, "No rvalue"), file, startLine);
        }
        ++i;
        skipWs();
        // Multiple '=' before the semicolon.
        if (i < text.size() && text[i] == '=') {
            throw ScriptError(formatLine(file, startLine, "Multiple assignment"), file,
                              startLine);
        }
        const std::string rvalue = parseRvalue();
        ScriptAssignment a;
        a.lvalue = lowerAscii(lvalue);
        a.rvalue = rvalue;
        a.line = startLine;
        for (std::size_t k = 0; k < block.assignments.size(); ++k) {
            if (block.assignments[k].lvalue == a.lvalue) {
                throw ScriptError(
                    formatLine(file, startLine,
                               "Double variable \"" + a.lvalue + "\""),
                    file, startLine);
            }
        }
        block.assignments.push_back(a);
    }

    void parseBlock(ScriptBlock& parent) {
        const unsigned startLine = line;
        ScriptBlock child;
        child.name = parseTagName();
        child.line = startLine;
        skipWs();
        if (i >= text.size() || text[i] != '{') {
            throw ScriptError(formatLine(file, startLine, "Tag without content"), file,
                              startLine);
        }
        ++i;
        parseContent(child);
        parent.children.push_back(child);
    }

    void parseFile(ScriptBlock& root) {
        root.name = std::string();
        root.line = 1;
        while (!eof()) {
            if (peek() != '[') {
                throw ScriptError(formatLine(file, line, "Data without tag"), file, line);
            }
            parseBlock(root);
        }
    }
};

int childIndexByName(const ScriptBlock& block, const std::string& name, int nth) {
    const std::string key = lowerAscii(name);
    int seen = 0;
    for (std::size_t i = 0; i < block.children.size(); ++i) {
        if (block.children[i].name == key) {
            if (seen == nth) {
                return static_cast<int>(i);
            }
            ++seen;
        }
    }
    return -1;
}

}  // namespace

ScriptError::ScriptError(const std::string& message, const std::string& file, unsigned line)
    : std::runtime_error(message), file_(file), line_(line) {}

bool scriptNamesEqual(const std::string& a, const std::string& b) {
    return lowerAscii(a) == lowerAscii(b);
}

const ScriptBlock& Script::blockAt(const std::vector<int>& path) const {
    const ScriptBlock* b = &root_;
    for (std::size_t i = 0; i < path.size(); ++i) {
        const int idx = path[i];
        if (idx < 0 || idx >= static_cast<int>(b->children.size())) {
            throw ScriptError("R_Script: lock path out of range", sourceName_, b->line);
        }
        b = &b->children[static_cast<std::size_t>(idx)];
    }
    return *b;
}

Script Script::fromExpanded(const std::string& expanded, const std::string& sourceName) {
    Parser parser(expanded, sourceName);
    Script script;
    script.sourceName_ = sourceName;
    parser.parseFile(script.root_);
    return script;
}

Script Script::loadFile(const std::string& path, bool requiredIncludes) {
    Loader loader;
    loader.requiredIncludes = requiredIncludes;
    return fromExpanded(loader.loadFile(path), path);
}

Script Script::parseText(const std::string& text, const std::string& sourceName,
                         const std::string& includeBaseDir) {
    Loader loader;
    return fromExpanded(loader.preprocess(text, sourceName, includeBaseDir), sourceName);
}

int Script::count() const { return static_cast<int>(current().children.size()); }

int Script::count(const std::string& blockName) const {
    const std::string key = lowerAscii(blockName);
    int n = 0;
    const ScriptBlock& cur = current();
    for (std::size_t i = 0; i < cur.children.size(); ++i) {
        if (cur.children[i].name == key) {
            ++n;
        }
    }
    return n;
}

int Script::countLvalues() const {
    return static_cast<int>(current().assignments.size());
}

int Script::countLvalues(const std::string& lvalue) const {
    const std::string key = lowerAscii(lvalue);
    int n = 0;
    const ScriptBlock& cur = current();
    for (std::size_t i = 0; i < cur.assignments.size(); ++i) {
        if (cur.assignments[i].lvalue == key) {
            ++n;
        }
    }
    return n;
}

void Script::lock(int index) {
    if (index < 0 || index >= count()) {
        throw ScriptError("R_Script: Block not found (index " +
                              Loader::unsignedToString(static_cast<unsigned>(index)) + ")",
                          sourceName_, current().line);
    }
    lockPath_.push_back(index);
}

void Script::lock(const std::string& blockName, int nth) {
    const int idx = childIndexByName(current(), blockName, nth);
    if (idx < 0) {
        throw ScriptError("R_Script: Named block \"" + lowerAscii(blockName) +
                              "\" not found",
                          sourceName_, current().line);
    }
    lockPath_.push_back(idx);
}

void Script::unlock() {
    if (lockPath_.empty()) {
        throw ScriptError("R_Script: unlock without lock", sourceName_, current().line);
    }
    lockPath_.pop_back();
}

std::string Script::blockName() const { return current().name; }

const ScriptBlock& Script::current() const { return blockAt(lockPath_); }

std::string Script::assignment(const std::string& lvalue, int nth) const {
    const std::string key = lowerAscii(lvalue);
    int seen = 0;
    const ScriptBlock& cur = current();
    for (std::size_t i = 0; i < cur.assignments.size(); ++i) {
        if (cur.assignments[i].lvalue == key) {
            if (seen == nth) {
                return cur.assignments[i].rvalue;
            }
            ++seen;
        }
    }
    return std::string();
}

bool Script::hasAssignment(const std::string& lvalue) const {
    return countLvalues(lvalue) > 0;
}

std::string Script::staticNesting() const {
    std::string out;
    std::vector<int> path;
    for (std::size_t i = 0; i < lockPath_.size(); ++i) {
        path.push_back(lockPath_[i]);
        const ScriptBlock& b = blockAt(path);
        if (b.name.empty()) {
            continue;
        }
        if (!out.empty()) {
            out += "/";
        }
        out += "[";
        out += b.name;
        out += "]";
    }
    return out;
}

namespace {

void throwParse(const std::string& lvalue, unsigned line, const std::string& file,
                const std::string& rvalue, const char* type) {
    std::ostringstream oss;
    oss << "R_Script: Parsing " << type << " \"" << lvalue << "\" = \"" << rvalue
        << "\" in line " << line << " of \"" << file << "\"";
    throw ScriptError(oss.str(), file, line);
}

std::string stripPercent(const std::string& s) {
    std::string t = trimCopy(s);
    if (!t.empty() && t[t.size() - 1] == '%') {
        t.erase(t.size() - 1);
        t = trimCopy(t);
    }
    return t;
}

bool isTrueWord(const std::string& s) {
    const std::string l = lowerAscii(s);
    return l == "true" || l == "yes" || l == "on";
}

bool isFalseWord(const std::string& s) {
    const std::string l = lowerAscii(s);
    return l == "false" || l == "no" || l == "off";
}

}  // namespace

int parseScriptInt(const std::string& rvalue, const std::string& lvalue, unsigned line,
                   const std::string& file) {
    const std::string t = stripPercent(rvalue);
    if (isTrueWord(t)) {
        return 1;
    }
    if (isFalseWord(t)) {
        return 0;
    }
    char* end = 0;
    const long v = std::strtol(t.c_str(), &end, 0);
    if (end == t.c_str() || (end != 0 && *end != 0)) {
        throwParse(lvalue, line, file, rvalue, "int");
    }
    return static_cast<int>(v);
}

unsigned parseScriptUnsigned(const std::string& rvalue, const std::string& lvalue,
                             unsigned line, const std::string& file) {
    const int v = parseScriptInt(rvalue, lvalue, line, file);
    if (v < 0) {
        throwParse(lvalue, line, file, rvalue, "unsigned");
    }
    return static_cast<unsigned>(v);
}

float parseScriptFloat(const std::string& rvalue, const std::string& lvalue, unsigned line,
                       const std::string& file) {
    const std::string t = stripPercent(rvalue);
    if (isTrueWord(t)) {
        return 1.0f;
    }
    if (isFalseWord(t)) {
        return 0.0f;
    }
    char* end = 0;
    const float v = static_cast<float>(std::strtod(t.c_str(), &end));
    if (end == t.c_str() || (end != 0 && *end != 0)) {
        throwParse(lvalue, line, file, rvalue, "float");
    }
    return v;
}

void parseScriptFloat3(const std::string& rvalue, float out[3], const std::string& lvalue,
                       unsigned line, const std::string& file) {
    // R_Script::t_float3 = "(%f,%f,%f)". Real levels.txt files put spaces
    // around commas (`( 0, GRAVITY_VALUE , 0 )`); a space in the format
    // string makes sscanf accept any amount of whitespace there.
    const std::string t = trimCopy(rvalue);
    float x = 0, y = 0, z = 0;
    int n = std::sscanf(t.c_str(), "(%f,%f,%f)", &x, &y, &z);
    if (n != 3) {
        n = std::sscanf(t.c_str(), "( %f , %f , %f )", &x, &y, &z);
    }
    if (n != 3) {
        throwParse(lvalue, line, file, rvalue, "float3");
    }
    out[0] = x;
    out[1] = y;
    out[2] = z;
}

std::string parseScriptString(const std::string& rvalue) {
    std::string t = rvalue;
    if (t.size() >= 2 && t[0] == '"' && t[t.size() - 1] == '"') {
        t = t.substr(1, t.size() - 2);
    }
    if (t.size() > 255) {
        t.resize(255);
    }
    return t;
}

}  // namespace maxfx
