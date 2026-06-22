#pragma once

#include <string>
#include <vector>

class FactionNetwork;
class KnowledgeGraph;
class WorldState;

struct GameEvent {
    enum class Type {
        battle,
        trade_deal,
        betrayal,
        natural_disaster,
        diplomatic_shift,
        refugee_wave,
        plague,
        discovery,
        assassination
    };

    std::string id;
    Type type;
    std::string description;
    std::string location_id;
    std::string source_faction_id;
    std::string target_faction_id;

    int trigger_day;
    bool triggered = false;
    bool player_aware = false;

    // Effects
    int power_shift = 0;
    int wealth_shift = 0;
    int cohesion_shift = 0;

    // Facts generated
    std::vector<std::string> generated_fact_ids;
};

class EventSimulator {
  public:
    EventSimulator(FactionNetwork &factions, KnowledgeGraph &knowledge, WorldState &world_state);

    void add_event(GameEvent event);
    void update(int current_day);

    std::vector<GameEvent> const &all_events() const { return events_; }
    std::vector<GameEvent> const &triggered_events() const { return triggered_events_; }

  private:
    FactionNetwork &factions_;
    KnowledgeGraph &knowledge_;
    WorldState &world_state_;

    std::vector<GameEvent> events_;
    std::vector<GameEvent> triggered_events_;

    void apply_event(GameEvent &event, int current_day);
    void generate_facts(GameEvent const &event, int current_day);
};
