#pragma once

#include "core/game-types.hpp"
#include <array>
#include <bit>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <format>
#include <string>
#include <vector>

// Component bitmask for entity sync — each bit corresponds to a
// component block in the per-entity payload.  LSB is checked first.
namespace SyncComponent {
enum Mask : uint16_t {
    entity_kind = 1 << 0, // EntityKindComp    (1 byte)
    position = 1 << 1,    // PositionComp      (8 bytes)
    combat = 1 << 2,      // CombatComp        (22 bytes:
                          // hp,max_hp,alive,team,atk,def,range)
    movement = 1 << 3,    // MovementComp      (9 bytes)
    soldier_ai = 1 << 4,  // SoldierAIComp     (17 bytes)
    interact = 1 << 5,    // InteractComp      (1 byte)
    survival = 1 << 6, // SurvivalComp      (16 bytes: food,water,health,energy)
};
constexpr uint16_t wire_size(uint16_t mask)
{
    uint16_t sz = 0;
    if (mask & entity_kind)
        sz += 1;
    if (mask & position)
        sz += 8;
    if (mask & combat)
        sz += 22;
    if (mask & movement)
        sz += 9;
    if (mask & soldier_ai)
        sz += 17;
    if (mask & interact)
        sz += 1;
    if (mask & survival)
        sz += 16;
    return sz;
}
} // namespace SyncComponent

// Entity kind values for SyncComponent::entity_kind
namespace EntityKind {
enum Value : uint8_t {
    player = 1,
    soldier = 2,
    npc = 3,
    enemy = 4,
    structure = 5,
};
}

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
        dialogue_sync,
        dialogue_action,
        entity_removed,
        state_delta,
        kicked,
    };
    Type type;
    std::vector<uint8_t> payload;
};

template <>
struct std::formatter<NetPacket::Type> : std::formatter<std::string_view> {
    auto format(NetPacket::Type t, std::format_context &ctx) const
    {
        using enum NetPacket::Type;
        std::string_view name = "unknown";
        switch (t) {
        case join:
            name = "join";
            break;
        case state_full:
            name = "state_full";
            break;
        case entity_update:
            name = "entity_update";
            break;
        case chat:
            name = "chat";
            break;
        case disconnect:
            name = "disconnect";
            break;
        case combat_event:
            name = "combat_event";
            break;
        case recruit_soldier:
            name = "recruit_soldier";
            break;
        case spawn_enemy_wave:
            name = "spawn_enemy_wave";
            break;
        case player_input:
            name = "player_input";
            break;
        case interact:
            name = "interact";
            break;
        case rest:
            name = "rest";
            break;
        case return_pid:
            name = "return_pid";
            break;
        case dialogue_sync:
            name = "dialogue_sync";
            break;
        case dialogue_action:
            name = "dialogue_action";
            break;
        case entity_removed:
            name = "entity_removed";
            break;
        case state_delta:
            name = "state_delta";
            break;
        case kicked:
            name = "kicked";
            break;
        }
        return std::formatter<std::string_view>::format(name, ctx);
    }
};

struct NetHead {
    using Type = NetPacket::Type;
    Type type;
    std::uint32_t size;
};

// Serialize an integral value into bytes (little-endian), sizeof(T) bytes
template <std::integral T> void write_bytes(std::vector<uint8_t> &out, T val)
{
    if constexpr (std::endian::native != std::endian::little)
        val = std::byteswap(val);
    auto bytes = std::bit_cast<std::array<uint8_t, sizeof(T)>>(val);
    out.insert(out.end(), bytes.begin(), bytes.end());
}

inline void write_float(std::vector<uint8_t> &out, float val)
{
    auto bytes = std::bit_cast<std::array<uint8_t, 4>>(val);
    out.insert(out.end(), bytes.begin(), bytes.end());
}

// Deserialize helpers
template <std::integral T>
T read_bytes(std::vector<uint8_t> const &data, size_t offset)
{
    std::array<uint8_t, sizeof(T)> arr{};
    for (size_t i = 0; i < sizeof(T); ++i)
        arr[i] = data[offset + i];
    if constexpr (std::endian::native != std::endian::little)
        return std::byteswap(std::bit_cast<T>(arr));
    return std::bit_cast<T>(arr);
}

inline float read_float(std::vector<uint8_t> const &data, size_t offset)
{
    std::array<uint8_t, 4> arr{data[offset], data[offset + 1], data[offset + 2],
                               data[offset + 3]};
    return std::bit_cast<float>(arr);
}

// --- Wire format structs ---
// Entity IDs are now 8 bytes (uint64_t / EntityId) matching flecs::entity_t

struct EntityUpdateData {
    EntityId id;
    float x, y;
    int hp, max_hp;
    bool alive;
};

// Layout: id(8) + x(4) + y(4) + hp(4) + max_hp(4) + alive(1) = 25 bytes
inline EntityUpdateData parse_entity_update(std::vector<uint8_t> const &d,
                                            size_t off = 0)
{
    EntityUpdateData r;
    memcpy(&r.id, d.data() + off, 8);
    memcpy(&r.x, d.data() + off + 8, 4);
    memcpy(&r.y, d.data() + off + 12, 4);
    memcpy(&r.hp, d.data() + off + 16, 4);
    memcpy(&r.max_hp, d.data() + off + 20, 4);
    r.alive = d[off + 24];
    return r;
}

struct SyncEntityData {
    EntityId id;
    float x, y;
    int hp, max_hp;
    bool alive;
    int team;
    uint8_t flags;
};

// Layout: id(8)+x(4)+y(4)+hp(4)+max_hp(4)+alive(1)+team(1)+flags(1) = 27 bytes
inline SyncEntityData parse_sync_entity(std::vector<uint8_t> const &d,
                                        size_t off = 0)
{
    SyncEntityData r;
    memcpy(&r.id, d.data() + off, 8);
    memcpy(&r.x, d.data() + off + 8, 4);
    memcpy(&r.y, d.data() + off + 12, 4);
    memcpy(&r.hp, d.data() + off + 16, 4);
    memcpy(&r.max_hp, d.data() + off + 20, 4);
    r.alive = d[off + 24];
    r.team = d[off + 25];
    r.flags = d[off + 26];
    return r;
}

struct CombatEventData {
    EntityId attacker_id, defender_id;
    int damage;
    bool killed;
};

// Layout: attacker(8)+defender(8)+damage(4)+killed(1) = 21 bytes
inline CombatEventData parse_combat_event(std::vector<uint8_t> const &d)
{
    CombatEventData r;
    memcpy(&r.attacker_id, d.data(), 8);
    memcpy(&r.defender_id, d.data() + 8, 8);
    memcpy(&r.damage, d.data() + 16, 4);
    r.killed = d[20];
    return r;
}

struct PlayerInputData {
    EntityId pid;
    float mx, my;
};

// Layout: pid(8)+mx(4)+my(4) = 16 bytes
inline PlayerInputData parse_player_input(std::vector<uint8_t> const &d)
{
    PlayerInputData r;
    memcpy(&r.pid, d.data(), 8);
    memcpy(&r.mx, d.data() + 8, 4);
    memcpy(&r.my, d.data() + 12, 4);
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

// --- Convenience packet builders ---

inline std::vector<uint8_t> make_entity_update(EntityId id, float x, float y,
                                               int hp, int max_hp, bool alive)
{
    std::vector<uint8_t> p;
    write_bytes(p, id);
    write_float(p, x);
    write_float(p, y);
    write_bytes(p, hp);
    write_bytes(p, max_hp);
    p.push_back(alive ? 1 : 0);
    return serialize_packet(NetPacket{NetPacket::entity_update, std::move(p)});
}

inline std::vector<uint8_t> make_combat_event(EntityId att_id, EntityId def_id,
                                              int dmg, bool killed)
{
    std::vector<uint8_t> p;
    write_bytes(p, att_id);
    write_bytes(p, def_id);
    write_bytes(p, dmg);
    p.push_back(killed ? 1 : 0);
    return serialize_packet(NetPacket{NetPacket::combat_event, std::move(p)});
}

inline std::vector<uint8_t> make_chat(std::string const &msg)
{
    std::vector<uint8_t> p(msg.begin(), msg.end());
    return serialize_packet(NetPacket{NetPacket::chat, std::move(p)});
}

inline std::vector<uint8_t> make_entity_removed(EntityId eid)
{
    std::vector<uint8_t> p;
    write_bytes(p, eid);
    return serialize_packet(NetPacket{NetPacket::entity_removed, std::move(p)});
}

// --- Dialogue sync parsing ---

struct DialogueLineData {
    uint8_t speaker;
    std::string text;
    bool use_raw;
    std::string npc_name;
};

struct DialogueSyncData {
    std::string npc_name;
    int npc_trust;
    std::vector<DialogueLineData> lines;
    std::vector<std::string> topics;
    std::vector<std::string> actions;
    bool can_gift;
    bool can_threaten;
};

inline DialogueSyncData parse_dialogue_sync(std::vector<uint8_t> const &d)
{
    DialogueSyncData r;
    size_t off = 0;
    auto rstr = [&]() {
        uint16_t len;
        memcpy(&len, d.data() + off, 2);
        off += 2;
        std::string s(d.begin() + off, d.begin() + off + len);
        off += len;
        return s;
    };
    r.npc_name = rstr();
    memcpy(&r.npc_trust, d.data() + off, 4);
    off += 4;
    uint8_t line_count = d[off++];
    for (uint8_t i = 0; i < line_count; ++i) {
        DialogueLineData l;
        l.speaker = d[off++];
        l.text = rstr();
        l.use_raw = d[off++];
        l.npc_name = rstr();
        r.lines.push_back(std::move(l));
    }
    uint8_t topic_count = d[off++];
    for (uint8_t i = 0; i < topic_count; ++i)
        r.topics.push_back(rstr());
    uint8_t action_count = d[off++];
    for (uint8_t i = 0; i < action_count; ++i)
        r.actions.push_back(rstr());
    uint8_t flags = d[off];
    r.can_gift = flags & 1;
    r.can_threaten = flags & 2;
    return r;
}

inline void serialize_dialogue_sync(std::vector<uint8_t> &out,
                                    DialogueState const &ds)
{
    if (!ds.active || ds.npc_name.empty()) {
        write_bytes(out, uint32_t{0}); // name_len=0 → client clears dialogue
        return;
    }
    write_bytes(out, static_cast<uint32_t>(ds.npc_name.size()));
    // npc_name (2-byte-prefixed, matching parse_dialogue_sync's rstr)
    write_bytes(out, static_cast<uint16_t>(ds.npc_name.size()));
    out.insert(out.end(), ds.npc_name.begin(), ds.npc_name.end());
    // npc_trust
    write_bytes(out, ds.npc_trust);
    // lines
    out.push_back(static_cast<uint8_t>(ds.history.size()));
    for (auto const &line : ds.history) {
        out.push_back(static_cast<uint8_t>(line.speaker));
        auto const &text = line.use_raw ? line.raw_text : line.text_key;
        uint16_t tlen = static_cast<uint16_t>(text.size());
        write_bytes(out, tlen);
        out.insert(out.end(), text.begin(), text.end());
        out.push_back(line.use_raw ? 1 : 0);
        uint16_t nlen = static_cast<uint16_t>(line.npc_name.size());
        write_bytes(out, nlen);
        out.insert(out.end(), line.npc_name.begin(), line.npc_name.end());
    }
    // topics
    out.push_back(static_cast<uint8_t>(ds.available_topics.size()));
    for (auto const &t : ds.available_topics) {
        uint16_t tlen = static_cast<uint16_t>(t.size());
        write_bytes(out, tlen);
        out.insert(out.end(), t.begin(), t.end());
    }
    // actions
    out.push_back(static_cast<uint8_t>(ds.available_actions.size()));
    for (auto const &a : ds.available_actions) {
        uint16_t alen = static_cast<uint16_t>(a.size());
        write_bytes(out, alen);
        out.insert(out.end(), a.begin(), a.end());
    }
    // flags: bit0=can_gift, bit1=can_threaten
    uint8_t flags =
        (ds.can_gift ? 1 : 0) | (ds.can_threaten ? 2 : 0);
    out.push_back(flags);
}
