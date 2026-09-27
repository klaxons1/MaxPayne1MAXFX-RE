// Camera paths from data/database/camerapaths/camerapaths.txt (PC layout).
//
// In-engine cutscenes fly the camera along a keyframed path while scripted
// clips act the characters. From the Android decompile
// (X_SharedDBCameraPath::construct) and the shipped file:
//
//   [path_name]
//   {
//     [Attributes]
//     {
//       FadeInTime = 0.5;
//       FadeOutTime = 0.5;
//       Abortable = TRUE;
//     }
//     [AnimationSet]
//     {
//       [Animation] Filename = "..\..\camerapaths\foo.kf2";
//     }
//     [Exit]
//     {
//       [Message] String = "this->FSM_Send( ... );";
//     }
//   }
//
// CAM_AnimateAbsolute(blockName, roomName) plays the path in world space,
// CAM_AnimateInPlace / Parented / PlayerParented parent it to the camera /
// triggering character / player. [Exit] messages run when the path finishes.
#ifndef MAXFX_GAME_CAMERAPATHS_H
#define MAXFX_GAME_CAMERAPATHS_H

#include <string>
#include <vector>

namespace maxfx {

struct CameraPathDef {
    std::string name;
    float fadeInTime;
    float fadeOutTime;
    bool abortable;
    std::string animationFile;      // [Animation] Filename (script form)
    std::string resolvedAnimation;  // resolved path ("" when missing)
    std::vector<std::string> exitMessages;  // [Exit] [Message] String list

    CameraPathDef() : fadeInTime(0.0f), fadeOutTime(0.0f), abortable(false) {}
};

class CameraPathCatalog {
public:
    // Loads dbRoot/camerapaths/camerapaths.txt (falls back to
    // dbRoot/camerapaths.txt). An empty catalog is returned when the file is
    // missing; parse errors skip the broken block.
    void load(const std::string& dbRoot);

    bool empty() const { return paths_.empty(); }
    std::size_t size() const { return paths_.size(); }
    const CameraPathDef* find(const std::string& name) const;
    const std::vector<CameraPathDef>& paths() const { return paths_; }

private:
    std::vector<CameraPathDef> paths_;
};

}  // namespace maxfx

#endif  // MAXFX_GAME_CAMERAPATHS_H
