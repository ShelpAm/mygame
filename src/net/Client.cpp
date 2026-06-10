#include "net/Client.hpp"
#include "net/NetworkManager.hpp"
#include "systems/CombatSystem.hpp"
#include "world/WorldState.hpp"
#include "systems/QuestManager.hpp"
#include "entities/components/Position.hpp"
#include "entities/components/Sprite.hpp"

Client::Client() {
    m_myPlayer = m_em.createEntity();
    m_em.addComponent<Position>(m_myPlayer, Position{{0,0}, {0,0}, 1.f});
    m_em.addComponent<Sprite>(m_myPlayer, Sprite{"player", {}, {16, 16}, {0.3f, 0.8f, 0.3f, 1.f}, 1.f, true});
    m_em.addComponent<CombatStats>(m_myPlayer, CombatStats{Team::Player, 20, 20, 4, 3, 80.f});
}

void Client::setManagers(CombatSystem*, WorldState*, QuestManager*) {}

EntityId Client::localPlayer() const { return m_myPlayer; }

Vec2f Client::localPlayerPos() {
    auto* p = m_em.getComponent<Position>(m_myPlayer);
    return p ? p->worldPos : Vec2f{};
}

void Client::setLocalPlayerPos(Vec2f pos) {
    auto* p = m_em.getComponent<Position>(m_myPlayer);
    if (p) p->worldPos = pos;
}

void Client::update(float dt, NetworkManager& net) {
    (void)dt;
    net.setCallback([this](const NetMessage& msg) {
        if (msg.type == NetMessage::StateFull) {
            applySync(msg.data);
        } else if (msg.type == NetMessage::CombatEvent && msg.data.size() >= 13) {
            int att, def, dmg; uint8_t k;
            auto read = [&](int off) { int v; memcpy(&v, msg.data.data()+off, 4); return v; };
            att = read(0); def = read(4); dmg = read(8); k = msg.data[12];
            handleCombatEvent(att, def, dmg, k);
        }
    });
}

void Client::applySync(const std::vector<uint8_t>& data) {
    for (size_t i = 0; i + 22 <= data.size(); i += 22) {
        auto readInt = [&](size_t off) { int v; memcpy(&v, data.data()+i+off, 4); return v; };
        auto readFloat = [&](size_t off) { float v; memcpy(&v, data.data()+i+off, 4); return v; };
        int nid = readInt(0);
        // Skip our own player entity (ID 0)
        if (nid == 0) continue;
        float x = readFloat(4), y = readFloat(8);
        int hp = readInt(12), maxHp = readInt(16);
        bool alive = data[i+20] != 0;
        int team = data[i+21];

        auto it = m_idMap.find(nid);
        if (it == m_idMap.end()) {
            auto eid = m_em.createEntity();
            m_idMap[nid] = eid;
            m_em.addComponent<Position>(eid, Position{{x,y}, {0,0}, 0.5f});
            m_em.addComponent<Sprite>(eid, Sprite{"", {}, {12, 12}, {1,1,1,1}, 0.8f, true});
            m_em.addComponent<CombatStats>(eid, CombatStats{
                team == 1 ? Team::Enemy : (team == 2 ? Team::Neutral : Team::Player),
                maxHp, hp, 3, 2, 80.f
            });
        } else {
            auto eid = it->second;
            auto* p = m_em.getComponent<Position>(eid);
            auto* c = m_em.getComponent<CombatStats>(eid);
            if (p) p->worldPos = {x, y};
            if (c) { c->hp = hp; c->alive = alive; }
        }
    }
}

void Client::handleCombatEvent(int attackerId, int defenderId, int damage, bool killed) {
    auto it = m_idMap.find(defenderId);
    EntityId eid = (it != m_idMap.end()) ? it->second : INVALID_ENTITY;
    auto* cs = m_em.getComponent<CombatStats>(eid);
    if (cs) { cs->hp -= damage; if (killed) cs->alive = false; }
}
