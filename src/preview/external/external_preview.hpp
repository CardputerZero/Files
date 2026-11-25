#pragma once

#include "preview/preview_support.hpp"

namespace files {

// Opens a file in another installed APPLaunch app.
//
// Any .desktop file in the APPLaunch applications folder that lists
// MimeType=... (for example "application/pdf;") or X-Files-Extensions=...
// (for example ".pdf;.xps;") becomes the handler for those files. Files runs
// its Exec with the file path as the last argument (or in place of %f/%F/%u/%U),
// stops drawing until that app exits and ignores the keyboard meanwhile.
std::unique_ptr<PreviewSupport> createExternalPreviewSupport();

}  // namespace files
