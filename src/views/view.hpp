#pragma once

#include <lvgl.h>

namespace files {

class View {
public:
    virtual ~View() = default;

    View(const View&)            = delete;
    View& operator=(const View&) = delete;

    virtual void onEnter(lv_obj_t* parent) = 0;
    virtual void onExit()                  = 0;
    virtual void tick(uint32_t nowMs)
    {
        (void)nowMs;
    }

protected:
    View() = default;
};

}  // namespace files
