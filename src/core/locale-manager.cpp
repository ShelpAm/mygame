#include "core/locale-manager.hpp"
#include <boost/json.hpp>
#include <filesystem>
#include <fstream>
#include <spdlog/spdlog.h>

LocaleManager::LocaleManager()
{
}

int LocaleManager::discover_languages(std::string const &locale_dir)
{
    language_names_.clear();
    strings_.clear();

    try {
        for (auto const &entry : std::filesystem::directory_iterator(locale_dir)) {
            if (!entry.is_regular_file())
                continue;
            auto ext = entry.path().extension().string();
            if (ext != ".json")
                continue;

            auto stem = entry.path().stem().string();
            if (load_language_file(entry.path().string(), stem)) {
                language_names_.push_back(stem);
            }
        }
    }
    catch (std::exception const &e) {
        spdlog::error("Failed to discover locales: {}", e.what());
    }
    return static_cast<int>(language_names_.size());
}

bool LocaleManager::load_language_file(std::string const &path, std::string const &name)
{
    std::ifstream file(path);
    if (!file.is_open())
        return false;

    try {
        std::string content{std::istreambuf_iterator<char>(file), {}};
        auto root = boost::json::parse(content).as_object();

        std::unordered_map<std::string, std::string> strings;
        for (auto const &[key, value] : root) {
            strings[std::string(key)] = std::string(value.as_string());
        }
        strings_.push_back(std::move(strings));
        return true;
    }
    catch (std::exception const &e) {
        spdlog::error("Failed to load locale {}: {}", path, e.what());
        return false;
    }
}

void LocaleManager::set_language(int lang_index)
{
    if (lang_index >= 0 && lang_index < static_cast<int>(strings_.size()))
        current_ = lang_index;
}

std::string LocaleManager::language_name() const
{
    if (current_ >= 0 && current_ < static_cast<int>(language_names_.size()))
        return language_names_[current_];
    return "?";
}

std::string const &LocaleManager::language_name(int idx) const
{
    static std::string const empty;
    if (idx >= 0 && idx < static_cast<int>(language_names_.size()))
        return language_names_[idx];
    return empty;
}

std::string const &LocaleManager::get(std::string const &key) const
{
    if (current_ < 0 || current_ >= static_cast<int>(strings_.size())) {
        throw std::runtime_error("LocaleManager: current language index is out of range");
    }
    auto const &strings = current_strings();
    auto it = strings.find(key);
    if (it != strings.end())
        return it->second;

    // Fallback to first language (usually English)
    if (current_ != 0 && !strings_.empty()) {
        auto it0 = strings_[0].find(key);
        if (it0 != strings_[0].end())
            return it0->second;
    }

    throw std::runtime_error("LocaleManager: missing key '" + key + "' in language '" +
                             language_name() + "'");
}

std::string LocaleManager::fmt(std::string const &key, std::string const &arg0,
                               std::string const &arg1, std::string const &arg2) const
{
    std::string text = get(key);
    if (text.empty())
        return key;

    auto replace = [&](std::string const &from, std::string const &to) {
        size_t pos = text.find(from);
        if (pos != std::string::npos)
            text.replace(pos, from.length(), to);
    };
    if (!arg0.empty())
        replace("{0}", arg0);
    if (!arg1.empty())
        replace("{1}", arg1);
    if (!arg2.empty())
        replace("{2}", arg2);
    return text;
}

std::unordered_map<std::string, std::string> const &LocaleManager::current_strings() const
{
    return strings_[current_];
}
