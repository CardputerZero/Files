#include "preview/external/desktop_entry.hpp"

#include "models/file_type_registry.hpp"
#include <algorithm>
#include <cctype>
#include <utility>

namespace files {
namespace {

struct MimeExtension {
    const char* extension;
    const char* mime;
};

// Document types an installed app may claim through MimeType=. Types Files
// previews itself (images, audio, video, text) can still be claimed with an
// explicit X-Files-Extensions= entry.
constexpr MimeExtension kMimeExtensions[] = {
    {".pdf", "application/pdf"},
    {".xps", "application/oxps"},
    {".oxps", "application/oxps"},
    {".xps", "application/vnd.ms-xpsdocument"},
    {".epub", "application/epub+zip"},
    {".fb2", "application/x-fictionbook+xml"},
    {".mobi", "application/x-mobipocket-ebook"},
    {".cbz", "application/vnd.comicbook+zip"},
    {".djvu", "image/vnd.djvu"},
    {".docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
    {".xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
    {".pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
    {".odt", "application/vnd.oasis.opendocument.text"},
    {".ods", "application/vnd.oasis.opendocument.spreadsheet"},
    {".odp", "application/vnd.oasis.opendocument.presentation"},
};

std::string trim(const std::string& value)
{
    size_t begin = 0;
    size_t end   = value.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::vector<std::string> splitList(const std::string& value)
{
    std::vector<std::string> out;
    std::string item;
    for (char c : value) {
        if (c == ';' || c == ',') {
            item = trim(item);
            if (!item.empty()) {
                out.push_back(item);
            }
            item.clear();
        } else {
            item.push_back(c);
        }
    }
    item = trim(item);
    if (!item.empty()) {
        out.push_back(item);
    }
    return out;
}

void addExtension(std::vector<std::string>& extensions, const std::string& extension)
{
    if (extension.empty() || extension == ".") {
        return;
    }
    if (std::find(extensions.begin(), extensions.end(), extension) == extensions.end()) {
        extensions.push_back(extension);
    }
}

}  // namespace

std::vector<std::string> extensionsForMimeType(const std::string& mime)
{
    const std::string key = lower(trim(mime));
    std::vector<std::string> out;
    for (const MimeExtension& known : kMimeExtensions) {
        if (key == known.mime) {
            addExtension(out, known.extension);
        }
    }
    return out;
}

bool splitDesktopExec(const std::string& exec, std::vector<std::string>& argv)
{
    std::vector<std::string> tokens;
    std::string token;
    bool inToken = false;
    bool quoted  = false;
    for (size_t i = 0; i < exec.size(); ++i) {
        const char c = exec[i];
        if (quoted) {
            if (c == '\\' && i + 1 < exec.size()) {
                token.push_back(exec[++i]);
            } else if (c == '"') {
                quoted = false;
            } else {
                token.push_back(c);
            }
            continue;
        }
        if (c == '"') {
            quoted  = true;
            inToken = true;
        } else if (std::isspace(static_cast<unsigned char>(c))) {
            if (inToken) {
                tokens.push_back(token);
                token.clear();
                inToken = false;
            }
        } else {
            token.push_back(c);
            inToken = true;
        }
    }
    if (quoted) {
        return false;
    }
    if (inToken) {
        tokens.push_back(token);
    }

    std::vector<std::string> out;
    bool hasFileCode = false;
    for (const std::string& arg : tokens) {
        if (arg == "%f" || arg == "%F" || arg == "%u" || arg == "%U") {
            if (!hasFileCode) {
                out.push_back("%f");
                hasFileCode = true;
            }
            continue;
        }
        if (arg.size() == 2 && arg[0] == '%' && arg[1] != '%') {
            continue;  // %i, %c, %k and the deprecated codes
        }
        std::string clean;
        for (size_t i = 0; i < arg.size(); ++i) {
            if (arg[i] == '%' && i + 1 < arg.size() && arg[i + 1] == '%') {
                clean.push_back('%');
                ++i;
            } else {
                clean.push_back(arg[i]);
            }
        }
        out.push_back(clean);
    }
    if (out.empty() || out.front().empty() || out.front() == "%f") {
        return false;
    }
    if (!hasFileCode) {
        out.push_back("%f");
    }
    argv = std::move(out);
    return true;
}

bool parseDesktopEntry(std::istream& in, DesktopEntry& entry)
{
    if (!in) {
        return false;
    }
    bool inEntry = false;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        if (line[0] == '[') {
            inEntry = line == "[Desktop Entry]";
            continue;
        }
        if (!inEntry) {
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key   = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));
        if (key == "Name") {
            entry.name = value;
        } else if (key == "Exec") {
            entry.exec = value;
        } else if (key == "Type") {
            entry.application = value == "Application";
        } else if (key == "Hidden") {
            entry.hidden = lower(value) == "true";
        } else if (key == "MimeType") {
            for (const std::string& mime : splitList(value)) {
                for (const std::string& extension : extensionsForMimeType(mime)) {
                    addExtension(entry.extensions, extension);
                }
            }
        } else if (key == "X-Files-Extensions") {
            for (const std::string& extension : splitList(value)) {
                addExtension(entry.extensions, normalizedExtension(extension));
            }
        }
    }
    return true;
}

}  // namespace files
