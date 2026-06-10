#include "net/server.hpp"
#include "net/network-manager.hpp"
#include "systems/combat-system.hpp"
#include <unordered_set>
#include "world/world-state.hpp"
#include "systems/quest-manager.hpp"
Server::Server() {}
Server::~Server() {}

void Server::set_managers(CombatSystem* cs, WorldState* ws, QuestManager* qm) {
    cs_ = cs; ws_ = ws; qm_ = qm;
}

EntityId Server::spawn_player(int id) {
    // Placeholder — actual spawning done via GameMode
    return em_.create_entity();
}

void Server::update(float dt, NetworkManager& net) {
    if (!cs_) return;

    // Run authoritative combat
    cs_->update(em_, dt);
    for (const auto& ev : cs_->events())
        if (ev.killed) qm_->report_kill("enemy");

    // Sync to clients every 50ms
    sync_timer_ += dt;
    if (sync_timer_ >= 0.05f) {
        sync_timer_ = 0.f;
        auto payload = build_sync_payload();
        if (!payload.empty()) net.send_full_sync(payload);
    }
}

void Server::handle_combat_event(int attacker_id, int defender_id, int damage, bool killed) {
    auto* cs = em_.get_component<CombatStats>(defender_id);
    if (cs) { cs->hp -= damage; if (killed) cs->alive = false; }
}

EntityId Server::add_entity(Vec2f pos, Team team, int hp, int max_hp) {
    auto eid = em_.create_entity();
    em_.add_component<Position>(eid, Position{pos, {0,0}, 0.5f});
    em_.add_component<CombatStats>(eid, CombatStats{team, max_hp, hp, 3, 2, 80.f});
    return eid;
}

void Server::remove_entity(EntityId id) {
    em_.destroy_entity(id);
}

bool Server::remote_player(int player_id) {
    return remote_player_map_.contains(player_id);
}

void Server::send_full_state(NetworkManager& net) {
    net.send_full_sync(build_sync_payload());
}

std::vector<uint8_t> Server::build_sync_payload() {
    std::unordered_set<EntityId> remoteIds;
    for (const auto& [pid, eid] : remote_player_map_) remoteIds.insert(eid);
    std::vector<uint8_t> out;
    for (auto id : em_.all_entities()) {
        if (remoteIds.contains(id)) continue;
        auto* ep = em_.get_component<Position>(id);
        auto* ec = em_.get_component<CombatStats>(id);
        if (!ep || !ec) continue;
        int nid = static_cast<int>(id);
        float x = ep->world_pos.x, y = ep->world_pos.y;
        int hp = ec->hp, max_hp = ec->max_hp;
        uint8_t alive = ec->alive ? 1 : 0;
        uint8_t team = static_cast<uint8_t>(ec->team);
        auto push = [&](auto v) { auto p = (uint8_t*)&v; out.insert(out.end(), p, p + sizeof(v)); };
        push(nid); push(x); push(y); push(hp); push(max_hp);
        out.push_back(alive); out.push_back(team);
    }
    return out;
}

void Server::add_remote_player(int player_id, Vec2f pos) {
    auto eid = em_.create_entity();
    remote_player_map_[player_id] = eid;
    em_.add_component<Position>(eid, Position{pos, {0,0}, 1.f});
    em_.add_component<CombatStats>(eid, CombatStats{Team::player, 20, 20, 4, 3, 80.f});
    mark_needs_full_sync();
}

void Server::update_remote_player(int player_id, Vec2f pos, int hp, int max_hp, bool alive) {
    auto it = remote_player_map_.find(player_id);
    if (it == remote_player_map_.end()) return;
    auto* p = em_.get_component<Position>(it->second);
    auto* c = em_.get_component<CombatStats>(it->second);
    if (p) p->world_pos = pos;
    if (c) { c->hp = hp; c->alive = alive; }
    // Send initial full state on first update
    if (!sent_initial_sync_[player_id]) {
        sent_initial_sync_[player_id] = true;
        // Can't send here, need NetworkManager. Handled in App callback.
    }
}
