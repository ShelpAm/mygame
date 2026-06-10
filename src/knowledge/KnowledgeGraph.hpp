#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <ostream>

struct Fact {
    enum class Type {
        Event,        // "Ugarit was sacked on the full moon"
        Relationship, // "The King of Byblos is allied with the Sea Peoples"
        Location,     // "There is a hidden cove near Cape Gelidonya"
        Identity,     // "The merchant Ishtar-abi is a Hittite spy"
        Resource,     // "Cyprus has copper again"
        Status        // "The temple of Baal in Sidon is accepting refugees"
    };

    enum class Certainty {
        Unverified,    // Heard from one source, not corroborated
        Plausible,     // Multiple independent reports agree
        Corroborated,  // Witnessed by player or reported by trusted source
        Confirmed      // Player witnessed personally
    };

    struct Origin {
        enum class Source { PlayerWitness, NPCTestimony, ArtifactRead, Inference };
        Source source;
        std::string npcId;
        std::string locationId;
        int gameDay = 0;
        int confidence = 50;  // 0-100
    };

    std::string id;
    std::string description;
    Type type = Type::Event;

    std::vector<Origin> origins;

    Certainty playerCertainty = Certainty::Unverified;
    bool playerBelieves = true;
    bool playerLogged = false;

    bool isPublicKnowledge = false;
    int spreadCount = 0;
};

inline std::ostream& operator<<(std::ostream& os, Fact::Type t) {
    switch (t) {
        case Fact::Type::Event: return os << "Event";
        case Fact::Type::Relationship: return os << "Relationship";
        case Fact::Type::Location: return os << "Location";
        case Fact::Type::Identity: return os << "Identity";
        case Fact::Type::Resource: return os << "Resource";
        case Fact::Type::Status: return os << "Status";
    }
    return os;
}

inline std::ostream& operator<<(std::ostream& os, Fact::Certainty c) {
    switch (c) {
        case Fact::Certainty::Unverified: return os << "Unverified";
        case Fact::Certainty::Plausible: return os << "Plausible";
        case Fact::Certainty::Corroborated: return os << "Corroborated";
        case Fact::Certainty::Confirmed: return os << "Confirmed";
    }
    return os;
}

class KnowledgeGraph {
public:
    const Fact* fact(const std::string& id) const;
    Fact* factMutable(const std::string& id);
    void addOrUpdateFact(Fact fact);
    bool hasFact(const std::string& id) const;

    void addRelation(const std::string& factA, const std::string& factB);
    std::vector<std::string> relatedFacts(const std::string& id) const;

    bool isTopicKnown(const std::string& topic) const;
    void markTopicKnown(const std::string& topic);
    const std::unordered_set<std::string>& knownTopics() const { return m_knownTopics; }

    std::vector<const Fact*> factsWitnessed() const;
    std::vector<const Fact*> factsHeard() const;

    size_t totalFacts() const { return m_facts.size(); }

private:
    std::unordered_map<std::string, Fact> m_facts;
    std::unordered_multimap<std::string, std::string> m_factRelations;
    std::unordered_set<std::string> m_knownTopics;
};
