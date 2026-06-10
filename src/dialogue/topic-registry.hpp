#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

class TopicRegistry {
public:
    void register_topic(const std::string& id, const std::string& display_name,
                       const std::string& category);
    bool exists(const std::string& id) const;
    const std::string& display_name(const std::string& id) const;

    std::vector<std::string> all_topics() const;
    std::vector<std::string> by_category(const std::string& category) const;

private:
    struct Topic {
        std::string display_name;
        std::string category;
    };
    std::unordered_map<std::string, Topic> topics_;
};
