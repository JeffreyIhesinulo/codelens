// CodeLens — entry point.
#include "screens.h"
#include "memview_preview.h"

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

int main(int argc, char** argv) {
    const bool memory_preview = argc > 1 && std::string(argv[1]) == "--memory-preview";
    const std::string root = find_root();
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(1366, 800, memory_preview ? "CodeLens · Memory visualizer" : "CodeLens");
    SetWindowMinSize(1100, 640);
    SetTargetFPS(60);
    if (!fonts_load(root + "/assets")) TraceLog(LOG_WARNING, "CodeLens: fonts not found in %s/assets", root.c_str());
    if (memory_preview) {
        run_memory_preview(root);
        fonts_unload();
        CloseWindow();
        return 0;
    }

    App app;
    app.root = root;
    app.lessons = lessons_load(app.root + "/lessons");
    app.progress = progress_load(app.progress_path());
    ai_init();
    SetExitKey(KEY_NULL);   // Esc is used for "back"
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
