#include "core/files_language.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace files {
namespace {

enum class Language { System, English, Czech };

std::string environment(const char* name)
{
    const auto* value = std::getenv(name);
    return value ? value : "";
}

std::filesystem::path preferencesPath()
{
    auto config = environment("XDG_CONFIG_HOME");
    if (config.empty()) {
        const auto home = environment("HOME");
        if (home.empty()) return {};
        config = home + "/.config";
    }
    return std::filesystem::path(config) / "cardputer-files/language";
}

Language readPreference()
{
    std::ifstream input(preferencesPath());
    std::string value;
    input >> value;
    if (value == "cs") return Language::Czech;
    if (value == "en") return Language::English;
    return Language::System;
}

Language& preference()
{
    static Language value = readPreference();
    return value;
}

bool systemCzech()
{
    for (const auto* variable : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        const auto value = environment(variable);
        if (!value.empty()) return value.rfind("cs", 0) == 0;
    }
    return false;
}

}  // namespace

bool inCzech()
{
    const auto language = preference();
    return language == Language::Czech || (language == Language::System && systemCzech());
}

const char* local(const char* english, const char* czech) { return inCzech() ? czech : english; }

}  // namespace files
