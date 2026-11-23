#pragma once

#include "core/files_types.hpp"
#include <string>

namespace files {

std::string browserRowText(const FileEntry& entry, int width);
std::string browserKeyHint(bool places, bool eject, bool usb, bool selected, bool canMove, bool menu);

}  // namespace files
