#include "net/client.hpp"
#include "net/network-manager.hpp"
#include "systems/combat-system.hpp"
#include "world/world-state.hpp"
#include "systems/quest-manager.hpp"
#include "entities/components/position.hpp"
#include "entities/components/sprite.hpp"
Client::Client() {
    my_player_ = em_.create_entity();
    em_.add_component<Position>(my_player_, Position{{0,0}, {0,0}, 1.f});
    em_.add_component<Sprite>(my_player_, Sprite{"player", {}, {16, 16}, {0.3f, 0.8f, 0.3f, 1.f}, 1.f, true});
    em_.add_component<CombatStats>(my_player_, CombatStats{Team::player, 20, 20, 4, 3, 80.f});
}

void Client::set_managers(CombatSystem*, WorldState*, QuestManager*) {}

EntityId Client::local_player() const { return my_player_; }

Vec2f Client::local_player_pos() {
    auto* p = em_.get_component<Position>(my_player_);
    return p ? p->world_pos : Vec2f{};
}

void Client::set_local_player_pos(Vec2f pos) {
    auto* p = em_.get_component<Position>(my_player_);
    if (p) p->world_pos = pos;
}

void Client::update(float dt, NetworkManager& net) {
    (void)dt;
    net.set_callback([this](const NetMessage& msg) {
        if (msg.type == NetMessage::state_full) {
            apply_sync(msg.data);
        } else if (msg.type == NetMessage::combat_event && msg.data.size() >= 13) {
            int att, def, dmg; uint8_t k;
            auto read = [&](int off) { int v; memcpy(&v, msg.data.data()+off, 4); return v; };
            att = read(0); def = read(4); dmg = read(8); k = msg.data[12];
            handle_combat_event(att, def, dmg, k);
        }
    });
}

void Client::apply_sync(const std::vector<uint8_t>& data) {
    for (size_t i = 0; i + 22 <= data.size(); i += 22) {
        auto readInt = [&](size_t off) { int v; memcpy(&v, data.data()+i+off, 4); return v; };
        auto read_float = [&](size_t off) { float v; memcpy(&v, data.data()+i+off, 4); return v; };
        int nid = readInt(0);
        // Skip our own player entity (ID 0)
        if (nid == 0) continue;
        float x = read_float(4), y = read_float(8);
        int hp = readInt(12), max_hp = readInt(16);
        bool alive = data[i+20] != 0;
        int team = data[i+21];

        auto it = id_map_.find(nid);
        if (it == id_map_.end()) {
            auto eid = em_.create_entity();
            id_map_[nid] = eid;
            em_.add_component<Position>(eid, Position{{x,y}, {0,0}, 0.5f});
            em_.add_component<Sprite>(eid, Sprite{"", {}, {12, 12}, {1,1,1,1}, 0.8f, true});
            em_.add_component<CombatStats>(eid, CombatStats{
                team == 1 ? Team::enemy : (team == 2 ? Team::neutral : Team::player),
                max_hp, hp, 3, 2, 80.f
            });
        } else {
            auto eid = it->second;
            auto* p = em_.get_component<Position>(eid);
            auto* c = em_.get_component<CombatStats>(eid);
            if (p) p->world_pos = {x, y};
            if (c) { c->hp = hp; c->alive = alive; }
        }
    }
}

void Client::handle_combat_event(int attacker_id, int defender_id, int damage, bool killed) {
    auto it = id_map_.find(defender_id);
    EntityId eid = (it != id_map_.end()) ? it->second : invalid_entity;
    auto* cs = em_.get_component<CombatStats>(eid);
    if (cs) { cs->hp -= damage; if (killed) cs->alive = false; }
}
