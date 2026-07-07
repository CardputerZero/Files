#pragma once

#include "core/files_types.hpp"
#include <tools/observable/observable.hpp>
#include <vector>

namespace files {

class FilesRouter {
public:
    smooth_ui_toolkit::Observable<PageId>& currentPage()
    {
        return _current_page;
    }

    PageId page() const
    {
        return _current_page.get();
    }

    void replace(PageId page);
    void push(PageId page);
    void back();

private:
    smooth_ui_toolkit::Observable<PageId> _current_page{PageId::Browser};
    std::vector<PageId> _history;
};

}  // namespace files
