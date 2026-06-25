#pragma once

#include <locale>
#include <string>
#include <unordered_map>
#include <vector>

class LocaleManager {
  public:
    LocaleManager();

    /// Discover and load all .json files in the locale directory
    /// @return Number of loaded languages
    std::size_t discover_languages(std::string const &locale_dir);

    std::vector<std::string> available_languages() const;

    void set_language(std::string const &name);
    std::string_view current_language_name() const { return current_; }

    std::string const &get(std::string const &key) const;
    [[deprecated("Bad API, don't use it")]] std::string fmt(std::string const &key,
                                                            std::string const &arg0 = "",
                                                            std::string const &arg1 = "",
                                                            std::string const &arg2 = "") const;

  private:
    void load_language_file(std::string const &path, std::string const &name);
    std::unordered_map<std::string, std::string> const &current_strings() const;

    std::string current_;
    // name, key -> value
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> strings_;
};
