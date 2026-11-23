#pragma once

#include <lvgl.h>
#include <cstdint>
#include <initializer_list>
#include <array>
#include <string>

namespace files {

class BottomKeyBar {
public:
    struct Item { uint32_t key; const void* image; const char* label = nullptr; };
    struct Slot { int x; int width; std::string text; };
    explicit BottomKeyBar(lv_obj_t* parent);
    ~BottomKeyBar();
    void setItems(std::initializer_list<Item> items);
    void setSlots(std::initializer_list<Slot> slots);
    void show(bool visible);

private:
    lv_obj_t* _bar;
    lv_obj_t* _label;
    std::array<lv_obj_t*, 5> _slots{};
};

}  // namespace files