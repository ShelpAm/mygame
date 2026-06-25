#pragma once

#include "core/game-types.hpp"
#include <algorithm>
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
    movement = 1 << 3,    // MovementComp      (16 bytes: vx,vy,fx,fy)
    soldier_ai = 1 << 4,  // SoldierAIComp     (17 bytes)
    interact = 1 << 5,    // InteractComp      (1 byte)
    survival = 1 << 6,    // SurvivalComp      (16 bytes: food,water,health,energy)
    vision = 1 << 7,      // VisionComp        (4 bytes: range)
};
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

inline auto const &auth_payload()
{
    constexpr std::string_view auth_string = "thesunsetstraits";
    static std::vector<std::uint8_t> auth_payload(auth_string.begin(), auth_string.end());
    return auth_payload;
}

enum class ClientMsgType : std::uint32_t {
    auth = -1U,
    join = 0,
    entity_update = 2,
    chat = 3,
    combat_event = 5,
    recruit_soldier = 6,
    player_input = 8,
    interact = 9,
    rest = 10,
    dialogue_action = 13,
    soldier_command = 17,
    recruit_ranged = 18,
    respawn = 20,
    formation = 21,
};

enum class ServerMsgType : std::uint32_t {
    auth = -1U,
    state_full = 1,
    entity_update = 2,
    chat = 3,
    combat_event = 5,
    return_pid = 11,
    dialogue_sync = 12,
    entity_removed = 14,
    state_delta = 15,
    kicked = 16,
    projectile_fired = 19,
    town_discovered = 22,
    town_left = 23,
};

struct NetPacket {
    std::uint32_t type;
    std::vector<uint8_t> payload;
};

template <> struct std::formatter<ClientMsgType> {
    constexpr auto parse(std::format_parse_context &ctx) { return ctx.begin(); }
    auto format(ClientMsgType t, std::format_context &ctx) const
    {
        std::string_view name = "unknown";
        switch (t) {
        case ClientMsgType::join:
            name = "join";
            break;
        case ClientMsgType::entity_update:
            name = "entity_update";
            break;
        case ClientMsgType::chat:
            name = "chat";
            break;
        case ClientMsgType::combat_event:
            name = "combat_event";
            break;
        case ClientMsgType::recruit_soldier:
            name = "recruit_soldier";
            break;
        case ClientMsgType::player_input:
            name = "player_input";
            break;
        case ClientMsgType::interact:
            name = "interact";
            break;
        case ClientMsgType::rest:
            name = "rest";
            break;
        case ClientMsgType::dialogue_action:
            name = "dialogue_action";
            break;
        case ClientMsgType::soldier_command:
            name = "soldier_command";
            break;
        case ClientMsgType::recruit_ranged:
            name = "recruit_ranged";
            break;
        case ClientMsgType::respawn:
            name = "respawn";
            break;
        case ClientMsgType::formation:
            name = "formation";
            break;
        case ClientMsgType::auth:
            name = "auth";
            break;
        }
        return std::copy(name.begin(), name.end(), ctx.out());
    }
};

template <> struct std::formatter<ServerMsgType> {
    constexpr auto parse(std::format_parse_context &ctx) { return ctx.begin(); }
    auto format(ServerMsgType t, std::format_context &ctx) const
    {
        std::string_view name = "unknown";
        switch (t) {
        case ServerMsgType::state_full:
            name = "state_full";
            break;
        case ServerMsgType::entity_update:
            name = "entity_update";
            break;
        case ServerMsgType::chat:
            name = "chat";
            break;
        case ServerMsgType::combat_event:
            name = "combat_event";
            break;
        case ServerMsgType::return_pid:
            name = "return_pid";
            break;
        case ServerMsgType::dialogue_sync:
            name = "dialogue_sync";
            break;
        case ServerMsgType::entity_removed:
            name = "entity_removed";
            break;
        case ServerMsgType::state_delta:
            name = "state_delta";
            break;
        case ServerMsgType::kicked:
            name = "kicked";
            break;
        case ServerMsgType::projectile_fired:
            name = "projectile_fired";
            break;
        case ServerMsgType::town_discovered:
            name = "town_discovered";
            break;
        case ServerMsgType::town_left:
            name = "town_left";
            break;
        case ServerMsgType::auth:
            name = "auth";
            break;
        }
        return std::copy(name.begin(), name.end(), ctx.out());
    }
};

// Streaming operators for Boost.Test and logging
inline std::ostream &operator<<(std::ostream &os, ClientMsgType t)
{
    return os << std::format("{}", t);
}
inline std::ostream &operator<<(std::ostream &os, ServerMsgType t)
{
    return os << std::format("{}", t);
}

struct NetHead {
    std::uint32_t type;
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
template <std::integral T> T read_bytes(std::vector<uint8_t> const &data, size_t offset)
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
    std::array<uint8_t, 4> arr{data[offset], data[offset + 1], data[offset + 2], data[offset + 3]};
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
inline EntityUpdateData parse_entity_update(std::vector<uint8_t> const &d, size_t off = 0)
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
inline SyncEntityData parse_sync_entity(std::vector<uint8_t> const &d, size_t off = 0)
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
    uint32_t client_ms;
};

// Layout: pid(8)+mx(4)+my(4)+client_ms(4) = 20 bytes
inline PlayerInputData parse_player_input(std::vector<uint8_t> const &d)
{
    PlayerInputData r;
    memcpy(&r.pid, d.data(), 8);
    memcpy(&r.mx, d.data() + 8, 4);
    memcpy(&r.my, d.data() + 12, 4);
    memcpy(&r.client_ms, d.data() + 16, 4);
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
// Each returns the raw payload (no header). ITransport::write()
// calls serialize_packet() to produce the final wire format.

inline std::vector<uint8_t> make_entity_update(EntityId id, float x, float y, int hp, int max_hp,
                                               bool alive)
{
    std::vector<uint8_t> p;
    write_bytes(p, id);
    write_float(p, x);
    write_float(p, y);
    write_bytes(p, hp);
    write_bytes(p, max_hp);
    p.push_back(alive ? 1 : 0);
    return p;
}

inline std::vector<uint8_t> make_combat_event(EntityId att_id, EntityId def_id, int dmg,
                                              bool killed)
{
    std::vector<uint8_t> p;
    write_bytes(p, att_id);
    write_bytes(p, def_id);
    write_bytes(p, dmg);
    p.push_back(killed ? 1 : 0);
    return p;
}

inline std::vector<uint8_t> make_chat(std::string const &msg)
{
    return std::vector<uint8_t>(msg.begin(), msg.end());
}

inline std::vector<uint8_t> make_entity_removed(EntityId eid)
{
    std::vector<uint8_t> p;
    write_bytes(p, eid);
    return p;
}

inline std::vector<uint8_t> make_player_input(EntityId pid, float mx, float my, uint32_t client_ms)
{
    std::vector<uint8_t> p;
    write_bytes(p, pid);
    write_float(p, mx);
    write_float(p, my);
    write_bytes(p, client_ms);
    return p;
}

inline std::vector<uint8_t> make_entity_id_payload(EntityId id)
{
    std::vector<uint8_t> p;
    write_bytes(p, id);
    return p;
}

// pid(8) + role_mask(1)
inline std::vector<uint8_t> make_formation_payload(EntityId id, uint8_t role_mask)
{
    std::vector<uint8_t> p;
    write_bytes(p, id);
    p.push_back(role_mask);
    return p;
}

inline std::vector<uint8_t> make_projectile_fired(float sx, float sy, float tx, float ty)
{
    std::vector<uint8_t> p;
    write_float(p, sx);
    write_float(p, sy);
    write_float(p, tx);
    write_float(p, ty);
    return p;
}

inline std::vector<uint8_t> make_return_pid(EntityId eid, uint8_t team)
{
    std::vector<uint8_t> p;
    write_bytes(p, eid);
    write_bytes(p, team);
    return p;
}

// --- Town discovery data ---

struct TownDiscoveredData {
    std::string loc_id;
    std::string locale_key;
};

inline TownDiscoveredData parse_town_discovered(std::vector<uint8_t> const &d, size_t off = 0)
{
    TownDiscoveredData r;
    uint16_t id_len;
    memcpy(&id_len, d.data() + off, 2);
    off += 2;
    r.loc_id.assign(d.begin() + static_cast<std::ptrdiff_t>(off),
                     d.begin() + static_cast<std::ptrdiff_t>(off) + id_len);
    off += id_len;
    uint16_t key_len;
    memcpy(&key_len, d.data() + off, 2);
    off += 2;
    r.locale_key.assign(d.begin() + static_cast<std::ptrdiff_t>(off),
                         d.begin() + static_cast<std::ptrdiff_t>(off) + key_len);
    return r;
}

inline std::vector<uint8_t> make_town_discovered(std::string const &loc_id,
                                                  std::string const &locale_key)
{
    std::vector<uint8_t> p;
    uint16_t id_len = static_cast<uint16_t>(loc_id.size());
    write_bytes(p, id_len);
    p.insert(p.end(), loc_id.begin(), loc_id.end());
    uint16_t key_len = static_cast<uint16_t>(locale_key.size());
    write_bytes(p, key_len);
    p.insert(p.end(), locale_key.begin(), locale_key.end());
    return p;
}

// --- Dialogue sync parsing ---

struct DialogueLineData {
    uint8_t speaker;
    bool use_template = false;
    // For template-based lines (use_template=true)
    std::string template_type;
    int variant_index = 0;
    std::vector<std::pair<std::string, std::string>> slots;
    // For non-template lines (use_template=false)
    std::string text;
    bool use_raw = false;
    // Always present
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
        uint8_t flags = d[off++];
        l.use_template = flags & 1;
        if (l.use_template) {
            l.template_type = rstr();
            memcpy(&l.variant_index, d.data() + off, 4);
            off += 4;
            uint16_t slot_count;
            memcpy(&slot_count, d.data() + off, 2);
            off += 2;
            for (uint16_t j = 0; j < slot_count; ++j) {
                auto key = rstr();
                auto val = rstr();
                l.slots.emplace_back(std::move(key), std::move(val));
            }
        }
        else {
            l.text = rstr();
            l.use_raw = flags & 2;
        }
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

inline void serialize_dialogue_sync(std::vector<uint8_t> &out, DialogueState const &ds)
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
        uint8_t flags = (line.use_template ? 1 : 0) | (line.use_raw ? 2 : 0);
        out.push_back(flags);
        if (line.use_template) {
            // template_type
            uint16_t ttlen = static_cast<uint16_t>(line.template_type.size());
            write_bytes(out, ttlen);
            out.insert(out.end(), line.template_type.begin(), line.template_type.end());
            // variant_index
            write_bytes(out, line.variant_index);
            // slots
            uint16_t sc = static_cast<uint16_t>(line.slots.size());
            write_bytes(out, sc);
            for (auto const &[key, val] : line.slots) {
                uint16_t klen = static_cast<uint16_t>(key.size());
                write_bytes(out, klen);
                out.insert(out.end(), key.begin(), key.end());
                uint16_t vlen = static_cast<uint16_t>(val.size());
                write_bytes(out, vlen);
                out.insert(out.end(), val.begin(), val.end());
            }
        }
        else {
            auto const &text = line.use_raw ? line.raw_text : line.text_key;
            uint16_t tlen = static_cast<uint16_t>(text.size());
            write_bytes(out, tlen);
            out.insert(out.end(), text.begin(), text.end());
            // Note: use_raw flag was already sent in the flags byte
        }
        // npc_name (always)
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
    uint8_t flags = (ds.can_gift ? 1 : 0) | (ds.can_threaten ? 2 : 0);
    out.push_back(flags);
}
