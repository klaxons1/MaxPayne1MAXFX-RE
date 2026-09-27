#include "viewer/Audio.h"

#include "maxfx/core/Fs.h"
#include "maxfx/sound/Sound.h"

#include <AL/al.h>
#include <AL/alc.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace maxfx {
namespace {

void wavToS16(const WavFile& wav, bool forceMono, std::vector<short>* pcm, int* channels) {
    pcm->clear();
    *channels = 0;
    if (wav.empty()) {
        return;
    }
    const int frames = wav.frameCount();
    const int srcCh = wav.channels;
    const int srcBytes = wav.bitsPerSample / 8;
    const bool toMono = forceMono || srcCh == 1;
    *channels = toMono ? 1 : 2;
    pcm->resize(static_cast<std::size_t>(frames * (*channels)));
    for (int i = 0; i < frames; ++i) {
        const unsigned char* p = &wav.pcm[static_cast<std::size_t>(i * srcCh * srcBytes)];
        short left = 0;
        short right = 0;
        if (wav.bitsPerSample == 8) {
            left = static_cast<short>((static_cast<int>(p[0]) - 128) << 8);
            right = srcCh > 1 ? static_cast<short>((static_cast<int>(p[1]) - 128) << 8) : left;
        } else {
            left = static_cast<short>(p[0] | (p[1] << 8));
            if (srcCh > 1) {
                right = static_cast<short>(p[2] | (p[3] << 8));
            } else {
                right = left;
            }
        }
        if (toMono) {
            (*pcm)[static_cast<std::size_t>(i)] = static_cast<short>((static_cast<int>(left) + right) / 2);
        } else {
            (*pcm)[static_cast<std::size_t>(i * 2 + 0)] = left;
            (*pcm)[static_cast<std::size_t>(i * 2 + 1)] = right;
        }
    }
}

ALCdevice* asDevice(void* p) { return static_cast<ALCdevice*>(p); }
ALCcontext* asContext(void* p) { return static_cast<ALCcontext*>(p); }

bool wavIsSilence(const WavFile& wav) {
    if (wav.empty()) {
        return true;
    }
    if (wav.bitsPerSample == 8) {
        for (std::size_t i = 0; i < wav.pcm.size(); ++i) {
            const int d = static_cast<int>(wav.pcm[i]) - 128;
            if (d < -2 || d > 2) {
                return false;
            }
        }
        return true;
    }
    for (std::size_t i = 0; i + 1 < wav.pcm.size(); i += 2) {
        const int s = static_cast<short>(wav.pcm[i] | (wav.pcm[i + 1] << 8));
        if (s < -16 || s > 16) {
            return false;
        }
    }
    return true;
}

}  // namespace

ViewerAudio::ViewerAudio()
    : device_(0),
      context_(0),
      muted_(false),
      envPaused_(false),
      musicCount_(0),
      soundCount_(0),
      emitterWanted_(0),
      emitterHave_(0),
      musicSource_(0),
      storySource_(0) {}

ViewerAudio::~ViewerAudio() { shutdown(); }

bool ViewerAudio::init() {
    shutdown();
    device_ = alcOpenDevice(0);
    if (device_ == 0) {
        status_ = "openal off (no device)";
        return false;
    }
    context_ = alcCreateContext(asDevice(device_), 0);
    if (context_ == 0 || alcMakeContextCurrent(asContext(context_)) == ALC_FALSE) {
        status_ = "openal off (no context)";
        if (context_) {
            alcDestroyContext(asContext(context_));
            context_ = 0;
        }
        alcCloseDevice(asDevice(device_));
        device_ = 0;
        return false;
    }
    alDistanceModel(AL_LINEAR_DISTANCE_CLAMPED);
    alListenerf(AL_GAIN, 1.0f);
    status_ = "openal on";
    return true;
}

void ViewerAudio::shutdown() {
    if (context_) {
        alcMakeContextCurrent(asContext(context_));
        stopSource(&musicSource_);
        stopSource(&storySource_);
        for (std::size_t i = 0; i < oneShotSources_.size(); ++i) {
            stopSource(&oneShotSources_[i]);
        }
        oneShotSources_.clear();
        clearEnv();
        for (std::map<std::string, Buffer>::iterator it = buffers_.begin(); it != buffers_.end(); ++it) {
            if (it->second.id) {
                alDeleteBuffers(1, &it->second.id);
            }
        }
        buffers_.clear();
        alcMakeContextCurrent(0);
        alcDestroyContext(asContext(context_));
        context_ = 0;
    }
    if (device_) {
        alcCloseDevice(asDevice(device_));
        device_ = 0;
    }
    musicName_.clear();
    status_ = "openal off";
}

void ViewerAudio::stopSource(unsigned int* src) {
    if (src == 0 || *src == 0) {
        return;
    }
    alSourceStop(*src);
    alDeleteSources(1, src);
    *src = 0;
}

void ViewerAudio::clearEnv() {
    for (std::size_t i = 0; i < envSources_.size(); ++i) {
        if (envSources_[i]) {
            alSourceStop(envSources_[i]);
            alDeleteSources(1, &envSources_[i]);
        }
    }
    envSources_.clear();
    emitterHave_ = 0;
}

unsigned int ViewerAudio::makeSource() {
    unsigned int src = 0;
    alGenSources(1, &src);
    if (alGetError() != AL_NO_ERROR) {
        return 0;
    }
    return src;
}

unsigned int ViewerAudio::loadBuffer(const std::string& path, bool forceMono, int* rateOut) {
    if (rateOut) {
        *rateOut = 0;
    }
    if (path.empty() || context_ == 0) {
        return 0;
    }
    char key[1024];
    std::snprintf(key, sizeof(key), "%s|%d", path.c_str(), forceMono ? 1 : 0);
    std::map<std::string, Buffer>::iterator it = buffers_.find(key);
    if (it != buffers_.end()) {
        if (rateOut) {
            *rateOut = it->second.rate;
        }
        return it->second.id;
    }
    WavFile wav;
    std::string err;
    if (!loadWavFile(path, wav, &err) || wavIsSilence(wav)) {
        return 0;
    }
    std::vector<short> pcm;
    int channels = 0;
    wavToS16(wav, forceMono, &pcm, &channels);
    if (pcm.empty()) {
        return 0;
    }
    Buffer b;
    alGenBuffers(1, &b.id);
    if (alGetError() != AL_NO_ERROR || b.id == 0) {
        return 0;
    }
    const ALenum fmt = channels == 1 ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
    alBufferData(b.id, fmt, &pcm[0], static_cast<ALsizei>(pcm.size() * sizeof(short)), wav.sampleRate);
    if (alGetError() != AL_NO_ERROR) {
        alDeleteBuffers(1, &b.id);
        return 0;
    }
    b.channels = channels;
    b.rate = wav.sampleRate;
    buffers_[key] = b;
    if (rateOut) {
        *rateOut = b.rate;
    }
    return b.id;
}

void ViewerAudio::applyMute() {
    if (context_ == 0) {
        return;
    }
    alListenerf(AL_GAIN, muted_ ? 0.0f : 1.0f);
}

void ViewerAudio::setMuted(bool on) {
    muted_ = on;
    applyMute();
}

void ViewerAudio::setEnvPaused(bool on) {
    envPaused_ = on;
    for (std::size_t i = 0; i < oneShotSources_.size(); ++i) {
        if (oneShotSources_[i] == 0) {
            continue;
        }
        if (on) {
            alSourcePause(oneShotSources_[i]);
        } else {
            // Resume only what this loop actually paused. alSourcePlay on a
            // source that is still (or already finished and not yet reaped)
            // playing RESTARTS it from the beginning; update() calls this
            // every frame, which used to slice every one-shot into a 60 Hz
            // restart buzz.
            ALint state = AL_STOPPED;
            alGetSourcei(oneShotSources_[i], AL_SOURCE_STATE, &state);
            if (state == AL_PAUSED) {
                alSourcePlay(oneShotSources_[i]);
            }
        }
    }
    if (context_ == 0) {
        return;
    }
    for (std::size_t i = 0; i < envSources_.size(); ++i) {
        if (envSources_[i] == 0) {
            continue;
        }
        if (envPaused_) {
            alSourcePause(envSources_[i]);
        } else {
            ALint state = AL_STOPPED;
            alGetSourcei(envSources_[i], AL_SOURCE_STATE, &state);
            if (state != AL_PLAYING) {
                alSourcePlay(envSources_[i]);
            }
        }
    }
}

void ViewerAudio::setListener(const Vec3& ldbPos, const Vec3& ldbForward) {
    if (context_ == 0) {
        return;
    }
    alListener3f(AL_POSITION, ldbPos.x, ldbPos.y, ldbPos.z);
    Vec3 fwd = ldbForward;
    if (length(fwd) < 1.0e-5f) {
        fwd = Vec3(0.0f, 0.0f, 1.0f);
    } else {
        fwd = normalize(fwd);
    }
    const float ori[6] = {fwd.x, fwd.y, fwd.z, 0.0f, 1.0f, 0.0f};
    alListenerfv(AL_ORIENTATION, ori);
    alListener3f(AL_VELOCITY, 0.0f, 0.0f, 0.0f);
}

void ViewerAudio::playLevel(const Database& db, const std::string& worldSphereName) {
    (void)worldSphereName;
    // No auto music: the original engine starts level music only from
    // scripted A_PlayMusic cues (level FSM messages), and looping a random
    // track over gameplay sounded wrong. Only count for the status line.
    musicCount_ = static_cast<int>(db.music.size());
    soundCount_ = static_cast<int>(db.sounds.size());
    refreshStatus();
}

void ViewerAudio::playMusic(const Database& db, const std::string& name) {
    stopSource(&musicSource_);
    musicName_.clear();
    if (context_ == 0 || name.empty() || name == "empty") {
        refreshStatus();
        return;
    }
    const SoundDef* def = db.findMusic(name);
    if (def == 0) {
        refreshStatus();
        return;
    }
    int rate = 0;
    const unsigned int buf = loadBuffer(def->resolvedPath, false, &rate);
    if (buf == 0) {
        refreshStatus();
        return;
    }
    musicSource_ = makeSource();
    if (musicSource_ == 0) {
        refreshStatus();
        return;
    }
    alSourcei(musicSource_, AL_BUFFER, static_cast<ALint>(buf));
    alSourcei(musicSource_, AL_LOOPING, def->looping ? AL_TRUE : AL_TRUE);
    alSourcei(musicSource_, AL_SOURCE_RELATIVE, AL_TRUE);
    alSource3f(musicSource_, AL_POSITION, 0.0f, 0.0f, 0.0f);
    alSourcef(musicSource_, AL_ROLLOFF_FACTOR, 0.0f);
    alSourcef(musicSource_, AL_GAIN, def->volume > 0.01f ? def->volume : 1.0f);
    alSourcef(musicSource_, AL_PITCH, soundPitchMultiplier(def->pitch, rate));
    alSourcePlay(musicSource_);
    musicName_ = def->name;
    refreshStatus();
}

void ViewerAudio::stopMusic() {
    stopSource(&musicSource_);
    musicName_.clear();
    refreshStatus();
}

void ViewerAudio::playOneShot(const Database& db, const SoundRequest& q) {
    if (context_ == 0 || q.name.empty() || q.name == "empty") {
        return;
    }
    // World one-shots are dropped while the reader / menu is up. Check before
    // creating the source: the old late return leaked an OpenAL source per
    // dropped request.
    if (envPaused_ && q.is3d) {
        return;
    }
    const SoundDef* def = db.findSound(q.category, q.name);
    if (def == 0) {
        def = db.findSound(q.name);
    }
    if (def == 0) {
        return;
    }
    // Random variation: pick one of the [Random] files (or the base file).
    std::string path = def->resolvedPath;
    if (!def->randomPaths.empty()) {
        const std::size_t pick =
            static_cast<std::size_t>(static_cast<double>(std::rand()) / (static_cast<double>(RAND_MAX) + 1.0) *
                                     static_cast<double>(def->randomPaths.size() + 1));
        if (pick < def->randomPaths.size()) {
            path = def->randomPaths[pick];
        }
    }
    int rate = 0;
    const unsigned int buf = loadBuffer(path, q.is3d, &rate);
    if (buf == 0) {
        return;
    }
    const unsigned int src = makeSource();
    if (src == 0) {
        return;
    }
    alSourcei(src, AL_BUFFER, static_cast<ALint>(buf));
    // Message one-shots always play to the end: the engine pairs looping
    // emitters with explicit stop messages we do not model, so honouring
    // `Looping` here stacks an endless voice per repeated request.
    alSourcei(src, AL_LOOPING, AL_FALSE);
    float vol = def->volume > 0.01f ? def->volume : 1.0f;
    if (vol > 1.0f) {
        vol = 1.0f;
    }
    alSourcef(src, AL_GAIN, vol);
    alSourcef(src, AL_PITCH, soundPitchMultiplier(def->pitch, rate));
    if (q.is3d && !envPaused_) {
        alSourcei(src, AL_SOURCE_RELATIVE, AL_FALSE);
        alSource3f(src, AL_POSITION, q.position.x, q.position.y, q.position.z);
        float hot = def->hotspot > 0.01f ? def->hotspot : 1.0f;
        float fall = def->falloff > hot ? def->falloff : hot + 0.01f;
        alSourcef(src, AL_REFERENCE_DISTANCE, hot);
        alSourcef(src, AL_MAX_DISTANCE, fall);
        alSourcef(src, AL_ROLLOFF_FACTOR, 1.0f);
    } else {
        alSourcei(src, AL_SOURCE_RELATIVE, AL_TRUE);
        alSource3f(src, AL_POSITION, 0.0f, 0.0f, 0.0f);
        alSourcef(src, AL_ROLLOFF_FACTOR, 0.0f);
    }
    alSourcePlay(src);
    oneShotSources_.push_back(src);
    // Cap concurrent one-shots (drop the oldest).
    if (oneShotSources_.size() > 32) {
        stopSource(&oneShotSources_[0]);
        oneShotSources_.erase(oneShotSources_.begin());
    }
}

void ViewerAudio::playRequests(const Database& db, const std::vector<SoundRequest>& requests) {
    for (std::size_t i = 0; i < requests.size(); ++i) {
        if (requests[i].music) {
            if (requests[i].stopMusic) {
                stopMusic();
            } else {
                playMusic(db, requests[i].name);
            }
            continue;
        }
        if (requests[i].stopMusic) {
            stopMusic();
            continue;
        }
        playOneShot(db, requests[i]);
    }
}

void ViewerAudio::startCues(const Database& db, const std::vector<SoundCueRequest>& cues) {
    if (context_ == 0) {
        emitterWanted_ = static_cast<int>(cues.size());
        emitterHave_ = 0;
        refreshStatus();
        return;
    }
    clearEnv();
    emitterWanted_ = static_cast<int>(cues.size());
    for (std::size_t i = 0; i < cues.size(); ++i) {
        const SoundCueRequest& q = cues[i];
        const SoundDef* def = db.findSound(q.category, q.name);
        if (def == 0) {
            def = db.findSound(q.name);
        }
        if (def == 0 || def->resolvedPath.empty() || !isFile(def->resolvedPath)) {
            continue;
        }
        const bool want3d = q.is3d || def->is3d;
        int rate = 0;
        const unsigned int buf = loadBuffer(def->resolvedPath, want3d, &rate);
        if (buf == 0) {
            continue;
        }
        const unsigned int src = makeSource();
        if (src == 0) {
            continue;
        }
        alSourcei(src, AL_BUFFER, static_cast<ALint>(buf));
        alSourcei(src, AL_LOOPING, def->looping ? AL_TRUE : AL_FALSE);
        float vol = def->volume > 0.01f ? def->volume : 1.0f;
        if (vol > 1.0f) {
            vol = 1.0f;
        }
        alSourcef(src, AL_GAIN, vol);
        alSourcef(src, AL_PITCH, soundPitchMultiplier(def->pitch, rate));
        if (want3d) {
            alSourcei(src, AL_SOURCE_RELATIVE, AL_FALSE);
            alSource3f(src, AL_POSITION, q.origin.x, q.origin.y, q.origin.z);
            float hot = def->hotspot > 0.01f ? def->hotspot : 1.0f;
            float fall = def->falloff > hot ? def->falloff : hot + 0.01f;
            alSourcef(src, AL_REFERENCE_DISTANCE, hot);
            alSourcef(src, AL_MAX_DISTANCE, fall);
            alSourcef(src, AL_ROLLOFF_FACTOR, 1.0f);
        } else {
            alSourcei(src, AL_SOURCE_RELATIVE, AL_TRUE);
            alSource3f(src, AL_POSITION, 0.0f, 0.0f, 0.0f);
            alSourcef(src, AL_ROLLOFF_FACTOR, 0.0f);
        }
        if (!envPaused_) {
            alSourcePlay(src);
        }
        envSources_.push_back(src);
        ++emitterHave_;
    }
    refreshStatus();
}

void ViewerAudio::play2d(const Database& db, const std::string& category, const std::string& name) {
    stop2d();
    if (context_ == 0 || name.empty() || name == "empty") {
        return;
    }
    const SoundDef* def = db.findSound(category, name);
    if (def == 0) {
        def = db.findSound(name);
    }
    if (def == 0 || def->resolvedPath.empty() || !isFile(def->resolvedPath)) {
        return;
    }
    int rate = 0;
    const unsigned int buf = loadBuffer(def->resolvedPath, false, &rate);
    if (buf == 0) {
        return;
    }
    storySource_ = makeSource();
    if (storySource_ == 0) {
        return;
    }
    alSourcei(storySource_, AL_BUFFER, static_cast<ALint>(buf));
    alSourcei(storySource_, AL_LOOPING, AL_FALSE);
    alSourcei(storySource_, AL_SOURCE_RELATIVE, AL_TRUE);
    alSource3f(storySource_, AL_POSITION, 0.0f, 0.0f, 0.0f);
    alSourcef(storySource_, AL_ROLLOFF_FACTOR, 0.0f);
    alSourcef(storySource_, AL_GAIN, def->volume > 0.01f ? def->volume : 1.0f);
    alSourcef(storySource_, AL_PITCH, soundPitchMultiplier(def->pitch, rate));
    alSourcePlay(storySource_);
    if (musicSource_) {
        alSourcef(musicSource_, AL_GAIN, 0.2f);
    }
}

void ViewerAudio::stop2d() {
    stopSource(&storySource_);
    if (musicSource_ && context_) {
        alSourcef(musicSource_, AL_GAIN, 1.0f);
    }
}

void ViewerAudio::pump() {
    if (context_ == 0) {
        return;
    }
    for (std::size_t i = 0; i < oneShotSources_.size();) {
        ALint state = AL_PLAYING;
        alGetSourcei(oneShotSources_[i], AL_SOURCE_STATE, &state);
        if (state != AL_PLAYING && state != AL_PAUSED) {
            stopSource(&oneShotSources_[i]);
            oneShotSources_.erase(oneShotSources_.begin() + static_cast<long>(i));
            continue;
        }
        ++i;
    }
    if (storySource_) {
        ALint state = AL_STOPPED;
        alGetSourcei(storySource_, AL_SOURCE_STATE, &state);
        if (state != AL_PLAYING && state != AL_PAUSED) {
            stop2d();
        }
    }
}

void ViewerAudio::refreshStatus() {
    if (context_ == 0) {
        status_ = "openal off";
        return;
    }
    char line[256];
    if (musicName_.empty() && emitterHave_ == 0) {
        if (musicCount_ == 0 && soundCount_ == 0) {
            std::snprintf(line, sizeof(line), "openal on  (no sound scripts)");
        } else {
            std::snprintf(line, sizeof(line), "openal on  3d 0/%d  (wavs not extracted)", emitterWanted_);
        }
    } else if (musicName_.empty()) {
        std::snprintf(line, sizeof(line), "openal on  3d %d/%d", emitterHave_, emitterWanted_);
    } else {
        std::snprintf(line, sizeof(line), "openal on  music %s  3d %d/%d", musicName_.c_str(),
                      emitterHave_, emitterWanted_);
    }
    status_ = line;
}

}  // namespace maxfx
