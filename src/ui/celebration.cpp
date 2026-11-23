// The "Lesson complete" card: XP earned and the streak, with Bit the raccoon.
#include "screens.h"

#include <cmath>

void draw_celebration(App &app) {
    float w = GetScreenWidth(), h = GetScreenHeight();
    DrawRectangle(0, 0, (int)w, (int)h, {0, 0, 0, 150});
    // a little confetti
    for (int i = 0; i < 40; ++i) {
        float t = (float)GetTime() * (0.6f + (i % 5) * 0.12f) + i * 1.7f;
        float x = std::fmod(i * 97.0f + std::sin(t) * 30, w), y = std::fmod(t * 90 + i * 53.0f, h);
        Color c = i % 3 == 0 ? theme::accent : i % 3 == 1 ? Color{250, 204, 21, 255} : theme::success;
        DrawRectanglePro({x, y, 8, 4}, {4, 2}, t * 120, ColorAlpha(c, 0.8f));
    }
    Rectangle card{w / 2 - 270, h / 2 - 190, 540, 380};
    draw_glass(card, 0.12f);
    draw_raccoon(w / 2, card.y + 78, 52, 1);
    auto centred = [&](const Font &f, float size, const std::string &s, float y, Color c) {
        draw_text(f, size, s, w / 2 - text_width(f, size, s) / 2, y, c);
    };
    centred(g_fonts.title, 30, "Lesson complete!", card.y + 148, theme::text);
    centred(g_fonts.ui, 18, app.celebration, card.y + 192, theme::text_dim);
    centred(g_fonts.title, 26, "+" + std::to_string(app.celebration_xp) + " XP", card.y + 230, {250, 204, 21, 255});
    int days = app.rewards.streak;
    centred(g_fonts.ui_bold, 18,
            days == 1 ? "Streak started: 1 day. Come back tomorrow!" : "Streak: " + std::to_string(days) + " days in a row!",
            card.y + 274, {251, 146, 60, 255});
    if (button({w / 2 - 110, card.y + card.height - 62, 220, 44}, "Keep going", true, ButtonStyle::Primary) ||
        IsKeyPressed(KEY_ENTER)) {
        app.celebration.clear();   // stay where we are: the step screen still shows "Finish lesson"
    }
}
