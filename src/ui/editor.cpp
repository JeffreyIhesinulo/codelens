#include "editor.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_set>

static const int TAB = 4;

static const std::unordered_set<std::string> KEYWORDS = {
    "if", "else", "for", "while", "do", "return", "break", "continue", "switch", "case",
    "default", "goto", "sizeof", "struct", "union", "enum", "typedef", "static", "const",
    "extern", "volatile", "inline", "class", "public", "private", "protected", "new",
    "delete", "this", "namespace", "using", "template", "typename", "virtual", "override",
    "true", "false", "nullptr", "NULL", "operator", "try", "catch", "throw", "auto",
};
static const std::unordered_set<std::string> TYPES = {
    "int", "char", "float", "double", "void", "long", "short", "unsigned", "signed", "bool",
    "size_t", "FILE", "string", "vector", "map", "std", "uint8_t", "uint32_t", "int64_t", "uint64_t",
};

// ---------------------------------------------------------------- text

void CodeEditor::set_text(const std::string& text) {
    lines.clear();
    size_t start = 0;
    while (true) {
        size_t nl = text.find('\n', start);
        std::string l = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        // tabs -> spaces, drop CR
        std::string clean;
        for (char c : l) {
            if (c == '\t') clean.append(TAB - clean.size() % TAB, ' ');
            else if (c != '\r') clean += c;
        }
        lines.push_back(clean);
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    if (lines.size() > 1 && lines.back().empty()) lines.pop_back();
    row = col = 0;
    scroll_y = scroll_x = 0;
    error_lines.clear();
}

std::string CodeEditor::text() const {
    std::string s;
    for (const std::string& l : lines) s += l + "\n";
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
    row = std::clamp(row, 0, (int)lines.size() - 1);
    col = std::clamp(col, 0, (int)lines[row].size());
}

void CodeEditor::insert_char(char c) {
    std::string& l = lines[row];
    // typing '}' on a blank line removes one indent level
    if (c == '}' && l.find_first_not_of(' ') == std::string::npos && col >= TAB && col == (int)l.size()) {
        l.erase(col - TAB, TAB);
        col -= TAB;
    }
    l.insert(l.begin() + col, c);
    ++col;
    error_lines.clear();
}

void CodeEditor::insert_text(const std::string& s) {
    for (char c : s) {
        if (c == '\n') {
            std::string rest = lines[row].substr(col);
            lines[row].erase(col);
            lines.insert(lines.begin() + row + 1, rest);
            ++row; col = 0;
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
}

// Indentation of the nearest non-blank line at or above r.
static std::string indent_of(const std::vector<std::string>& lines, int r) {
    for (int i = r; i >= 0; --i) {
        size_t ind = lines[i].find_first_not_of(' ');
        if (ind != std::string::npos) return std::string(ind, ' ');
    }
    return "";
}

static int brace_balance(const std::vector<std::string>& lines) {
    int b = 0;
    for (const std::string& l : lines)
        for (char c : l) b += c == '{' ? 1 : c == '}' ? -1 : 0;
    return b;
}

void CodeEditor::new_line() {
    std::string& l = lines[row];
    std::string indent = indent_of(lines, row);
    size_t own = l.find_first_not_of(' ');
    if (own != std::string::npos && own > (size_t)col) indent.resize(std::min(indent.size(), (size_t)col));
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
        if (before_close) lines.insert(lines.begin() + row + 2, indent + rest);
        else lines[row + 1] += rest;
        ++row;
        col = (int)inner.size();
    } else {
        lines.insert(lines.begin() + row + 1, indent + rest);
        ++row;
        col = (int)indent.size();
    }
    error_lines.clear();
}

void CodeEditor::backspace() {
    if (col > 0) {
        std::string& l = lines[row];
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
}

void CodeEditor::del() {
    if (col < (int)lines[row].size()) lines[row].erase(col, 1);
    else if (row + 1 < (int)lines.size()) {
        lines[row] += lines[row + 1];
        lines.erase(lines.begin() + row + 1);
    }
    error_lines.clear();
}

// ---------------------------------------------------------------- highlighting

struct Span { int start, end; Color color; };

static std::vector<Span> highlight(const std::string& l, bool& in_block_comment) {
    std::vector<Span> spans;
    int n = (int)l.size(), i = 0;
    auto first_non_space = l.find_first_not_of(' ');

    while (i < n) {
        if (in_block_comment) {
            size_t end = l.find("*/", i);
            int e = end == std::string::npos ? n : (int)end + 2;
            spans.push_back({i, e, theme::comment});
            if (end != std::string::npos) in_block_comment = false;
            i = e;
            continue;
        }
        char c = l[i];
        if (c == '/' && i + 1 < n && l[i + 1] == '/') { spans.push_back({i, n, theme::comment}); break; }
        if (c == '/' && i + 1 < n && l[i + 1] == '*') { in_block_comment = true; continue; }
        if (c == '#' && (size_t)i == first_non_space) {
            int e = i + 1;
            while (e < n && std::isalpha((unsigned char)l[e])) ++e;
            spans.push_back({i, e, theme::preproc});
            i = e;
            continue;
        }
        if (c == '"' || c == '\'' || (c == '<' && first_non_space != std::string::npos && l[first_non_space] == '#')) {
            char close = c == '<' ? '>' : c;
            int e = i + 1;
            while (e < n && l[e] != close) { if (l[e] == '\\') ++e; ++e; }
            e = std::min(e + 1, n);
            spans.push_back({i, e, theme::str});
            i = e;
            continue;
        }
        if (std::isdigit((unsigned char)c)) {
            int e = i;
            while (e < n && (std::isalnum((unsigned char)l[e]) || l[e] == '.')) ++e;
            spans.push_back({i, e, theme::num});
            i = e;
            continue;
        }
        if (std::isalpha((unsigned char)c) || c == '_') {
            int e = i;
            while (e < n && (std::isalnum((unsigned char)l[e]) || l[e] == '_')) ++e;
            std::string word = l.substr(i, e - i);
            int k = e;
            while (k < n && l[k] == ' ') ++k;
            if (KEYWORDS.count(word))      spans.push_back({i, e, theme::kw});
            else if (TYPES.count(word))    spans.push_back({i, e, theme::type});
            else if (k < n && l[k] == '(') spans.push_back({i, e, theme::func});
            i = e;
            continue;
        }
        ++i;
    }
    return spans;
}

// ---------------------------------------------------------------- update + draw

void CodeEditor::update_and_draw(Rectangle r) {
    const Font& font = g_fonts.mono;
    const float fs = g_fonts.mono_size;
    const float line_h = std::round(fs * 1.45f);
    const float char_w = MeasureTextEx(font, "M", fs, 0).x;
    const int digits = std::max(3, (int)std::to_string(lines.size()).size());
    const float gutter = digits * char_w + 20;
    Rectangle text_area = {r.x + gutter, r.y + 6, r.width - gutter - 10, r.height - 12};

    Vector2 m = GetMousePosition();
    bool hover = CheckCollisionPointRec(m, r);

    // ---- mouse
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (hover) {
            g_focus = Focus::Editor;
            row = (int)((m.y - text_area.y + scroll_y) / line_h);
            clamp_cursor();
            col = (int)std::round((m.x - text_area.x + scroll_x) / char_w);
            clamp_cursor();
            touch();
            scroll_to_cursor = false;
        } else if (g_focus == Focus::Editor) {
            g_focus = Focus::None;
        }
    }
    if (hover) {
        if (IsKeyDown(KEY_LEFT_SHIFT)) scroll_x -= GetMouseWheelMove() * char_w * 6;
        else scroll_y -= GetMouseWheelMove() * line_h * 3;
    }

    // ---- keyboard
    if (g_focus == Focus::Editor) {
        bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        for (int cp = GetCharPressed(); cp > 0; cp = GetCharPressed())
            if (cp >= 32 && cp < 127 && !ctrl) { insert_char((char)cp); touch(); }

        if (!ctrl && (key_pressed(KEY_ENTER) || key_pressed(KEY_KP_ENTER))) { new_line(); touch(); }
        if (key_pressed(KEY_BACKSPACE)) { backspace(); touch(); }
        if (key_pressed(KEY_DELETE))    { del(); touch(); }
        if (key_pressed(KEY_TAB))       { insert_text("\t"); touch(); }
        if (key_pressed(KEY_LEFT)) {
            if (col > 0) --col; else if (row > 0) { --row; col = (int)lines[row].size(); }
            touch();
        }
        if (key_pressed(KEY_RIGHT)) {
            if (col < (int)lines[row].size()) ++col; else if (row + 1 < (int)lines.size()) { ++row; col = 0; }
            touch();
        }
        if (key_pressed(KEY_UP))        { --row; clamp_cursor(); touch(); }
        if (key_pressed(KEY_DOWN))      { ++row; clamp_cursor(); touch(); }
        if (key_pressed(KEY_PAGE_UP))   { row -= (int)(text_area.height / line_h); clamp_cursor(); touch(); }
        if (key_pressed(KEY_PAGE_DOWN)) { row += (int)(text_area.height / line_h); clamp_cursor(); touch(); }
        if (key_pressed(KEY_HOME)) {
            int ind = (int)lines[row].find_first_not_of(' ');
            if (ind < 0) ind = (int)lines[row].size();
            col = col == ind ? 0 : ind;   // smart home
            touch();
        }
        if (key_pressed(KEY_END)) { col = (int)lines[row].size(); touch(); }
        if (ctrl && IsKeyPressed(KEY_V)) {
            if (const char* clip = GetClipboardText()) { insert_text(clip); touch(); }
        }
        if (ctrl && IsKeyPressed(KEY_C)) SetClipboardText(text().c_str());   // copy all (MVP)
    }

    // ---- keep cursor visible
    if (scroll_to_cursor) {
        float cy = row * line_h, cx = col * char_w;
        if (cy < scroll_y) scroll_y = cy;
        if (cy + line_h > scroll_y + text_area.height) scroll_y = cy + line_h - text_area.height;
        if (cx < scroll_x) scroll_x = std::max(0.0f, cx - char_w * 8);
        if (cx > scroll_x + text_area.width - char_w * 2) scroll_x = cx - text_area.width + char_w * 8;
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
    int last  = std::min((int)lines.size() - 1, (int)((scroll_y + text_area.height) / line_h) + 1);

    bool in_block = false;   // block-comment state from the top of the file
    for (int i = 0; i < first; ++i) highlight(lines[i], in_block);

    for (int i = first; i <= last; ++i) {
        float y = text_area.y + i * line_h - scroll_y;
        bool is_err = error_lines.count(i + 1);
        if (is_err)
            DrawRectangle((int)(r.x + gutter - 8), (int)y, (int)(r.width - gutter + 8), (int)line_h, theme::error_bg);
        else if (i == row && g_focus == Focus::Editor)
            DrawRectangle((int)(r.x + gutter - 8), (int)y, (int)(r.width - gutter + 8), (int)line_h, theme::line_hi);

        std::string num = std::to_string(i + 1);
        draw_text(font, fs, num, r.x + gutter - 16 - num.size() * char_w, y + (line_h - fs) / 2,
                  is_err ? theme::danger : (i == row ? theme::text : theme::text_dim));
    }

    BeginScissorMode((int)text_area.x - 2, (int)r.y, (int)text_area.width + 2, (int)r.height);
    for (int i = first; i <= last; ++i) {
        float y = text_area.y + i * line_h - scroll_y + (line_h - fs) / 2;
        const std::string& l = lines[i];
        std::vector<Span> spans = highlight(l, in_block);
        // draw default-colored text, then colored spans over it char by char
        std::vector<Color> colors(l.size(), theme::text);
        for (const Span& s : spans)
            for (int k = s.start; k < s.end && k < (int)l.size(); ++k) colors[k] = s.color;
        int k = 0;
        while (k < (int)l.size()) {
            int e = k;
            while (e < (int)l.size() && ColorToInt(colors[e]) == ColorToInt(colors[k])) ++e;
            draw_text(font, fs, l.substr(k, e - k), text_area.x + k * char_w - scroll_x, y, colors[k]);
            k = e;
        }
    }
    // cursor
    if (g_focus == Focus::Editor && std::fmod(GetTime() - last_input, 1.0) < 0.55) {
        float cx = text_area.x + col * char_w - scroll_x;
        float cy = text_area.y + row * line_h - scroll_y;
        DrawRectangle((int)cx, (int)cy + 2, 2, (int)line_h - 4, theme::accent);
    }
    EndScissorMode();

    if (hover) ui_cursor_ibeam();
    DrawRectangleLinesEx(r, 1, g_focus == Focus::Editor ? theme::accent_dk : theme::border);
}
