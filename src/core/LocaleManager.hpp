#pragma once

#include <string>
#include <unordered_map>
#include <vector>

class LocaleManager {
public:
    LocaleManager();

    // Discover and load all .json files in the locale directory
    int discoverLanguages(const std::string& localeDir);

    void setLanguage(int langIndex);
    int currentLanguageIndex() const { return m_current; }
    std::string languageName() const;

    const std::string& get(const std::string& key) const;
    std::string fmt(const std::string& key,
                    const std::string& arg0 = "",
                    const std::string& arg1 = "",
                    const std::string& arg2 = "") const;

    int languageCount() const { return static_cast<int>(m_languageNames.size()); }
    const std::string& languageName(int idx) const;

private:
    int m_current = 0;
    std::vector<std::string> m_languageNames;
    std::vector<std::unordered_map<std::string, std::string>> m_strings;

    bool loadLanguageFile(const std::string& path, const std::string& name);
    const std::unordered_map<std::string, std::string>& currentStrings() const;
};
