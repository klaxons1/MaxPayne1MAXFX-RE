// OpenAL Soft mixer: 2D music plus S_SoundOmni 3D cues (A_Play3DSound).
#ifndef MAXFX_VIEWER_AUDIO_H
#define MAXFX_VIEWER_AUDIO_H

#include "maxfx/core/Math.h"
#include "maxfx/db/Database.h"
#include "maxfx/game/Runtime.h"

#include <map>
#include <string>
#include <vector>

namespace maxfx {

class ViewerAudio {
public:
    ViewerAudio();
    ~ViewerAudio();

    bool init();
    void shutdown();

    // Looping level theme (max_payne, else first existing music wav).
    void playLevel(const Database& db, const std::string& worldSphereName);
    // FSM startup A_Play3DSound emitters (Hotspot / FallOff omni).
    void startCues(const Database& db, const std::vector<SoundCueRequest>& cues);
    // One-shot 2D (graphic-novel OnInit A_PlaySound).
    void play2d(const Database& db, const std::string& category, const std::string& name);
    void stop2d();
    void setListener(const Vec3& ldbPos, const Vec3& ldbForward);
    void setEnvPaused(bool on);
    void setMuted(bool on);
    bool muted() const { return muted_; }
    void toggleMuted() { setMuted(!muted_); }

    void pump();

    const char* statusLine() const { return status_.c_str(); }
    int musicCount() const { return musicCount_; }
    int soundCount() const { return soundCount_; }
    int emitterCount() const { return emitterWanted_; }
    int emitterPlaying() const { return emitterHave_; }
    bool playing() const { return !muted_ && (musicSource_ != 0 || !envSources_.empty()); }

private:
    struct Buffer {
        unsigned int id;
        int channels;
        int rate;

        Buffer() : id(0), channels(0), rate(0) {}
    };

    unsigned int loadBuffer(const std::string& path, bool forceMono);
    unsigned int makeSource();
    void stopSource(unsigned int* src);
    void clearEnv();
    void applyMute();
    void refreshStatus();

    void* device_;
    void* context_;
    bool muted_;
    bool envPaused_;
    int musicCount_;
    int soundCount_;
    int emitterWanted_;
    int emitterHave_;
    std::string status_;
    std::string musicName_;
    unsigned int musicSource_;
    unsigned int storySource_;
    std::vector<unsigned int> envSources_;
    std::map<std::string, Buffer> buffers_;
};

}  // namespace maxfx

#endif  // MAXFX_VIEWER_AUDIO_H
