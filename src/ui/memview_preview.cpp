#include "memview_preview.h"
#include "memview.h"
#include "ui.h"

#include <algorithm>
#include <fstream>
#include <iterator>

namespace {
struct Snapshot {
    std::string filename;
    ProgramState state;
    std::string error;
};

Snapshot load_snapshot(const std::string& root, const std::string& id) {
    Snapshot snapshot;
    snapshot.filename = id + ".json";
    std::ifstream file(root + "/tests/fixtures/" + snapshot.filename);
    if (!file) {
        snapshot.error = "Could not open tests/fixtures/" + snapshot.filename;
        return snapshot;
    }
    std::string json((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    state_from_json(json, snapshot.state, snapshot.error);
    return snapshot;
}

void draw_preview(const std::vector<Snapshot>& snapshots, const std::vector<std::string>& labels,
                  int& active, TextArea& notes) {
    constexpr float margin = 28;
    constexpr float sidebar_width = 258;
    const float width = GetScreenWidth() - 2 * margin;
    const float height = GetScreenHeight() - 128;

    draw_text(g_fonts.title, g_fonts.title_size, "Memory visualizer", margin, 23, theme::text);
    if (int clicked = action_buttons({margin, 69, width, 36}, labels, active); clicked >= 0) active = clicked;

    const Snapshot& snapshot = snapshots[active];
    ProgramState state = snapshot.state;
    Rectangle side = {margin + width - sidebar_width, 120, sidebar_width, height - margin};
    Rectangle diagram = {margin, side.y, width - sidebar_width - 16, side.height};
    panel(side, "Snapshot");
    Rectangle info = panel_body(side, true);

    draw_text(g_fonts.ui_bold, g_fonts.ui_size, labels[active], info.x, info.y + 4, theme::accent);
    draw_text(g_fonts.mono, 14, snapshot.filename, info.x, info.y + 31, theme::text_dim);
    float y = info.y + 63;
    y += draw_wrapped(g_fonts.ui, g_fonts.ui_size, state.note, {info.x, y, info.width, 0}, theme::text);

    // The array snapshot starts with p pointing at a[2]. Moving the slider picks another cell.
    if (active == 0 && !state.stack.empty() && state.stack.front().vars.size() >= 2) {
        auto& vars = state.stack.front().vars;
        const auto& cells = vars[0].children;
        if (!cells.empty() && vars[1].is_pointer) {
            static int index = 2;
            slider({info.x, y + 20, info.width, 46}, "Target index", index, 0, (int)cells.size() - 1);
            vars[1].points_to = cells[index].address;
            y += 76;
        }
    }

    const float notes_y = std::max(y + 20, info.y + 205);
    draw_text(g_fonts.ui_bold, g_fonts.ui_size, "Notes", info.x, notes_y, theme::text);
    text_area({info.x, notes_y + 27, info.width, info.y + info.height - notes_y - 27},
              notes, "Write an observation…");

    if (snapshot.error.empty()) draw_memory(state, diagram);
    else draw_wrapped(g_fonts.ui, g_fonts.ui_size, snapshot.error, diagram, theme::danger);
}
}

void run_memory_preview(const std::string& root) {
    const std::vector<std::string> labels = {"Array", "Swap", "Struct", "Heap", "Recursion", "NULL", "Leak"};
    std::vector<Snapshot> snapshots;
    for (const char* id : {"array", "swap", "struct", "heap", "recursion", "null_crash", "leak"})
        snapshots.push_back(load_snapshot(root, id));

    int active = 0;
    TextArea notes;
    while (!WindowShouldClose()) {
        ui_begin_frame();
        BeginDrawing();
        ClearBackground(theme::bg);
        draw_preview(snapshots, labels, active, notes);
        EndDrawing();
        ui_end_frame();
    }
}
