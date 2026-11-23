// CodeLens logo and the short loading screen shown when the app starts.
#include "screens.h"

#include <algorithm>
#include <cmath>

// A magnifying lens over a code sign: "look inside your code".
void draw_logo(float cx, float cy, float r, float alpha) {
    auto a = [&](Color c) { c.a = (unsigned char)(c.a * alpha); return c; };   // keeps the colour's own alpha
    // soft glow
    for (int i = 4; i >= 1; --i)
        DrawCircleV({cx, cy}, r + i * r * 0.12f, a(ColorAlpha(theme::accent, 0.05f)));
    // handle at 45 degrees, bottom right
    float hx = cx + r * 0.78f, hy = cy + r * 0.78f;
    DrawLineEx({hx, hy}, {hx + r * 0.62f, hy + r * 0.62f}, r * 0.26f, a(theme::accent_dk));
    DrawLineEx({hx, hy}, {hx + r * 0.62f, hy + r * 0.62f}, r * 0.14f, a(theme::accent));
    // glass
    DrawCircleV({cx, cy}, r, a(theme::accent));
    DrawCircleV({cx, cy}, r * 0.86f, a(theme::accent_soft));
    DrawCircleV({cx - r * 0.3f, cy - r * 0.3f}, r * 0.16f, a(ColorAlpha(WHITE, 0.10f)));   // shine
    // "</>" inside the glass
    float s = r * 0.62f;
    std::string sign = "</>";
    float w = MeasureTextEx(g_fonts.mono, sign.c_str(), s, 0).x;
    DrawTextEx(g_fonts.mono, sign.c_str(), {cx - w / 2, cy - s * 0.55f}, s, 0, a(theme::str));
}

// The word mark: "Code" in the text colour, "Lens" in the accent colour, centred on cx.
void draw_wordmark(float cx, float y, float size, float alpha) {
    float w1 = text_width(g_fonts.title, size, "Code"), w2 = text_width(g_fonts.title, size, "Lens");
    float x = cx - (w1 + w2) / 2;
    draw_text(g_fonts.title, size, "Code", x, y, ColorAlpha(theme::text, alpha));
    draw_text(g_fonts.title, size, "Lens", x + w1, y, ColorAlpha(theme::accent, alpha));
}

// One frame of the splash; t = seconds since start. Returns true when it is finished.
bool draw_splash_screen(float t) {
    const float duration = 2.2f;
    float w = GetScreenWidth(), h = GetScreenHeight();
    float fade_in = std::min(1.0f, t / 0.5f);
    float fade_out = std::clamp((duration - t) / 0.35f, 0.0f, 1.0f);
    float alpha = std::min(fade_in, fade_out);
    float cy = h / 2 - 70;
    float pop = 1.0f - std::pow(1.0f - std::min(1.0f, t / 0.6f), 3.0f);   // ease-out grow
    draw_logo(w / 2, cy, 62 * (0.85f + 0.15f * pop), alpha);
    draw_wordmark(w / 2, cy + 104, 46, alpha);
    std::string tag = "Learn C and C++ by watching your code run";
    float tw = text_width(g_fonts.ui, 18, tag);
    draw_text(g_fonts.ui, 18, tag, (w - tw) / 2, cy + 160, ColorAlpha(theme::text_dim, alpha));

    // loading bar
    float bw = 280, bx = (w - bw) / 2, by = cy + 214;
    float p = std::min(1.0f, t / (duration - 0.4f));
    p = 1.0f - std::pow(1.0f - p, 2.0f);
    DrawRectangleRounded({bx, by, bw, 6}, 1, 6, ColorAlpha(theme::panel_hi, alpha));
    DrawRectangleRounded({bx, by, std::max(6.0f, bw * p), 6}, 1, 6, ColorAlpha(theme::accent, alpha));
    std::string step = p < 0.45f ? "Loading lessons…" : p < 0.9f ? "Checking the local tutor…" : "Ready";
    float sw = text_width(g_fonts.ui, 14, step);
    draw_text(g_fonts.ui, 14, step, (w - sw) / 2, by + 18, ColorAlpha(theme::text_dim, alpha));

    bool skip = t > 0.3f && (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || GetKeyPressed() != 0);
    return t >= duration || skip;
}
