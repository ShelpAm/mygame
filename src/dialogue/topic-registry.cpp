#include "dialogue/topic-registry.hpp"
void TopicRegistry::register_topic(const std::string& id, const std::string& display_name,
                                   const std::string& category) {
    topics_[id] = {display_name, category};
}

bool TopicRegistry::exists(const std::string& id) const {
    return topics_.contains(id);
}

const std::string& TopicRegistry::display_name(const std::string& id) const {
    static const std::string empty;
    auto it = topics_.find(id);
    return it != topics_.end() ? it->second.display_name : empty;
}

std::vector<std::string> TopicRegistry::all_topics() const {
    std::vector<std::string> result;
    for (const auto& [id, topic] : topics_) {
        result.push_back(id);
    }
    return result;
}

std::vector<std::string> TopicRegistry::by_category(const std::string& category) const {
    std::vector<std::string> result;
    for (const auto& [id, topic] : topics_) {
        if (topic.category == category) {
            result.push_back(id);
        }
    }
    return result;
}
