#pragma once

#include "app.h"
#include "ui.h"

#include <vector>

struct ChatMessage {
    bool from_ai;
    std::string text;
};

// Chat bubbles shared by the tutor panel and the Practice screen: tutor on the left,
// student on the right, `code` drawn in the code font, paragraphs and "- " lists kept.
// Returns the content height. stick_bottom scrolls to the newest message once.
float draw_chat(Rectangle area, const std::vector<ChatMessage> &messages, bool thinking, float &scroll,
                bool &stick_bottom);

// The same formatting for longer texts (the lesson reading): "## " headings, 4-space code
// blocks, lists, `code` and **bold**. Scrolls inside area.
float draw_rich_text(Rectangle area, const std::string &text, float &scroll);
// The same, without its own scrolling: draws at (x, y) and returns the height (draw = false only measures).
float draw_rich_block(float x, float y, float width, const std::string &text, bool draw = true);

struct TutorPanel {
    bool visible = false;
    Lesson lesson;
    std::string code, facts, answer;
    TextInput question;
    float scroll = 0;
    int request = -1;
    std::string history;                 // plain text sent to the model as the conversation
    std::vector<ChatMessage> messages;   // what the panel draws
    bool stick_bottom = false;
    float slide = 0;

    void open(const Lesson &context, const std::string &shown_code, const std::string &shown_facts);
    void update_and_draw();
};
