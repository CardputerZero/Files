#include "core/files_language.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <unistd.h>

namespace files {
namespace {

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

Language languagePreference() { return preference(); }

bool setLanguagePreference(Language language)
{
    const auto path = preferencesPath();
    if (path.empty()) return false;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;

    auto temporary = path.string() + ".XXXXXX";
    const int fd = mkstemp(temporary.data());
    if (fd < 0) return false;
    const std::string_view value = language == Language::Czech ? "cs\n" :
                                   language == Language::English ? "en\n" : "system\n";
    const auto written = write(fd, value.data(), value.size());
    const int closed = close(fd);
    if (written == static_cast<ssize_t>(value.size()) && closed == 0) {
        std::filesystem::rename(temporary, path, error);
        if (!error) {
            preference() = language;
            return true;
        }
    }
    std::filesystem::remove(temporary, error);
    return false;
}

bool inCzech()
{
    const auto language = preference();
    return language == Language::Czech || (language == Language::System && systemCzech());
}

const char* local(const char* english, const char* czech) { return inCzech() ? czech : english; }

std::string sizeInUnits(uint64_t bytes)
{
    constexpr std::array<const char*, 7> units{"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB"};
    size_t unit = 0;
    double amount = static_cast<double>(bytes);
    while (amount >= 1024 && unit + 1 < units.size()) {
        amount /= 1024;
        ++unit;
    }
    if (!unit) return std::to_string(bytes) + " B";
    auto tenths = static_cast<uint64_t>(amount * 10 + 0.5);
    if (tenths == 10240 && unit + 1 < units.size()) {
        ++unit;
        tenths = 10;
    }
    auto result = std::to_string(tenths / 10);
    if (tenths % 10) result += std::string(local(".", ",")) + std::to_string(tenths % 10);
    return result + " " + units[unit];
}

std::string localizedMessage(std::string_view message)
{
    if (!inCzech()) return std::string(message);
    // Keep file names and the diagnostic output of external commands untouched.
    static constexpr std::pair<std::string_view, std::string_view> exact[] = {
        {"Home", "Domů"}, {"File system", "Souborový systém"}, {"Empty folder", "Prázdná složka"},
        {"Disk is full", "Disk je plný"}, {"Disk is read-only", "Disk je jen pro čtení"},
        {"Could not save language", "Jazyk se nepodařilo uložit"}, {"Hold Esc to exit", "Podržet Esc · Ukončit"},
        {"Select a removable drive", "Vyberte vyměnitelný disk"},
        {"Drive has multiple mount points", "Disk je připojen na více místech"},
        {"Cannot rename a location", "Umístění nelze přejmenovat"},
        {"Cannot delete a location", "Umístění nelze smazat"},
        {"Folder contains a mounted filesystem", "Složka obsahuje připojený souborový systém"},
        {"Already at Places", "Už jste v Umístěních"},
        {"Already here", "Položka už je tady"}, {"Folder not found", "Složka nenalezena"},
        {"Permission denied", "Přístup odepřen"},
        {"Enter a file name.", "Zadejte název souboru."},
        {"Enter a folder name.", "Zadejte název složky."},
        {"Move cancelled", "Přesun zrušen"}, {"Copy cancelled", "Kopírování zrušeno"}, {"Name already exists", "Název už existuje"},
        {"Invalid name", "Neplatný název"}, {"No preview available", "Náhled není dostupný"},
        {"No preview", "Náhled není dostupný"}, {"Read failed", "Čtení se nezdařilo"},
        {"Drive disconnected", "Disk odpojen"}, {"USB drive not connected", "USB disk není připojen"},
        {"No file selected", "Není vybrána položka"}, {"Nothing to paste", "Schránka je prázdná"},
        {"Open a folder first", "Nejdřív otevřete složku"}, {"Transfer cancelled", "Přenos zrušen"},
        {"Copy cancelled", "Kopírování zrušeno"}, {"Move cancelled", "Přesun zrušen"},
        {"System password required", "Je vyžadováno systémové heslo"},
        {"Sudo authentication failed", "Ověření hesla sudo selhalo"},
        {"Invalid Debian package metadata", "Neplatné údaje balíčku Debian"},
        {"Cannot calculate the SHA-256 of the reviewed package", "Nelze vypočítat SHA-256 balíčku"},
        {"Debian packages require a Debian-compatible Linux system", "Balíčky Debian vyžadují kompatibilní Linux"},
        {"The installed Files package helper is missing or its path is writable.", "Pomocník instalace Files chybí nebo jeho cesta umožňuje zápis."},
        {"Open the Debian package for review first.", "Nejprve otevřete náhled balíčku Debian."},
        {"The prepared package changed after review or cannot be opened. Open it again.", "Balíček se po kontrole změnil nebo nejde otevřít. Otevřete jej znovu."},
    };
    for (const auto& [en, cs] : exact) {
        if (message == en) return std::string(cs);
    }
    static constexpr std::pair<std::string_view, std::string_view> prefixes[] = {
        {"Created copy ", "Vytvořena kopie "},
        {"Ready to paste: ", "Připraveno k vložení: "}, {"Clipboard for move: ", "K přesunu: "},
        {"Renamed to ", "Přejmenováno na "},
        {"Copied ", "Zkopírováno "}, {"Cut ", "Vyjmuto "}, {"Moved ", "Přesunuto "},
        {"Renamed ", "Přejmenováno "}, {"Deleted ", "Smazáno "}, {"Created ", "Složka vytvořena: "},
        {"Unmounting ", "Odpojuji "}, {"Powering off ", "Vypínám "}, {"Could not unmount ", "Nelze odpojit "},
        {"Package installation failed", "Instalace balíčku selhala"}, {"Sudo authorization failed", "Ověření sudo selhalo"}, {"Installed ", "Nainstalováno "},
        {"Unsupported architecture: ", "Nepodporovaná architektura: "},
        {"Damaged Debian package", "Poškozený balíček Debian"},
        {"Invalid or unreadable Debian package", "Neplatný nebo nečitelný balíček"},
        {"Item is no longer a folder", "Položka už není složka"},
        {"Cannot read package: ", "Balíček nelze přečíst: "},
        {"Cannot prepare package: ", "Balíček nelze připravit: "},
        {"Cannot seal the package for review: ", "Balíček nelze zajistit pro kontrolu: "},
        {"Rename failed: ", "Přejmenování selhalo: "}, {"Delete failed: ", "Mazání selhalo: "},
        {"Copy failed: ", "Kopírování selhalo: "}, {"Move failed: ", "Přesun selhal: "},
        {"Open failed: ", "Otevření selhalo: "}, {"Read failed: ", "Čtení selhalo: "},
        {"Cut failed: ", "Přesun selhal: "}, {"Create folder failed: ", "Vytvoření složky selhalo: "},
        {"Cannot verify the APT installation plan. Check dependencies and package manager state.",
         "Nelze ověřit plán APT. Zkontrolujte závislosti a stav správce balíčků."},
        {"Unmounted. Could not power off ", "Disk odpojen, vypnutí selhalo: "},
        {"Other volumes on ", "Další svazky jsou stále připojené: "},
        {"Cannot open ", "Nelze otevřít "}, {"Cannot read ", "Nelze přečíst "},
    };
    for (const auto& [en, cs] : prefixes) {
        if (message.substr(0, en.size()) == en) return std::string(cs) + std::string(message.substr(en.size()));
    }
    return std::string(message);
}

}  // namespace files