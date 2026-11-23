#include "screens.h"
#include "services.h"
#include <algorithm>
#include <cmath>
#include <fstream>

void draw_course_screen(App &app) {
    float w = GetScreenWidth(), h = GetScreenHeight();
    draw_logo(52, 50, 20);
    draw_text(g_fonts.title, 30, "Your learning path", 88, 25, theme::text);
    draw_text(g_fonts.ui, 17, "Choose a topic, or continue where you stopped.", 88, 65, theme::text_dim);
    if (app.guides)
        draw_streak_badges(app.rewards, w - 372, 24);
    if (button({w - 355, 24, 155, 36}, "Tutor setup", true, ButtonStyle::Ghost)) {
        tutor_refresh();
        app.screen = Screen::AiSetup;
    }
    if (button({w - 190, 24, 158, 36}, "Ask the tutor", true, ButtonStyle::Soft)) {
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
    Rectangle view{32, 104, w - 64, h - 192};
    // Two columns side by side: C on the left, C++ on the right (one column if only one language).
    bool has_cpp = std::any_of(app.lessons.begin(), app.lessons.end(), [](const Lesson &l) { return l.lang == Lang::CPP; });
    bool has_c = std::any_of(app.lessons.begin(), app.lessons.end(), [](const Lesson &l) { return l.lang != Lang::CPP; });
    bool two = has_cpp && has_c;
    float gap = 36;
    float cardw = two ? std::min(700.0f, (view.width - 40 - gap) / 2) : std::min(900.0f, view.width - 60);
    float left_x = view.x + (view.width - (two ? 2 * cardw + gap : cardw)) / 2;
    std::vector<float> top(app.lessons.size()), colx(app.lessons.size());
    std::vector<int> number(app.lessons.size());
    float column_y[2] = {64, 64};   // under each column's header
    int count[2] = {0, 0};
    for (size_t i = 0; i < app.lessons.size(); ++i) {
        int c = two && app.lessons[i].lang == Lang::CPP ? 1 : 0;
        colx[i] = left_x + c * (cardw + gap);
        top[i] = column_y[c];
        number[i] = ++count[c];
        column_y[c] += 122;
    }
    float content = std::max(column_y[0], column_y[1]);
    scroll_area(view, content, app.course_scroll);
    BeginScissorMode(view.x, view.y, view.width, view.height);
    for (size_t i = 0; i < app.lessons.size(); ++i) {
        const auto &l = app.lessons[i];
        float y = view.y + top[i] - app.course_scroll, x = colx[i];
        if (number[i] == 1) {
            bool cpp = l.lang == Lang::CPP;
            draw_text(g_fonts.title, 24, cpp ? "C++" : "C", x, y - 50, theme::accent);
            draw_text(g_fonts.ui, 15,
                      fit_text(g_fonts.ui, 15,
                               cpp ? "cout, strings and vectors. Builds on C." : "Start here: how a program runs.",
                               cardw - 70),
                      x + (cpp ? 64 : 34), y - 42, theme::text_dim);
        }
        Rectangle card{x, y, cardw, 105};
        bool done = app.progress.completed.count(l.id);
        bool hover = CheckCollisionPointRec(GetMousePosition(), card) &&
                     CheckCollisionPointRec(GetMousePosition(), view);
        if (i + 1 < app.lessons.size() && app.lessons[i + 1].lang == l.lang && colx[i + 1] == x)
            DrawLineEx({x + 23, y + 50}, {x + 23, y + 130}, 3, theme::border);
        DrawRectangleRounded(card, .08f, 6, hover ? theme::panel_hi : theme::panel);
        DrawRectangleRoundedLinesEx(card, .08f, 6, 1, (int)i == next ? theme::accent : theme::border);
        DrawCircle(x + 25, y + 31, 15, done ? theme::success : theme::accent_dk);
        std::string num = std::to_string(number[i]);
        draw_text(g_fonts.ui_bold, 17, num, x + 25 - text_width(g_fonts.ui_bold, 17, num) / 2, y + 22, theme::text);
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
            float sx = x + 54 + stage * std::min(205.0f, (cardw - 70) / 3);
            DrawCircle(sx + 4, y + 81, 4, total && passed == total ? theme::success : theme::border);
            draw_text(g_fonts.ui, 14, stages[stage], sx + 15, y + 73, total ? theme::text : theme::text_dim);
        }
        if (hover && app.tour_step < 0 && app.celebration.empty()) {
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
    if (!app.lessons.empty() && button({w - 260, h - 66, 228, 42}, "Continue →", app.tour_step < 0 && app.celebration.empty(), ButtonStyle::Primary))
        app.steps.open(app, resume);
    if (app.lessons.empty())
        draw_text(g_fonts.ui, 18, "No lessons were found. Check the lessons folder.", 40, 140, theme::danger);

    // First-run tour: Bit the raccoon shows where things are.
    if (app.guides && app.tour_step >= 0 && !app.tutor.visible) {
        struct Stop { const char *text; Rectangle target; };
        float cw = two ? cardw : cardw;
        Stop stops[] = {
            {"Hi, I'm Bit! I'm your coding buddy. Let me show you around. It takes 20 seconds.", {0, 0, 0, 0}},
            {"This is your learning path. C is on the left, C++ on the right. Start with lesson 1 and go down.",
             {left_x - 8, view.y + 10, two ? 2 * cardw + gap + 16 : cw + 16, std::min(view.height - 10, 300.0f)}},
            {"Each topic has three parts: Explore, Understand and Practice. The dots turn green as you finish them.",
             {left_x + 40, view.y + top[0] + 62, cw - 60, 32}},
            {"Stuck anywhere? Press Ask the tutor. It runs on your computer, so it is private and works offline.",
             {w - 196, 18, 170, 48}},
            {"Finish lessons to earn XP and grow your streak. Come back every day to keep it going!",
             {w - 372 - 330, 18, 340, 48}},
            {"Press Continue whenever you are ready. Every lesson starts with a short reading. Have fun!",
             {w - 266, h - 72, 240, 54}},
        };
        const int n = sizeof(stops) / sizeof(stops[0]);
        DrawRectangle(0, 0, (int)w, (int)h, {0, 0, 0, 120});
        const Stop &st = stops[std::min(app.tour_step, n - 1)];
        if (st.target.width > 0) {
            float pulse = 2 + std::sin((float)GetTime() * 4) * 1.5f;
            DrawRectangleRoundedLinesEx(st.target, 0.15f, 8, pulse, theme::accent);
        }
        // one glass card: Bit on the left, the sentence, the buttons
        Rectangle card{24, h - 250, std::min(760.0f, w - 48), 170};
        draw_glass(card, 0.18f);
        float bob = std::sin((float)GetTime() * 2.2f) * 2;
        draw_raccoon(card.x + 74, card.y + 78 + bob, 46, app.tour_step == 0 || app.tour_step + 1 == n ? 1 : 0);
        Rectangle text{card.x + 150, card.y + 22, card.width - 175, 80};
        draw_text(g_fonts.ui_bold, 14, "Bit · " + std::to_string(app.tour_step + 1) + " of " + std::to_string(n),
                  text.x, text.y, theme::accent);
        draw_wrapped(g_fonts.ui, 18, st.text, {text.x, text.y + 26, text.width, 70}, theme::text, 6);
        bool last = app.tour_step + 1 >= n;
        if (button({text.x, card.y + card.height - 52, 130, 38}, last ? "Let's go!" : "Next", true,
                   ButtonStyle::Primary) ||
            IsKeyPressed(KEY_ENTER))
            ++app.tour_step;
        if (!last && button({text.x + 142, card.y + card.height - 52, 120, 38}, "Skip tour", true))
            app.tour_step = n;
        if (app.tour_step >= n) {
            app.tour_step = -1;
            std::ofstream(app.root + "/tour.txt") << "done\n";
        }
    }
}
