#pragma once

#include <string>
#include <unordered_map>
#include <vector>

class LocaleManager {
public:
    LocaleManager();

    // Discover and load all .json files in the locale directory
    int discover_languages(const std::string& locale_dir);

    void set_language(int lang_index);
    int current_language_index() const { return current_; }
    std::string language_name() const;

    const std::string& get(const std::string& key) const;
    std::string fmt(const std::string& key,
                    const std::string& arg0 = "",
                    const std::string& arg1 = "",
                    const std::string& arg2 = "") const;

    int language_count() const { return static_cast<int>(language_names_.size()); }
    const std::string& language_name(int idx) const;

private:
    int current_ = 0;
    std::vector<std::string> language_names_;
    std::vector<std::unordered_map<std::string, std::string>> strings_;

    bool load_language_file(const std::string& path, const std::string& name);
    const std::unordered_map<std::string, std::string>& current_strings() const;
};
