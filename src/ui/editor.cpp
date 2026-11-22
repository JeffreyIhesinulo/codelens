#include "editor.h"
#include "app.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_set>

static const int TAB = 4;

static const std::unordered_set<std::string> KEYWORDS = {
    "if",     "else",    "for",       "while",    "do",       "return",   "break",   "continue",  "switch",
    "case",   "default", "goto",      "sizeof",   "struct",   "union",    "enum",    "typedef",   "static",
    "const",  "extern",  "volatile",  "inline",   "class",    "public",   "private", "protected", "new",
    "delete", "this",    "namespace", "using",    "template", "typename", "virtual", "override",  "true",
    "false",  "nullptr", "NULL",      "operator", "try",      "catch",    "throw",   "auto",
};
static const std::unordered_set<std::string> TYPES = {
    "int",    "char", "float",  "double", "void", "long", "short",   "unsigned", "signed",  "bool",
    "size_t", "FILE", "string", "vector", "map",  "std",  "uint8_t", "uint32_t", "int64_t", "uint64_t",
};

// ---------------------------------------------------------------- text

void CodeEditor::set_text(const std::string &text) {
    lines.clear();
    size_t start = 0;
    while (true) {
        size_t nl = text.find('\n', start);
        std::string l = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        // tabs -> spaces, drop CR
        std::string clean;
        for (char c : l) {
            if (c == '\t')
                clean.append(TAB - clean.size() % TAB, ' ');
            else if (c != '\r')
                clean += c;
        }
        lines.push_back(clean);
        if (nl == std::string::npos)
            break;
        start = nl + 1;
    }
    if (lines.size() > 1 && lines.back().empty())
        lines.pop_back();
    row = col = 0;
    scroll_y = scroll_x = 0;
    error_lines.clear();
    locked_prefix = locked_suffix = 0;
    clear_selection();
    dragging = false;
    undo_history.clear();
    redo_history.clear();
}

bool CodeEditor::editable(int line) const {
    return !read_only && line >= locked_prefix && line < (int)lines.size() - locked_suffix;
}

void CodeEditor::lock_template(const std::string &starter, int from, int to) {
    locked_prefix = locked_suffix = 0;
    if (from <= 0 || to < from)
        return;
    CodeEditor original;
    original.set_text(starter);
    if (to > (int)original.lines.size())
        return;
    locked_prefix = from - 1;
    locked_suffix = (int)original.lines.size() - to;
    if ((int)lines.size() < locked_prefix + locked_suffix + 1)
        lines = original.lines;
    std::copy(original.lines.begin(), original.lines.begin() + locked_prefix, lines.begin());
    std::copy(original.lines.end() - locked_suffix, original.lines.end(), lines.end() - locked_suffix);
    clamp_cursor();
    clear_selection();
    undo_history.clear();
    redo_history.clear();
}

std::string CodeEditor::text() const {
    std::string s;
    for (const std::string &l : lines)
        s += l + "\n";
    return s;
}

std::string CodeEditor::numbered_text() const {
    std::string s;
    for (size_t i = 0; i < lines.size(); ++i) {
        std::string n = std::to_string(i + 1);
        s += std::string(n.size() < 3 ? 3 - n.size() : 0, ' ') + n + " | " + lines[i] + "\n";
    }
    return s;
}

// ---------------------------------------------------------------- editing

void CodeEditor::touch() {
    last_input = GetTime();
    scroll_to_cursor = true;
}

void CodeEditor::clamp_cursor() {
    int first = read_only ? 0 : locked_prefix;
    int last = read_only ? (int)lines.size() - 1 : (int)lines.size() - locked_suffix - 1;
    row = std::clamp(row, first, last);
    col = std::clamp(col, 0, (int)lines[row].size());
}

CodeEditor::Snapshot CodeEditor::snapshot() const { return {lines, row, col, anchor_row, anchor_col}; }
void CodeEditor::remember_edit() {
    if (read_only)
        return;
    if (undo_history.empty() || undo_history.back().lines != lines) {
        undo_history.push_back(snapshot());
        if (undo_history.size() > 150)
            undo_history.erase(undo_history.begin());
    }
    redo_history.clear();
}
void CodeEditor::restore(const Snapshot &saved) {
    lines = saved.lines;
    row = saved.row;
    col = saved.col;
    anchor_row = saved.anchor_row;
    anchor_col = saved.anchor_col;
    error_lines.clear();
    dragging = false;
    touch();
}
void CodeEditor::undo() {
    if (read_only || undo_history.empty())
        return;
    redo_history.push_back(snapshot());
    auto saved = undo_history.back();
    undo_history.pop_back();
    restore(saved);
}
void CodeEditor::redo() {
    if (read_only || redo_history.empty())
        return;
    undo_history.push_back(snapshot());
    auto saved = redo_history.back();
    redo_history.pop_back();
    restore(saved);
}
void CodeEditor::clear_selection() {
    anchor_row = row;
    anchor_col = col;
}
bool CodeEditor::has_selection() const { return row != anchor_row || col != anchor_col; }
void CodeEditor::selection_bounds(int &r1, int &c1, int &r2, int &c2) const {
    r1 = anchor_row;
    c1 = anchor_col;
    r2 = row;
    c2 = col;
    if (r1 > r2 || (r1 == r2 && c1 > c2)) {
        std::swap(r1, r2);
        std::swap(c1, c2);
    }
}
std::string CodeEditor::selected_text() const {
    if (!has_selection())
        return "";
    int r1, c1, r2, c2;
    selection_bounds(r1, c1, r2, c2);
    if (r1 == r2)
        return lines[r1].substr(c1, c2 - c1);
    std::string out = lines[r1].substr(c1) + "\n";
    for (int i = r1 + 1; i < r2; ++i)
        out += lines[i] + "\n";
    return out + lines[r2].substr(0, c2);
}
bool CodeEditor::erase_selection() {
    if (read_only || !has_selection())
        return false;
    int r1, c1, r2, c2;
    selection_bounds(r1, c1, r2, c2);
    if (!editable(r1) || !editable(r2))
        return false;
    lines[r1] = lines[r1].substr(0, c1) + lines[r2].substr(c2);
    lines.erase(lines.begin() + r1 + 1, lines.begin() + r2 + 1);
    row = r1;
    col = c1;
    clear_selection();
    error_lines.clear();
    return true;
}
void CodeEditor::move_to(int r, int c, bool extend) {
    row = r;
    col = c;
    clamp_cursor();
    if (!extend)
        clear_selection();
    touch();
}
void CodeEditor::select_line(int line) {
    row = line;
    col = 0;
    clamp_cursor();
    clear_selection();
    if (row + 1 < (int)lines.size() && (read_only || editable(row + 1))) {
        ++row;
        col = 0;
    } else
        col = (int)lines[row].size();
    touch();
}
void CodeEditor::select_all() {
    row = read_only ? 0 : locked_prefix;
    col = 0;
    clear_selection();
    row = read_only ? (int)lines.size() - 1 : (int)lines.size() - locked_suffix - 1;
    col = (int)lines[row].size();
    touch();
}
void CodeEditor::delete_line() {
    if (!editable(row))
        return;
    remember_edit();
    if (has_selection()) {
        erase_selection();
        return;
    }
    if ((int)lines.size() - locked_prefix - locked_suffix > 1)
        lines.erase(lines.begin() + row);
    else
        lines[row].clear();
    col = 0;
    clamp_cursor();
    clear_selection();
    error_lines.clear();
    touch();
}
void CodeEditor::indent_selection(bool outdent) {
    if (!editable(row))
        return;
    remember_edit();
    int r1, c1, r2, c2;
    selection_bounds(r1, c1, r2, c2);
    if (r2 > r1 && c2 == 0)
        --r2;
    for (int i = r1; i <= r2; ++i) {
        if (!editable(i))
            continue;
        int amount = TAB;
        if (outdent) {
            amount = 0;
            while (amount < TAB && amount < (int)lines[i].size() && lines[i][amount] == ' ')
                ++amount;
            lines[i].erase(0, amount);
            amount = -amount;
        } else
            lines[i].insert(0, TAB, ' ');
        if (row == i)
            col = std::max(0, col + amount);
        if (anchor_row == i)
            anchor_col = std::max(0, anchor_col + amount);
    }
    error_lines.clear();
    touch();
}
void CodeEditor::word_left() {
    if (col == 0) {
        if (row > (read_only ? 0 : locked_prefix)) {
            --row;
            col = lines[row].size();
        }
        return;
    }
    const auto &line = lines[row];
    while (col > 0 && std::isspace((unsigned char)line[col - 1]))
        --col;
    if (col > 0 && (std::isalnum((unsigned char)line[col - 1]) || line[col - 1] == '_'))
        while (col > 0 && (std::isalnum((unsigned char)line[col - 1]) || line[col - 1] == '_'))
            --col;
    else if (col > 0)
        --col;
}
void CodeEditor::word_right() {
    if (col == (int)lines[row].size()) {
        if (row < (int)lines.size() - (read_only ? 0 : locked_suffix) - 1) {
            ++row;
            col = 0;
        }
        return;
    }
    const auto &line = lines[row];
    if (std::isalnum((unsigned char)line[col]) || line[col] == '_')
        while (col < (int)line.size() && (std::isalnum((unsigned char)line[col]) || line[col] == '_'))
            ++col;
    else
        ++col;
    while (col < (int)line.size() && std::isspace((unsigned char)line[col]))
        ++col;
}

void CodeEditor::insert_char(char c) {
    if (!editable(row))
        return;
    remember_edit();
    erase_selection();
    std::string &l = lines[row];
    // typing '}' on a blank line removes one indent level
    if (c == '}' && l.find_first_not_of(' ') == std::string::npos && col >= TAB && col == (int)l.size()) {
        l.erase(col - TAB, TAB);
        col -= TAB;
    }
    l.insert(l.begin() + col, c);
    ++col;
    error_lines.clear();
    clear_selection();
}

void CodeEditor::insert_text(const std::string &s) {
    if (!editable(row))
        return;
    remember_edit();
    erase_selection();
    for (char c : s) {
        if (c == '\n') {
            std::string rest = lines[row].substr(col);
            lines[row].erase(col);
            lines.insert(lines.begin() + row + 1, rest);
            ++row;
            col = 0;
        } else if (c == '\t') {
            int n = TAB - col % TAB;
            lines[row].insert(col, n, ' ');
            col += n;
        } else if (c != '\r' && (unsigned char)c >= 32 && (unsigned char)c < 127) {
            lines[row].insert(lines[row].begin() + col, c);
            ++col;
        }
    }
    error_lines.clear();
    clear_selection();
}

// Indentation of the nearest non-blank line at or above r.
static std::string indent_of(const std::vector<std::string> &lines, int r) {
    for (int i = r; i >= 0; --i) {
        size_t ind = lines[i].find_first_not_of(' ');
        if (ind != std::string::npos)
            return std::string(ind, ' ');
    }
    return "";
}

static int brace_balance(const std::vector<std::string> &lines) {
    int b = 0;
    for (const std::string &l : lines)
        for (char c : l)
            b += c == '{' ? 1 : c == '}' ? -1 : 0;
    return b;
}

void CodeEditor::new_line() {
    if (!editable(row))
        return;
    remember_edit();
    erase_selection();
    std::string &l = lines[row];
    std::string indent = indent_of(lines, row);
    size_t own = l.find_first_not_of(' ');
    if (own != std::string::npos && own > (size_t)col)
        indent.resize(std::min(indent.size(), (size_t)col));
    bool after_brace = col > 0 && l[col - 1] == '{';
    bool before_close = col < (int)l.size() && l[col] == '}';
    // Enter after an unmatched '{' also adds the closing brace
    if (after_brace && !before_close && l.substr(col).find_first_not_of(' ') == std::string::npos &&
        brace_balance(lines) > 0) {
        l += "}";
        before_close = true;
    }

    std::string rest = l.substr(col);
    l.erase(col);
    // strip spaces that were before the cursor's new line content
    size_t rs = rest.find_first_not_of(' ');
    rest = rs == std::string::npos ? "" : rest.substr(rs);

    if (after_brace) {
        std::string inner = indent + std::string(TAB, ' ');
        lines.insert(lines.begin() + row + 1, inner);
        if (before_close)
            lines.insert(lines.begin() + row + 2, indent + rest);
        else
            lines[row + 1] += rest;
        ++row;
        col = (int)inner.size();
    } else {
        lines.insert(lines.begin() + row + 1, indent + rest);
        ++row;
        col = (int)indent.size();
    }
    error_lines.clear();
    clear_selection();
}

void CodeEditor::backspace() {
    if (!editable(row))
        return;
    if (has_selection()) {
        remember_edit();
        erase_selection();
        return;
    }
    if (col == 0 && row == locked_prefix)
        return;
    remember_edit();
    if (col > 0) {
        std::string &l = lines[row];
        // in leading whitespace: remove back to the previous tab stop
        bool only_spaces = l.find_first_not_of(' ') >= (size_t)col;
        int n = only_spaces ? ((col - 1) % TAB) + 1 : 1;
        l.erase(col - n, n);
        col -= n;
    } else if (row > 0) {
        col = (int)lines[row - 1].size();
        lines[row - 1] += lines[row];
        lines.erase(lines.begin() + row);
        --row;
    }
    error_lines.clear();
    clear_selection();
}

void CodeEditor::del() {
    if (!editable(row))
        return;
    remember_edit();
    if (erase_selection())
        return;
    if (col < (int)lines[row].size())
        lines[row].erase(col, 1);
    else if (editable(row + 1)) {
        lines[row] += lines[row + 1];
        lines.erase(lines.begin() + row + 1);
    }
    error_lines.clear();
    clear_selection();
}

// ---------------------------------------------------------------- highlighting

struct Span {
    int start, end;
    Color color;
};

static std::vector<Span> highlight(const std::string &l, bool &in_block_comment) {
    std::vector<Span> spans;
    int n = (int)l.size(), i = 0;
    auto first_non_space = l.find_first_not_of(' ');

    while (i < n) {
        if (in_block_comment) {
            size_t end = l.find("*/", i);
            int e = end == std::string::npos ? n : (int)end + 2;
            spans.push_back({i, e, theme::comment});
            if (end != std::string::npos)
                in_block_comment = false;
            i = e;
            continue;
        }
        char c = l[i];
        if (c == '/' && i + 1 < n && l[i + 1] == '/') {
            spans.push_back({i, n, theme::comment});
            break;
        }
        if (c == '/' && i + 1 < n && l[i + 1] == '*') {
            in_block_comment = true;
            continue;
        }
        if (c == '#' && (size_t)i == first_non_space) {
            int e = i + 1;
            while (e < n && std::isalpha((unsigned char)l[e]))
                ++e;
            spans.push_back({i, e, theme::preproc});
            i = e;
            continue;
        }
        if (c == '"' || c == '\'' ||
            (c == '<' && first_non_space != std::string::npos && l[first_non_space] == '#')) {
            char close = c == '<' ? '>' : c;
            int e = i + 1;
            while (e < n && l[e] != close) {
                if (l[e] == '\\')
                    ++e;
                ++e;
            }
            e = std::min(e + 1, n);
            spans.push_back({i, e, theme::str});
            i = e;
            continue;
        }
        if (std::isdigit((unsigned char)c)) {
            int e = i;
            while (e < n && (std::isalnum((unsigned char)l[e]) || l[e] == '.'))
                ++e;
            spans.push_back({i, e, theme::num});
            i = e;
            continue;
        }
        if (std::isalpha((unsigned char)c) || c == '_') {
            int e = i;
            while (e < n && (std::isalnum((unsigned char)l[e]) || l[e] == '_'))
                ++e;
            std::string word = l.substr(i, e - i);
            int k = e;
            while (k < n && l[k] == ' ')
                ++k;
            if (KEYWORDS.count(word))
                spans.push_back({i, e, theme::kw});
            else if (TYPES.count(word))
                spans.push_back({i, e, theme::type});
            else if (k < n && l[k] == '(')
                spans.push_back({i, e, theme::func});
            i = e;
            continue;
        }
        ++i;
    }
    return spans;
}

// ---------------------------------------------------------------- update + draw

void CodeEditor::update_and_draw(Rectangle r) {
    const Font &font = g_fonts.mono;
    const float fs = g_fonts.mono_size;
    const float line_h = std::round(fs * 1.45f);
    const float char_w = MeasureTextEx(font, "M", fs, 0).x;
    const int digits = std::max(3, (int)std::to_string(lines.size()).size());
    const float gutter = digits * char_w + 20;
    Rectangle text_area = {r.x + gutter, r.y + 6, r.width - gutter - 10, r.height - 12};

    Vector2 m = GetMousePosition();
    bool hover = CheckCollisionPointRec(m, r);

    const bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    const bool control = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    const bool command = IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
    const bool alt = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
    const bool primary = control || command;
#ifdef __APPLE__
    const bool by_word = alt || control;
#else
    const bool by_word = control || alt;
#endif
    auto mouse_position = [&] {
        row = (int)std::floor((m.y - text_area.y + scroll_y) / line_h);
        row = std::clamp(row, read_only ? 0 : locked_prefix,
                         (int)lines.size() - (read_only ? 0 : locked_suffix) - 1);
        col = (int)std::round((m.x - text_area.x + scroll_x) / char_w);
        clamp_cursor();
    };
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (hover) {
            g_focus = Focus::Editor;
            mouse_position();
            if (!shift)
                clear_selection();
            if (m.x < text_area.x) {
                select_line(row);
                dragging = false;
            } else {
                bool repeated = GetTime() - last_click < .36 && row == last_click_row &&
                                std::abs(col - last_click_col) <= 1;
                click_count = repeated ? click_count + 1 : 1;
                last_click = GetTime();
                last_click_row = row;
                last_click_col = col;
                if (!shift && click_count == 2) {
                    int begin = col, end = col;
                    const auto &line = lines[row];
                    auto word = [](char c) { return std::isalnum((unsigned char)c) || c == '_'; };
                    if (begin == (int)line.size() && begin > 0)
                        --begin;
                    end = begin;
                    if (begin < (int)line.size()) {
                        if (word(line[begin])) {
                            while (begin > 0 && word(line[begin - 1]))
                                --begin;
                            while (end < (int)line.size() && word(line[end]))
                                ++end;
                        } else
                            ++end;
                    }
                    anchor_row = row;
                    anchor_col = begin;
                    col = end;
                    dragging = false;
                } else if (!shift && click_count >= 3) {
                    select_line(row);
                    click_count = 0;
                    dragging = false;
                } else
                    dragging = true;
            }
            touch();
            scroll_to_cursor = false;
        } else {
            dragging = false;
            if (g_focus == Focus::Editor)
                g_focus = Focus::None;
        }
    }
    if (dragging && IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (m.y < r.y)
            scroll_y -= line_h * GetFrameTime() * 18;
        if (m.y > r.y + r.height)
            scroll_y += line_h * GetFrameTime() * 18;
        if (m.x < text_area.x)
            scroll_x -= char_w * GetFrameTime() * 25;
        if (m.x > r.x + r.width)
            scroll_x += char_w * GetFrameTime() * 25;
        scroll_y = std::max(0.0f, scroll_y);
        scroll_x = std::max(0.0f, scroll_x);
        mouse_position();
        last_input = GetTime();
    }
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        dragging = false;
    if (hover) {
        if (shift)
            scroll_x -= GetMouseWheelMove() * char_w * 6;
        else
            scroll_y -= GetMouseWheelMove() * line_h * 3;
    }
    if (g_focus == Focus::Editor) {
        if (primary && IsKeyPressed(KEY_A))
            select_all();
        if (primary && IsKeyPressed(KEY_L))
            select_line(row);
        if (primary && (IsKeyPressed(KEY_C) || IsKeyPressed(KEY_X))) {
            std::string copied = has_selection() ? selected_text() : lines[row] + "\n";
            SetClipboardText(copied.c_str());
            if (IsKeyPressed(KEY_X) && !read_only) {
                if (has_selection()) {
                    remember_edit();
                    erase_selection();
                    touch();
                } else
                    delete_line();
            }
        }
        if (primary && IsKeyPressed(KEY_Z)) {
            if (shift)
                redo();
            else
                undo();
        }
        if (primary && IsKeyPressed(KEY_Y))
            redo();
        if (primary && shift && IsKeyPressed(KEY_K))
            delete_line();
        if (primary && IsKeyPressed(KEY_V) && !read_only) {
            if (const char *clip = GetClipboardText()) {
                insert_text(clip);
                touch();
            }
        }
        for (int cp = GetCharPressed(); cp > 0; cp = GetCharPressed())
            if (!read_only && cp >= 32 && cp < 127 && !primary && !alt) {
                insert_char((char)cp);
                touch();
            }
        if (!read_only) {
            if (!primary && (key_pressed(KEY_ENTER) || key_pressed(KEY_KP_ENTER))) {
                new_line();
                touch();
            }
            if (key_pressed(KEY_TAB)) {
                if (has_selection() || shift)
                    indent_selection(shift);
                else {
                    insert_text("\t");
                    touch();
                }
            }
            if (key_pressed(KEY_BACKSPACE) || key_pressed(KEY_DELETE)) {
                bool backwards = key_pressed(KEY_BACKSPACE);
                if (!has_selection() && (command || by_word)) {
                    clear_selection();
                    if (command)
                        col = backwards ? 0 : (int)lines[row].size();
                    else if (backwards)
                        word_left();
                    else
                        word_right();
                }
                if (!command || has_selection()) {
                    if (backwards)
                        backspace();
                    else
                        del();
                }
                touch();
            }
        }
        auto navigate = [&](int key) {
            if (!key_pressed(key))
                return;
            if (!shift && has_selection() && (key == KEY_LEFT || key == KEY_RIGHT) && !command && !by_word) {
                int r1, c1, r2, c2;
                selection_bounds(r1, c1, r2, c2);
                move_to(key == KEY_LEFT ? r1 : r2, key == KEY_LEFT ? c1 : c2, false);
                return;
            }
            if (key == KEY_LEFT) {
                if (command)
                    col = 0;
                else if (by_word)
                    word_left();
                else if (col > 0)
                    --col;
                else if (row > (read_only ? 0 : locked_prefix)) {
                    --row;
                    col = lines[row].size();
                }
            }
            if (key == KEY_RIGHT) {
                if (command)
                    col = lines[row].size();
                else if (by_word)
                    word_right();
                else if (col < (int)lines[row].size())
                    ++col;
                else if (row < (int)lines.size() - (read_only ? 0 : locked_suffix) - 1) {
                    ++row;
                    col = 0;
                }
            }
            if (key == KEY_UP) {
                if (command) {
                    row = read_only ? 0 : locked_prefix;
                    col = 0;
                } else
                    --row;
            }
            if (key == KEY_DOWN) {
                if (command) {
                    row = (int)lines.size() - (read_only ? 0 : locked_suffix) - 1;
                    col = lines[row].size();
                } else
                    ++row;
            }
            if (key == KEY_PAGE_UP)
                row -= (int)(text_area.height / line_h);
            if (key == KEY_PAGE_DOWN)
                row += (int)(text_area.height / line_h);
            if (key == KEY_HOME) {
                if (primary) {
                    row = read_only ? 0 : locked_prefix;
                    col = 0;
                } else {
                    size_t indent = lines[row].find_first_not_of(' ');
                    int at = indent == std::string::npos ? lines[row].size() : indent;
                    col = col == at ? 0 : at;
                }
            }
            if (key == KEY_END) {
                if (primary)
                    row = (int)lines.size() - (read_only ? 0 : locked_suffix) - 1;
                col = lines[row].size();
            }
            move_to(row, col, shift);
        };
        for (int key : {KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN, KEY_PAGE_UP, KEY_PAGE_DOWN, KEY_HOME, KEY_END})
            navigate(key);
        if (IsKeyPressed(KEY_ESCAPE)) {
            clear_selection();
            dragging = false;
        }
    }

    // ---- keep cursor visible
    if (scroll_to_cursor) {
        float cy = row * line_h, cx = col * char_w;
        if (cy < scroll_y)
            scroll_y = cy;
        if (cy + line_h > scroll_y + text_area.height)
            scroll_y = cy + line_h - text_area.height;
        if (cx < scroll_x)
            scroll_x = std::max(0.0f, cx - char_w * 8);
        if (cx > scroll_x + text_area.width - char_w * 2)
            scroll_x = cx - text_area.width + char_w * 8;
        scroll_to_cursor = false;
    }
    float content_h = lines.size() * line_h;
    scroll_y = std::clamp(scroll_y, 0.0f, std::max(0.0f, content_h - text_area.height + line_h * 3));
    scroll_x = std::max(0.0f, scroll_x);

    // ---- draw
    DrawRectangleRec(r, theme::bg);
    DrawRectangle((int)r.x, (int)r.y, (int)gutter - 8, (int)r.height, theme::panel);

    BeginScissorMode((int)r.x, (int)r.y, (int)r.width, (int)r.height);
    int first = std::max(0, (int)(scroll_y / line_h) - 1);
    int last = std::min((int)lines.size() - 1, (int)((scroll_y + text_area.height) / line_h) + 1);

    bool in_block = false; // block-comment state from the top of the file
    for (int i = 0; i < first; ++i)
        highlight(lines[i], in_block);

    for (int i = first; i <= last; ++i) {
        float y = text_area.y + i * line_h - scroll_y;
        bool is_err = error_lines.count(i + 1);
        if (i + 1 == highlighted_line)
            DrawRectangleRec({r.x + gutter - 8, y, r.width - gutter + 8, line_h}, theme::accent_dk);
        else if (is_err)
            DrawRectangle((int)(r.x + gutter - 8), (int)y, (int)(r.width - gutter + 8), (int)line_h,
                          theme::error_bg);
        else if (i == row && g_focus == Focus::Editor)
            DrawRectangle((int)(r.x + gutter - 8), (int)y, (int)(r.width - gutter + 8), (int)line_h,
                          theme::line_hi);

        std::string num = std::to_string(i + 1);
        draw_text(font, fs, num, r.x + gutter - 16 - num.size() * char_w, y + (line_h - fs) / 2,
                  is_err ? theme::danger : (i == row ? theme::text : theme::text_dim));
    }

    BeginScissorMode((int)text_area.x - 2, (int)r.y, (int)text_area.width + 2, (int)r.height);
    if (has_selection()) {
        int r1, c1, r2, c2;
        selection_bounds(r1, c1, r2, c2);
        for (int i = std::max(first, r1); i <= std::min(last, r2); ++i) {
            int start = i == r1 ? c1 : 0, end = i == r2 ? c2 : (int)lines[i].size() + 1;
            DrawRectangleRec({text_area.x + start * char_w - scroll_x, text_area.y + i * line_h - scroll_y,
                              std::max(0, (end - start)) * char_w, line_h},
                             theme::accent_dk);
        }
    }
    for (int i = first; i <= last; ++i) {
        float y = text_area.y + i * line_h - scroll_y + (line_h - fs) / 2;
        const std::string &l = lines[i];
        std::vector<Span> spans = highlight(l, in_block);
        // draw default-colored text, then colored spans over it char by char
        std::vector<Color> colors(l.size(), theme::text);
        for (const Span &s : spans)
            for (int k = s.start; k < s.end && k < (int)l.size(); ++k)
                colors[k] = s.color;
        if (!read_only && !editable(i))
            std::fill(colors.begin(), colors.end(), theme::text_dim);
        int k = 0;
        while (k < (int)l.size()) {
            int e = k;
            while (e < (int)l.size() && ColorToInt(colors[e]) == ColorToInt(colors[k]))
                ++e;
            draw_text(font, fs, l.substr(k, e - k), text_area.x + k * char_w - scroll_x, y, colors[k]);
            k = e;
        }
    }
    // cursor
    if (!read_only && g_focus == Focus::Editor && std::fmod(GetTime() - last_input, 1.0) < 0.55) {
        float cx = text_area.x + col * char_w - scroll_x;
        float cy = text_area.y + row * line_h - scroll_y;
        DrawRectangle((int)cx, (int)cy + 2, 2, (int)line_h - 4, theme::accent);
    }
    EndScissorMode();

    if (hover)
        ui_cursor_ibeam();
    DrawRectangleLinesEx(r, 1, g_focus == Focus::Editor ? theme::accent_dk : theme::border);
    if (hover && m.x >= text_area.x && !has_selection() && !dragging) {
        int line = (int)((m.y - text_area.y + scroll_y) / line_h);
        int at = (int)((m.x - text_area.x + scroll_x) / char_w);
        if (line >= 0 && line < (int)lines.size() && at >= 0 && at < (int)lines[line].size()) {
            const auto &source = lines[line];
            auto word = [](char c) { return std::isalnum((unsigned char)c) || c == '_' || c == '#'; };
            int begin = at, end = at + 1;
            if (word(source[at])) {
                while (begin > 0 && word(source[begin - 1]))
                    --begin;
                while (end < (int)source.size() && word(source[end]))
                    ++end;
            }
            std::string token = source.substr(begin, end - begin);
            std::string explanation = glossary_lookup(token);
            for (int start = std::max(0, at - 1); start <= at; ++start) {
                auto pair = source.substr(start, 2);
                if (!glossary_lookup(pair).empty()) {
                    token = pair;
                    explanation = glossary_lookup(pair);
                    break;
                }
            }
            if (!explanation.empty()) {
                float width = std::min(345.0f, GetScreenWidth() - 32.0f);
                float height =
                    wrap_text(g_fonts.ui, 15, token + ": " + explanation, width - 24).size() * 20 + 20;
                Rectangle tooltip{std::clamp(m.x + 14, 8.0f, GetScreenWidth() - width - 8),
                                  std::clamp(m.y + 24, 8.0f, GetScreenHeight() - height - 8), width, height};
                DrawRectangleRounded(tooltip, .08f, 6, theme::panel_hi);
                DrawRectangleRoundedLinesEx(tooltip, .08f, 6, 1, theme::accent);
                draw_wrapped(g_fonts.ui, 15, token + ": " + explanation,
                             {tooltip.x + 12, tooltip.y + 10, width - 24, 0}, theme::text, 5);
            }
        }
    }
}
