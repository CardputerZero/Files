#pragma once

#include "core/files_router.hpp"
#include "core/files_types.hpp"
#include <lvgl.h>
#include <memory>
#include <string>
#include <vector>

namespace files {

class PreviewPage {
public:
    virtual ~PreviewPage() = default;

    virtual const std::string& title() const              = 0;
    virtual void attach(lv_obj_t* parent)                 = 0;
    virtual void detach()                                 = 0;
    virtual void onKey(uint32_t key, FilesRouter& router) = 0;
    virtual void onKeyState(uint32_t key, bool pressed, FilesRouter& router)
    {
        (void)key;
        (void)pressed;
        (void)router;
    }
    virtual void tick(uint32_t nowMs)
    {
        (void)nowMs;
    }
    virtual bool shouldClose() const
    {
        return false;
    }
    virtual bool suspendsHostRendering() const
    {
        return false;
    }
};

class PreviewSupport {
public:
    virtual ~PreviewSupport() = default;

    virtual const char* id() const                                         = 0;
    virtual bool supports(const FileEntry& file) const                     = 0;
    virtual std::unique_ptr<PreviewPage> open(const FileEntry& file) const = 0;
};

class PreviewRegistry {
public:
    PreviewRegistry();

    void add(std::unique_ptr<PreviewSupport> support);
    std::unique_ptr<PreviewPage> open(const FileEntry& file) const;
    std::unique_ptr<PreviewPage> openWithSupport(const FileEntry& file, const char* supportId) const;

private:
    std::vector<std::unique_ptr<PreviewSupport>> _supports;
};

}  // namespace files
