#pragma once

#include "app.h"
#include "raylib.h"

namespace ui {
using ShowLevel = ::ShowLevel;
}

// The developer preview defaults to all details. Student views pass an explicit level.
void draw_memory(const ProgramState& state, Rectangle area, ui::ShowLevel level = ui::ShowLevel::Heap,
                 uint64_t highlighted_address = 0);
