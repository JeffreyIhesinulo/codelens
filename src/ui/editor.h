#pragma once
// Code editor widget: typing, cursor, scrolling, auto-indent, C/C++ syntax highlighting.
#include "ui.h"
#include <set>
#include <string>
#include <vector>

struct CodeEditor {
    std::vector<std::string> lines{""};
    int row = 0, col = 0;          // cursor (col is a byte index; code is ASCII)
    float scroll_y = 0, scroll_x = 0;
    std::set<int> error_lines;     // 1-based, highlighted red
    double last_input = 0;         // for cursor blinking
    bool scroll_to_cursor = false;

    void set_text(const std::string& text);
    std::string text() const;
    std::string numbered_text() const;   // "  1 | code" — for AI prompts

    // Handles input and draws inside r.
    void update_and_draw(Rectangle r);

private:
    void insert_char(char c);
    void insert_text(const std::string& s);
    void new_line();
    void backspace();
    void del();
    void clamp_cursor();
    void touch();
};
