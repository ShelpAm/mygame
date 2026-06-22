#pragma once

#include <ostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct Fact {
    enum class Type {
        event,        // "Ugarit was sacked on the full moon"
        relationship, // "The King of Byblos is allied with the Sea Peoples"
        location,     // "There is a hidden cove near Cape Gelidonya"
        identity,     // "The merchant Ishtar-abi is a Hittite spy"
        resource,     // "Cyprus has copper again"
        status        // "The temple of Baal in Sidon is accepting refugees"
    };

    enum class Certainty {
        unverified,   // Heard from one source, not corroborated
        plausible,    // Multiple independent reports agree
        corroborated, // Witnessed by player or reported by trusted source
        confirmed     // Player witnessed personally
    };

    struct Origin {
        enum class Source { player_witness, npc_testimony, artifact_read, inference };
        Source source;
        std::string npc_id;
        std::string location_id;
        int game_day = 0;
        int confidence = 50; // 0-100
    };

    std::string id;
    std::string description;
    Type type = Type::event;

    std::vector<Origin> origins;

    Certainty player_certainty = Certainty::unverified;
    bool player_believes = true;
    bool player_logged = false;

    bool is_public_knowledge = false;
    int spread_count = 0;
};

inline std::ostream &operator<<(std::ostream &os, Fact::Type t)
{
    switch (t) {
    case Fact::Type::event:
        return os << "event";
    case Fact::Type::relationship:
        return os << "relationship";
    case Fact::Type::location:
        return os << "location";
    case Fact::Type::identity:
        return os << "identity";
    case Fact::Type::resource:
        return os << "resource";
    case Fact::Type::status:
        return os << "status";
    }
    return os;
}

inline std::ostream &operator<<(std::ostream &os, Fact::Certainty c)
{
    switch (c) {
    case Fact::Certainty::unverified:
        return os << "unverified";
    case Fact::Certainty::plausible:
        return os << "plausible";
    case Fact::Certainty::corroborated:
        return os << "corroborated";
    case Fact::Certainty::confirmed:
        return os << "confirmed";
    }
    return os;
}

class KnowledgeGraph {
  public:
    Fact const *fact(std::string const &id) const;
    Fact *fact_mutable(std::string const &id);
    void add_or_update_fact(Fact fact);
    bool has_fact(std::string const &id) const;

    void add_relation(std::string const &fact_a, std::string const &fact_b);
    std::vector<std::string> related_facts(std::string const &id) const;

    bool is_topic_known(std::string const &topic) const;
    void mark_topic_known(std::string const &topic);
    std::unordered_set<std::string> const &known_topics() const { return known_topics_; }

    std::vector<Fact const *> facts_witnessed() const;
    std::vector<Fact const *> facts_heard() const;

    size_t total_facts() const { return facts_.size(); }

  private:
    std::unordered_map<std::string, Fact> facts_;
    std::unordered_multimap<std::string, std::string> fact_relations_;
    std::unordered_set<std::string> known_topics_;
};
