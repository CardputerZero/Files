#include "input/cardputer_keymap.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

}  // namespace

int main()
{
    const files::CardputerKeymap built_in;
    require(!built_in.loadedRuntimeMap(), "empty path unexpectedly loaded a runtime keymap");
    require(built_in.characterFor(26) == '!', "built-in exclamation mapping is wrong");
    require(built_in.characterFor(52) == '*', "built-in asterisk mapping is wrong");
    require(built_in.characterFor(91) == '.', "built-in period mapping is wrong");
    require(built_in.characterFor(10) == 0, "unmapped standard key was intercepted");

    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto fixture =
        std::filesystem::temp_directory_path() / ("files-cardputer-keymap-" + std::to_string(nonce) + ".map");
    {
        std::ofstream stream(fixture);
        stream << "# runtime override\n"
               << "keycode 10 = period\n"
               << "keycode 11 = exclam\n"
               << "invalid line\n";
    }

    const files::CardputerKeymap runtime(fixture);
    std::error_code error;
    std::filesystem::remove(fixture, error);
    require(runtime.loadedRuntimeMap(), "runtime keymap was not loaded");
    require(runtime.characterFor(10) == '.', "runtime period mapping is wrong");
    require(runtime.characterFor(11) == '!', "runtime exclamation mapping is wrong");
    require(runtime.characterFor(26) == 0, "runtime keymap did not replace the built-in map");
    return 0;
}
