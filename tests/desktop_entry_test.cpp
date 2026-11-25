#include "preview/external/desktop_entry.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

std::vector<std::string> split(const std::string& exec)
{
    std::vector<std::string> argv;
    require(files::splitDesktopExec(exec, argv), exec.c_str());
    return argv;
}

bool contains(const std::vector<std::string>& values, const std::string& value)
{
    return std::find(values.begin(), values.end(), value) != values.end();
}

files::DesktopEntry parse(const std::string& text)
{
    std::istringstream in(text);
    files::DesktopEntry entry;
    require(files::parseDesktopEntry(in, entry), "parse desktop entry");
    return entry;
}

}  // namespace

int main()
{
    using Args = std::vector<std::string>;

    // Exec splitting
    require(split("bin/czpdf") == Args({"bin/czpdf", "%f"}), "path appended when no field code");
    require(split("bin/viewer --page 1 %f") == Args({"bin/viewer", "--page", "1", "%f"}), "%f kept in place");
    require(split("bin/viewer %U --fit") == Args({"bin/viewer", "%f", "--fit"}), "%U becomes the file slot");
    require(split("bin/viewer %f %F") == Args({"bin/viewer", "%f"}), "only one file slot");
    require(split("bin/viewer %i %c %k %f") == Args({"bin/viewer", "%f"}), "other field codes dropped");
    require(split("bin/viewer --label=100%%") == Args({"bin/viewer", "--label=100%", "%f"}), "%% unescaped");
    require(split("  \"/opt/My App/run\"  \"a \\\"b\\\"\"  ") == Args({"/opt/My App/run", "a \"b\"", "%f"}),
            "quotes and escapes");
    require(split("\"\"bin/x") == Args({"bin/x", "%f"}), "empty quotes join the token");

    std::vector<std::string> argv = {"untouched"};
    require(!files::splitDesktopExec("", argv), "empty Exec");
    require(!files::splitDesktopExec("   ", argv), "blank Exec");
    require(!files::splitDesktopExec("%f", argv), "Exec without a program");
    require(!files::splitDesktopExec("\"bin/x", argv), "unterminated quote");
    require(!files::splitDesktopExec("\"\" %f", argv), "empty program");
    require(argv == Args({"untouched"}), "argv left alone on failure");

    // MimeType table
    require(files::extensionsForMimeType("application/pdf") == Args({".pdf"}), "pdf");
    require(files::extensionsForMimeType(" Application/PDF ") == Args({".pdf"}), "mime is case-insensitive");
    require(files::extensionsForMimeType("application/oxps") == Args({".xps", ".oxps"}), "oxps");
    require(files::extensionsForMimeType("application/epub+zip") == Args({".epub"}), "epub");
    require(files::extensionsForMimeType("image/vnd.djvu") == Args({".djvu"}), "djvu");
    require(files::extensionsForMimeType("image/png").empty(), "types Files previews itself are not mapped");
    require(files::extensionsForMimeType("").empty(), "empty mime");

    // Desktop entry parsing
    const files::DesktopEntry pdf = parse(
        "# comment\n"
        "[Desktop Entry]\n"
        "Name=PDF Viewer\n"
        "Exec=bin/czpdf\n"
        "Type=Application\n"
        "MimeType=application/pdf;application/x-unknown;application/oxps;\n"
        "X-Files-Extensions=PDF; .cbz ,txt\n"
        "\n"
        "[Desktop Action Other]\n"
        "Name=Ignored\n"
        "Exec=bin/other\n");
    require(pdf.name == "PDF Viewer", "Name");
    require(pdf.exec == "bin/czpdf", "Exec");
    require(pdf.application && !pdf.hidden, "Type and Hidden defaults");
    require(pdf.extensions == Args({".pdf", ".xps", ".oxps", ".cbz", ".txt"}), "extensions merged without duplicates");

    const files::DesktopEntry hidden = parse("[Desktop Entry]\nHidden=True\nType=Link\nMimeType=\n");
    require(hidden.hidden, "Hidden=True");
    require(!hidden.application, "Type=Link");
    require(hidden.extensions.empty(), "empty MimeType");

    const files::DesktopEntry outside = parse("Exec=bin/x\nMimeType=application/pdf;\n");
    require(outside.exec.empty() && !contains(outside.extensions, ".pdf"), "keys outside [Desktop Entry] ignored");

    std::cout << "Desktop entry tests passed\n";
    return 0;
}
