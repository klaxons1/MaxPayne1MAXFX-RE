// PCM WAV loader used by the database sound catalog and the viewer mixer.
// Official Max Payne 1 banks are 8- or 16-bit PCM (no ADPCM in the PC RAS).
#ifndef MAXFX_SOUND_SOUND_H
#define MAXFX_SOUND_SOUND_H

#include <string>
#include <vector>

namespace maxfx {

struct WavFile {
    int sampleRate;
    int channels;
    int bitsPerSample;
    std::vector<unsigned char> pcm;  // interleaved native-endian samples

    WavFile() : sampleRate(0), channels(0), bitsPerSample(0) {}

    bool empty() const { return pcm.empty() || sampleRate <= 0 || channels <= 0; }
    int frameCount() const {
        if (channels <= 0 || bitsPerSample <= 0) {
            return 0;
        }
        const int bytesPerFrame = channels * (bitsPerSample / 8);
        if (bytesPerFrame <= 0) {
            return 0;
        }
        return static_cast<int>(pcm.size() / static_cast<unsigned>(bytesPerFrame));
    }
};

// Returns false and writes `error` (if non-null) on failure.
bool loadWavFile(const std::string& path, WavFile& out, std::string* error = 0);

// S_SoundOmni / DirectSound linear rolloff: full gain inside Hotspot,
// silence past FallOff (PC X_SharedDB 3D sound requires both).
inline float soundOmniGain(float dist, float hotspot, float falloff) {
    if (dist < 0.0f) {
        dist = 0.0f;
    }
    if (hotspot < 0.01f) {
        hotspot = 0.01f;
    }
    if (falloff <= hotspot) {
        falloff = hotspot + 0.01f;
    }
    if (dist <= hotspot) {
        return 1.0f;
    }
    if (dist >= falloff) {
        return 0.0f;
    }
    return (falloff - dist) / (falloff - hotspot);
}

// X_SoundFactory<X_SoundOmni>::getSound (Android decompile):
//   Pitch = X_SharedDBSoundSound::getPitch(sound);            // script, Hz
//   setFrequency(Pitch / S_Sound::getSamplesPerSecond(sound)); // / WAV rate
// i.e. the script Pitch divided by the file's own sample rate. 0 = not set
// (play at the native rate). Dividing by a hardcoded 22050 made every
// 44.1 kHz cue (graphic-novel narration) run at double speed.
inline float soundPitchMultiplier(int scriptPitch, int wavSampleRate) {
    if (scriptPitch <= 0 || wavSampleRate <= 0) {
        return 1.0f;
    }
    const float f = static_cast<float>(scriptPitch) / static_cast<float>(wavSampleRate);
    if (f < 0.05f) {
        return 0.05f;
    }
    if (f > 20.0f) {
        return 20.0f;
    }
    return f;
}

}  // namespace maxfx

#endif  // MAXFX_SOUND_SOUND_H
