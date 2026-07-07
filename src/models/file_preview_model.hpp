#pragma once

#include "core/files_types.hpp"
#include "preview/preview_support.hpp"
#include <tools/observable/single_observable.hpp>
#include <memory>

namespace files {

class FilePreviewModel {
public:
    FilePreviewModel();

    smooth_ui_toolkit::SingleObservable<PreviewPage*>& page()
    {
        return _page_observable;
    }

    bool open(const FileEntry& file);
    bool openInfo(const FileEntry& file);
    void close();

private:
    PreviewRegistry _registry;
    std::unique_ptr<PreviewPage> _page;
    smooth_ui_toolkit::SingleObservable<PreviewPage*> _page_observable{nullptr};
};

}  // namespace files
