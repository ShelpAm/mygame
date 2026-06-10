#include "dialogue/topic-registry.hpp"
void TopicRegistry::register_topic(std::string const &id,
                                   std::string const &display_name,
                                   std::string const &category)
{
    topics_[id] = {display_name, category};
}

bool TopicRegistry::exists(std::string const &id) const
{
    return topics_.contains(id);
}

std::string const &TopicRegistry::display_name(std::string const &id) const
{
    static std::string const empty;
    auto it = topics_.find(id);
    return it != topics_.end() ? it->second.display_name : empty;
}

std::vector<std::string> TopicRegistry::all_topics() const
{
    std::vector<std::string> result;
    for (auto const &[id, topic] : topics_) {
        result.push_back(id);
    }
    return result;
}

std::vector<std::string>
TopicRegistry::by_category(std::string const &category) const
{
    std::vector<std::string> result;
    for (auto const &[id, topic] : topics_) {
        if (topic.category == category) {
            result.push_back(id);
        }
    }
    return result;
}
