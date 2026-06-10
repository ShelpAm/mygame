#include "factions/EventSimulator.hpp"
#include "factions/FactionNetwork.hpp"
#include "knowledge/KnowledgeGraph.hpp"
#include "world/WorldState.hpp"
#include <sstream>

EventSimulator::EventSimulator(FactionNetwork& factions, KnowledgeGraph& knowledge,
                               WorldState& worldState)
    : m_factions(factions), m_knowledge(knowledge), m_worldState(worldState)
{}

void EventSimulator::addEvent(GameEvent event) {
    m_events.push_back(std::move(event));
}

void EventSimulator::update(int currentDay) {
    for (auto& event : m_events) {
        if (!event.triggered && event.triggerDay <= currentDay) {
            applyEvent(event, currentDay);
        }
    }
}

void EventSimulator::applyEvent(GameEvent& event, int currentDay) {
    event.triggered = true;

    // Apply faction effects
    if (!event.sourceFactionId.empty()) {
        m_factions.applyEvent(event.sourceFactionId, event.powerShift,
                              event.targetFactionId);
    }
    if (!event.targetFactionId.empty()) {
        m_factions.modifyPower(event.targetFactionId, event.powerShift < 0 ? event.powerShift / 2 : 0);
        m_factions.modifyWealth(event.targetFactionId, event.wealthShift);
        m_factions.modifyCohesion(event.targetFactionId, event.cohesionShift);
    }

    // Generate knowledge facts
    generateFacts(event, currentDay);

    event.triggered = true;
    m_triggeredEvents.push_back(event);
}

void EventSimulator::generateFacts(const GameEvent& event, int currentDay) {
    // Create a fact about this event
    Fact f;
    f.id = "event_" + event.id;
    f.type = Fact::Type::Event;

    std::ostringstream desc;
    switch (event.type) {
        case GameEvent::Type::Battle:
            desc << event.description;
            break;
        case GameEvent::Type::TradeDeal:
            desc << event.description;
            break;
        case GameEvent::Type::Betrayal:
            desc << event.description;
            break;
        case GameEvent::Type::DiplomaticShift:
            desc << event.description;
            break;
        case GameEvent::Type::RefugeeWave:
            desc << event.description;
            break;
        case GameEvent::Type::NaturalDisaster:
            desc << event.description;
            break;
        case GameEvent::Type::Plague:
            desc << event.description;
            break;
        case GameEvent::Type::Discovery:
            desc << event.description;
            break;
        case GameEvent::Type::Assassination:
            desc << event.description;
            break;
    }
    f.description = desc.str();
    f.isPublicKnowledge = false;

    m_knowledge.addOrUpdateFact(f);
}
