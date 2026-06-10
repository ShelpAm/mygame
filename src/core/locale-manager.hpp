#pragma once

#include <string>
#include <unordered_map>
#include <vector>

class LocaleManager {
  public:
    LocaleManager();

    // Discover and load all .json files in the locale directory
    int discover_languages(std::string const &locale_dir);

    void set_language(int lang_index);
    int current_language_index() const
    {
        return current_;
    }
    std::string language_name() const;

    std::string const &get(std::string const &key) const;
    std::string fmt(std::string const &key, std::string const &arg0 = "",
                    std::string const &arg1 = "",
                    std::string const &arg2 = "") const;

    int language_count() const
    {
        return static_cast<int>(language_names_.size());
    }
    std::string const &language_name(int idx) const;

  private:
    int current_ = 0;
    std::vector<std::string> language_names_;
    std::vector<std::unordered_map<std::string, std::string>> strings_;

    bool load_language_file(std::string const &path, std::string const &name);
    std::unordered_map<std::string, std::string> const &current_strings() const;
};
