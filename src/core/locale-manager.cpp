#include "core/locale-manager.hpp"
#include <boost/json.hpp>
#include <fstream>
#include <filesystem>
#include <iostream>

LocaleManager::LocaleManager() {}

int LocaleManager::discover_languages(const std::string& locale_dir) {
    language_names_.clear();
    strings_.clear();

    try {
        for (const auto& entry : std::filesystem::directory_iterator(locale_dir)) {
            if (!entry.is_regular_file()) continue;
            auto ext = entry.path().extension().string();
            if (ext != ".json") continue;

            auto stem = entry.path().stem().string();
            if (load_language_file(entry.path().string(), stem)) {
                language_names_.push_back(stem);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Failed to discover locales: " << e.what() << '\n';
    }
    return static_cast<int>(language_names_.size());
}

bool LocaleManager::load_language_file(const std::string& path, const std::string& name) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    try {
        std::string content{std::istreambuf_iterator<char>(file), {}};
        auto root = boost::json::parse(content).as_object();

        std::unordered_map<std::string, std::string> strings;
        for (const auto& [key, value] : root) {
            strings[std::string(key)] = std::string(value.as_string());
        }
        strings_.push_back(std::move(strings));
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to load locale " << path << ": " << e.what() << '\n';
        return false;
    }
}

void LocaleManager::set_language(int lang_index) {
    if (lang_index >= 0 && lang_index < (int)strings_.size())
        current_ = lang_index;
}

std::string LocaleManager::language_name() const {
    if (current_ >= 0 && current_ < (int)language_names_.size())
        return language_names_[current_];
    return "?";
}

const std::string& LocaleManager::language_name(int idx) const {
    static const std::string empty;
    if (idx >= 0 && idx < (int)language_names_.size())
        return language_names_[idx];
    return empty;
}

const std::string& LocaleManager::get(const std::string& key) const {
    if (current_ < 0 || current_ >= (int)strings_.size()) {
        static const std::string empty;
        return empty;
    }
    const auto& strings = current_strings();
    auto it = strings.find(key);
    if (it != strings.end()) return it->second;

    // Fallback to first language (usually English)
    if (current_ != 0 && !strings_.empty()) {
        auto it0 = strings_[0].find(key);
        if (it0 != strings_[0].end()) return it0->second;
    }

    static const std::string empty;
    return empty;
}

std::string LocaleManager::fmt(const std::string& key,
                                const std::string& arg0,
                                const std::string& arg1,
                                const std::string& arg2) const {
    std::string text = get(key);
    if (text.empty()) return key;

    auto replace = [&](const std::string& from, const std::string& to) {
        size_t pos = text.find(from);
        if (pos != std::string::npos) text.replace(pos, from.length(), to);
    };
    if (!arg0.empty()) replace("{0}", arg0);
    if (!arg1.empty()) replace("{1}", arg1);
    if (!arg2.empty()) replace("{2}", arg2);
    return text;
}

const std::unordered_map<std::string, std::string>& LocaleManager::current_strings() const {
    return strings_[current_];
}
