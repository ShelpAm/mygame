#include "core/LocaleManager.hpp"
#include <boost/json.hpp>
#include <fstream>
#include <filesystem>
#include <iostream>

LocaleManager::LocaleManager() {}

int LocaleManager::discoverLanguages(const std::string& localeDir) {
    m_languageNames.clear();
    m_strings.clear();

    try {
        for (const auto& entry : std::filesystem::directory_iterator(localeDir)) {
            if (!entry.is_regular_file()) continue;
            auto ext = entry.path().extension().string();
            if (ext != ".json") continue;

            auto stem = entry.path().stem().string();
            if (loadLanguageFile(entry.path().string(), stem)) {
                m_languageNames.push_back(stem);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Failed to discover locales: " << e.what() << '\n';
    }
    return static_cast<int>(m_languageNames.size());
}

bool LocaleManager::loadLanguageFile(const std::string& path, const std::string& name) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    try {
        std::string content{std::istreambuf_iterator<char>(file), {}};
        auto root = boost::json::parse(content).as_object();

        std::unordered_map<std::string, std::string> strings;
        for (const auto& [key, value] : root) {
            strings[std::string(key)] = std::string(value.as_string());
        }
        m_strings.push_back(std::move(strings));
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to load locale " << path << ": " << e.what() << '\n';
        return false;
    }
}

void LocaleManager::setLanguage(int langIndex) {
    if (langIndex >= 0 && langIndex < (int)m_strings.size())
        m_current = langIndex;
}

std::string LocaleManager::languageName() const {
    if (m_current >= 0 && m_current < (int)m_languageNames.size())
        return m_languageNames[m_current];
    return "?";
}

const std::string& LocaleManager::languageName(int idx) const {
    static const std::string empty;
    if (idx >= 0 && idx < (int)m_languageNames.size())
        return m_languageNames[idx];
    return empty;
}

const std::string& LocaleManager::get(const std::string& key) const {
    if (m_current < 0 || m_current >= (int)m_strings.size()) {
        static const std::string empty;
        return empty;
    }
    const auto& strings = currentStrings();
    auto it = strings.find(key);
    if (it != strings.end()) return it->second;

    // Fallback to first language (usually English)
    if (m_current != 0 && !m_strings.empty()) {
        auto it0 = m_strings[0].find(key);
        if (it0 != m_strings[0].end()) return it0->second;
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

const std::unordered_map<std::string, std::string>& LocaleManager::currentStrings() const {
    return m_strings[m_current];
}
