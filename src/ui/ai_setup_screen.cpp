#include "screens.h"
#include "services.h"
#include <algorithm>

void draw_ai_setup_screen(App &app) {
    static double checked = -10;
    if (GetTime() - checked > 4) {
        tutor_refresh();
        checked = GetTime();
    }
    auto status = tutor_status();
    float width = std::min(780.0f, GetScreenWidth() - 60.0f);
    Rectangle r = {(GetScreenWidth() - width) / 2, 65, width, GetScreenHeight() - 150.0f};
    draw_text(g_fonts.title, 28, "Set up your local tutor", r.x, r.y, theme::text);
    draw_wrapped(g_fonts.ui, 18,
                 "Follow these steps to enable help. You can also continue learning without the tutor.",
                 {r.x, r.y + 45, r.width, 60}, theme::text);
    float y = r.y + 112;
    panel({r.x, y, r.width, 170});
    draw_text(g_fonts.ui_bold, 20, "1. Start Ollama", r.x + 18, y + 16, theme::text);
    draw_wrapped(
        g_fonts.ui, 17,
        "Ollama runs the tutor on your computer. Install it from ollama.com, open it, then return here.",
        {r.x + 18, y + 52, r.width - 36, 70}, theme::text_dim);
    if (button({r.x + 18, y + 116, 168, 34}, "Open ollama.com"))
        OpenURL("https://ollama.com/download");
    if (button({r.x + 198, y + 116, 110, 34}, "Check again"))
        tutor_refresh();
    draw_text(g_fonts.ui, 16, status.server ? "Ollama is running" : "Ollama is not running", r.x + 330,
              y + 126, status.server ? theme::success : theme::warning);
    y += 188;
    panel({r.x, y, r.width, 175});
    draw_text(g_fonts.ui_bold, 20, "2. Download the tutor", r.x + 18, y + 16, theme::text);
    draw_text(g_fonts.ui, 16, "Model: " + ai_model() + ". Internet is needed only for the download.",
              r.x + 18, y + 52, theme::text_dim);
    if (button({r.x + 18, y + 88, 190, 36}, status.installed ? "Model installed" : "Download model",
               status.server && !status.installed && !status.downloading))
        tutor_download();
    if (status.downloading) {
        Rectangle bar{r.x + 225, y + 96, r.width - 245, 16};
        DrawRectangleRec(bar, theme::border);
        DrawRectangleRec({bar.x, bar.y, bar.width * std::clamp(status.progress, 0.0f, 1.0f), bar.height},
                         theme::accent);
    }
    draw_text(g_fonts.ui, 15, fit_text(g_fonts.ui, 15, status.message, r.width - 36), r.x + 18, y + 139,
              theme::text_dim);
    if (button({r.x + r.width - 240, GetScreenHeight() - 64.0f, 240, 40},
               status.installed ? "Continue learning" : "Continue without AI", true, ButtonStyle::Primary)) {
        app.screen = Screen::Course;
        app.ai_setup_checked = true;
    }
    if (button({24, 18, 160, 34}, "Ask the tutor", true, ButtonStyle::Ghost)) {
        Lesson l;
        l.title = "Tutor setup";
        app.tutor.open(l, "", "The student is setting up Ollama.");
    }
}
