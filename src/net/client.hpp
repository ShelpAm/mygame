#pragma once

#include "entities/entity-manager.hpp"
#include <unordered_map>

class NetworkManager;
class CombatSystem;
class WorldState;
class QuestManager;

class Client {
public:
    Client();
    void set_managers(CombatSystem* cs, WorldState* ws, QuestManager* qm);

    void update(float dt, NetworkManager& net);
    void handle_combat_event(int attacker_id, int defender_id, int damage, bool killed);

    EntityManager& entities() { return em_; }
    EntityId local_player() const;
    Vec2f local_player_pos();
    void set_local_player_pos(Vec2f pos);
    const EntityManager& entities() const { return em_; }

private:
    EntityManager em_;
    EntityId my_player_ = 0;
    std::unordered_map<int, EntityId> id_map_;

    void apply_sync(const std::vector<uint8_t>& data);
};
