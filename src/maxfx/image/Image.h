#ifndef MAXFX_IMAGE_IMAGE_H
#define MAXFX_IMAGE_IMAGE_H

#include <string>
#include <vector>

namespace maxfx {

struct Image {
    int width;
    int height;
    int channels;  // 3 = RGB, 4 = RGBA
    std::vector<unsigned char> pixels;  // top-left origin, tightly packed

    Image() : width(0), height(0), channels(0) {}

    bool empty() const { return pixels.empty() || width <= 0 || height <= 0; }
};

// Decode an embedded LDB texture (TGA / JPEG / PCX / DDS).
// SCX (Remedy proprietary) is not supported and returns false.
bool decodeEmbeddedImage(int fileType, const unsigned char* data, std::size_t size, Image& out,
                         std::string* errorMessage = 0);

}  // namespace maxfx

#endif  // MAXFX_IMAGE_IMAGE_H
