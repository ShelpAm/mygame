#pragma once

#include "core/math.hpp"
#include "entities/components/combat-stats.hpp"
#include <array>
#include <bit>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

struct NetPacket {
    enum Type : uint32_t {
        join = 0,
        state_full = 1,
        entity_update = 2,
        chat = 3,
        disconnect = 4,
        combat_event = 5,
        recruit_soldier = 6,
        spawn_enemy_wave = 7,
        player_input = 8,
        interact = 9,
        rest = 10,
        return_pid,
    };
    Type type;
    std::vector<uint8_t> payload; // 4 bytes size, left: real payload
    // Layout:
    // [type:4][size:4][payload:size]
};

// Serialize a value into bytes (little-endian)
template <std::integral T> void write_bytes(std::vector<uint8_t> &out, T val)
{
    if constexpr (std::endian::native != std::endian::little) {
        // Swap to little-endian if needed
        val = std::byteswap(val);
    }
    auto bytes = std::bit_cast<std::array<uint8_t, sizeof(T)>>(val);
    out.insert(out.end(), bytes.begin(), bytes.end());
}

inline void write_float(std::vector<uint8_t> &out, float val)
{
    auto bytes = std::bit_cast<std::array<uint8_t, 4>>(val);
    out.insert(out.end(), bytes.begin(), bytes.end());
}

inline void write_string(std::vector<uint8_t> &out, std::string const &s)
{
    out.insert(out.end(), s.begin(), s.end());
}

// Serialize a NetPacket for sending
inline std::vector<uint8_t> serialize_packet(NetPacket const &pkt)
{
    std::vector<uint8_t> data;
    uint32_t payload_size = static_cast<uint32_t>(pkt.payload.size());
    write_bytes(data, static_cast<uint32_t>(pkt.type));
    write_bytes(data, payload_size);
    data.insert(data.end(), pkt.payload.begin(), pkt.payload.end());
    return data;
}

// Deserialize helpers
template <std::integral T>
T read_bytes(std::vector<uint8_t> const &data, size_t offset)
{
    std::array<uint8_t, sizeof(T)> arr{};
    for (size_t i = 0; i < sizeof(T); ++i)
        arr[i] = data[offset + i];
    if constexpr (std::endian::native != std::endian::little) {
        return std::byteswap(std::bit_cast<T>(arr));
    }
    return std::bit_cast<T>(arr);
}

inline float read_float(std::vector<uint8_t> const &data, size_t offset)
{
    std::array<uint8_t, 4> arr{data[offset], data[offset + 1], data[offset + 2],
                               data[offset + 3]};
    return std::bit_cast<float>(arr);
}

// Convenience packet builders
inline std::vector<uint8_t> make_entity_update(int id, float x, float y, int hp,
                                               int max_hp, bool alive)
{
    std::vector<uint8_t> p;
    write_bytes(p, id);
    write_float(p, x);
    write_float(p, y);
    write_bytes(p, hp);
    write_bytes(p, max_hp);
    p.push_back(alive ? 1 : 0);
    return serialize_packet({NetPacket::entity_update, std::move(p)});
}

inline std::vector<uint8_t> make_full_sync(std::vector<uint8_t> const &entities)
{
    return serialize_packet({NetPacket::state_full, entities});
}

inline std::vector<uint8_t> make_chat(std::string const &msg)
{
    std::vector<uint8_t> p(msg.begin(), msg.end());
    return serialize_packet({NetPacket::chat, std::move(p)});
}

inline std::vector<uint8_t> make_combat_event(int att_id, int def_id, int dmg,
                                              bool killed)
{
    std::vector<uint8_t> p;
    write_bytes(p, att_id);
    write_bytes(p, def_id);
    write_bytes(p, dmg);
    p.push_back(killed ? 1 : 0);
    return serialize_packet({NetPacket::combat_event, std::move(p)});
}

inline std::vector<uint8_t> make_recruit_request(std::uint16_t player_id)
{
    std::vector<uint8_t> p;
    write_bytes(p, player_id);
    return serialize_packet({NetPacket::recruit_soldier, std::move(p)});
}

inline std::vector<uint8_t> make_enemy_wave(Vec2f center, int count, Team team)
{
    std::vector<uint8_t> p;
    write_float(p, center.x);
    write_float(p, center.y);
    write_bytes(p, count);
    write_bytes(p, static_cast<uint8_t>(team));
    p.push_back(0);
    return serialize_packet({NetPacket::spawn_enemy_wave, std::move(p)});
}

// --- Parse helpers ---

struct EntityUpdateData {
    int id;
    float x, y;
    int hp, max_hp;
    bool alive;
};

inline EntityUpdateData parse_entity_update(std::vector<uint8_t> const &d,
                                            size_t off = 0)
{
    EntityUpdateData r;
    memcpy(&r.id, d.data() + off, 4);
    memcpy(&r.x, d.data() + off + 4, 4);
    memcpy(&r.y, d.data() + off + 8, 4);
    memcpy(&r.hp, d.data() + off + 12, 4);
    memcpy(&r.max_hp, d.data() + off + 16, 4);
    r.alive = d[off + 20];
    return r;
}

struct SyncEntityData {
    int id;
    float x, y;
    int hp, max_hp;
    bool alive;
    int team;
};

inline SyncEntityData parse_sync_entity(std::vector<uint8_t> const &d,
                                        size_t off = 0)
{
    SyncEntityData r;
    memcpy(&r.id, d.data() + off, 4);
    memcpy(&r.x, d.data() + off + 4, 4);
    memcpy(&r.y, d.data() + off + 8, 4);
    memcpy(&r.hp, d.data() + off + 12, 4);
    memcpy(&r.max_hp, d.data() + off + 16, 4);
    r.alive = d[off + 20];
    r.team = d[off + 21];
    return r;
}

struct CombatEventData {
    int attacker_id, defender_id, damage;
    bool killed;
};

inline CombatEventData parse_combat_event(std::vector<uint8_t> const &d)
{
    CombatEventData r;
    memcpy(&r.attacker_id, d.data(), 4);
    memcpy(&r.defender_id, d.data() + 4, 4);
    memcpy(&r.damage, d.data() + 8, 4);
    r.killed = d[12];
    return r;
}

struct PlayerInputData {
    uint32_t pid;
    float mx, my;
};

inline PlayerInputData parse_player_input(std::vector<uint8_t> const &d)
{
    PlayerInputData r;
    memcpy(&r.pid, d.data(), 4);
    memcpy(&r.mx, d.data() + 4, 4);
    memcpy(&r.my, d.data() + 8, 4);
    return r;
}

struct EnemyWaveData {
    float cx, cy;
    int count;
    uint8_t team;
};

inline EnemyWaveData parse_enemy_wave(std::vector<uint8_t> const &d)
{
    EnemyWaveData r;
    memcpy(&r.cx, d.data(), 4);
    memcpy(&r.cy, d.data() + 4, 4);
    memcpy(&r.count, d.data() + 8, 4);
    r.team = d[12];
    return r;
}
