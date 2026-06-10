#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class TopicRegistry {
  public:
    void register_topic(std::string const &id, std::string const &display_name,
                        std::string const &category);
    bool exists(std::string const &id) const;
    std::string const &display_name(std::string const &id) const;

    std::vector<std::string> all_topics() const;
    std::vector<std::string> by_category(std::string const &category) const;

  private:
    struct Topic {
        std::string display_name;
        std::string category;
    };
    std::unordered_map<std::string, Topic> topics_;
};
