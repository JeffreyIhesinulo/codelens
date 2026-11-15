#include "ui.h"

#include <algorithm>
#include <cmath>

Fonts g_fonts;
Focus g_focus = Focus::None;
static bool g_want_hand = false;
static bool g_want_ibeam = false;
static const int* g_dragged_slider = nullptr;

// ---------------------------------------------------------------- fonts

static std::vector<int> codepoints() {
    std::vector<int> cps;
    for (int c = 32; c < 127; ++c) cps.push_back(c);
    for (int c = 0xA0; c <= 0xFF; ++c) cps.push_back(c);     // Latin-1
    for (int c = 0x400; c <= 0x45F; ++c) cps.push_back(c);   // Cyrillic (AI may answer in Russian)
    for (int c : {0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026,
                  0x2190, 0x2192, 0x2191, 0x2193, 0x2713, 0x2717, 0x25B6, 0x25CF, 0x2205})
        cps.push_back(c);
    return cps;
}

static Font load(const std::string& path, float size) {
    std::vector<int> cps = codepoints();
    Font f = LoadFontEx(path.c_str(), (int)size, cps.data(), (int)cps.size());
    SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
    return f;
}

bool fonts_load(const std::string& dir) {
    g_fonts.ui      = load(dir + "/fonts/DejaVuSans.ttf",      g_fonts.ui_size);
    g_fonts.ui_bold = load(dir + "/fonts/DejaVuSans-Bold.ttf", g_fonts.ui_size);
    g_fonts.title   = load(dir + "/fonts/DejaVuSans-Bold.ttf", g_fonts.title_size);
    g_fonts.mono    = load(dir + "/fonts/DejaVuSansMono.ttf",  g_fonts.mono_size);
    return g_fonts.ui.glyphCount > 95 && g_fonts.mono.glyphCount > 95;
}

void fonts_unload() {
    UnloadFont(g_fonts.ui);
    UnloadFont(g_fonts.ui_bold);
    UnloadFont(g_fonts.title);
    UnloadFont(g_fonts.mono);
}

// ---------------------------------------------------------------- text

float text_width(const Font& f, float size, const std::string& s) {
    return MeasureTextEx(f, s.c_str(), size, 0).x;
}

void draw_text(const Font& f, float size, const std::string& s, float x, float y, Color c) {
    DrawTextEx(f, s.c_str(), {std::round(x), std::round(y)}, size, 0, c);
}

static size_t next_character(const std::string& text, size_t offset) {
    int bytes = 0;
    GetCodepointNext(text.c_str() + offset, &bytes);
    return std::min(text.size(), offset + std::max(1, bytes));
}

static size_t previous_character(const std::string& text, size_t offset) {
    if (offset > 0) --offset;
    while (offset > 0 && ((unsigned char)text[offset] & 0xC0) == 0x80) --offset;
    return offset;
}

std::string fit_text(const Font& font, float size, const std::string& text, float width) {
    if (text_width(font, size, text) <= width) return text;
    const std::string ellipsis = "…";
    if (text_width(font, size, ellipsis) > width) return "";
    size_t end = 0;
    while (end < text.size()) {
        size_t next = next_character(text, end);
        if (text_width(font, size, text.substr(0, next) + ellipsis) > width) break;
        end = next;
    }
    return text.substr(0, end) + ellipsis;
}

std::vector<std::string> wrap_text(const Font& f, float size, const std::string& s, float width) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t nl = s.find('\n', start);
        std::string para = s.substr(start, nl == std::string::npos ? std::string::npos : nl - start);

        // keep leading indentation (code samples)
        size_t indent_len = para.find_first_not_of(' ');
        if (indent_len == std::string::npos) indent_len = para.size();
        std::string indent = para.substr(0, indent_len);

        std::string line = indent;
        size_t i = indent_len;
        bool empty = true;
        while (i < para.size()) {
            size_t sp = para.find(' ', i);
            std::string word = para.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
            std::string candidate = empty ? line + word : line + " " + word;
            if (!empty && text_width(f, size, candidate) > width) {
                out.push_back(line);
                line = indent + word;
            } else {
                line = candidate;
            }
            empty = false;
            if (sp == std::string::npos) break;
            i = sp + 1;
        }
        out.push_back(line);
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return out;
}

float draw_wrapped(const Font& f, float size, const std::string& s, Rectangle area, Color c, float gap) {
    std::vector<std::string> lines = wrap_text(f, size, s, area.width);
    float y = area.y;
    for (const std::string& l : lines) {
        draw_text(f, size, l, area.x, y, c);
        y += size + gap;
    }
    return y - area.y;
}

// ---------------------------------------------------------------- frame

void ui_begin_frame() {
    g_want_hand = false;
    g_want_ibeam = false;
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT) && !IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        g_dragged_slider = nullptr;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) g_dragged_slider = nullptr;
}

void ui_end_frame() {
    SetMouseCursor(g_want_hand ? MOUSE_CURSOR_POINTING_HAND :
                   g_want_ibeam ? MOUSE_CURSOR_IBEAM : MOUSE_CURSOR_DEFAULT);
}

void ui_cursor_hand()  { g_want_hand = true; }
void ui_cursor_ibeam() { g_want_ibeam = true; }

bool key_pressed(int key) { return IsKeyPressed(key) || IsKeyPressedRepeat(key); }

// ---------------------------------------------------------------- widgets

bool button(Rectangle r, const std::string& label, bool enabled, ButtonStyle style) {
    bool hover = enabled && CheckCollisionPointRec(GetMousePosition(), r);
    bool down  = hover && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    if (hover) g_want_hand = true;

    Color fill = theme::panel_hi, fg = theme::text, line = theme::border;
    if (style == ButtonStyle::Primary) { fill = theme::accent_dk; line = theme::accent; }
    if (style == ButtonStyle::Ghost)   { fill = {0, 0, 0, 0}; line = {0, 0, 0, 0}; }
    if (hover) fill = style == ButtonStyle::Primary ? Color{60, 108, 156, 255} : theme::border;
    if (down)  fill = theme::accent_dk;
    if (!enabled) { fg = theme::text_dim; fill = theme::panel; }

    DrawRectangleRounded(r, 0.25f, 6, fill);
    if (line.a) DrawRectangleRoundedLinesEx(r, 0.25f, 6, 1, line);
    float w = text_width(g_fonts.ui_bold, g_fonts.ui_size, label);
    draw_text(g_fonts.ui_bold, g_fonts.ui_size, label,
              r.x + (r.width - w) / 2, r.y + (r.height - g_fonts.ui_size) / 2, fg);
    return hover && !g_dragged_slider && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

bool slider(Rectangle r, const std::string& label, int& value, int min, int max) {
    if (r.width <= 16 || r.height < 28 || min > max) return false;

    const float left = r.x + 8, right = r.x + r.width - 8;
    const float y = r.y + r.height - 10;
    const int previous = value;
    value = std::clamp(value, min, max);
    const double range = (double)max - min;
    const Vector2 mouse = GetMousePosition();
    const Rectangle hit = {r.x, y - 10, r.width, 20};
    bool hover = range > 0 && CheckCollisionPointRec(mouse, hit);
    if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) g_dragged_slider = &value;
    bool dragging = range > 0 && g_dragged_slider == &value;
    if (dragging) {
        double fraction = std::clamp((double)(mouse.x - left) / (right - left), 0.0, 1.0);
        value = (int)std::lround(min + range * fraction);
    }
    if (hover || dragging) ui_cursor_hand();

    float thumb = left;
    if (range > 0) thumb += (right - left) * (float)(((double)value - min) / range);
    DrawLineEx({left, y}, {right, y}, 4, theme::border);
    DrawLineEx({left, y}, {thumb, y}, 4, theme::accent_dk);
    DrawCircleV({thumb, y}, 6, range > 0 ? theme::accent : theme::text_dim);
    const float size = g_fonts.ui_size - 2;
    const std::string number = std::to_string(value);
    float number_x = right - text_width(g_fonts.mono, size, number);
    draw_text(g_fonts.ui, size, fit_text(g_fonts.ui, size, label, number_x - left - 12),
              left, r.y, theme::text);
    draw_text(g_fonts.mono, size, number, number_x, r.y, theme::text);
    return value != previous;
}

int action_buttons(Rectangle r, const std::vector<std::string>& labels, int selected) {
    if (labels.empty() || r.width <= 0 || r.height <= 0) return -1;
    float gap = std::min(8.0f, r.width / (2 * labels.size()));
    float width = (r.width - gap * (labels.size() - 1)) / labels.size();
    int clicked = -1;
    for (size_t i = 0; i < labels.size(); ++i) {
        std::string label = fit_text(g_fonts.ui_bold, g_fonts.ui_size, labels[i], width - 12);
        ButtonStyle style = (int)i == selected ? ButtonStyle::Primary : ButtonStyle::Normal;
        if (button({r.x + i * (width + gap), r.y, width, r.height}, label, true, style))
            clicked = (int)i;
    }
    return clicked;
}

static const float HEADER_H = 34;

void panel(Rectangle r, const std::string& title) {
    DrawRectangleRec(r, theme::panel);
    DrawRectangleLinesEx(r, 1, theme::border);
    if (!title.empty()) {
        DrawRectangle((int)r.x, (int)r.y, (int)r.width, (int)HEADER_H, theme::panel_hi);
        DrawLine((int)r.x, (int)(r.y + HEADER_H), (int)(r.x + r.width), (int)(r.y + HEADER_H), theme::border);
        draw_text(g_fonts.ui_bold, g_fonts.ui_size, title, r.x + 12, r.y + (HEADER_H - g_fonts.ui_size) / 2, theme::text);
    }
}

Rectangle panel_body(Rectangle r, bool has_title) {
    float top = has_title ? HEADER_H : 0;
    return {r.x + 12, r.y + top + 10, r.width - 24, r.height - top - 20};
}

void scroll_area(Rectangle view, float content_h, float& offset) {
    float max_off = std::max(0.0f, content_h - view.height);
    if (CheckCollisionPointRec(GetMousePosition(), view))
        offset -= GetMouseWheelMove() * 48;
    offset = std::clamp(offset, 0.0f, max_off);
    if (max_off > 0) {
        float bar_h = std::max(30.0f, view.height * view.height / content_h);
        float bar_y = view.y + (view.height - bar_h) * (offset / max_off);
        DrawRectangleRounded({view.x + view.width + 4, bar_y, 4, bar_h}, 1, 4, theme::border);
    }
}

// Removes the last UTF-8 code point.
static void pop_utf8(std::string& s) {
    while (!s.empty()) {
        unsigned char c = (unsigned char)s.back();
        s.pop_back();
        if ((c & 0xC0) != 0x80) break;
    }
}

bool text_input(Rectangle r, TextInput& in, const std::string& placeholder) {
    Vector2 m = GetMousePosition();
    bool hover = CheckCollisionPointRec(m, r);
    if (hover) g_want_ibeam = true;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        in.focused = hover;
        if (hover) g_focus = Focus::Input;
    }
    if (g_focus != Focus::Input) in.focused = false;

    bool submitted = false;
    if (in.focused) {
        for (int cp = GetCharPressed(); cp > 0; cp = GetCharPressed()) {
            int n = 0;
            const char* utf8 = CodepointToUTF8(cp, &n);
            in.text.append(utf8, (size_t)n);
        }
        if (key_pressed(KEY_BACKSPACE)) pop_utf8(in.text);
        if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_V))
            if (const char* clip = GetClipboardText()) in.text += clip;
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) submitted = true;
    }

    DrawRectangleRounded(r, 0.2f, 6, theme::bg);
    DrawRectangleRoundedLinesEx(r, 0.2f, 6, 1, in.focused ? theme::accent : theme::border);

    BeginScissorMode((int)r.x + 8, (int)r.y, (int)r.width - 16, (int)r.height);
    float ty = r.y + (r.height - g_fonts.ui_size) / 2;
    float w = text_width(g_fonts.ui, g_fonts.ui_size, in.text);
    float shift = std::max(0.0f, w - (r.width - 22));   // keep the end visible
    if (in.text.empty() && !in.focused)
        draw_text(g_fonts.ui, g_fonts.ui_size, placeholder, r.x + 10, ty, theme::text_dim);
    else
        draw_text(g_fonts.ui, g_fonts.ui_size, in.text, r.x + 10 - shift, ty, theme::text);
    if (in.focused && std::fmod(GetTime(), 1.0) < 0.55)
        DrawRectangle((int)(r.x + 11 + w - shift), (int)ty, 2, (int)g_fonts.ui_size, theme::accent);
    EndScissorMode();
    return submitted;
}

namespace {
struct TextLine {
    size_t start, end;
};

// Keep byte offsets so wrapping preserves spaces and cursor positions.
std::vector<TextLine> input_lines(const std::string& text, float width) {
    std::vector<TextLine> lines;
    size_t start = 0, end = 0, last_space = 0;
    while (end < text.size()) {
        if (text[end] == '\n') {
            lines.push_back({start, end});
            start = ++end;
            last_space = start;
            continue;
        }
        size_t next = next_character(text, end);
        if (end > start && text_width(g_fonts.ui, g_fonts.ui_size, text.substr(start, next - start)) > width) {
            if (last_space > start) end = last_space;
            lines.push_back({start, end});
            start = last_space = end;
            continue;
        }
        if (text[end] == ' ' || text[end] == '\t') last_space = next;
        end = next;
    }
    lines.push_back({start, end});
    return lines;
}

size_t cursor_on_line(const std::string& text, TextLine line, float x) {
    for (size_t pos = line.start; pos < line.end;) {
        size_t next = next_character(text, pos);
        float left = text_width(g_fonts.ui, g_fonts.ui_size, text.substr(line.start, pos - line.start));
        float right = text_width(g_fonts.ui, g_fonts.ui_size, text.substr(line.start, next - line.start));
        if (x < (left + right) / 2) return pos;
        pos = next;
    }
    return line.end;
}
}

bool text_area(Rectangle r, TextArea& in, const std::string& placeholder) {
    Rectangle view = {r.x + 10, r.y + 8, r.width - 25, r.height - 16};
    if (view.width <= 0 || view.height <= 0) return false;
    const float size = g_fonts.ui_size, line_h = size + 5;
    Vector2 mouse = GetMousePosition();
    bool hover = CheckCollisionPointRec(mouse, r);
    if (hover) ui_cursor_ibeam();
    in.cursor = std::min(in.cursor, in.text.size());
    while (in.cursor > 0 && in.cursor < in.text.size() && ((unsigned char)in.text[in.cursor] & 0xC0) == 0x80)
        --in.cursor;

    auto lines = input_lines(in.text, view.width - 2);
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (in.focused && !hover && g_focus == Focus::Input) g_focus = Focus::None;
        in.focused = hover;
        if (hover) {
            g_focus = Focus::Input;
            int row = std::clamp((int)((mouse.y - view.y + in.scroll) / line_h), 0, (int)lines.size() - 1);
            in.cursor = cursor_on_line(in.text, lines[row], mouse.x - view.x);
        }
    }
    if (g_focus != Focus::Input) in.focused = false;

    bool edited = false;
    size_t before = in.cursor;
    if (in.focused) {
        auto insert = [&](const std::string& text) {
            in.text.insert(in.cursor, text);
            in.cursor += text.size();
            edited = edited || !text.empty();
        };
        bool command = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) ||
                       IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
        for (int cp = GetCharPressed(); cp > 0; cp = GetCharPressed()) {
            if (cp < 32 || command) continue;
            int bytes = 0;
            const char* encoded = CodepointToUTF8(cp, &bytes);
            insert(std::string(encoded, bytes));
        }
        if (command && IsKeyPressed(KEY_V)) {
            if (const char* clipboard = GetClipboardText()) {
                std::string pasted;
                for (const char* c = clipboard; *c; ++c) {
                    if (*c == '\r') {
                        if (c[1] != '\n') pasted += '\n';
                    } else pasted += *c;
                }
                insert(pasted);
            }
        }
        if (key_pressed(KEY_ENTER) || key_pressed(KEY_KP_ENTER)) insert("\n");
        if (key_pressed(KEY_BACKSPACE) && in.cursor > 0) {
            size_t start = previous_character(in.text, in.cursor);
            in.text.erase(start, in.cursor - start);
            in.cursor = start;
            edited = true;
        }
        if (key_pressed(KEY_DELETE) && in.cursor < in.text.size()) {
            in.text.erase(in.cursor, next_character(in.text, in.cursor) - in.cursor);
            edited = true;
        }
        if (key_pressed(KEY_LEFT)) in.cursor = previous_character(in.text, in.cursor);
        if (key_pressed(KEY_RIGHT) && in.cursor < in.text.size())
            in.cursor = next_character(in.text, in.cursor);
        if (edited) lines = input_lines(in.text, view.width - 2);
    }

    size_t row = 0;
    while (row + 1 < lines.size() && in.cursor >= lines[row + 1].start) ++row;
    float x = text_width(g_fonts.ui, size, in.text.substr(lines[row].start, in.cursor - lines[row].start));
    if (in.focused) {
        size_t target = row;
        if (key_pressed(KEY_UP) && row > 0) --target;
        if (key_pressed(KEY_DOWN) && row + 1 < lines.size()) ++target;
        if (target != row) {
            row = target;
            in.cursor = cursor_on_line(in.text, lines[row], x);
            x = text_width(g_fonts.ui, size, in.text.substr(lines[row].start, in.cursor - lines[row].start));
        }
    }
    float caret_y = row * line_h;
    if (edited || before != in.cursor || (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))) {
        if (caret_y < in.scroll) in.scroll = caret_y;
        if (caret_y + line_h > in.scroll + view.height) in.scroll = caret_y + line_h - view.height;
    }

    DrawRectangleRounded(r, 0.15f, 6, theme::bg);
    DrawRectangleRoundedLinesEx(r, 0.15f, 6, 1, in.focused ? theme::accent : theme::border);
    scroll_area(view, lines.size() * line_h, in.scroll);
    BeginScissorMode((int)view.x, (int)view.y, (int)view.width, (int)view.height);
    if (in.text.empty() && !in.focused) {
        draw_wrapped(g_fonts.ui, size, placeholder, view, theme::text_dim);
    } else {
        for (size_t i = (size_t)(in.scroll / line_h); i < lines.size(); ++i) {
            float y = i * line_h - in.scroll;
            if (y >= view.height) break;
            const TextLine& line = lines[i];
            draw_text(g_fonts.ui, size, in.text.substr(line.start, line.end - line.start),
                      view.x, view.y + y, theme::text);
        }
    }
    if (in.focused && (edited || std::fmod(GetTime(), 1.0) < 0.55))
        DrawRectangleRec({view.x + x, view.y + caret_y - in.scroll, 2, size}, theme::accent);
    EndScissorMode();
    return edited;
}
