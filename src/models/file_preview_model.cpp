#include "models/file_preview_model.hpp"

#include <spdlog/spdlog.h>

namespace files {

FilePreviewModel::FilePreviewModel()
{
}

bool FilePreviewModel::open(const FileEntry& file)
{
    if (!isRegularPreviewFile(file)) {
        spdlog::info("FilePreviewModel: refusing non-regular preview path='{}'", file.path);
        close();
        return false;
    }

    _page = _registry.open(file);
    _page_observable.set(_page.get());
    return _page != nullptr;
}

bool FilePreviewModel::openInfo(const FileEntry& file)
{
    _page = _registry.openWithSupport(file, "info");
    _page_observable.set(_page.get());
    return _page != nullptr;
}

void FilePreviewModel::close()
{
    if (_page) {
        _page->detach();
    }
    _page.reset();
    _page_observable.set(nullptr);
}

}  // namespace files
