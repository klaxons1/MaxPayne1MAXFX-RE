#include "maxfx/game/Message.h"

#include "maxfx/core/Fs.h"

namespace maxfx {
namespace {

std::string trimCopy(const std::string& s) {
    std::size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\n' || s[a] == '\r')) {
        ++a;
    }
    std::size_t b = s.size();
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\n' || s[b - 1] == '\r')) {
        --b;
    }
    return s.substr(a, b - a);
}

std::string stripQuotes(const std::string& s) {
    const std::string t = trimCopy(s);
    if (t.size() >= 2 && ((t[0] == '"' && t[t.size() - 1] == '"') ||
                          (t[0] == '\'' && t[t.size() - 1] == '\''))) {
        return t.substr(1, t.size() - 2);
    }
    return t;
}

void splitStatements(const std::string& text, std::vector<std::string>* out) {
    std::string cur;
    bool inQuote = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '"') {
            inQuote = !inQuote;
            cur.push_back(c);
            continue;
        }
        if (!inQuote && c == ';') {
            const std::string t = trimCopy(cur);
            if (!t.empty()) {
                out->push_back(t);
            }
            cur.clear();
            continue;
        }
        cur.push_back(c);
    }
    const std::string t = trimCopy(cur);
    if (!t.empty()) {
        out->push_back(t);
    }
}

}  // namespace

std::vector<GameMessage> parseGameMessages(const std::string& text) {
    std::vector<GameMessage> out;
    std::vector<std::string> stmts;
    splitStatements(text, &stmts);
    for (std::size_t s = 0; s < stmts.size(); ++s) {
        const std::string& st = stmts[s];
        const std::size_t arrow = st.find("->");
        if (arrow == std::string::npos) {
            continue;
        }
        GameMessage m;
        m.raw = st;
        m.target = lowerCopy(trimCopy(st.substr(0, arrow)));
        std::string rest = trimCopy(st.substr(arrow + 2));
        std::size_t par = rest.find('(');
        if (par == std::string::npos) {
            m.method = lowerCopy(rest);
            out.push_back(m);
            continue;
        }
        m.method = lowerCopy(trimCopy(rest.substr(0, par)));
        std::string inside = rest.substr(par + 1);
        if (!inside.empty() && inside[inside.size() - 1] == ')') {
            inside = inside.substr(0, inside.size() - 1);
        }
        std::string arg;
        bool inQuote = false;
        for (std::size_t i = 0; i < inside.size(); ++i) {
            const char c = inside[i];
            if (c == '"') {
                inQuote = !inQuote;
                arg.push_back(c);
                continue;
            }
            if (!inQuote && c == ',') {
                const std::string a = stripQuotes(arg);
                if (!a.empty()) {
                    m.args.push_back(a);
                }
                arg.clear();
                continue;
            }
            arg.push_back(c);
        }
        const std::string a = stripQuotes(arg);
        if (!a.empty()) {
            m.args.push_back(a);
        }
        out.push_back(m);
    }
    return out;
}

}  // namespace maxfx
