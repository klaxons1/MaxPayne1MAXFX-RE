#include "maxfx/core/Stream.h"

#include <cstring>
#include <sstream>

namespace maxfx {

ReadError::ReadError(const std::string& message, std::size_t position)
    : std::runtime_error(message), position_(position) {}

static std::string makeError(const char* what, std::size_t pos) {
    std::ostringstream oss;
    oss << what << " at offset 0x" << std::hex << pos;
    return oss.str();
}

TaggedReader::TaggedReader(const std::uint8_t* data, std::size_t size)
    : data_(data), size_(size), pos_(0) {}

void TaggedReader::need(std::size_t bytes) const {
    if (pos_ + bytes > size_) {
        throw ReadError(makeError("unexpected end of LDB stream", pos_), pos_);
    }
}

std::uint8_t TaggedReader::peekTag() const {
    need(1);
    return data_[pos_];
}

std::uint8_t TaggedReader::readRawU8() {
    need(1);
    return data_[pos_++];
}

std::uint16_t TaggedReader::readRawU16() {
    need(2);
    const std::uint16_t v = static_cast<std::uint16_t>(data_[pos_] | (data_[pos_ + 1] << 8));
    pos_ += 2;
    return v;
}

std::uint32_t TaggedReader::readRawU32() {
    need(4);
    const std::uint32_t v = static_cast<std::uint32_t>(data_[pos_]) |
                           (static_cast<std::uint32_t>(data_[pos_ + 1]) << 8) |
                           (static_cast<std::uint32_t>(data_[pos_ + 2]) << 16) |
                           (static_cast<std::uint32_t>(data_[pos_ + 3]) << 24);
    pos_ += 4;
    return v;
}

float TaggedReader::readRawF32() { return readF32(); }

void TaggedReader::skip(std::size_t bytes) {
    need(bytes);
    pos_ += bytes;
}

void TaggedReader::seek(std::size_t position) {
    if (position > size_) {
        throw ReadError(makeError("seek past end of stream", position), position);
    }
    pos_ = position;
}

void TaggedReader::readRaw(void* dst, std::size_t bytes) {
    need(bytes);
    std::memcpy(dst, data_ + pos_, bytes);
    pos_ += bytes;
}

std::vector<std::uint8_t> TaggedReader::readBytes(std::size_t bytes) {
    need(bytes);
    std::vector<std::uint8_t> out(data_ + pos_, data_ + pos_ + bytes);
    pos_ += bytes;
    return out;
}

std::uint8_t TaggedReader::readTag() {
    return readRawU8();
}

void TaggedReader::expectTag(std::uint8_t tag) {
    const std::uint8_t got = readTag();
    if (got != tag) {
        std::ostringstream oss;
        oss << "expected tag 0x" << std::hex << static_cast<int>(tag)
            << " but found 0x" << static_cast<int>(got);
        throw ReadError(oss.str(), pos_ - 1);
    }
}

int TaggedReader::readContainerCount() {
    const std::size_t tagPos = pos_;
    const std::uint8_t tag = readTag();
    if (tag != kTagVector && tag != kTagMap) {
        std::ostringstream oss;
        oss << "expected container tag 0x1C/0x1F but found 0x" << std::hex
            << static_cast<int>(tag);
        throw ReadError(oss.str(), tagPos);
    }
    return readInt();
}

int TaggedReader::readVectorHeader() {
    return readContainerCount();
}

int TaggedReader::readMapHeader() {
    return readContainerCount();
}

void TaggedReader::expectPairTag() {
    expectTag(kTagPair);
}

std::int32_t TaggedReader::readSignedPayload(int byteCount) {
    need(static_cast<std::size_t>(byteCount));
    std::uint32_t raw = 0;
    for (int i = 0; i < byteCount; ++i) {
        raw |= static_cast<std::uint32_t>(data_[pos_++]) << (8 * i);
    }
    const std::uint32_t signBit = 1u << (byteCount * 8 - 1);
    if ((raw & signBit) != 0) {
        raw |= ~((1u << (byteCount * 8)) - 1u);
    }
    return static_cast<std::int32_t>(raw);
}

std::uint32_t TaggedReader::readUnsignedPayload(int byteCount) {
    need(static_cast<std::size_t>(byteCount));
    std::uint32_t raw = 0;
    for (int i = 0; i < byteCount; ++i) {
        raw |= static_cast<std::uint32_t>(data_[pos_++]) << (8 * i);
    }
    return raw;
}

float TaggedReader::readF32() {
    need(4);
    float value = 0.0f;
    std::memcpy(&value, data_ + pos_, 4);
    pos_ += 4;
    return value;
}

float TaggedReader::halfToFloat(std::uint16_t h) const {
    const std::uint32_t sign = (static_cast<std::uint32_t>(h) & 0x8000u) << 16;
    const std::uint32_t exp = (h >> 10) & 0x1Fu;
    const std::uint32_t mant = h & 0x3FFu;
    std::uint32_t bits = 0;
    if (exp == 0) {
        if (mant == 0) {
            bits = sign;
        } else {
            std::uint32_t m = mant;
            std::uint32_t e = 127 - 15 + 1;
            while ((m & 0x400u) == 0) {
                m <<= 1;
                --e;
            }
            m &= 0x3FFu;
            bits = sign | (e << 23) | (m << 13);
        }
    } else if (exp == 31) {
        bits = sign | 0x7F800000u | (mant << 13);
    } else {
        bits = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
    }
    float out = 0.0f;
    std::memcpy(&out, &bits, 4);
    return out;
}

int TaggedReader::readInt() {
    const std::size_t tagPos = pos_;
    const std::uint8_t tag = readTag();
    switch (tag) {
        case kTagLong:
        case kTagInt32:
            return readSignedPayload(4);
        case kTagInt24:
            return readSignedPayload(3);
        case kTagShort:
        case kTagInt16:
            return readSignedPayload(2);
        case kTagChar:
        case kTagSChar:
        case kTagInt8:
            return readSignedPayload(1);
        case kTagULong:
        case kTagUInt32:
            return static_cast<int>(readUnsignedPayload(4));
        case kTagUInt24:
            return static_cast<int>(readUnsignedPayload(3));
        case kTagUShort:
        case kTagUInt16:
            return static_cast<int>(readUnsignedPayload(2));
        case kTagUChar:
        case kTagUInt8:
            return static_cast<int>(readUnsignedPayload(1));
        default:
            throw ReadError(makeError("not an integer tag", tagPos), tagPos);
    }
}

unsigned int TaggedReader::readUInt() {
    const std::size_t tagPos = pos_;
    const std::uint8_t tag = readTag();
    switch (tag) {
        case kTagULong:
        case kTagUInt32:
            return readUnsignedPayload(4);
        case kTagUInt24:
            return readUnsignedPayload(3);
        case kTagUShort:
        case kTagUInt16:
            return readUnsignedPayload(2);
        case kTagUChar:
        case kTagUInt8:
            return readUnsignedPayload(1);
        case kTagLong:
        case kTagInt32:
            return static_cast<unsigned int>(readSignedPayload(4));
        case kTagInt24:
            return static_cast<unsigned int>(readSignedPayload(3));
        case kTagShort:
        case kTagInt16:
            return static_cast<unsigned int>(readSignedPayload(2));
        case kTagChar:
        case kTagSChar:
        case kTagInt8:
            return static_cast<unsigned int>(readSignedPayload(1));
        default:
            throw ReadError(makeError("not an unsigned integer tag", tagPos), tagPos);
    }
}

float TaggedReader::readFloat() {
    const std::size_t tagPos = pos_;
    const std::uint8_t tag = readTag();
    if (tag == kTagFloat32) {
        return readF32();
    }
    if (tag == kTagFloat16) {
        need(2);
        std::uint16_t h = 0;
        std::memcpy(&h, data_ + pos_, 2);
        pos_ += 2;
        return halfToFloat(h);
    }
    if (tag == kTagDouble) {
        need(8);
        double value = 0.0;
        std::memcpy(&value, data_ + pos_, 8);
        pos_ += 8;
        return static_cast<float>(value);
    }
    throw ReadError(makeError("not a float tag", tagPos), tagPos);
}

bool TaggedReader::readBool() {
    expectTag(kTagBool);
    return readRawU8() != 0;
}

std::string TaggedReader::readString() {
    expectTag(kTagString);
    const int length = readInt();
    if (length < 0) {
        throw ReadError(makeError("negative string length", pos_), pos_);
    }
    const std::vector<std::uint8_t> bytes = readBytes(static_cast<std::size_t>(length));
    // Max Payne 1 strings are 8-bit (Windows-1252 / Latin-1), not UTF-8.
    if (bytes.empty()) {
        return std::string();
    }
    return std::string(reinterpret_cast<const char*>(&bytes[0]), bytes.size());
}

Vec2 TaggedReader::readVec2() {
    expectTag(kTagVec2);
    Vec2 v;
    v.x = readF32();
    v.y = readF32();
    return v;
}

Vec3 TaggedReader::readVec3() {
    expectTag(kTagVec3);
    Vec3 v;
    v.x = readF32();
    v.y = readF32();
    v.z = readF32();
    return v;
}

Vec4 TaggedReader::readVec4() {
    expectTag(kTagVec4);
    Vec4 v;
    v.x = readF32();
    v.y = readF32();
    v.z = readF32();
    v.w = readF32();
    return v;
}

Mat3 TaggedReader::readMat3() {
    expectTag(kTagMat3);
    Mat3 m;
    for (int i = 0; i < 9; ++i) {
        m.m[i] = readF32();
    }
    return m;
}

Mat4x3 TaggedReader::readMat4x3() {
    expectTag(kTagMat4x3);
    Mat4x3 m;
    for (int r = 0; r < 4; ++r) {
        m.rows[r].x = readF32();
        m.rows[r].y = readF32();
        m.rows[r].z = readF32();
    }
    return m;
}

}  // namespace maxfx
