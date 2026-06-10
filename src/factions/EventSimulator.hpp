#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

class FactionNetwork;
class KnowledgeGraph;
class WorldState;

struct GameEvent {
    enum class Type {
        Battle, TradeDeal, Betrayal, NaturalDisaster, DiplomaticShift,
        RefugeeWave, Plague, Discovery, Assassination
    };

    std::string id;
    Type type;
    std::string description;
    std::string locationId;
    std::string sourceFactionId;
    std::string targetFactionId;

    int triggerDay;
    bool triggered = false;
    bool playerAware = false;

    // Effects
    int powerShift = 0;
    int wealthShift = 0;
    int cohesionShift = 0;

    // Facts generated
    std::vector<std::string> generatedFactIds;
};

class EventSimulator {
public:
    EventSimulator(FactionNetwork& factions, KnowledgeGraph& knowledge,
                   WorldState& worldState);

    void addEvent(GameEvent event);
    void update(int currentDay);

    const std::vector<GameEvent>& allEvents() const { return m_events; }
    const std::vector<GameEvent>& triggeredEvents() const { return m_triggeredEvents; }

private:
    FactionNetwork& m_factions;
    KnowledgeGraph& m_knowledge;
    WorldState& m_worldState;

    std::vector<GameEvent> m_events;
    std::vector<GameEvent> m_triggeredEvents;

    void applyEvent(GameEvent& event, int currentDay);
    void generateFacts(const GameEvent& event, int currentDay);
};
