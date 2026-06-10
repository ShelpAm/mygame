#include "net/Server.hpp"
#include "net/NetworkManager.hpp"
#include "systems/CombatSystem.hpp"
#include <unordered_set>
#include "world/WorldState.hpp"
#include "systems/QuestManager.hpp"

Server::Server() {}
Server::~Server() {}

void Server::setManagers(CombatSystem* cs, WorldState* ws, QuestManager* qm) {
    m_cs = cs; m_ws = ws; m_qm = qm;
}

EntityId Server::spawnPlayer(int id) {
    // Placeholder — actual spawning done via GameMode
    return m_em.createEntity();
}

void Server::update(float dt, NetworkManager& net) {
    if (!m_cs) return;

    // Run authoritative combat
    m_cs->update(m_em, dt);
    for (const auto& ev : m_cs->events())
        if (ev.killed) m_qm->reportKill("enemy");

    // Sync to clients every 50ms
    m_syncTimer += dt;
    if (m_syncTimer >= 0.05f) {
        m_syncTimer = 0.f;
        auto payload = buildSyncPayload();
        if (!payload.empty()) net.sendFullSync(payload);
    }
}

void Server::handleCombatEvent(int attackerId, int defenderId, int damage, bool killed) {
    auto* cs = m_em.getComponent<CombatStats>(defenderId);
    if (cs) { cs->hp -= damage; if (killed) cs->alive = false; }
}

EntityId Server::addEntity(Vec2f pos, Team team, int hp, int maxHp) {
    auto eid = m_em.createEntity();
    m_em.addComponent<Position>(eid, Position{pos, {0,0}, 0.5f});
    m_em.addComponent<CombatStats>(eid, CombatStats{team, maxHp, hp, 3, 2, 80.f});
    return eid;
}

void Server::removeEntity(EntityId id) {
    m_em.destroyEntity(id);
}

bool Server::remotePlayer(int playerId) {
    return m_remotePlayerMap.contains(playerId);
}

void Server::addRemotePlayer(int playerId, Vec2f pos) {
    auto eid = m_em.createEntity();
    m_remotePlayerMap[playerId] = eid;
    m_em.addComponent<Position>(eid, Position{pos, {0,0}, 1.f});
    m_em.addComponent<CombatStats>(eid, CombatStats{Team::Enemy, 20, 20, 4, 3, 80.f});
}

void Server::updateRemotePlayer(int playerId, Vec2f pos, int hp, int maxHp, bool alive) {
    auto it = m_remotePlayerMap.find(playerId);
    if (it == m_remotePlayerMap.end()) return;
    auto* p = m_em.getComponent<Position>(it->second);
    auto* c = m_em.getComponent<CombatStats>(it->second);
    if (p) p->worldPos = pos;
    if (c) { c->hp = hp; c->alive = alive; }
}

std::vector<uint8_t> Server::buildSyncPayload() {
    // Collect IDs of remote players so we don't sync them back
    std::unordered_set<EntityId> remoteIds;
    for (const auto& [pid, eid] : m_remotePlayerMap) remoteIds.insert(eid);

    std::vector<uint8_t> out;
    for (auto id : m_em.allEntities()) {
        if (remoteIds.contains(id)) continue;  // Skip remote players
        auto* ep = m_em.getComponent<Position>(id);
        auto* ec = m_em.getComponent<CombatStats>(id);
        if (!ep || !ec) continue;
        int nid = static_cast<int>(id);
        float x = ep->worldPos.x, y = ep->worldPos.y;
        int hp = ec->hp, maxHp = ec->maxHp;
        uint8_t alive = ec->alive ? 1 : 0;
        uint8_t team = static_cast<uint8_t>(ec->team);
        auto push = [&](auto v) { auto p = (uint8_t*)&v; out.insert(out.end(), p, p + sizeof(v)); };
        push(nid); push(x); push(y); push(hp); push(maxHp);
        out.push_back(alive); out.push_back(team);
    }
    return out;
}
