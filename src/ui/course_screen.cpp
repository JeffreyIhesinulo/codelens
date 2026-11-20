#include "screens.h"
#include "services.h"
#include <algorithm>

void draw_course_screen(App &app) {
    float w = GetScreenWidth(), h = GetScreenHeight();
    draw_text(g_fonts.title, 30, "Your learning path", 32, 25, theme::text);
    draw_text(g_fonts.ui, 17, "Choose a topic, or continue where you stopped.", 32, 65, theme::text_dim);
    if (button({w - 355, 24, 155, 36}, "Tutor setup", true, ButtonStyle::Ghost)) {
        tutor_refresh();
        app.screen = Screen::AiSetup;
    }
    if (button({w - 190, 24, 158, 36}, "Ask the tutor", true, ButtonStyle::Ghost)) {
        Lesson l;
        l.title = "Learning path";
        app.tutor.open(l, "",
                       "Choose a lesson. Explore means try a picture; Understand means follow examples; "
                       "Practice means write code.");
    }
    int next = -1;
    for (size_t i = 0; i < app.lessons.size(); ++i)
        if (!app.progress.completed.count(app.lessons[i].id)) {
            next = (int)i;
            break;
        }
    int resume = app.resume_lesson >= 0 && app.resume_lesson < (int)app.lessons.size() ? app.resume_lesson
                                                                                       : std::max(0, next);
    if (resume < (int)app.lessons.size() && app.progress.completed.count(app.lessons[resume].id) && next >= 0)
        resume = next;
    Rectangle view{32, 110, w - 64, h - 198};
    scroll_area(view, app.lessons.size() * 122.0f, app.course_scroll);
    BeginScissorMode(view.x, view.y, view.width, view.height);
    float cardw = std::min(900.0f, view.width - 60), x = view.x + (view.width - cardw) / 2;
    for (size_t i = 0; i < app.lessons.size(); ++i) {
        const auto &l = app.lessons[i];
        float y = view.y + i * 122 - app.course_scroll;
        Rectangle card{x, y, cardw, 105};
        bool done = app.progress.completed.count(l.id);
        bool hover = CheckCollisionPointRec(GetMousePosition(), card) &&
                     CheckCollisionPointRec(GetMousePosition(), view);
        if (i + 1 < app.lessons.size())
            DrawLineEx({x + 23, y + 50}, {x + 23, y + 130}, 3, theme::border);
        DrawRectangleRounded(card, .08f, 6, hover ? theme::panel_hi : theme::panel);
        DrawRectangleRoundedLinesEx(card, .08f, 6, 1, (int)i == next ? theme::accent : theme::border);
        DrawCircle(x + 25, y + 31, 15, done ? theme::success : theme::accent_dk);
        draw_text(g_fonts.ui_bold, 17, std::to_string(i + 1), x + 20, y + 22, theme::text);
        draw_text(g_fonts.ui_bold, 20, l.title, x + 54, y + 16, theme::text);
        draw_text(g_fonts.ui, 14,
                  done             ? "Completed"
                  : (int)i == next ? "Recommended next topic"
                                   : "Open to review or explore",
                  x + 54, y + 44, done ? theme::success : theme::text_dim);
        const char *stages[] = {"Explore: try", "Understand: follow", "Practice: write"};
        for (int stage = 0; stage < 3; ++stage) {
            int total = 0, passed = 0;
            for (size_t j = 0; j < l.steps.size(); ++j) {
                int kind = l.steps[j].kind == StepKind::Explore    ? 0
                           : l.steps[j].kind == StepKind::Practice ? 2
                                                                   : 1;
                if (kind == stage) {
                    ++total;
                    if (done || app.step_progress[l.id].count(j))
                        ++passed;
                }
            }
            float sx = x + 54 + stage * 205;
            DrawCircle(sx + 4, y + 81, 4, total && passed == total ? theme::success : theme::border);
            draw_text(g_fonts.ui, 13, stages[stage], sx + 15, y + 74, total ? theme::text : theme::text_dim);
        }
        if (hover) {
            ui_cursor_hand();
            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                app.steps.open(app, i);
            }
        }
    }
    EndScissorMode();
    bool ready = tutor_status().installed && ai_available();
    draw_text(g_fonts.ui, 15, ready ? "Local tutor ready" : "Tutor offline — lessons still work", 32, h - 47,
              ready ? theme::success : theme::text_dim);
    if (!app.lessons.empty() && button({w - 260, h - 66, 228, 42}, "Continue →", true, ButtonStyle::Primary))
        app.steps.open(app, resume);
    if (app.lessons.empty())
        draw_text(g_fonts.ui, 18, "No lessons were found. Check the lessons folder.", 40, 140, theme::danger);
}
