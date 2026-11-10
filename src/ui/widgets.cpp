#include "ui.h"

#include <algorithm>
#include <cmath>

Fonts g_fonts;
Focus g_focus = Focus::None;
static bool g_want_hand = false;
static bool g_want_ibeam = false;

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
    return hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
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
