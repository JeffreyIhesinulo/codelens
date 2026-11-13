#pragma once

#include "app.h"
#include "raylib.h"

// Draws a snapshot and lets the user scroll each column independently.
void draw_memory(const ProgramState& state, Rectangle area);
