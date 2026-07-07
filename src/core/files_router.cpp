#include "core/files_router.hpp"

namespace files {

void FilesRouter::replace(PageId page)
{
    if (_current_page.get() == page) {
        return;
    }
    _current_page.set(page);
}

void FilesRouter::push(PageId page)
{
    if (_current_page.get() == page) {
        return;
    }
    _history.push_back(_current_page.get());
    _current_page.set(page);
}

void FilesRouter::back()
{
    if (_history.empty()) {
        replace(PageId::Browser);
        return;
    }

    PageId previous = _history.back();
    _history.pop_back();
    _current_page.set(previous);
}

}  // namespace files
