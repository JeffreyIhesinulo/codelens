#include "screens.h"

#include <fstream>

void draw_welcome_screen(App &app) {
    const float width = 700;
    const float x = (GetScreenWidth() - width) / 2;
    const float y = (GetScreenHeight() - 380) / 2.0f;
    if (button({GetScreenWidth() - 192.0f, 20, 164, 36}, "Ask the tutor", true, ButtonStyle::Soft)) {
        Lesson context;
        context.title = "Welcome to CodeLens";
        context.theory = "Explore means trying an interactive example. Understand means following "
                         "code in small steps. Practice means solving a short task.";
        app.tutor.open(context, "", context.theory);
    }
    draw_logo(x + 30, y - 70, 30);
    if (app.guides)
        mascot_say(x + 76, y - 70, 26, "Hi, I'm Bit! I'll help you find your way.", 1, 380);
    draw_text(g_fonts.title, 32, "Welcome to CodeLens", x, y, theme::text);
    draw_text(g_fonts.ui, 20, "Learn how programs work, one small step at a time.", x, y + 52, theme::text);
    draw_text(g_fonts.ui, g_fonts.ui_size,
              "Choose Start lesson 1 to begin. No programming experience is needed.", x, y + 86,
              theme::text_dim);

    const std::vector<std::string> stages = {
        "Explore — try an interactive example and see what changes.",
        "Understand — follow a program in small steps and think about the result.",
        "Practice — write a little code and try it out."};
    float line_y = y + 148;
    for (const auto &stage : stages) {
        draw_wrapped(g_fonts.ui, 19, stage, {x, line_y, width, 50}, theme::text);
        line_y += 58;
    }
    if (app.lessons.empty())
        draw_text(g_fonts.ui, g_fonts.ui_size, "No lessons were found. Check the lessons folder.", x, line_y,
                  theme::danger);
    if (!app.welcome_error.empty())
        draw_text(g_fonts.ui, 15, app.welcome_error, x, line_y, theme::danger);
    if (button({x + width - 196, y + 350, 196, 42}, "Start lesson 1", !app.lessons.empty(),
               ButtonStyle::Primary)) {
        std::ofstream flag(app.welcome_path());
        flag << "seen\n";
        flag.close();
        if (!flag) {
            app.welcome_error = "Could not save your welcome setting. Check the project folder permissions.";
            return;
        }
        app.steps.open(app, 0);
    }
}
