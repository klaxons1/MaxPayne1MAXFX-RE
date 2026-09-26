#include "maxfx/image/Image.h"

#include "maxfx/core/Fs.h"
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

bool decodeRowRle(const unsigned char* data, std::size_t size, std::size_t& src, int encoding,
                  int rowBytes, std::vector<unsigned char>& row, std::string* error) {
    row.assign(static_cast<std::size_t>(rowBytes), 0);
    int decoded = 0;
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
    return true;
}

bool paletteIsGreyscale(const unsigned char* palette) {
    for (int i = 0; i < 256; ++i) {
        const unsigned char r = palette[i * 3 + 0];
        const unsigned char g = palette[i * 3 + 1];
        const unsigned char b = palette[i * 3 + 2];
        if (r != g || g != b) {
            return false;
        }
    }
    return true;
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

    if (width <= 0 || height <= 0 || bpp != 8 || nplanes < 1 || nplanes > 4 || bytesPerLine < width) {
        if (error) {
            *error = "unsupported PCX format (need 8-bit, 1-4 planes)";
        }
        return false;
    }

    const unsigned char* palette = 0;
    if (nplanes == 1) {
        if (size >= 769 && data[size - 769] == 0x0C) {
            palette = data + (size - 768);
        } else {
            if (error) {
                *error = "PCX palette missing";
            }
            return false;
        }
    }

    out.width = width;
    out.height = height;
    out.channels = 4;
    out.pixels.assign(static_cast<std::size_t>(width * height * 4), 255);

    std::size_t src = 128;
    std::vector<unsigned char> row;
    for (int y = 0; y < height; ++y) {
        unsigned char* dst = &out.pixels[static_cast<std::size_t>(y * width * 4)];
        if (nplanes == 1) {
            if (!decodeRowRle(data, size, src, encoding, bytesPerLine, row, error)) {
                return false;
            }
            for (int x = 0; x < width; ++x) {
                const unsigned char idx = row[static_cast<std::size_t>(x)];
                dst[x * 4 + 0] = palette[idx * 3 + 0];
                dst[x * 4 + 1] = palette[idx * 3 + 1];
                dst[x * 4 + 2] = palette[idx * 3 + 2];
                dst[x * 4 + 3] = 255;
            }
        } else {
            std::vector<unsigned char> plane(static_cast<std::size_t>(width), 0);
            for (int p = 0; p < nplanes; ++p) {
                if (!decodeRowRle(data, size, src, encoding, bytesPerLine, row, error)) {
                    return false;
                }
                for (int x = 0; x < width; ++x) {
                    dst[x * 4 + p] = row[static_cast<std::size_t>(x)];
                }
            }
            (void)plane;
        }
    }

    // MAX-FX alpha maps are 8-bit paletted greyscale PCX (see baseballbat_alpha.pcx
    // and sharedtextures/*_alpha.pcx). Put the grey value in alpha so the viewer
    // and any later compositing step see a real opacity channel.
    if (nplanes == 1 && palette != 0 && paletteIsGreyscale(palette)) {
        for (int i = 0; i < width * height; ++i) {
            out.pixels[static_cast<std::size_t>(i * 4 + 3)] = out.pixels[static_cast<std::size_t>(i * 4 + 0)];
        }
    }
    return true;
}

bool decodeDds(const unsigned char* data, std::size_t size, Image& out, std::string* error) {
    (void)data;
    (void)size;
    (void)out;
    if (error) {
        *error = "DDS textures are not decoded yet";
    }
    return false;
}

bool decodeStb(const unsigned char* data, std::size_t size, Image& out, std::string* error) {
    int w = 0, h = 0, n = 0;
    unsigned char* pixels = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &n, 4);
    if (pixels == 0) {
        if (error) {
            *error = stbi_failure_reason() ? stbi_failure_reason() : "stb_image failed";
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

std::string lowerExt(const std::string& hint) {
    std::string e = lowerCopy(hint);
    if (!e.empty() && e[0] != '.') {
        e = "." + e;
    }
    return e;
}

int fileTypeFromHint(const std::string& hint) {
    const std::string e = lowerExt(hint);
    if (e == ".pcx") {
        return kTexPcx;
    }
    if (e == ".scx") {
        return kTexScx;
    }
    if (e == ".dds") {
        return kTexDds;
    }
    if (e == ".tga") {
        return kTexTga;
    }
    if (e == ".jpg" || e == ".jpeg") {
        return kTexJpg;
    }
    return -1;
}

std::string companionAlphaPath(const std::string& path) {
    const std::string dir = parentDir(path);
    const std::string stem = fileStem(path);
    static const char* kSuffixes[] = {"_alpha.pcx", "_alpha.jpg", "_alpha.tga", "_alpha.png", 0};
    for (int i = 0; kSuffixes[i] != 0; ++i) {
        const std::string cand = existingPathIgnoreCase(joinPath(dir, stem + kSuffixes[i]));
        if (!cand.empty()) {
            return cand;
        }
    }
    return std::string();
}

}  // namespace

bool applyAlphaMap(Image& color, const Image& alpha) {
    if (color.empty() || alpha.empty() || color.width != alpha.width || color.height != alpha.height) {
        return false;
    }
    const int n = color.width * color.height;
    for (int i = 0; i < n; ++i) {
        const unsigned char r = alpha.pixels[static_cast<std::size_t>(i * 4 + 0)];
        const unsigned char g = alpha.pixels[static_cast<std::size_t>(i * 4 + 1)];
        const unsigned char b = alpha.pixels[static_cast<std::size_t>(i * 4 + 2)];
        const unsigned char a = alpha.pixels[static_cast<std::size_t>(i * 4 + 3)];
        unsigned char lum = r;
        if (g > lum) {
            lum = g;
        }
        if (b > lum) {
            lum = b;
        }
        // Greyscale PCX already wrote luminance into alpha; prefer that when it
        // is not a fully-opaque colour image.
        color.pixels[static_cast<std::size_t>(i * 4 + 3)] = (a != 255) ? a : lum;
    }
    return true;
}

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
    return decodeStb(data, size, out, errorMessage);
}

bool decodeImageMemory(const unsigned char* data, std::size_t size, const std::string& fileTypeHint,
                       Image& out, std::string* errorMessage) {
    int type = fileTypeFromHint(fileTypeHint);
    if (type < 0 && data != 0 && size >= 4) {
        if (data[0] == 0x0A) {
            type = kTexPcx;
        } else if (data[0] == 'D' && data[1] == 'D' && data[2] == 'S' && data[3] == ' ') {
            type = kTexDds;
        } else {
            type = kTexJpg;
        }
    }
    return decodeEmbeddedImage(type, data, size, out, errorMessage);
}

bool loadImageFile(const std::string& path, Image& out, std::string* errorMessage) {
    out = Image();
    const std::string resolved = existingPathIgnoreCase(path);
    if (resolved.empty()) {
        if (errorMessage) {
            *errorMessage = "image not found: " + path;
        }
        return false;
    }
    std::vector<unsigned char> bytes;
    try {
        bytes = readFileBytes(resolved);
    } catch (const std::exception& ex) {
        if (errorMessage) {
            *errorMessage = ex.what();
        }
        return false;
    }
    if (bytes.empty()) {
        if (errorMessage) {
            *errorMessage = "empty image file";
        }
        return false;
    }
    if (!decodeImageMemory(&bytes[0], bytes.size(), fileExtension(resolved), out, errorMessage)) {
        return false;
    }
    const std::string alphaPath = companionAlphaPath(resolved);
    if (!alphaPath.empty() && alphaPath != resolved) {
        Image alpha;
        std::string alphaErr;
        if (loadImageFile(alphaPath, alpha, &alphaErr)) {
            applyAlphaMap(out, alpha);
        }
    }
    return true;
}

}  // namespace maxfx
