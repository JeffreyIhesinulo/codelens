#pragma once

#include "app.h"
#include "ui.h"

struct TutorPanel {
    bool visible = false;
    Lesson lesson;
    std::string code, facts, answer;
    TextInput question;
    float scroll = 0;
    int request = -1;
    std::string history;
    float slide = 0;

    void open(const Lesson &context, const std::string &shown_code, const std::string &shown_facts);
    void update_and_draw();
};
