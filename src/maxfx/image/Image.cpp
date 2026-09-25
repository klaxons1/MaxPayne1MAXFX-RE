#include "maxfx/image/Image.h"

#include "maxfx/ldb/Ldb.h"

#include <cstdint>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_TGA
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb/stb_image.h"

#include <cstring>
#include <sstream>

namespace maxfx {
namespace {

std::uint16_t readU16LE(const unsigned char* p) {
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

bool decodePcx(const unsigned char* data, std::size_t size, Image& out, std::string* error) {
    if (size < 128 || data[0] != 0x0A) {
        if (error) {
            *error = "invalid PCX header";
        }
        return false;
    }

    const int encoding = data[2];
    const int bpp = data[3];
    const int xmin = readU16LE(data + 4);
    const int ymin = readU16LE(data + 6);
    const int xmax = readU16LE(data + 8);
    const int ymax = readU16LE(data + 10);
    const int nplanes = data[65];
    const int bytesPerLine = readU16LE(data + 66);
    const int width = xmax - xmin + 1;
    const int height = ymax - ymin + 1;

    if (width <= 0 || height <= 0 || bpp != 8 || nplanes != 1) {
        if (error) {
            *error = "unsupported PCX format (need 8-bit paletted)";
        }
        return false;
    }

    const unsigned char* palette = 0;
    if (size >= 769 && data[size - 769] == 0x0C) {
        palette = data + (size - 768);
    } else {
        if (error) {
            *error = "PCX palette missing";
        }
        return false;
    }

    std::vector<unsigned char> indices(static_cast<std::size_t>(width * height));
    std::size_t src = 128;
    for (int y = 0; y < height; ++y) {
        int decoded = 0;
        const int rowBytes = bytesPerLine;
        std::vector<unsigned char> row(static_cast<std::size_t>(rowBytes));
        while (decoded < rowBytes) {
            if (src >= size) {
                if (error) {
                    *error = "truncated PCX RLE stream";
                }
                return false;
            }
            unsigned char b = data[src++];
            int count = 1;
            if (encoding == 1 && (b & 0xC0) == 0xC0) {
                count = b & 0x3F;
                if (src >= size) {
                    if (error) {
                        *error = "truncated PCX RLE stream";
                    }
                    return false;
                }
                b = data[src++];
            }
            while (count-- > 0 && decoded < rowBytes) {
                row[static_cast<std::size_t>(decoded++)] = b;
            }
        }
        std::memcpy(&indices[static_cast<std::size_t>(y * width)], &row[0],
                    static_cast<std::size_t>(width));
    }

    out.width = width;
    out.height = height;
    out.channels = 4;
    out.pixels.resize(static_cast<std::size_t>(width * height * 4));
    for (int i = 0; i < width * height; ++i) {
        const unsigned char idx = indices[static_cast<std::size_t>(i)];
        out.pixels[static_cast<std::size_t>(i * 4 + 0)] = palette[idx * 3 + 0];
        out.pixels[static_cast<std::size_t>(i * 4 + 1)] = palette[idx * 3 + 1];
        out.pixels[static_cast<std::size_t>(i * 4 + 2)] = palette[idx * 3 + 2];
        out.pixels[static_cast<std::size_t>(i * 4 + 3)] = 255;
    }
    return true;
}

bool decodeDds(const unsigned char* data, std::size_t size, Image& out, std::string* error) {
    // Uncompressed 32-bit DDS is rare in MP1; compressed blocks need a BC decoder.
    // Keep a clear error so the viewer can substitute a placeholder.
    (void)data;
    (void)size;
    (void)out;
    if (error) {
        *error = "DDS textures are not decoded yet";
    }
    return false;
}

}  // namespace

bool decodeEmbeddedImage(int fileType, const unsigned char* data, std::size_t size, Image& out,
                         std::string* errorMessage) {
    out = Image();
    if (data == 0 || size == 0) {
        if (errorMessage) {
            *errorMessage = "empty image blob";
        }
        return false;
    }

    if (fileType == kTexPcx) {
        return decodePcx(data, size, out, errorMessage);
    }
    if (fileType == kTexScx) {
        if (errorMessage) {
            *errorMessage = "SCX (Remedy) textures are not supported";
        }
        return false;
    }
    if (fileType == kTexDds) {
        return decodeDds(data, size, out, errorMessage);
    }

    int w = 0, h = 0, n = 0;
    unsigned char* pixels = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &n, 4);
    if (pixels == 0) {
        if (errorMessage) {
            *errorMessage = stbi_failure_reason() ? stbi_failure_reason() : "stb_image failed";
        }
        return false;
    }
    out.width = w;
    out.height = h;
    out.channels = 4;
    out.pixels.assign(pixels, pixels + static_cast<std::size_t>(w * h * 4));
    stbi_image_free(pixels);
    return true;
}

}  // namespace maxfx
