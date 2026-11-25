#pragma once

#include <istream>
#include <string>
#include <vector>

namespace files {

// The parts of an APPLaunch .desktop file that Files needs to open a file in
// another app.
struct DesktopEntry {
    std::string name;
    std::string exec;
    bool hidden      = false;
    bool application = true;
    // Normalized extensions (".pdf") from MimeType= and X-Files-Extensions=.
    std::vector<std::string> extensions;
};

// Reads the [Desktop Entry] group. Returns false if the stream can't be read.
bool parseDesktopEntry(std::istream& in, DesktopEntry& entry);

// Splits a Desktop Entry Exec value into argv. Whitespace separates
// arguments, double quotes group them and a backslash escapes the next
// character inside quotes. %f, %F, %u and %U become a single "%f" placeholder
// for the file path (appended at the end if none is given), other field codes
// are dropped and %% becomes %. Returns false for an unterminated quote or a
// missing program.
bool splitDesktopExec(const std::string& exec, std::vector<std::string>& argv);

// Extensions (".pdf") for a document MIME type an app may claim with
// MimeType=. Empty if Files doesn't map that type. Case-insensitive.
std::vector<std::string> extensionsForMimeType(const std::string& mime);

}  // namespace files
