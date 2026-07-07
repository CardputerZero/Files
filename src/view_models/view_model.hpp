#pragma once

#include "core/files_router.hpp"
#include "core/files_types.hpp"

namespace files {

class ViewModel {
public:
    explicit ViewModel(FilesRouter& router) : _router(router)
    {
    }
    virtual ~ViewModel() = default;

    ViewModel(const ViewModel&)            = delete;
    ViewModel& operator=(const ViewModel&) = delete;

    virtual PageId pageId() const = 0;
    virtual void onEnter()
    {
    }
    virtual void onExit()
    {
    }
    virtual void onKey(uint32_t key)
    {
        (void)key;
    }
    virtual void onKeyState(uint32_t key, bool pressed)
    {
        (void)key;
        (void)pressed;
    }
    virtual void tick(uint32_t nowMs)
    {
        (void)nowMs;
    }
    virtual bool suspendsHostRendering() const
    {
        return false;
    }

protected:
    FilesRouter& _router;
};

}  // namespace files
