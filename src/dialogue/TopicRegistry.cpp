#include "dialogue/TopicRegistry.hpp"

void TopicRegistry::registerTopic(const std::string& id, const std::string& displayName,
                                   const std::string& category) {
    m_topics[id] = {displayName, category};
}

bool TopicRegistry::exists(const std::string& id) const {
    return m_topics.contains(id);
}

const std::string& TopicRegistry::displayName(const std::string& id) const {
    static const std::string empty;
    auto it = m_topics.find(id);
    return it != m_topics.end() ? it->second.displayName : empty;
}

std::vector<std::string> TopicRegistry::allTopics() const {
    std::vector<std::string> result;
    for (const auto& [id, topic] : m_topics) {
        result.push_back(id);
    }
    return result;
}

std::vector<std::string> TopicRegistry::byCategory(const std::string& category) const {
    std::vector<std::string> result;
    for (const auto& [id, topic] : m_topics) {
        if (topic.category == category) {
            result.push_back(id);
        }
    }
    return result;
}
