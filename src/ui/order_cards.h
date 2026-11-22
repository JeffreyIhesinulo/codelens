#pragma once
#include "ui.h"

// Draggable code cards, with arrow buttons as an alternative to dragging.
struct OrderCards {
    int dragged = -1;
    int destination = -1;
    float grab_y = 0;
    bool block_release = false;

    bool active() const { return dragged >= 0; }
    bool update_and_draw(Rectangle view, const std::vector<std::string> &lines, std::vector<int> &order,
                         bool checked, bool enabled, float &scroll);
};
