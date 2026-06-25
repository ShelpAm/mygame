#include "core/locale-manager.hpp"
#include <boost/json.hpp>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <spdlog/spdlog.h>

LocaleManager::LocaleManager() = default;

std::size_t LocaleManager::discover_languages(std::string const &locale_dir)
{
    std::size_t num{};
    for (auto const &entry : std::filesystem::directory_iterator(locale_dir)) {
        if (!entry.is_regular_file())
            continue;
        auto ext = entry.path().extension().string();
        if (ext != ".json")
            continue;

        auto stem = entry.path().stem().string();
        try {
            if (strings_.contains(stem)) {
                spdlog::warn("LocaleManager: duplicate locale {} found, skipping", stem);
                continue;
            }
            load_language_file(entry.path().string(), stem);
            ++num;
        }
        catch (std::exception const &e) {
            spdlog::error("LocaleManager: failed to load locale {}: {}", entry.path().string(),
                          e.what());
        }
    }
    return num;
}

std::vector<std::string> LocaleManager::available_languages() const
{
    return strings_ | std::views::keys | std::ranges::to<std::vector>();
}

void LocaleManager::load_language_file(std::string const &path, std::string const &name)
{
    std::ifstream file(path);
    if (!file.is_open())
        throw std::runtime_error("LocaleManager: failed to open locale file " + path);

    std::string content{std::istreambuf_iterator<char>(file), {}};
    auto root = boost::json::parse(content).as_object();

    std::unordered_map<std::string, std::string> strings;
    for (auto const &[key, value] : root) {
        strings[std::string(key)] = std::string(value.as_string());
    }
    strings_.insert({name, std::move(strings)});
}

void LocaleManager::set_language(std::string const &name)
{
    if (!strings_.contains(name)) {
        throw std::runtime_error("LocaleManager: language '" + std::string(name) + "' not found");
    }
    current_ = name;
}

std::string const &LocaleManager::get(std::string const &key) const
{
    auto const &strings = current_strings();
    auto it = strings.find(key);
    if (it != strings.end())
        return it->second;
    throw std::runtime_error("LocaleManager: missing key '" + key + "' in language '" + current_ +
                             "'");
}

// std::string LocaleManager::fmt(std::string const &key, std::string const &arg0,
//                                std::string const &arg1, std::string const &arg2) const
// {
//     std::string text = get(key);
//     if (text.empty())
//         return key;
//
//     auto replace = [&](std::string const &from, std::string const &to) {
//         size_t pos = text.find(from);
//         if (pos != std::string::npos)
//             text.replace(pos, from.length(), to);
//     };
//     if (!arg0.empty())
//         replace("{0}", arg0);
//     if (!arg1.empty())
//         replace("{1}", arg1);
//     if (!arg2.empty())
//         replace("{2}", arg2);
//     return text;
// }

std::unordered_map<std::string, std::string> const &LocaleManager::current_strings() const
{
    if (!strings_.contains(current_))
        throw std::runtime_error(
            std::format("LocaleManager: current language '{}' not found", current_));
    return strings_.at(current_);
}
