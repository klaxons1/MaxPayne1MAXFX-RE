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

}  // namespace maxfx

#endif  // MAXFX_SOUND_SOUND_H
