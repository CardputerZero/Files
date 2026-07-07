#pragma once

#include <lvgl.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>

namespace files {

class BottomKeyBar {
public:
    struct Item {
        uint32_t key      = 0;
        const void* image = nullptr;
    };

    explicit BottomKeyBar(lv_obj_t* parent);
    ~BottomKeyBar();

    void setItems(std::initializer_list<Item> items);
    void tick();

private:
    class KeyIcon;
    class Indicator;

    struct KeySlot {
        uint32_t key = 0;
        int32_t x    = 0;
        std::unique_ptr<KeyIcon> icon;
        std::unique_ptr<Indicator> indicator;
    };

    std::array<KeySlot, 5> _slots;

    KeySlot* slotFor(uint32_t key);
};

}  // namespace files
