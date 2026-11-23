#pragma once
#include "memview.h"
#include "ui.h"
#include "list_view.h"

struct ProgramPictureFacts {
    bool has_values = false;
    std::string counter;
    std::string selected_box;
    uint64_t selected_address = 0;
};
ProgramPictureFacts program_picture_facts(const ProgramState &state, const std::string &focus = "");

// One view for the early output/condition/loop/array lessons and later pointer lessons.
struct ProgramView {
    float output_scroll = 0;
    ListView list;
    std::string branch;
    std::string previous_note;
    void reset();
    void update_and_draw(const ProgramState &state, Rectangle area, ShowLevel level, const std::string &topic,
                         bool waiting, const std::string &focus = "");
    static Rectangle why_button(Rectangle area);
};
