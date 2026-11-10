#pragma once
// Shared UI toolkit: theme, fonts, immediate-mode widgets.
#include "raylib.h"
#include <string>
#include <vector>

// ---------------------------------------------------------------- theme
namespace theme {
inline const Color bg        = {24, 26, 31, 255};
inline const Color panel     = {32, 35, 42, 255};
inline const Color panel_hi  = {40, 44, 53, 255};
inline const Color border    = {52, 57, 68, 255};
inline const Color text      = {220, 223, 228, 255};
inline const Color text_dim  = {140, 147, 160, 255};
inline const Color accent    = {97, 175, 239, 255};
inline const Color accent_dk = {48, 88, 128, 255};
inline const Color success   = {120, 200, 120, 255};
inline const Color danger    = {235, 110, 110, 255};
inline const Color warning   = {229, 192, 123, 255};
inline const Color error_bg  = {90, 40, 44, 255};
inline const Color line_hi   = {38, 42, 50, 255};
// syntax
inline const Color kw        = {198, 120, 221, 255};
inline const Color type      = {229, 192, 123, 255};
inline const Color str       = {152, 195, 121, 255};
inline const Color num       = {209, 154, 102, 255};
inline const Color comment   = {110, 118, 130, 255};
inline const Color preproc   = {224, 108, 117, 255};
inline const Color func      = {97, 175, 239, 255};
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
float text_width(const Font& f, float size, const std::string& s);
void  draw_text(const Font& f, float size, const std::string& s, float x, float y, Color c);
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

enum class ButtonStyle { Normal, Primary, Ghost };
bool button(Rectangle r, const std::string& label, bool enabled = true, ButtonStyle style = ButtonStyle::Normal);

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

// Which widget owns the keyboard: the code editor or a text input.
enum class Focus { None, Editor, Input };
extern Focus g_focus;

bool key_pressed(int key);   // pressed or auto-repeat
