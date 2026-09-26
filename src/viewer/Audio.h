// SDL3 software mixer for database music / WAV cues.
#ifndef MAXFX_VIEWER_AUDIO_H
#define MAXFX_VIEWER_AUDIO_H

#include "maxfx/db/Database.h"
#include "maxfx/sound/Sound.h"

#include <string>
#include <vector>

struct SDL_AudioStream;

namespace maxfx {

class ViewerAudio {
public:
    ViewerAudio();
    ~ViewerAudio();

    bool init();
    void shutdown();

    // Pick looping music for this level (max_payne, else first existing wav).
    void playLevel(const Database& db, const std::string& worldSphereName);
    void setMuted(bool on);
    bool muted() const { return muted_; }
    void toggleMuted() { setMuted(!muted_); }

    // Queue mixed PCM into the SDL stream. Call once per frame.
    void pump();

    const char* statusLine() const { return status_.c_str(); }
    int musicCount() const { return musicCount_; }
    int soundCount() const { return soundCount_; }
    bool playing() const { return !muted_ && !musicPcm_.empty(); }

private:
    void mixFrames(short* dst, int frames);
    bool loadMusicPcm(const std::string& path);

    SDL_AudioStream* stream_;
    bool muted_;
    int musicCount_;
    int soundCount_;
    std::string status_;
    std::string musicName_;
    std::vector<short> musicPcm_;  // interleaved s16 stereo 44100
    int musicPos_;
};

}  // namespace maxfx

#endif  // MAXFX_VIEWER_AUDIO_H
