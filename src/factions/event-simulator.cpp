#include "factions/event-simulator.hpp"
#include "factions/faction-network.hpp"
#include "knowledge/knowledge-graph.hpp"
#include "world/world-state.hpp"
#include <spdlog/spdlog.h>
#include <sstream>

EventSimulator::EventSimulator(FactionNetwork &factions, KnowledgeGraph &knowledge,
                               WorldState &world_state)
    : factions_(factions), knowledge_(knowledge), world_state_(world_state)
{
}

void EventSimulator::add_event(GameEvent event)
{
    events_.push_back(std::move(event));
}

void EventSimulator::update(int current_day)
{
    for (auto &event : events_) {
        if (!event.triggered && event.trigger_day <= current_day) {
            spdlog::debug("applying event: {}", event.description);
            apply_event(event, current_day);
        }
    }
}

void EventSimulator::apply_event(GameEvent &event, int current_day)
{
    event.triggered = true;

    // Apply faction effects
    if (!event.source_faction_id.empty()) {
        factions_.apply_event(event.source_faction_id, event.power_shift, event.target_faction_id);
    }
    if (!event.target_faction_id.empty()) {
        factions_.modify_power(event.target_faction_id,
                               event.power_shift < 0 ? event.power_shift / 2 : 0);
        factions_.modify_wealth(event.target_faction_id, event.wealth_shift);
        factions_.modify_cohesion(event.target_faction_id, event.cohesion_shift);
    }

    // Generate knowledge facts
    generate_facts(event, current_day);

    event.triggered = true;
    triggered_events_.push_back(event);
}

void EventSimulator::generate_facts(GameEvent const &event, int current_day)
{
    // Create a fact about this event
    Fact f;
    f.id = "event_" + event.id;
    f.type = Fact::Type::event;

    std::ostringstream desc;
    switch (event.type) {
    case GameEvent::Type::battle:
        desc << event.description;
        break;
    case GameEvent::Type::trade_deal:
        desc << event.description;
        break;
    case GameEvent::Type::betrayal:
        desc << event.description;
        break;
    case GameEvent::Type::diplomatic_shift:
        desc << event.description;
        break;
    case GameEvent::Type::refugee_wave:
        desc << event.description;
        break;
    case GameEvent::Type::natural_disaster:
        desc << event.description;
        break;
    case GameEvent::Type::plague:
        desc << event.description;
        break;
    case GameEvent::Type::discovery:
        desc << event.description;
        break;
    case GameEvent::Type::assassination:
        desc << event.description;
        break;
    }
    f.description = desc.str();
    f.is_public_knowledge = false;

    knowledge_.add_or_update_fact(f);
}
