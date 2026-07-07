#pragma once

#include "models/file_browser_model.hpp"
#include "models/file_preview_model.hpp"

namespace files {

class FilesModel {
public:
    explicit FilesModel(std::string start_directory = {});

    FileBrowserModel& browser()
    {
        return _browser;
    }

    FilePreviewModel& preview()
    {
        return _preview;
    }

private:
    FileBrowserModel _browser;
    FilePreviewModel _preview;
};

}  // namespace files
