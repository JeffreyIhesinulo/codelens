#pragma once
// Code editor widget: typing, cursor, scrolling, auto-indent, C/C++ syntax highlighting.
#include "ui.h"
#include <set>
#include <string>
#include <vector>

struct CodeEditor {
    std::vector<std::string> lines{""};
    int row = 0, col = 0; // cursor (col is a byte index; code is ASCII)
    float scroll_y = 0, scroll_x = 0;
    std::set<int> error_lines; // 1-based, highlighted red
    double last_input = 0;     // for cursor blinking
    bool scroll_to_cursor = false;
    bool read_only = false;
    int highlighted_line = 0;
    int locked_prefix = 0, locked_suffix = 0;
    int anchor_row = 0, anchor_col = 0;
    bool dragging = false;

    void set_text(const std::string &text);
    std::string text() const;
    std::string numbered_text() const; // "  1 | code" — for AI prompts
    void lock_template(const std::string &starter, int from, int to);
    bool editable(int line) const;
    bool has_selection() const;
    std::string selected_text() const;

    // Handles input and draws inside r.
    void update_and_draw(Rectangle r);

  private:
    struct Snapshot {
        std::vector<std::string> lines;
        int row, col, anchor_row, anchor_col;
    };
    std::vector<Snapshot> undo_history, redo_history;
    double last_click = -1;
    int last_click_row = -1, last_click_col = -1;
    int click_count = 0;
    Snapshot snapshot() const;
    void remember_edit();
    void restore(const Snapshot &saved);
    void undo();
    void redo();
    void clear_selection();
    void selection_bounds(int &first_row, int &first_col, int &last_row, int &last_col) const;
    bool erase_selection();
    void move_to(int new_row, int new_col, bool extend);
    void select_line(int line);
    void select_all();
    void delete_line();
    void indent_selection(bool outdent);
    void word_left();
    void word_right();
    void insert_char(char c);
    void insert_text(const std::string &s);
    void new_line();
    void backspace();
    void del();
    void clamp_cursor();
    void touch();
};
