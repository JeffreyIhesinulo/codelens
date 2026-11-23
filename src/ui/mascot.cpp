// Bit the raccoon, liquid-glass bubbles, XP and the daily streak.
#include "mascot.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <fstream>

namespace {

const Color kFur = {139, 92, 246, 255};
const Color kFurDark = {98, 66, 190, 255};
const Color kMask = {44, 28, 88, 255};
const Color kWhite = {246, 244, 255, 255};
const Color kEar = {216, 190, 254, 255};
const Color kNose = {28, 18, 48, 255};

Color fade(Color c, float a) {
    c.a = (unsigned char)std::clamp(c.a * a, 0.0f, 255.0f);
    return c;
}

// raylib wants triangles counter-clockwise; accept any order
void tri(Vector2 a, Vector2 b, Vector2 c, Color col) {
    float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (cross > 0) DrawTriangle(a, c, b, col);
    else DrawTriangle(a, b, c, col);
}

void ellipse(float x, float y, float rx, float ry, Color c) { DrawEllipse((int)std::round(x), (int)std::round(y), rx, ry, c); }

void star(Vector2 c, float r, Color col) {
    Vector2 p[10];
    for (int i = 0; i < 10; ++i) {
        float a = -PI / 2 + i * PI / 5, rr = i % 2 ? r * 0.45f : r;
        p[i] = {c.x + std::cos(a) * rr, c.y + std::sin(a) * rr};
    }
    for (int i = 0; i < 10; ++i) tri(c, p[i], p[(i + 1) % 10], col);
}

void flame(Vector2 c, float r, Color outer, Color inner) {
    DrawCircleV({c.x, c.y + r * 0.25f}, r * 0.62f, outer);
    tri({c.x - r * 0.6f, c.y + r * 0.15f}, {c.x + r * 0.6f, c.y + r * 0.15f}, {c.x + r * 0.1f, c.y - r}, outer);
    DrawCircleV({c.x, c.y + r * 0.42f}, r * 0.32f, inner);
    tri({c.x - r * 0.3f, c.y + r * 0.4f}, {c.x + r * 0.3f, c.y + r * 0.4f}, {c.x + r * 0.05f, c.y - r * 0.2f}, inner);
}

long today() {
    std::time_t t = std::time(nullptr);
    std::tm local{};
    localtime_r(&t, &local);
    return (long)((t + local.tm_gmtoff) / 86400);
}

} // namespace

// ---------------------------------------------------------------- Bit the raccoon

void draw_raccoon(float cx, float cy, float r, int mood, float alpha) {
    auto A = [&](Color c) { return fade(c, alpha); };
    // ears
    for (int s : {-1, 1}) {
        tri({cx + s * 0.98f * r, cy - 0.3f * r}, {cx + s * 0.62f * r, cy - 1.12f * r}, {cx + s * 0.2f * r, cy - 0.74f * r}, A(kFurDark));
        tri({cx + s * 0.8f * r, cy - 0.42f * r}, {cx + s * 0.6f * r, cy - 0.9f * r}, {cx + s * 0.36f * r, cy - 0.7f * r}, A(kEar));
    }
    // head and white cheek fluff
    ellipse(cx, cy, r, r * 0.88f, A(kFur));
    for (int s : {-1, 1})
        tri({cx + s * 1.08f * r, cy + 0.22f * r}, {cx + s * 0.55f * r, cy + 0.02f * r}, {cx + s * 0.62f * r, cy + 0.66f * r}, A(kWhite));
    // white muzzle and brows
    ellipse(cx, cy + 0.38f * r, 0.6f * r, 0.46f * r, A(kWhite));
    for (int s : {-1, 1}) ellipse(cx + s * 0.36f * r, cy - 0.4f * r, 0.27f * r, 0.11f * r, A(kWhite));
    // the raccoon mask
    ellipse(cx, cy - 0.06f * r, 0.5f * r, 0.13f * r, A(kMask));
    for (int s : {-1, 1}) ellipse(cx + s * 0.38f * r, cy - 0.05f * r, 0.34f * r, 0.23f * r, A(kMask));
    // eyes (blink every few seconds)
    bool blink = std::fmod(GetTime(), 4.2) < 0.13;
    for (int s : {-1, 1}) {
        float ex = cx + s * 0.38f * r, ey = cy - 0.06f * r;
        if (mood == 1 || blink) {   // happy / closed: a small arch
            float t = std::max(1.5f, 0.06f * r);
            DrawLineEx({ex - 0.12f * r, ey + 0.03f * r}, {ex, ey - 0.07f * r}, t, A(kWhite));
            DrawLineEx({ex, ey - 0.07f * r}, {ex + 0.12f * r, ey + 0.03f * r}, t, A(kWhite));
            continue;
        }
        DrawCircleV({ex, ey}, 0.15f * r, A(kWhite));
        float look = mood == 2 ? -0.05f * r : 0.01f * r;
        DrawCircleV({ex + s * 0.01f * r, ey + look}, 0.095f * r, A(kNose));
        DrawCircleV({ex + 0.03f * r, ey + look - 0.035f * r}, std::max(1.0f, 0.035f * r), A(kWhite));
    }
    // nose and smile
    ellipse(cx, cy + 0.2f * r, 0.12f * r, 0.08f * r, A(kNose));
    float t = std::max(1.2f, 0.04f * r);
    DrawLineEx({cx, cy + 0.27f * r}, {cx, cy + 0.36f * r}, t, A(kNose));
    DrawLineEx({cx - 0.14f * r, cy + 0.4f * r}, {cx, cy + 0.36f * r}, t, A(kNose));
    DrawLineEx({cx, cy + 0.36f * r}, {cx + 0.14f * r, cy + 0.4f * r}, t, A(kNose));
}

// ---------------------------------------------------------------- liquid glass

void draw_glass(Rectangle r, float roundness, float alpha) {
    auto A = [&](Color c) { return fade(c, alpha); };
    for (int i = 3; i >= 1; --i)   // soft shadow
        DrawRectangleRounded({r.x - i, r.y + 2 + i * 2.0f, r.width + 2 * i, r.height + i}, roundness, 10,
                             A({0, 0, 0, (unsigned char)(26 / i)}));
    DrawRectangleRounded(r, roundness, 10, A({30, 24, 52, 232}));        // frosted body
    DrawRectangleRounded(r, roundness, 10, A({167, 139, 250, 34}));      // violet tint
    // sheen: a lighter band across the top half
    Rectangle top{r.x + 3, r.y + 3, r.width - 6, r.height * 0.48f};
    DrawRectangleRounded(top, roundness, 10, A({255, 255, 255, 16}));
    // bright rim and a short specular streak, like light caught on the glass edge
    DrawRectangleRoundedLinesEx(r, roundness, 10, 1.4f, A({255, 255, 255, 70}));
    DrawRectangleRoundedLinesEx({r.x + 1.5f, r.y + 1.5f, r.width - 3, r.height - 3}, roundness, 10, 1, A({167, 139, 250, 60}));
    float sx = r.x + std::min(28.0f, r.width * 0.15f);
    DrawLineEx({sx, r.y + 2}, {sx + std::min(90.0f, r.width * 0.3f), r.y + 2}, 1.6f, A({255, 255, 255, 150}));
}

Rectangle mascot_say(float x, float y, float r, const std::string &text, int mood, float max_w) {
    float pad = 14, size = 16;
    auto lines = wrap_text(g_fonts.ui, size, text, max_w - 2 * pad);
    float w = 0;
    for (const auto &l : lines) w = std::max(w, text_width(g_fonts.ui, size, l));
    float line_h = std::round(size * text_scale(g_fonts.ui) * 1.35f);
    float h = lines.size() * line_h + 2 * pad - 4;
    float bob = std::sin((float)GetTime() * 2.2f) * 2;   // the raccoon breathes a little
    draw_raccoon(x + r, y + bob, r, mood);
    Rectangle b{x + 2 * r + 16, y - h / 2, w + 2 * pad, h};
    draw_glass(b, std::min(0.6f, 28.0f / h));
    tri({b.x + 1, b.y + h / 2 - 8}, {b.x + 1, b.y + h / 2 + 8}, {b.x - 9, b.y + h / 2 + 2}, {72, 58, 120, 230});
    float ty = b.y + (h - lines.size() * line_h) / 2 + (line_h - size) / 2 - 1;
    for (const auto &l : lines) {
        draw_text(g_fonts.ui, size, l, b.x + pad, ty, theme::text);
        ty += line_h;
    }
    return b;
}

// ---------------------------------------------------------------- XP and streak

void Rewards::load(const std::string &file) {
    path = file;
    std::ifstream in(file);
    std::string key;
    while (in >> key) {
        if (key == "xp") in >> xp;
        else if (key == "streak") in >> streak;
        else if (key == "best") in >> best;
        else if (key == "last_day") in >> last_day;
    }
}
void Rewards::save() const {
    if (path.empty()) return;
    std::ofstream out(path);
    out << "xp " << xp << "\nstreak " << streak << "\nbest " << best << "\nlast_day " << last_day << "\n";
}
int Rewards::step_done() {
    xp += 10;
    save();
    return 10;
}
int Rewards::lesson_done() {
    long d = today();
    if (last_day == d) {
        // already counted today
    } else if (last_day == d - 1)
        ++streak;
    else
        streak = 1;
    last_day = d;
    best = std::max(best, streak);
    xp += 50;
    save();
    return 50;
}
bool Rewards::streak_alive() const { return last_day >= today() - 1; }

void draw_streak_badges(const Rewards &r, float right, float y) {
    int days = r.streak_alive() ? r.streak : 0;
    std::string s1 = std::to_string(days) + (days == 1 ? " day streak" : " day streak");
    std::string s2 = std::to_string(r.xp) + " XP";
    float w1 = text_width(g_fonts.ui_bold, 15, s1) + 46, w2 = text_width(g_fonts.ui_bold, 15, s2) + 46;
    Rectangle b2{right - w2, y, w2, 36}, b1{b2.x - 10 - w1, y, w1, 36};
    draw_glass(b1, 1, 1);
    draw_glass(b2, 1, 1);
    flame({b1.x + 20, b1.y + 17}, 10, days ? Color{251, 146, 60, 255} : theme::text_dim,
          days ? Color{253, 224, 71, 255} : theme::border);
    draw_text(g_fonts.ui_bold, 15, s1, b1.x + 36, b1.y + 9, days ? theme::text : theme::text_dim);
    star({b2.x + 20, b2.y + 18}, 9, {250, 204, 21, 255});
    draw_text(g_fonts.ui_bold, 15, s2, b2.x + 36, b2.y + 9, theme::text);
}
