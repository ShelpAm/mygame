#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

class TopicRegistry {
public:
    void registerTopic(const std::string& id, const std::string& displayName,
                       const std::string& category);
    bool exists(const std::string& id) const;
    const std::string& displayName(const std::string& id) const;

    std::vector<std::string> allTopics() const;
    std::vector<std::string> byCategory(const std::string& category) const;

private:
    struct Topic {
        std::string displayName;
        std::string category;
    };
    std::unordered_map<std::string, Topic> m_topics;
};
