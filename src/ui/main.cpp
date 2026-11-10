// CodeLens — entry point.
#include "screens.h"

#include <sys/stat.h>

static bool dir_exists(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// lessons/ and assets/ live in the project root. Prefer the current directory,
// fall back to the source tree the app was built from.
static std::string find_root() {
    if (dir_exists("lessons") && dir_exists("assets")) return ".";
#ifdef CODELENS_ROOT
    return CODELENS_ROOT;
#else
    return ".";
#endif
}

int main() {
    App app;
    app.root = find_root();
    app.lessons = lessons_load(app.root + "/lessons");
    app.progress = progress_load(app.progress_path());
    ai_init();

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(1366, 800, "CodeLens");
    SetWindowMinSize(1100, 640);
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);   // Esc is used for "back"

    if (!fonts_load(app.root + "/assets")) TraceLog(LOG_WARNING, "CodeLens: fonts not found in %s/assets", app.root.c_str());

    while (!WindowShouldClose()) {
        ui_begin_frame();
        BeginDrawing();
        ClearBackground(theme::bg);
        if (app.screen == Screen::Course) draw_course_screen(app);
        else app.lesson.update_and_draw(app);
        EndDrawing();
        ui_end_frame();
    }

    if (app.screen == Screen::Lesson) app.lesson.close(app);
    fonts_unload();
    CloseWindow();
    return 0;
}
