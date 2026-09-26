#ifndef MAXFX_IMAGE_IMAGE_H
#define MAXFX_IMAGE_IMAGE_H

#include <string>
#include <vector>

namespace maxfx {

struct Image {
    int width;
    int height;
    int channels;  // always 4 (RGBA) after a successful decode
    std::vector<unsigned char> pixels;  // top-left origin, tightly packed

    Image() : width(0), height(0), channels(0) {}

    bool empty() const { return pixels.empty() || width <= 0 || height <= 0; }
};

// Decode an embedded LDB texture (TGA / JPEG / PCX / DDS).
// SCX (Remedy proprietary) is not supported and returns false.
bool decodeEmbeddedImage(int fileType, const unsigned char* data, std::size_t size, Image& out,
                         std::string* errorMessage = 0);

// Decode from a memory blob. Format is sniffed from `fileTypeHint` (".pcx",
// "pcx", "jpg", ...) or from the payload magic if the hint is empty.
bool decodeImageMemory(const unsigned char* data, std::size_t size, const std::string& fileTypeHint,
                       Image& out, std::string* errorMessage = 0);

// Load a file from disk. Paletted greyscale PCX stores the grey value in
// alpha so MAX-FX `*_alpha.pcx` maps display correctly. If a companion
// `stem_alpha.pcx` / `.jpg` / `.tga` sits next to a colour file, it is
// composited into the colour image's alpha channel.
bool loadImageFile(const std::string& path, Image& out, std::string* errorMessage = 0);

// Copy opacity from `alpha` into `color`'s alpha. JPEG / paletted PCX masks
// live in RGB (luminance); 32-bit sources use the alpha channel. If the
// maps differ in size they are bilinear-resampled — official levels pair
// 32x32 glass with a 64x64 alpha, and 32x32 water with an 8x8 50% tile.
bool applyAlphaMap(Image& color, const Image& alpha);

}  // namespace maxfx

#endif  // MAXFX_IMAGE_IMAGE_H
