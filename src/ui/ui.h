#pragma once
// Shared UI toolkit: theme, fonts, immediate-mode widgets.
#include "raylib.h"
#include <string>
#include <vector>

// ---------------------------------------------------------------- theme
// "Violet night" palette (22 Nov 2026), taken from the team's Figma mock-up.
namespace theme {
inline const Color bg        = {13, 14, 20, 255};
inline const Color panel     = {21, 22, 31, 255};
inline const Color panel_hi  = {30, 31, 44, 255};
inline const Color border    = {42, 43, 60, 255};
inline const Color text      = {232, 232, 242, 255};
inline const Color text_dim  = {140, 141, 166, 255};
inline const Color accent    = {167, 139, 250, 255};
inline const Color accent_dk = {70, 52, 128, 255};
inline const Color accent_hover = {94, 70, 170, 255};
inline const Color accent_soft = {36, 28, 62, 255};    // tinted cards and the tutor button
inline const Color success   = {52, 211, 153, 255};
inline const Color danger    = {248, 113, 113, 255};
inline const Color warning   = {245, 196, 94, 255};
inline const Color error_bg  = {84, 30, 42, 255};
inline const Color line_hi   = {26, 27, 38, 255};
// syntax
inline const Color kw        = {192, 132, 252, 255};
inline const Color type      = {125, 211, 252, 255};
inline const Color str       = {134, 239, 172, 255};
inline const Color num       = {251, 146, 60, 255};
inline const Color comment   = {102, 106, 134, 255};
inline const Color preproc   = {232, 121, 249, 255};
inline const Color func      = {96, 165, 250, 255};
}

// ---------------------------------------------------------------- fonts
struct Fonts {
    Font ui;        // body text
    Font ui_bold;   // headings, buttons
    Font title;     // large headings
    Font mono;      // code
    float ui_size = 17, title_size = 28, mono_size = 16;
};
extern Fonts g_fonts;
bool fonts_load(const std::string& assets_dir);
void fonts_unload();

// ---------------------------------------------------------------- text helpers
float text_scale(const Font& f);   // 1.15 for UI fonts, 1 for code
float text_width(const Font& f, float size, const std::string& s);
void  draw_text(const Font& f, float size, const std::string& s, float x, float y, Color c);
std::string fit_text(const Font& f, float size, const std::string& s, float width);
// Word wrap. Keeps explicit newlines. Lines are UTF-8.
std::vector<std::string> wrap_text(const Font& f, float size, const std::string& s, float width);
// Draws wrapped text, returns the height used.
float draw_wrapped(const Font& f, float size, const std::string& s, Rectangle area, Color c, float line_gap = 4);

// ---------------------------------------------------------------- widgets
// Call once at the start of each frame.
void ui_begin_frame();
void ui_end_frame();     // applies the mouse cursor shape
void ui_cursor_hand();
void ui_cursor_ibeam();

// Freeze the screen behind a modal at the window's native pixel resolution.
struct ModalBackdrop {
    Texture2D texture{};
    void capture();
    void draw() const;
    void release();
};

enum class ButtonStyle { Normal, Primary, Ghost, Soft };   // Soft: tinted, for "Ask the tutor"
bool button(Rectangle r, const std::string& label, bool enabled = true, ButtonStyle style = ButtonStyle::Normal);
bool slider(Rectangle r, const std::string& label, int& value, int min, int max);
int action_buttons(Rectangle r, const std::vector<std::string>& labels, int selected = -1);  // clicked index, or -1

void panel(Rectangle r, const std::string& title = "");   // title drawn in a header strip
Rectangle panel_body(Rectangle r, bool has_title);        // inner area under the header

// Vertical scroll: wheel when hovered, clamps offset, draws a thin bar.
void scroll_area(Rectangle view, float content_h, float& offset);

// Single-line text input. Returns true when Enter was pressed.
struct TextInput {
    std::string text;
    bool focused = false;
};
bool text_input(Rectangle r, TextInput& in, const std::string& placeholder);

struct TextArea {
    std::string text;
    size_t cursor = 0;  // UTF-8 byte offset
    float scroll = 0;
    bool focused = false;
};
bool text_area(Rectangle r, TextArea& in, const std::string& placeholder = "");  // true on edit

// Which widget owns the keyboard: the code editor or a text input.
enum class Focus { None, Editor, Input };
extern Focus g_focus;

bool key_pressed(int key);   // pressed or auto-repeat
