// Integration checks for the student's journey. Run with a display (xvfb-run on Linux).
#include <cstdlib>
#include <unistd.h>
#include "screens.h"
#include "services.h"
#include "memview.h"
#include <cassert>
#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include "fake_tutor.h"

static void event(unsigned type, int a, int b = 0) { PlayAutomationEvent({0, type, {a, b, 0, 0}}); }
int main(int argc, char **argv) {
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(1100, 650, "CodeLens UI checks");
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);
    assert(fonts_load("assets"));
    char temp[] = "/tmp/codelens-ui-XXXXXX";
    assert(mkdtemp(temp));
    App app;
    app.root = temp;
    app.lessons = lessons_load("lessons");
    assert(app.lessons.size() >= 5);
    app.screen = Screen::Welcome;
    std::string screenshots = argc > 1 ? argv[1] : "";
    if (!screenshots.empty())
        std::filesystem::create_directories(screenshots);
    RenderTexture2D backdrop=LoadRenderTexture(1100,650);
    auto render = [&](const std::string &shot = "") {
        for (int repeat = 0; repeat < (shot.empty() ? 1 : 3); ++repeat) {
            ui_begin_frame();
            if (!app.tutor.visible) {
                BeginTextureMode(backdrop);
                ClearBackground(theme::bg);
                if (app.screen == Screen::Welcome)
                    draw_welcome_screen(app);
                else if (app.screen == Screen::Course)
                    draw_course_screen(app);
                else if (app.screen == Screen::AiSetup)
                    draw_ai_setup_screen(app);
                else
                    app.steps.update_and_draw(app);
                EndTextureMode();
            }
            BeginDrawing();
            ClearBackground(theme::bg);
            DrawTextureRec(backdrop.texture,{0,0,1100,-650},{0,0},WHITE);
            app.tutor.update_and_draw();
            EndDrawing();
            ui_end_frame();
        }
        if (!shot.empty() && !screenshots.empty()) {
            Image image = LoadImageFromScreen();
            assert(ExportImage(image, (screenshots + "/" + shot + ".png").c_str()));
            UnloadImage(image);
        }
    };
    auto click = [&](int x, int y) {
        event(7, x, y);
        event(6, MOUSE_BUTTON_LEFT);
        render();
        event(7, x, y);
        event(5, MOUSE_BUTTON_LEFT);
        render();
        render();
    };
    auto wait = [&] {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
        while (app.steps.job.valid() || app.lesson.busy() || app.steps.generated_job.valid()) {
            assert(std::chrono::steady_clock::now() < deadline);
            render();
        }
    };
    auto next = [&] {
        click(990, 606);
        wait();
        render();
    };
    render("welcome");
    click(800, 510);
    assert(std::ifstream(app.welcome_path()));
    assert(app.screen == Screen::Lesson);
    while (app.steps.job.valid())   // lesson 1 now opens on a Watch step, which starts the debugger first
        render();
    click(65, 36);
    assert(app.screen == Screen::Course);
    render("course");
    int pointers = 0;
    while (app.lessons[pointers].id != "05_pointers")
        ++pointers;
    app.steps.open(app, pointers);
    render();
    assert(app.steps.step_index == 0);
    next();
    assert(app.steps.step_index == 0); // Explore cannot be skipped before an action.
    click(80, 585);                    // Outside controls: still locked.
    // The scene is a UI adapter until Jeffrey supplies scene_get, and can be replaced unchanged.
    auto state = ui_scene_state(app.steps.scene, {"p = &b", "*p = 5"});
    assert(state.stack[0].vars[1].value == "5");
    click(75, 510); // First pointer action at the bottom of the code panel.
    assert(app.steps.completed[0]);
    render("explore");
    next();
    assert(app.steps.step_index == 1);
    wait();
    assert(!app.steps.state.stack.empty());
    render("watch");
    // Follow a real function call and return. The public debugger API is identical on macOS/Linux.
    for (int i = 0; i < 35 && !app.steps.state.finished; ++i) {
        app.steps.launch_debug(app, i == 1 ? "into" : "next");
        wait();
    }
    assert(app.steps.state.finished);
    assert(app.steps.state.output == "5 3\n");
    render();
    next();
    assert(app.steps.step_index == 2);
    app.steps.answer.text = "999";
    next();
    assert(!app.steps.verdict.correct && app.steps.step_index == 2);
    render("predict-wrong");
    app.steps.answer.text = "10";
    next();
    assert(app.steps.verdict.correct);
    assert(app.steps.state.stack.back().vars[0].value == "10");
    render("predict");
    next();
    assert(app.steps.step_index == 3);
    app.steps.answer.text = "&y";
    next();
    assert(app.steps.verdict.correct);
    next();
    assert(app.steps.step_index == 4);
    app.steps.answer.text = "3 1";
    next();
    assert(app.steps.verdict.correct);
    next();
    assert(app.steps.step_index == 5);
    app.steps.choice = 0;
    next();
    assert(!app.steps.verdict.correct);
    app.steps.choice = 2;
    next();
    assert(app.steps.verdict.correct);
    render("choose");
    next();
    assert(app.steps.step_index == 6);
    next();
    assert(!app.steps.verdict.correct);
    render("order-wrong");
    for (size_t i = 0; i < app.steps.order.size(); ++i)
        app.steps.order[i] = i;
    next();
    assert(app.steps.verdict.correct);
    next();
    assert(app.steps.step_index == 7);
    next();
    assert(!app.lesson.just_completed);
    app.lesson.editor.set_text(app.lessons[pointers].steps[7].solution);
    next();
    assert(app.lesson.just_completed);
    assert(!app.progress.completed.count("05_pointers"));
    render("find-bug");
    next();
    assert(app.steps.step_index == 8);
    app.steps.explanation.text =
        "A function gets copies of values. Addresses let it change the caller's variables.";
    next();
    assert(app.steps.submitted);
    render("explain");
    next();
    assert(app.steps.step_index == 9);
    assert(app.lesson.editor.locked_prefix == 3);
    // A cursor cannot enter the fixed prefix or delete the boundary; the editable middle can grow.
    auto &editor = app.lesson.editor;
    const auto prefix = editor.lines[0];
    const auto suffix = editor.lines.back();
    g_focus = Focus::Editor;
    editor.row = editor.locked_prefix;
    editor.col = 0;
    event(2, KEY_BACKSPACE);
    render();
    event(1, KEY_BACKSPACE);
    render();
    assert(editor.lines[0] == prefix && editor.lines.back() == suffix);
    assert(editor.row == editor.locked_prefix);
    event(2, KEY_UP);
    render();
    event(1, KEY_UP);
    render();
    assert(editor.row == editor.locked_prefix);
    editor.set_text(app.lessons[pointers].solution);
    editor.lock_template(app.lesson.task.starter_code, app.lesson.edit_from, app.lesson.edit_to);
    // The full reference has a longer middle, which must retain its fixed suffix.
    next();
    assert(app.lesson.just_completed);
    assert(app.progress.completed.count("05_pointers"));
    render("practice-passed");
    click(80, 230);   // Lesson notes (the step header is 20 px taller)
    assert(app.steps.notes);
    render("notes");
    click(960, 606);
    assert(!app.steps.notes);
    click(990, 36);
    assert(app.tutor.visible);
    for (int i = 0; i < 20; ++i)
        render();
    render("tutor");
    int step = app.steps.step_index;
    next();
    assert(app.tutor.visible && app.steps.step_index == step);
    event(2, KEY_ESCAPE);
    render();
    event(1, KEY_ESCAPE);
    render();
    assert(!app.tutor.visible);
    next();
    assert(app.screen == Screen::Course);
    App restored;
    restored.root = app.root;
    restored.load_steps();
    assert(restored.step_progress["05_pointers"].size() == 10);
    app.screen = Screen::AiSetup;
    render("setup");
    click(930, 606);
    assert(app.screen == Screen::Course);
    // Every adapter scene produces consistent, renderable state at every display level.
    for (const char *id : {"variables", "condition", "array", "pointer_basics", "heap", "recursion"}) {
        auto scene = ui_scene_get(id);
        assert(!scene.id.empty());
        auto memory = ui_scene_state(scene, {"malloc"});
        assert(!scene.code.empty() && !memory.note.empty());
        for (int level = 0; level < 4; ++level) {
            BeginDrawing();
            ClearBackground(theme::bg);
            draw_memory(memory, {50, 50, 1000, 550}, (ShowLevel)level);
            EndDrawing();
        }
    }
    // Bad generated tasks are rejected before being shown.
    std::string error;
    assert(validated_practice("{}", app.lessons[pointers], error).id.empty());
    assert(!error.empty());
    // Verify the complete setup/download/chat/generation protocol against a local fixture.
    FakeTutor fake;
    setenv("CODELENS_OLLAMA_HOST", "127.0.0.1", 1);
    setenv("CODELENS_OLLAMA_PORT", std::to_string(fake.port).c_str(), 1);
    ai_init("127.0.0.1", fake.port, "test-model");
    tutor_refresh();
    auto until = [&](auto condition) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (!condition()) {
            assert(std::chrono::steady_clock::now() < deadline);
            render();
        }
    };
    until([&] { return tutor_status().server; });
    assert(!tutor_status().installed);
    app.screen = Screen::AiSetup;
    render("setup-offline-model");
    tutor_download();
    until([&] { return !tutor_status().downloading; });
    assert(tutor_status().installed);
    assert(tutor_status().progress == 1);
    render("setup-ready");
    click(930, 606);
    app.screen = Screen::Lesson;
    until([] { return ai_available(); });
    assert(app.lesson.just_completed);
    click(780, 606);
    assert(app.steps.generated_job.valid());
    wait();
    assert(app.steps.generated_task);
    assert(app.lesson.task.id == "05_pointers_extra");
    assert(!app.lesson.just_completed);
    app.lesson.editor.set_text(app.lesson.task.solution);
    next();
    assert(app.lesson.just_completed);
    render("more-practice");
    click(990, 36);
    app.tutor.question.text = "Why did the pointer change the value?";
    for (int i = 0; i < 20; ++i)
        render();
    click(1040, 617);
    until([&] { return app.tutor.request < 0; });
    assert(app.tutor.history.find("Follow the highlighted line") != std::string::npos);
    render("tutor-reply");
    event(2, KEY_ESCAPE);
    render();
    event(1, KEY_ESCAPE);
    render();
    auto bad = nlohmann::json::parse(FakeTutor::exercise());
    bad["tests"][0]["expected"] = "999\n";
    assert(validated_practice(bad.dump(), app.lessons[pointers], error).id.empty());
    // Modify uses its own task and draft, and Debug preserves the student's edits.
    Lesson modify=app.lessons[0];modify.id="ui_modify";modify.title="Change one value";
    Step change;change.kind=StepKind::Modify;change.title="Change one value";change.text="Print 1 instead of 0.";
    change.starter="#include <stdio.h>\nint main(void) {\n    puts(\"0\");\n    return 0;\n}\n";
    change.solution="#include <stdio.h>\nint main(void) {\n    puts(\"1\");\n    return 0;\n}\n";
    change.tests={{"","1\n",false}};change.edit_from=change.edit_to=3;modify.steps={change};
    app.lessons.push_back(modify);app.steps.open(app,app.lessons.size()-1);render();
    app.lesson.editor.set_text(change.solution);app.lesson.editor.lock_template(change.starter,3,3);
    auto saved=app.lesson.editor.text();click(250,230);wait();assert(app.steps.debugging&&!app.steps.state.stack.empty());render("modify-debug");
    click(435,520); // the secondary Back to code control, when in Watch mode
    if(app.steps.debugging){for(int i=0;i<15&&!app.steps.state.finished;++i){app.steps.launch_debug(app,"next");wait();}next();}
    assert(!app.steps.debugging&&app.lesson.editor.text()==saved);next();assert(app.lesson.just_completed);
    // Every real fixture is renderable at each progressive display level.
    for(const char* fixture:{"array","swap","struct","heap","recursion","null_crash","leak"}){
        std::ifstream file(std::string("tests/fixtures/")+fixture+".json");std::string data((std::istreambuf_iterator<char>(file)),{});ProgramState memory;std::string problem;
        assert(state_from_json(data,memory,problem));
        for(int level=0;level<4;++level){BeginDrawing();ClearBackground(theme::bg);draw_memory(memory,{50,50,1000,550},(ShowLevel)level);EndDrawing();}
    }
    // Compiler errors appear as friendly prose; details remain available.
    // (lesson 1 now starts with Understand steps, so use a copy that has only its Practice step)
    Lesson practice_only = app.lessons[0];
    practice_only.id = "ui_practice";
    practice_only.steps = {app.lessons[0].steps.back()};
    app.lessons.push_back(practice_only);
    app.steps.open(app, (int)app.lessons.size() - 1);
    render();
    app.lesson.editor.set_text("int main(void) {\n return\n}\n");
    app.lesson.check(app);
    wait();
    assert(!app.lesson.details.empty());
    assert(!app.lesson.console.empty());
    render("friendly-error");
    app.steps.shutdown();
    app.lesson.close(app);
    UnloadRenderTexture(backdrop);
    fonts_unload();
    CloseWindow();
    std::filesystem::remove_all(app.root);
    std::cout << "UI integration passed: guided pointers, real debugger, answer checks, edit locks, drafts, "
                 "progress, help, scenes, setup and friendly errors.\n";
}
