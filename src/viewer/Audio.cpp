#include "viewer/Audio.h"

#include "maxfx/core/Fs.h"
#include "maxfx/sound/Sound.h"

#include <AL/al.h>
#include <AL/alc.h>

#include <cstdio>
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

unsigned int ViewerAudio::loadBuffer(const std::string& path, bool forceMono) {
    if (path.empty() || context_ == 0) {
        return 0;
    }
    char key[1024];
    std::snprintf(key, sizeof(key), "%s|%d", path.c_str(), forceMono ? 1 : 0);
    std::map<std::string, Buffer>::iterator it = buffers_.find(key);
    if (it != buffers_.end()) {
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
    musicCount_ = static_cast<int>(db.music.size());
    soundCount_ = static_cast<int>(db.sounds.size());
    stopSource(&musicSource_);
    musicName_.clear();
    if (context_ == 0) {
        refreshStatus();
        return;
    }

    std::vector<const SoundDef*> order;
    const SoundDef* bySphere = db.findMusic(worldSphereName);
    const SoundDef* theme = db.findMusic("max_payne");
    if (bySphere) {
        order.push_back(bySphere);
    }
    if (theme) {
        order.push_back(theme);
    }
    for (std::size_t i = 0; i < db.music.size(); ++i) {
        order.push_back(&db.music[i]);
    }

    for (std::size_t i = 0; i < order.size(); ++i) {
        const SoundDef* def = order[i];
        if (def == 0 || def->resolvedPath.empty() || !isFile(def->resolvedPath)) {
            continue;
        }
        const unsigned int buf = loadBuffer(def->resolvedPath, false);
        if (buf == 0) {
            continue;
        }
        musicSource_ = makeSource();
        if (musicSource_ == 0) {
            break;
        }
        alSourcei(musicSource_, AL_BUFFER, static_cast<ALint>(buf));
        alSourcei(musicSource_, AL_LOOPING, AL_TRUE);
        alSourcei(musicSource_, AL_SOURCE_RELATIVE, AL_TRUE);
        alSource3f(musicSource_, AL_POSITION, 0.0f, 0.0f, 0.0f);
        alSourcef(musicSource_, AL_ROLLOFF_FACTOR, 0.0f);
        alSourcef(musicSource_, AL_GAIN, def->volume > 0.01f ? def->volume : 1.0f);
        if (def->pitch > 0 && def->pitch != 22050) {
            alSourcef(musicSource_, AL_PITCH, static_cast<float>(def->pitch) / 22050.0f);
        }
        alSourcePlay(musicSource_);
        musicName_ = def->name;
        break;
    }
    refreshStatus();
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
        const unsigned int buf = loadBuffer(def->resolvedPath, want3d);
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
        if (def->pitch > 0 && def->pitch != 22050) {
            alSourcef(src, AL_PITCH, static_cast<float>(def->pitch) / 22050.0f);
        }
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
    const unsigned int buf = loadBuffer(def->resolvedPath, false);
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
    if (def->pitch > 0 && def->pitch != 22050) {
        alSourcef(storySource_, AL_PITCH, static_cast<float>(def->pitch) / 22050.0f);
    }
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
