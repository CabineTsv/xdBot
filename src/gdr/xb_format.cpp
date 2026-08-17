#include "xb_format.hpp"

#include <algorithm>
#include <cstring>

namespace xb_format {
namespace {

constexpr uint8_t kMagic0 = 'X';
constexpr uint8_t kMagic1 = 'B';
constexpr uint8_t kVersionNoVelocity = 0x01;
constexpr uint8_t kVersionWithVelocity = 0x02;
constexpr uint8_t kCurrentVersion = kVersionWithVelocity;
constexpr size_t kHeaderSize = 3;
constexpr size_t kChecksumSize = 4;
// Generous enough to never be hit by a real macro (an hour at 240 TPS is
// under 900k frames), but bounded so a corrupted or hand-crafted count field
// can't force a multi-gigabyte reserve().
constexpr uint32_t kMaxRecordCount = 50'000'000u;

uint32_t fnv1a(std::span<uint8_t const> data) {
    uint32_t hash = 0x811c9dc5u;
    for (uint8_t byte : data) {
        hash ^= byte;
        hash *= 0x01000193u;
    }
    return hash;
}

void writeU8(std::vector<uint8_t>& out, uint8_t value) {
    out.push_back(value);
}

void writeU16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFFu));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
}

void writeU32(std::vector<uint8_t>& out, uint32_t value) {
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xFFu));
}

void writeU64(std::vector<uint8_t>& out, uint64_t value) {
    for (int i = 0; i < 8; ++i)
        out.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xFFu));
}

void writeF32(std::vector<uint8_t>& out, float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    writeU32(out, bits);
}

void writeString(std::vector<uint8_t>& out, std::string const& value) {
    uint16_t len = static_cast<uint16_t>(std::min<size_t>(value.size(), 0xFFFFu));
    writeU16(out, len);
    out.insert(out.end(), value.begin(), value.begin() + len);
}

void writeVarUint(std::vector<uint8_t>& out, uint64_t value) {
    do {
        uint8_t byte = static_cast<uint8_t>(value & 0x7Fu);
        value >>= 7;
        if (value != 0)
            byte |= 0x80u;
        out.push_back(byte);
    } while (value != 0);
}

bool readU8(std::span<uint8_t const> data, size_t& offset, uint8_t& out) {
    if (offset + 1 > data.size())
        return false;
    out = data[offset];
    offset += 1;
    return true;
}

bool readU16(std::span<uint8_t const> data, size_t& offset, uint16_t& out) {
    if (offset + 2 > data.size())
        return false;
    out = static_cast<uint16_t>(data[offset]) | (static_cast<uint16_t>(data[offset + 1]) << 8);
    offset += 2;
    return true;
}

bool readU32(std::span<uint8_t const> data, size_t& offset, uint32_t& out) {
    if (offset + 4 > data.size())
        return false;
    out = static_cast<uint32_t>(data[offset]) | (static_cast<uint32_t>(data[offset + 1]) << 8) |
          (static_cast<uint32_t>(data[offset + 2]) << 16) |
          (static_cast<uint32_t>(data[offset + 3]) << 24);
    offset += 4;
    return true;
}

bool readU64(std::span<uint8_t const> data, size_t& offset, uint64_t& out) {
    if (offset + 8 > data.size())
        return false;
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i)
        value |= static_cast<uint64_t>(data[offset + i]) << (8 * i);
    out = value;
    offset += 8;
    return true;
}

bool readF32(std::span<uint8_t const> data, size_t& offset, float& out) {
    uint32_t bits = 0;
    if (!readU32(data, offset, bits))
        return false;
    std::memcpy(&out, &bits, sizeof(out));
    return true;
}

bool readString(std::span<uint8_t const> data, size_t& offset, std::string& out) {
    uint16_t len = 0;
    if (!readU16(data, offset, len))
        return false;
    if (offset + len > data.size())
        return false;
    out.assign(reinterpret_cast<char const*>(data.data() + offset), len);
    offset += len;
    return true;
}

bool readVarUint(std::span<uint8_t const> data, size_t& offset, uint64_t& out) {
    uint64_t result = 0;
    int shift = 0;
    while (true) {
        if (offset >= data.size())
            return false;
        uint8_t byte = data[offset++];
        result |= static_cast<uint64_t>(byte & 0x7Fu) << shift;
        if ((byte & 0x80u) == 0)
            break;
        shift += 7;
        if (shift >= 64)
            return false;
    }
    out = result;
    return true;
}

} // namespace

bool isXBData(std::span<uint8_t const> data) {
    return data.size() >= kHeaderSize && data[0] == kMagic0 && data[1] == kMagic1 &&
           (data[2] == kVersionNoVelocity || data[2] == kVersionWithVelocity);
}

std::vector<uint8_t> exportXB(BotReplay const& replay) {
    std::vector<uint8_t> out;
    out.reserve(64 + replay.inputs.size() * 3 + replay.frameFixes.size() * 34);

    out.push_back(kMagic0);
    out.push_back(kMagic1);
    out.push_back(kCurrentVersion);

    writeU32(out, static_cast<uint32_t>(std::max(replay.botInfo.version, 0)));
    writeU32(out, static_cast<uint32_t>(std::max(replay.gameVersion, 0)));
    writeU32(out, static_cast<uint32_t>(replay.levelInfo.id));
    writeF32(out, replay.framerate);
    writeF32(out, replay.duration);
    writeU64(out, static_cast<uint64_t>(replay.seed));
    writeU8(out, static_cast<uint8_t>(std::clamp(replay.coins, 0, 255)));

    uint8_t flags = 0;
    if (replay.ldm)
        flags |= 0x01u;
    writeU8(out, flags);

    writeString(out, replay.levelInfo.name);
    writeString(out, replay.author);
    writeString(out, replay.description);

    writeU32(out, static_cast<uint32_t>(replay.inputs.size()));
    uint64_t previousFrame = 0;
    for (auto const& input : replay.inputs) {
        uint64_t frame = input.frame;
        writeVarUint(out, frame >= previousFrame ? frame - previousFrame : 0);
        previousFrame = frame;

        uint8_t packed = static_cast<uint8_t>(input.button & 0x3Fu);
        if (input.player2)
            packed |= 0x40u;
        if (input.down)
            packed |= 0x80u;
        writeU8(out, packed);
    }

    writeU32(out, static_cast<uint32_t>(replay.frameFixes.size()));
    uint64_t previousFixFrame = 0;
    for (auto const& fix : replay.frameFixes) {
        uint64_t frame = static_cast<uint64_t>(std::max(fix.frame, 0));
        writeVarUint(out, frame >= previousFixFrame ? frame - previousFixFrame : 0);
        previousFixFrame = frame;

        uint8_t fixFlags = 0;
        if (fix.p1.rotate)
            fixFlags |= 0x01u;
        if (fix.p2.rotate)
            fixFlags |= 0x02u;
        writeU8(out, fixFlags);

        writeF32(out, fix.p1.pos.x);
        writeF32(out, fix.p1.pos.y);
        writeF32(out, fix.p1.rotation);
        writeF32(out, static_cast<float>(fix.p1.yVelocity));
        writeF32(out, static_cast<float>(fix.p1.xVelocity));
        writeF32(out, fix.p2.pos.x);
        writeF32(out, fix.p2.pos.y);
        writeF32(out, fix.p2.rotation);
        writeF32(out, static_cast<float>(fix.p2.yVelocity));
        writeF32(out, static_cast<float>(fix.p2.xVelocity));
    }

    writeU32(out, fnv1a(out));

    return out;
}

gdr::Result<BotReplay> importXB(std::span<uint8_t const> data) {
    if (!isXBData(data))
        return gdr::Err<BotReplay>("Not an XB file");

    if (data.size() < kHeaderSize + kChecksumSize)
        return gdr::Err<BotReplay>("XB file too short");

    size_t payloadSize = data.size() - kChecksumSize;

    uint32_t storedChecksum = 0;
    size_t checksumOffset = payloadSize;
    if (!readU32(data, checksumOffset, storedChecksum))
        return gdr::Err<BotReplay>("XB: truncated checksum");

    uint32_t computedChecksum = fnv1a(data.subspan(0, payloadSize));
    if (computedChecksum != storedChecksum)
        return gdr::Err<BotReplay>("XB checksum mismatch - file is corrupted or not a genuine xb macro");

    bool hasVelocity = data[2] == kVersionWithVelocity;

    size_t offset = kHeaderSize;

    uint32_t botVersion = 0;
    uint32_t gameVersion = 0;
    uint32_t levelId = 0;
    float framerate = 240.f;
    float duration = 0.f;
    uint64_t seed = 0;
    uint8_t coins = 0;
    uint8_t flags = 0;

    if (!readU32(data, offset, botVersion) || !readU32(data, offset, gameVersion) ||
        !readU32(data, offset, levelId) || !readF32(data, offset, framerate) ||
        !readF32(data, offset, duration) || !readU64(data, offset, seed) ||
        !readU8(data, offset, coins) || !readU8(data, offset, flags))
        return gdr::Err<BotReplay>("XB: truncated header");

    std::string levelName;
    std::string author;
    std::string description;
    if (!readString(data, offset, levelName) || !readString(data, offset, author) ||
        !readString(data, offset, description))
        return gdr::Err<BotReplay>("XB: truncated header strings");

    BotReplay replay;
    replay.botInfo.name = "xdBot";
    replay.botInfo.version = static_cast<int>(botVersion);
    replay.gameVersion = static_cast<int>(gameVersion);
    replay.levelInfo.id = levelId;
    replay.levelInfo.name = levelName;
    replay.framerate = framerate;
    replay.duration = duration;
    replay.seed = static_cast<uintptr_t>(seed);
    replay.coins = static_cast<int>(coins);
    replay.ldm = (flags & 0x01u) != 0;
    replay.author = author;
    replay.description = description;
    replay.xdBotMacro = true;
    replay.isLegacy = false;

    uint32_t inputCount = 0;
    if (!readU32(data, offset, inputCount))
        return gdr::Err<BotReplay>("XB: truncated input count");
    if (inputCount > kMaxRecordCount)
        return gdr::Err<BotReplay>("XB: input count out of sane bounds");

    replay.inputs.reserve(inputCount);
    uint64_t currentFrame = 0;
    for (uint32_t i = 0; i < inputCount; ++i) {
        uint64_t delta = 0;
        uint8_t packed = 0;
        if (!readVarUint(data, offset, delta) || !readU8(data, offset, packed))
            return gdr::Err<BotReplay>("XB: truncated input data");

        currentFrame += delta;
        uint8_t button = packed & 0x3Fu;
        bool player2 = (packed & 0x40u) != 0;
        bool down = (packed & 0x80u) != 0;
        replay.inputs.emplace_back(currentFrame, button, player2, down);
    }

    uint32_t frameFixCount = 0;
    if (!readU32(data, offset, frameFixCount))
        return gdr::Err<BotReplay>("XB: truncated frame fix count");
    if (frameFixCount > kMaxRecordCount)
        return gdr::Err<BotReplay>("XB: frame fix count out of sane bounds");

    replay.frameFixes.reserve(frameFixCount);
    uint64_t currentFixFrame = 0;
    for (uint32_t i = 0; i < frameFixCount; ++i) {
        uint64_t delta = 0;
        uint8_t fixFlags = 0;
        gdr_legacy::FrameFix fix;

        if (!readVarUint(data, offset, delta) || !readU8(data, offset, fixFlags) ||
            !readF32(data, offset, fix.p1.pos.x) || !readF32(data, offset, fix.p1.pos.y) ||
            !readF32(data, offset, fix.p1.rotation))
            return gdr::Err<BotReplay>("XB: truncated frame fix data");

        if (hasVelocity) {
            float p1YVelocity = 0.f;
            float p1XVelocity = 0.f;
            if (!readF32(data, offset, p1YVelocity) || !readF32(data, offset, p1XVelocity))
                return gdr::Err<BotReplay>("XB: truncated frame fix data");
            fix.p1.yVelocity = static_cast<double>(p1YVelocity);
            fix.p1.xVelocity = static_cast<double>(p1XVelocity);
        }

        if (!readF32(data, offset, fix.p2.pos.x) || !readF32(data, offset, fix.p2.pos.y) ||
            !readF32(data, offset, fix.p2.rotation))
            return gdr::Err<BotReplay>("XB: truncated frame fix data");

        if (hasVelocity) {
            float p2YVelocity = 0.f;
            float p2XVelocity = 0.f;
            if (!readF32(data, offset, p2YVelocity) || !readF32(data, offset, p2XVelocity))
                return gdr::Err<BotReplay>("XB: truncated frame fix data");
            fix.p2.yVelocity = static_cast<double>(p2YVelocity);
            fix.p2.xVelocity = static_cast<double>(p2XVelocity);
        }

        currentFixFrame += delta;
        fix.frame = static_cast<int>(currentFixFrame);
        fix.p1.rotate = (fixFlags & 0x01u) != 0;
        fix.p2.rotate = (fixFlags & 0x02u) != 0;

        replay.frameFixes.push_back(fix);
    }

    return gdr::Ok<BotReplay>(std::move(replay));
}

} // namespace xb_format
