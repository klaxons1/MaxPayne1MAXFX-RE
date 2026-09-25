// Tagged binary stream used by every MAX-FX database (LDB, KF2, KFS, ...).
//
// On disk each value is stored as `{tag}{payload}`. Integer and float tags
// may use a compact encoding (1/2/3-byte ints, 16-bit floats). Container
// headers (std::vector / std::map / std::pair) have their own tags too.
//
// Reference: Android libMaxPayne.so (R_MemoryFile::readTagged) and the
// community LDB specification. PC Max Payne 1 writes the same layout.
#ifndef MAXFX_CORE_STREAM_H
#define MAXFX_CORE_STREAM_H

#include "maxfx/core/Math.h"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace maxfx {

enum Tag {
    kTagLong = 0x00,
    kTagULong = 0x01,
    kTagInt32 = 0x02,
    kTagUInt32 = 0x03,
    kTagShort = 0x04,
    kTagUShort = 0x05,
    kTagChar = 0x06,
    kTagSChar = 0x07,
    kTagUChar = 0x08,
    kTagFloat32 = 0x09,
    kTagDouble = 0x0A,
    kTagString = 0x0D,
    kTagBool = 0x0E,
    kTagUInt24 = 0x0F,
    kTagUInt16 = 0x10,
    kTagUInt8 = 0x11,
    kTagInt24 = 0x12,
    kTagInt16 = 0x13,
    kTagInt8 = 0x14,
    kTagVec2 = 0x15,
    kTagVec3 = 0x16,
    kTagVec4 = 0x17,
    kTagMat2 = 0x18,
    kTagMat3 = 0x19,
    kTagMat4x3 = 0x1A,
    kTagMat4 = 0x1B,
    kTagVector = 0x1C,  // Remedy std::vector stand-in
    kTagMap = 0x1F,     // Remedy std::map stand-in
    kTagPair = 0x25,    // Remedy std::pair stand-in
    kTagFloat16 = 0x26
};

class ReadError : public std::runtime_error {
public:
    ReadError(const std::string& message, std::size_t position);
    std::size_t position() const { return position_; }

private:
    std::size_t position_;
};

class TaggedReader {
public:
    TaggedReader(const std::uint8_t* data, std::size_t size);

    std::size_t position() const { return pos_; }
    std::size_t size() const { return size_; }
    std::size_t remaining() const { return size_ - pos_; }
    bool eof() const { return pos_ >= size_; }

    std::uint8_t peekTag() const;
    std::uint8_t readRawU8();
    void readRaw(void* dst, std::size_t bytes);
    std::vector<std::uint8_t> readBytes(std::size_t bytes);

    void expectTag(std::uint8_t tag);
    // Remedy serialises both std::vector (0x1C) and std::map (0x1F) as
    // `{tag}{count}{elements}`. Some LDB blocks mix the two tags, so the
    // reader accepts either and returns the element count.
    int readContainerCount();
    int readVectorHeader();  // alias of readContainerCount
    int readMapHeader();     // alias of readContainerCount
    void expectPairTag();    // 0x25

    int readInt();
    unsigned int readUInt();
    float readFloat();
    bool readBool();
    std::string readString();

    Vec2 readVec2();
    Vec3 readVec3();
    Vec4 readVec4();
    Mat3 readMat3();
    Mat4x3 readMat4x3();

private:
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t pos_;

    void need(std::size_t bytes) const;
    std::uint8_t readTag();
    std::int32_t readSignedPayload(int byteCount);
    std::uint32_t readUnsignedPayload(int byteCount);
    float readF32();
    float halfToFloat(std::uint16_t h) const;
};

}  // namespace maxfx

#endif  // MAXFX_CORE_STREAM_H
