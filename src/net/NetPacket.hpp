#pragma once

#include <cstdint>
#include <vector>
#include <array>
#include <bit>
#include <concepts>
#include <string>

struct NetPacket {
    enum Type : uint32_t { Join = 0, StateFull = 1, EntityUpdate = 2, Chat = 3, Disconnect = 4, CombatEvent = 5 };
    Type type;
    std::vector<uint8_t> payload;
};

// Serialize a value into bytes (little-endian)
template<std::integral T>
void writeBytes(std::vector<uint8_t>& out, T val) {
    if constexpr (std::endian::native != std::endian::little) {
        // Swap to little-endian if needed
        val = std::byteswap(val);
    }
    auto bytes = std::bit_cast<std::array<uint8_t, sizeof(T)>>(val);
    out.insert(out.end(), bytes.begin(), bytes.end());
}

inline void writeFloat(std::vector<uint8_t>& out, float val) {
    auto bytes = std::bit_cast<std::array<uint8_t, 4>>(val);
    out.insert(out.end(), bytes.begin(), bytes.end());
}

inline void writeString(std::vector<uint8_t>& out, const std::string& s) {
    out.insert(out.end(), s.begin(), s.end());
}

// Serialize a NetPacket for sending
inline std::vector<uint8_t> serializePacket(const NetPacket& pkt) {
    std::vector<uint8_t> data;
    uint32_t payloadSize = static_cast<uint32_t>(pkt.payload.size());
    writeBytes(data, static_cast<uint32_t>(pkt.type));
    writeBytes(data, payloadSize);
    data.insert(data.end(), pkt.payload.begin(), pkt.payload.end());
    return data;
}

// Deserialize helpers
template<std::integral T>
T readBytes(const std::vector<uint8_t>& data, size_t offset) {
    std::array<uint8_t, sizeof(T)> arr{};
    for (size_t i = 0; i < sizeof(T); ++i) arr[i] = data[offset + i];
    if constexpr (std::endian::native != std::endian::little) {
        return std::byteswap(std::bit_cast<T>(arr));
    }
    return std::bit_cast<T>(arr);
}

inline float readFloat(const std::vector<uint8_t>& data, size_t offset) {
    std::array<uint8_t, 4> arr{data[offset], data[offset+1], data[offset+2], data[offset+3]};
    return std::bit_cast<float>(arr);
}

// Convenience packet builders
inline std::vector<uint8_t> makeEntityUpdate(int id, float x, float y, int hp, int maxHp, bool alive) {
    std::vector<uint8_t> p;
    writeBytes(p, id);
    writeFloat(p, x);
    writeFloat(p, y);
    writeBytes(p, hp);
    writeBytes(p, maxHp);
    p.push_back(alive ? 1 : 0);
    return serializePacket({NetPacket::EntityUpdate, std::move(p)});
}

inline std::vector<uint8_t> makeFullSync(const std::vector<uint8_t>& entities) {
    return serializePacket({NetPacket::StateFull, entities});
}

inline std::vector<uint8_t> makeChat(const std::string& msg) {
    std::vector<uint8_t> p(msg.begin(), msg.end());
    return serializePacket({NetPacket::Chat, std::move(p)});
}

inline std::vector<uint8_t> makeCombatEvent(int attId, int defId, int dmg, bool killed) {
    std::vector<uint8_t> p;
    writeBytes(p, attId);
    writeBytes(p, defId);
    writeBytes(p, dmg);
    p.push_back(killed ? 1 : 0);
    return serializePacket({NetPacket::CombatEvent, std::move(p)});
}
