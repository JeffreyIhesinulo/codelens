// Course map: all lessons as cards, progress, AI status.
#include "screens.h"

#include <algorithm>
#include <cmath>

static void ai_status(float right, float y) {
    bool on = ai_available();
    std::string label = on ? "AI online · " + ai_model() : "AI offline · run: ollama serve";
    float w = text_width(g_fonts.ui, g_fonts.ui_size, label);
    DrawCircle((int)(right - w - 14), (int)(y + g_fonts.ui_size / 2 + 1), 5, on ? theme::success : theme::warning);
    draw_text(g_fonts.ui, g_fonts.ui_size, label, right - w, y, theme::text_dim);
}

void draw_course_screen(App& app) {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    float pad = 40;

    draw_text(g_fonts.title, g_fonts.title_size, "CodeLens", pad, 30, theme::text);
    draw_text(g_fonts.ui, g_fonts.ui_size, "Learn C and C++ by writing real code — with an AI tutor at your side.",
              pad, 30 + g_fonts.title_size + 8, theme::text_dim);
    ai_status(W - pad, 38);

    // progress bar
    int done = 0;
    for (const Lesson& l : app.lessons) done += app.progress.completed.count(l.id);
    int total = std::max(1, (int)app.lessons.size());
    float bar_y = 112;
    std::string ptxt = std::to_string(done) + " of " + std::to_string(app.lessons.size()) + " lessons completed";
    draw_text(g_fonts.ui, g_fonts.ui_size, ptxt, pad, bar_y, theme::text);
    Rectangle bar = {pad, bar_y + 28, W - 2 * pad, 8};
    DrawRectangleRounded(bar, 1, 6, theme::panel_hi);
    if (done) DrawRectangleRounded({bar.x, bar.y, bar.width * done / total, bar.height}, 1, 6, theme::success);

    // lesson cards
    Rectangle view = {pad, 170, W - 2 * pad, H - 190};
    int cols = std::max(1, (int)(view.width / 300));
    float gap = 16;
    float card_w = (view.width - gap * (cols - 1)) / cols, card_h = 112;
    int rows = ((int)app.lessons.size() + cols - 1) / cols;
    float content_h = rows * (card_h + gap);

    BeginScissorMode((int)view.x - 3, (int)view.y - 3, (int)view.width + 10, (int)view.height + 3);
    int next = -1;   // first not-completed lesson
    for (size_t i = 0; i < app.lessons.size(); ++i)
        if (!app.progress.completed.count(app.lessons[i].id)) { next = (int)i; break; }

    for (size_t i = 0; i < app.lessons.size(); ++i) {
        const Lesson& l = app.lessons[i];
        Rectangle c = {view.x + (i % cols) * (card_w + gap),
                       view.y + (i / cols) * (card_h + gap) - app.course_scroll, card_w, card_h};
        bool hover = CheckCollisionPointRec(GetMousePosition(), c) && CheckCollisionPointRec(GetMousePosition(), view);
        bool completed = app.progress.completed.count(l.id);

        DrawRectangleRounded(c, 0.08f, 8, hover ? theme::panel_hi : theme::panel);
        DrawRectangleRoundedLinesEx(c, 0.08f, 8, 1, (int)i == next ? theme::accent : theme::border);

        std::string num = std::to_string(i + 1);
        draw_text(g_fonts.title, g_fonts.title_size, num, c.x + 18, c.y + 16, completed ? theme::success : theme::accent);
        draw_text(g_fonts.ui_bold, g_fonts.ui_size, l.title, c.x + 62, c.y + 18, theme::text);
        draw_text(g_fonts.ui, g_fonts.ui_size - 2, l.topic, c.x + 62, c.y + 44, theme::text_dim);

        std::string tag = l.lang == Lang::C ? "C" : "C++";
        Rectangle t = {c.x + 62, c.y + c.height - 34, text_width(g_fonts.ui_bold, 14, tag) + 16, 22};
        DrawRectangleRounded(t, 0.4f, 6, theme::accent_dk);
        draw_text(g_fonts.ui_bold, 14, tag, t.x + 8, t.y + 4, theme::text);

        std::string state = completed ? "✓ Completed" : ((int)i == next ? "Up next" : "");
        if (!state.empty()) {
            float sw = text_width(g_fonts.ui, g_fonts.ui_size - 2, state);
            draw_text(g_fonts.ui, g_fonts.ui_size - 2, state, c.x + c.width - sw - 16, c.y + c.height - 32,
                      completed ? theme::success : theme::accent);
        }

        if (hover) {
            ui_cursor_hand();
            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                app.lesson.open(app, (int)i);
                app.screen = Screen::Lesson;
            }
        }
    }
    EndScissorMode();
    scroll_area(view, content_h, app.course_scroll);

    if (app.lessons.empty())
        draw_text(g_fonts.ui, g_fonts.ui_size, "No lessons found in " + app.root + "/lessons", pad, 190, theme::danger);
}
