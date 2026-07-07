#pragma once

#include <lvgl.h>
#include <memory>

namespace files {

class MagicView {
public:
    explicit MagicView(lv_obj_t* parent);
    ~MagicView();

    MagicView(const MagicView&)            = delete;
    MagicView& operator=(const MagicView&) = delete;

    void generate(uint32_t magicSerial);
    void tick(uint32_t nowMs);

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

}  // namespace files
