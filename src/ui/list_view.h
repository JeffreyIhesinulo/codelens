#pragma once
#include "ui.h"
#include "app.h"

// Presentation data taken only from the engine's current snapshot.
struct ListNodePicture {
    uint64_t address = 0, value_address = 0, next_address = 0, next = 0;
    std::string value, name;
    bool next_known = false, freed = false;
};
struct ListReferencePicture {
    std::string name;
    uint64_t target = 0;
    bool known = false;
};
struct ListPicture {
    std::vector<ListNodePicture> nodes;
    std::vector<ListReferencePicture> references;
    uint64_t focused_node = 0;
    std::string focus_result;
};
ListPicture list_picture(const ProgramState &state, const std::string &focus = "");
// Call on the debugger worker, never from rendering. Reads missing node fields through app.h.
ProgramState list_debug_snapshot();

struct ListView {
    float scroll = 0;
    uint64_t followed = 0;
    void draw(const ListPicture &picture, Rectangle area);
};
