#include "maxfx/sound/Sound.h"

#include "maxfx/core/Fs.h"

#include <cstring>
#include <exception>

namespace maxfx {
namespace {

unsigned readU16(const unsigned char* p) {
    return static_cast<unsigned>(p[0]) | (static_cast<unsigned>(p[1]) << 8);
}

unsigned readU32(const unsigned char* p) {
    return static_cast<unsigned>(p[0]) | (static_cast<unsigned>(p[1]) << 8) |
           (static_cast<unsigned>(p[2]) << 16) | (static_cast<unsigned>(p[3]) << 24);
}

}  // namespace

bool loadWavFile(const std::string& path, WavFile& out, std::string* error) {
    out = WavFile();
    std::vector<unsigned char> bytes;
    try {
        bytes = readFileBytes(path);
    } catch (const std::exception& ex) {
        if (error) {
            *error = ex.what();
        }
        return false;
    }
    if (bytes.size() < 44 || std::memcmp(&bytes[0], "RIFF", 4) != 0 ||
        std::memcmp(&bytes[8], "WAVE", 4) != 0) {
        if (error) {
            *error = "not a RIFF/WAVE file";
        }
        return false;
    }

    std::size_t i = 12;
    int format = 0;
    int channels = 0;
    int rate = 0;
    int bits = 0;
    const unsigned char* data = 0;
    std::size_t dataBytes = 0;
    while (i + 8 <= bytes.size()) {
        const char* id = reinterpret_cast<const char*>(&bytes[i]);
        const unsigned size = readU32(&bytes[i + 4]);
        i += 8;
        if (i + size > bytes.size()) {
            break;
        }
        if (std::memcmp(id, "fmt ", 4) == 0 && size >= 16) {
            format = static_cast<int>(readU16(&bytes[i]));
            channels = static_cast<int>(readU16(&bytes[i + 2]));
            rate = static_cast<int>(readU32(&bytes[i + 4]));
            bits = static_cast<int>(readU16(&bytes[i + 14]));
        } else if (std::memcmp(id, "data", 4) == 0) {
            data = &bytes[i];
            dataBytes = size;
        }
        i += size;
        if ((size & 1u) != 0u && i < bytes.size()) {
            ++i;
        }
    }
    if (format != 1 || channels < 1 || channels > 2 || rate <= 0 || (bits != 8 && bits != 16) ||
        data == 0 || dataBytes == 0) {
        if (error) {
            *error = "unsupported WAV (need PCM 8/16-bit mono/stereo)";
        }
        return false;
    }
    out.sampleRate = rate;
    out.channels = channels;
    out.bitsPerSample = bits;
    out.pcm.assign(data, data + dataBytes);
    return true;
}

}  // namespace maxfx
