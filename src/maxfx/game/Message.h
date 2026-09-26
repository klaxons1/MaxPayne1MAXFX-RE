// Parse MAX-FX FSM / OnInit message strings from the PC build.
//
// Official form (X_Message::parse in MP.exe):
//   target->Method( arg0, arg1, ... );
// Target is `this`, `maxpayne_gamemode`, `maxpayne_graphicnovelmode`,
// or a named entity. Method names are the C_/A_/DO_/T_/GM_/MPGNM_/LI_
// table dumped from the PC executable.
#ifndef MAXFX_GAME_MESSAGE_H
#define MAXFX_GAME_MESSAGE_H

#include <string>
#include <vector>

namespace maxfx {

struct GameMessage {
    std::string target;  // lower-cased
    std::string method;  // lower-cased, without the class prefix split
    std::vector<std::string> args;
    std::string raw;
};

// Empty vector if `text` has no `->`. Multiple statements split on `;`.
std::vector<GameMessage> parseGameMessages(const std::string& text);

inline bool methodIs(const GameMessage& m, const char* name) {
    // name is lower-case ASCII, e.g. "c_jump"
    const char* a = name;
    const std::string& b = m.method;
    std::size_t i = 0;
    for (; a[i] != 0 && i < b.size(); ++i) {
        if (a[i] != b[i]) {
            return false;
        }
    }
    return a[i] == 0 && i == b.size();
}

}  // namespace maxfx

#endif  // MAXFX_GAME_MESSAGE_H
