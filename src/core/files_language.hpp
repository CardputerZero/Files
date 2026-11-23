#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace files {

enum class Language { System, English, Czech };

Language languagePreference();
bool setLanguagePreference(Language language);
bool inCzech();
const char* local(const char* english, const char* czech);
std::string localizedMessage(std::string_view message);
std::string sizeInUnits(uint64_t bytes);

}  // namespace files