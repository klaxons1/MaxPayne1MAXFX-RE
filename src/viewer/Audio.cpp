#include "viewer/Audio.h"

#include "maxfx/core/Fs.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>

namespace maxfx {
namespace {

const int kRate = 44100;
const int kChannels = 2;

void convertToS16Stereo44100(const WavFile& wav, std::vector<short>* out) {
    out->clear();
    if (wav.empty()) {
        return;
    }
    const int srcFrames = wav.frameCount();
    if (srcFrames <= 0) {
        return;
    }
    const int dstFrames = static_cast<int>((static_cast<long long>(srcFrames) * kRate) / wav.sampleRate);
    if (dstFrames <= 0) {
        return;
    }
    out->resize(static_cast<std::size_t>(dstFrames * kChannels));
    const int srcCh = wav.channels;
    const int srcBytes = wav.bitsPerSample / 8;
    for (int i = 0; i < dstFrames; ++i) {
        const int src = static_cast<int>((static_cast<long long>(i) * wav.sampleRate) / kRate);
        const int clamped = src < 0 ? 0 : (src >= srcFrames ? srcFrames - 1 : src);
        short left = 0;
        short right = 0;
        const unsigned char* p = &wav.pcm[static_cast<std::size_t>(clamped * srcCh * srcBytes)];
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
        (*out)[static_cast<std::size_t>(i * 2 + 0)] = left;
        (*out)[static_cast<std::size_t>(i * 2 + 1)] = right;
    }
}

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
    : stream_(0), muted_(false), musicCount_(0), soundCount_(0), musicPos_(0) {}

ViewerAudio::~ViewerAudio() { shutdown(); }

bool ViewerAudio::init() {
    shutdown();
    SDL_AudioSpec spec;
    std::memset(&spec, 0, sizeof(spec));
    spec.freq = kRate;
    spec.format = SDL_AUDIO_S16;
    spec.channels = kChannels;
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, 0, 0);
    if (stream_ == 0) {
        status_ = std::string("audio off (") + SDL_GetError() + ")";
        return false;
    }
    SDL_ResumeAudioStreamDevice(stream_);
    status_ = "audio on";
    return true;
}

void ViewerAudio::shutdown() {
    if (stream_) {
        SDL_DestroyAudioStream(stream_);
        stream_ = 0;
    }
    musicPcm_.clear();
    musicPos_ = 0;
}

void ViewerAudio::setMuted(bool on) {
    muted_ = on;
    if (muted_ && stream_) {
        SDL_ClearAudioStream(stream_);
    }
}

bool ViewerAudio::loadMusicPcm(const std::string& path) {
    musicPcm_.clear();
    musicPos_ = 0;
    WavFile wav;
    std::string err;
    if (!loadWavFile(path, wav, &err)) {
        return false;
    }
    if (wavIsSilence(wav)) {
        return false;
    }
    convertToS16Stereo44100(wav, &musicPcm_);
    return !musicPcm_.empty();
}

void ViewerAudio::playLevel(const Database& db, const std::string& worldSphereName) {
    musicCount_ = static_cast<int>(db.music.size());
    soundCount_ = static_cast<int>(db.sounds.size());
    musicPcm_.clear();
    musicPos_ = 0;
    musicName_.clear();
    if (stream_) {
        SDL_ClearAudioStream(stream_);
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
    for (std::size_t i = 0; i < db.sounds.size(); ++i) {
        if (db.sounds[i].looping) {
            order.push_back(&db.sounds[i]);
        }
    }

    for (std::size_t i = 0; i < order.size(); ++i) {
        const SoundDef* def = order[i];
        if (def == 0 || def->resolvedPath.empty() || !isFile(def->resolvedPath)) {
            continue;
        }
        if (loadMusicPcm(def->resolvedPath)) {
            musicName_ = def->name;
            break;
        }
    }

    if (musicPcm_.empty()) {
        if (stream_ == 0) {
            status_ = "audio off";
        } else if (musicCount_ == 0 && soundCount_ == 0) {
            status_ = "audio on  (no sound scripts)";
        } else {
            status_ = "audio on  (wavs not extracted)";
        }
        return;
    }
    char line[256];
    std::snprintf(line, sizeof(line), "audio on  music %s  cues %d", musicName_.c_str(), soundCount_);
    status_ = line;
}

void ViewerAudio::mixFrames(short* dst, int frames) {
    const std::size_t n = static_cast<std::size_t>(frames * kChannels);
    if (muted_ || musicPcm_.empty()) {
        std::memset(dst, 0, n * sizeof(short));
        return;
    }
    const int loop = static_cast<int>(musicPcm_.size() / kChannels);
    if (loop <= 0) {
        std::memset(dst, 0, n * sizeof(short));
        return;
    }
    for (int i = 0; i < frames; ++i) {
        if (musicPos_ >= loop) {
            musicPos_ = 0;
        }
        dst[i * 2 + 0] = musicPcm_[static_cast<std::size_t>(musicPos_ * 2 + 0)];
        dst[i * 2 + 1] = musicPcm_[static_cast<std::size_t>(musicPos_ * 2 + 1)];
        ++musicPos_;
    }
}

void ViewerAudio::pump() {
    if (stream_ == 0) {
        return;
    }
    const int queued = SDL_GetAudioStreamQueued(stream_);
    const int target = kRate * kChannels * static_cast<int>(sizeof(short)) / 4;  // 250 ms
    if (queued >= target) {
        return;
    }
    short buf[2048 * 2];
    const int frames = 2048;
    mixFrames(buf, frames);
    SDL_PutAudioStreamData(stream_, buf, static_cast<int>(sizeof(buf)));
}

}  // namespace maxfx
