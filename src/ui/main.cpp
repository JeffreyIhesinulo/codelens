// CodeLens — entry point.
#include "screens.h"
#include "memview_preview.h"
#include "services.h"
#include "audio.h"

#include <sys/stat.h>
#include <fstream>
#include <cstdlib>

static bool dir_exists(const std::string &p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// lessons/ and assets/ live in the project root. Prefer the current directory,
// fall back to the source tree the app was built from.
static std::string find_root() {
    if (dir_exists("lessons") && dir_exists("assets"))
        return ".";
#ifdef CODELENS_ROOT
    return CODELENS_ROOT;
#else
    return ".";
#endif
}

int main(int argc, char **argv) {
    const bool memory_preview = argc > 1 && std::string(argv[1]) == "--memory-preview";
    const std::string root = find_root();
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(1366, 800, memory_preview ? "CodeLens · Memory visualizer" : "CodeLens");
    SetWindowMinSize(1100, 640);
    SetTargetFPS(60);
    if (!fonts_load(root + "/assets"))
        TraceLog(LOG_WARNING, "CodeLens: fonts not found in %s/assets", root.c_str());
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
    app.screen = std::ifstream(app.welcome_path()) ? Screen::Course : Screen::Welcome;
    app.load_steps();
    app.auto_reading = true;
    app.guides = true;
    app.rewards.load(app.root + "/rewards.txt");
    audio::init(root + "/assets", app.root + "/sound.txt");
    app.challenges.load(app.root);
    if (!std::ifstream(app.root + "/tour.txt"))
        app.tour_step = 0;
    const char *ai_host = std::getenv("CODELENS_OLLAMA_HOST");
    const char *ai_port = std::getenv("CODELENS_OLLAMA_PORT");
    ai_init(ai_host ? ai_host : "127.0.0.1", ai_port ? std::atoi(ai_port) : 11434);
    tutor_refresh();
    ModalBackdrop backdrop;
    SetExitKey(KEY_NULL); // Esc is used for "back"
    // Splash: logo and a short loading screen while the tutor check runs. CODELENS_NO_SPLASH=1 skips it.
    if (!std::getenv("CODELENS_NO_SPLASH")) {
        double start = GetTime();
        while (!WindowShouldClose()) {
            audio::music("lobbyMusic");
            audio::update();
            BeginDrawing();
            ClearBackground(theme::bg);
            bool done = draw_splash_screen((float)(GetTime() - start));
            EndDrawing();
            if (done) break;
        }
    }
    while (!WindowShouldClose()) {
        auto tutor = tutor_status();
        if (app.screen == Screen::Course && !app.ai_setup_checked && tutor.checked) {
            app.ai_setup_checked = true;
            if (!tutor.server || !tutor.installed)
                app.screen = Screen::AiSetup;
        }
        audio::update();
        if (app.screen != Screen::Challenges)
            audio::music("lobbyMusic");   // the challenges screen picks its own tracks
        bool over_panel = audio::mouse_over_panel();
        if (over_panel)
            SetMouseOffset(-100000, -100000);   // the sound panel is on top: screens below ignore the mouse
        ui_begin_frame();
        BeginDrawing();
        ClearBackground(theme::bg);
        if (!app.tutor.visible) {
            if (app.screen == Screen::Welcome)
                draw_welcome_screen(app);
            else if (app.screen == Screen::AiSetup)
                draw_ai_setup_screen(app);
            else if (app.screen == Screen::Course)
                draw_course_screen(app);
            else if (app.screen == Screen::Challenges)
                app.challenges.update_and_draw(app);
            else
                app.steps.update_and_draw(app);
            if (!app.celebration.empty())
                draw_celebration(app);
            if (app.tutor.visible)
                backdrop.capture();
        } else
            backdrop.draw();
        app.tutor.update_and_draw();
        SetMouseOffset(0, 0);
        audio::draw_panel();
        EndDrawing();
        ui_end_frame();
    }

    if (app.screen == Screen::Lesson)
        app.lesson.close(app);
    app.steps.shutdown();
    backdrop.release();
    audio::shutdown();
    fonts_unload();
    CloseWindow();
    return 0;
}
