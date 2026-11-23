// UI-only regression checks. No real profiles, lesson files or engine internals are modified.
#include <cstdlib>
#include <unistd.h>
#include "screens.h"
#include "services.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <numeric>
#include <nlohmann/json.hpp>

namespace {
int assertions = 0;
void check(bool condition, const char *message) {
    ++assertions;
    if (!condition)
        throw std::runtime_error(message);
}
void event(unsigned type, int a, int b = 0) { PlayAutomationEvent({0, type, {a, b, 0, 0}}); }
void key(int key_code, bool down) { event(down ? 2 : 1, key_code); }
struct Harness {
    std::function<void()> draw;
    std::string screenshots;
    void frame(const std::string &screenshot = "") {
        for (int repeat = 0; repeat < (screenshot.empty() ? 1 : 3); ++repeat) {
            ui_begin_frame();
            BeginDrawing();
            ClearBackground(theme::bg);
            draw();
            EndDrawing();
            ui_end_frame();
        }
        if (!screenshot.empty() && !screenshots.empty()) {
            Image image = LoadImageFromScreen();
            check(ExportImage(image, (screenshots + "/" + screenshot + ".png").c_str()), "Screenshot export");
            UnloadImage(image);
        }
    }
    void click(int x, int y) {
        event(7, x, y);
        event(6, MOUSE_BUTTON_LEFT);
        frame();
        event(5, MOUSE_BUTTON_LEFT);
        frame();
        frame();
    }
    void drag(int x1, int y1, int x2, int y2, const std::string &screenshot = "") {
        event(7, x1, y1);
        event(6, MOUSE_BUTTON_LEFT);
        frame();
        event(7, x2, y2);
        frame(screenshot);
        event(5, MOUSE_BUTTON_LEFT);
        frame();
        frame();
    }
    void press(int code, std::initializer_list<int> modifiers = {}) {
        for (int modifier : modifiers)
            key(modifier, true);
        key(code, true);
        frame();
        key(code, false);
        for (int modifier : modifiers)
            key(modifier, false);
        frame();
    }
};
void editor_checks(Harness &h) {
    CodeEditor editor;
    Rectangle bounds{20, 20, 1040, 650};
    h.draw = [&] { editor.update_and_draw(bounds); };
    auto reset = [&](const std::string &text) {
        editor.read_only = false;
        editor.set_text(text);
        g_focus = Focus::Editor;
        event(7, 1090, 780);
        h.frame();
    };
    auto paste = [&](const std::string &text) {
        SetClipboardText(text.c_str());
        h.press(KEY_V, {KEY_LEFT_CONTROL});
    };
    reset("alpha beta\ngamma delta\nlast");
    for (int i = 0; i < 5; ++i)
        h.press(KEY_RIGHT, {KEY_LEFT_SHIFT});
    check(editor.selected_text() == "alpha", "Shift arrows select text");
    h.press(KEY_C, {KEY_LEFT_CONTROL});
    check(std::string(GetClipboardText()) == "alpha", "Copy selection");
    h.press(KEY_X, {KEY_LEFT_CONTROL});
    check(editor.lines[0] == " beta", "Cut selection");
    h.press(KEY_Z, {KEY_LEFT_CONTROL});
    check(editor.lines[0] == "alpha beta", "Undo cut");
    h.press(KEY_Z, {KEY_LEFT_CONTROL, KEY_LEFT_SHIFT});
    check(editor.lines[0] == " beta", "Redo cut");
    h.press(KEY_Z, {KEY_LEFT_CONTROL});
    h.press(KEY_A, {KEY_LEFT_CONTROL});
    paste("one\ntwo\nthree");
    check(editor.text() == "one\ntwo\nthree\n", "Multiline paste replaces selection");
    h.press(KEY_Z, {KEY_LEFT_CONTROL});
    check(editor.lines[0] == "alpha beta", "Undo multiline paste");
    h.press(KEY_Y, {KEY_LEFT_CONTROL});
    check(editor.lines[1] == "two", "Redo multiline paste");
    float line_h = std::round(g_fonts.mono_size * 1.45f);
    float cw = MeasureTextEx(g_fonts.mono, "M", g_fonts.mono_size, 0).x;
    float tx = bounds.x + 3 * cw + 20, ty = bounds.y + 6;
    h.click(29, ty + line_h + 8);
    check(editor.selected_text() == "two\n", "Gutter click selects a complete line");
    h.press(KEY_BACKSPACE);
    check(editor.text() == "one\nthree\n", "Delete selected line");
    h.press(KEY_Z, {KEY_LEFT_CONTROL});
    h.press(KEY_RIGHT);
    h.press(KEY_K, {KEY_LEFT_CONTROL, KEY_LEFT_SHIFT});
    check(editor.text() == "one\ntwo\n", "Delete line shortcut");
    h.press(KEY_Z, {KEY_LEFT_CONTROL});
    h.press(KEY_RIGHT);
    h.press(KEY_HOME, {KEY_LEFT_CONTROL});
    h.drag(tx, ty + 8, tx + 2 * cw, ty + line_h + 8, "editor-selection");
    check(editor.selected_text() == "one\ntw", "Mouse drag selects across lines");
    paste("Q");
    check(editor.text() == "Qo\nthree\n", "Paste over a mouse selection");
    h.press(KEY_Z, {KEY_LEFT_CONTROL});
    h.click(tx + cw, ty + line_h + 8);
    h.click(tx + cw, ty + line_h + 8);
    check(editor.selected_text() == "two", "Double click selects a word");
    reset("alpha beta\nnext");
    editor.row = 0;
    editor.col = 10;
    editor.anchor_row = 0;
    editor.anchor_col = 10;
    h.press(KEY_LEFT, {KEY_LEFT_CONTROL, KEY_LEFT_SHIFT});
    check(editor.selected_text() == "beta", "Select previous word");
    h.press(KEY_BACKSPACE);
    check(editor.lines[0] == "alpha ", "Delete selected word");
    h.press(KEY_Z, {KEY_LEFT_CONTROL});
    editor.row = 1;
    editor.col = 0;
    editor.anchor_row = 1;
    editor.anchor_col = 0;
    h.press(KEY_BACKSPACE, {KEY_LEFT_SUPER});
    check(editor.lines.size() == 2, "Cmd Backspace at start keeps the preceding line");
    editor.row = 0;
    editor.col = 10;
    editor.anchor_row = 0;
    editor.anchor_col = 10;
    h.press(KEY_LEFT, {KEY_LEFT_SUPER, KEY_LEFT_SHIFT});
    check(editor.selected_text() == "alpha beta", "Cmd Shift Left selects to line start");
    h.press(KEY_BACKSPACE);
    check(editor.lines[0].empty(), "Delete a line's selected contents");
    reset("prefix\nbody\nsuffix");
    editor.lock_template("prefix\nbody\nsuffix", 2, 2);
    h.press(KEY_A, {KEY_LEFT_SUPER});
    check(editor.selected_text() == "body", "Select all excludes locked lines");
    SetClipboardText("new\nsecond");
    h.press(KEY_V, {KEY_LEFT_SUPER});
    check(editor.text() == "prefix\nnew\nsecond\nsuffix\n", "Paste grows the editable middle");
    h.press(KEY_K, {KEY_LEFT_CONTROL, KEY_LEFT_SHIFT});
    check(editor.lines.front() == "prefix" && editor.lines.back() == "suffix",
          "Deleting a line preserves template boundaries");
    h.press(KEY_A, {KEY_LEFT_CONTROL});
    h.press(KEY_DELETE);
    check(editor.text() == "prefix\n\nsuffix\n", "Deleting all editable text keeps the template");
    h.press(KEY_BACKSPACE);
    h.press(KEY_DELETE);
    check(editor.text() == "prefix\n\nsuffix\n", "Cannot join either locked boundary");
    h.press(KEY_Z, {KEY_LEFT_CONTROL});
    check(editor.lines.front() == "prefix" && editor.lines.back() == "suffix", "Undo preserves locked lines");
    reset("printf(\"Hello\");\nreturn 0;");
    editor.read_only = true;
    auto original = editor.text();
    h.press(KEY_A, {KEY_LEFT_CONTROL});
    h.press(KEY_C, {KEY_LEFT_CONTROL});
    check(std::string(GetClipboardText()) == original.substr(0, original.size() - 1),
          "Read-only code can be selected and copied");
    h.press(KEY_X, {KEY_LEFT_CONTROL});
    paste("changed");
    h.press(KEY_DELETE);
    check(editor.text() == original, "Read-only code cannot be edited");
}
void order_checks(Harness &h) {
    OrderCards cards;
    std::vector<std::string> lines;
    std::vector<int> order(12);
    std::iota(order.begin(), order.end(), 0);
    for (int i = 0; i < 12; ++i)
        lines.push_back("Line " + std::to_string(i));
    float scroll = 0;
    bool footer = false;
    Rectangle view{80, 80, 800, 330};
    h.draw = [&] {
        cards.update_and_draw(view, lines, order, false, true, scroll);
        if (button({20, 680, 160, 40}, "Check", !cards.active() && !cards.block_release))
            footer = true;
    };
    h.frame();
    h.drag(120, 100, 120, 285, "order-dragging");
    check(order[3] == 0, "Drop a card further down the order");
    h.drag(120, 80 + 3 * 58 + 20, 120, 86);
    check(order[0] == 0, "Drop a card at the top");
    h.click(850, 103);
    check(order[0] == 1 && order[1] == 0, "Arrow controls still work");
    auto before = order;
    event(7, 120, 100);
    event(6, MOUSE_BUTTON_LEFT);
    h.frame();
    event(7, 120, 250);
    h.frame();
    h.press(KEY_ESCAPE);
    event(5, MOUSE_BUTTON_LEFT);
    h.frame();
    check(order == before && !cards.active(), "Escape cancels a drag");
    h.drag(120, 100, 60, 700);
    check(order == before && !footer, "Dropping on a footer cannot activate it");
    event(7, 120, 100);
    event(6, MOUSE_BUTTON_LEFT);
    h.frame();
    event(7, 120, 405);
    for (int i = 0; i < 30; ++i)
        h.frame();
    check(scroll > 0, "Dragging near the edge scrolls the cards");
    h.press(KEY_ESCAPE);
    event(5, MOUSE_BUTTON_LEFT);
    h.frame();
    auto sorted = order;
    std::sort(sorted.begin(), sorted.end());
    check(std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end() && sorted.size() == 12,
          "Dragging never loses or duplicates cards");
}
void console_checks(Harness &h, const std::string &root) {
    App app;
    app.root = root;
    app.lessons = lessons_load("lessons");
    app.lesson.open(app, 1);
    h.draw = [&] { app.lesson.update_and_draw(app, {20, 20, 1060, 760}); };
    auto wait = [&] {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (app.lesson.busy()) {
            if (std::chrono::steady_clock::now() > deadline)
                throw std::runtime_error("Console action timed out");
            h.frame();
        }
    };
    auto result = [&](const RunResult &run) {
        std::promise<RunResult> promise;
        app.lesson.run_job = promise.get_future();
        promise.set_value(run);
        h.frame();
    };
    RunResult run;
    run.compiled = true;
    run.compile_errors = " \n\t";
    run.stderr_text = "\r\n";
    result(run);
    check(app.lesson.details.empty(), "Whitespace-only diagnostics do not offer details");
    h.click(740, 580);
    check(!app.lesson.show_details, "Absent diagnostics cannot open a blank details section");

    run.compile_errors = "\nCompiler warning";
    for (int i = 0; i < 35; ++i)
        run.compile_errors += "\nDiagnostic line " + std::to_string(i);
    run.compile_errors += "\n\n";
    result(run);
    check(app.lesson.details.rfind("Compiler warning", 0) == 0 && app.lesson.details.back() != '\n',
          "Raw diagnostics are preserved without artificial blank lines");
    h.click(740, 580);
    check(app.lesson.show_details, "Real diagnostic details open");
    app.lesson.console_scroll = 400;
    h.frame();
    h.click(740, 580);
    check(!app.lesson.show_details && app.lesson.console_scroll == 0,
          "Hiding details immediately restores the summary at the top");
    app.lesson.console_scroll = 10000;
    h.frame();
    check(app.lesson.console_scroll == 0, "Short console content cannot retain an obsolete scroll offset");
    h.frame("console-collapsed");

    app.lesson.editor.set_text("int main(void) { broken code }");
    app.lesson.check(app);
    check(app.lesson.details.empty() && !app.lesson.show_details, "Starting a check clears old diagnostics");
    wait();
    check(!app.lesson.details.empty(), "A real compiler failure provides details");
    app.lesson.editor.set_text(R"(#include <stdio.h>
int main(void) {
    int n;
    scanf("%d", &n);
    if (n > 0) printf("positive\n");
    if (n < 0) printf("negative\n");
    else printf("zero\n");
    return 0;
}
)");
    app.lesson.check(app);
    wait();
    check(!app.lesson.just_completed && app.lesson.details.empty(),
          "Independent ifs fail output tests without inventing compiler diagnostics");
    check(std::any_of(app.lesson.console.begin(), app.lesson.console.end(),
                      [](const ConsoleLine &line) { return line.text == "zero"; }),
          "The test difference shows the unwanted zero output");
    app.lesson.editor.set_text(app.lesson.task.solution);
    app.lesson.check(app);
    wait();
    check(app.lesson.just_completed && app.lesson.details.empty() && !app.lesson.show_details,
          "A successful check clears the previous failure's details");
    check(app.lesson.console_scroll == 0, "The new result starts at the top");
    h.frame("console-passed");
}
void progress_checks(const std::string &root) {
    App old;
    old.root = root;
    old.lessons = lessons_load("lessons");
    old.progress.completed = {"01_hello", "05_pointers"};
    progress_save(old.progress_path(), old.progress);
    std::ofstream(root + "/steps.json")
        << R"({"completed":{"01_hello":[0],"05_pointers":[0,1,2,3,4,5,6,7,8,9]},"lesson":0,"step":0})";
    std::filesystem::create_directories(root + "/drafts");
    std::ofstream(root + "/drafts/01_hello.c") << "KEEP MY DRAFT";
    old.load_steps();
    check(!old.progress.completed.count("01_hello"),
          "New guided steps are not completed by an old Practice pass");
    check(!old.step_progress["01_hello"].count(0), "New Watch step is exposed");
    check(old.step_progress["01_hello"].count(old.lessons[0].steps.size() - 1),
          "Previously passed Practice is retained");
    check(old.progress.completed.count("05_pointers"), "Existing guided Pointers progress is preserved");
    std::ifstream draft(root + "/drafts/01_hello.c");
    std::string text;
    std::getline(draft, text);
    check(text == "KEEP MY DRAFT", "Progress migration does not replace drafts");
    old.step_progress["01_hello"].insert(0);
    old.save_steps();
    Step inserted;
    inserted.kind = StepKind::Choose;
    inserted.title = "New question";
    old.lessons[0].steps.insert(old.lessons[0].steps.begin(), inserted);
    old.reconcile_steps();
    check(old.step_progress["01_hello"].count(1) && !old.step_progress["01_hello"].count(0),
          "Step progress follows unchanged content after insertion");
}
void list_checks(Harness &h, const std::string &root) {
    SetWindowSize(1100, 800);
    auto node = [](uint64_t address, int value, uint64_t next) {
        return Variable{"[0]",
                        "struct node",
                        "",
                        address,
                        false,
                        0,
                        {{"value", "int", std::to_string(value), address, false, 0, {}},
                         {"next", "struct node *", next ? "pointer" : "NULL", address + 8, true, next, {}}}};
    };
    ProgramState snapshot;
    snapshot.stack = {{"main",
                       20,
                       {{"head", "struct node *", "pointer", 0x5000, true, 0x1000, {}},
                        {"p", "struct node *", "pointer", 0x5008, true, 0x2000, {}}}}};
    snapshot.heap = {{0x3000, 16, false, {node(0x3000, 30, 0)}},
                     {0x2000, 16, false, {node(0x2000, 20, 0x3000)}},
                     {0x1000, 16, false, {node(0x1000, 10, 0x2000)}}};
    auto picture = list_picture(snapshot, "head->next->value");
    check(picture.nodes.size() == 3 && picture.nodes.front().address == 0x1000 &&
              picture.nodes.back().next == 0 && picture.nodes.back().next_known,
          "LLDB node layout follows head and next, not allocation order");
    check(picture.focus_result == "head->next->value = 20" && picture.focused_node == 0x2000,
          "Pointer expression focuses its measured node");
    for (auto &block : snapshot.heap) {
        auto fields = block.cells.front().children;
        block.cells = std::move(fields);
    }
    check(list_picture(snapshot).nodes[1].value == "20", "GDB struct fields use the same chain view");
    snapshot.heap[0].cells[1].points_to = 0x1000;
    check(list_picture(snapshot).nodes.size() == 3, "A cycle cannot hang or duplicate the picture");
    snapshot.heap[0].cells[1].points_to = 0x9999;
    check(list_picture(snapshot).nodes.back().next == 0x9999, "An invalid link remains visibly invalid");
    snapshot.heap[1].freed = true;
    snapshot.heap[1].cells.clear();
    check(list_picture(snapshot, "head->next->value").focus_result.empty(),
          "A freed target never supplies a guessed prediction value");
    snapshot.heap.clear();
    snapshot.stack[0].vars.push_back(
        {"nodes", "struct node [2]", "", 0x1000, false, 0, {node(0x1000, 1, 0x2000), node(0x2000, 2, 0)}});
    check(list_picture(snapshot).nodes.size() == 2, "Stack-based list nodes are supported for Find the bug");
    Scene allocation = ui_scene_get("heap");
    auto allocation_state = ui_scene_state(allocation, {"malloc"});
    check(list_picture(allocation_state).nodes.empty(),
          "Integer allocation boxes are not mislabeled as nodes");

    App app;
    app.root = root;
    app.lessons = lessons_load("lessons");
    auto lesson = std::find_if(app.lessons.begin(), app.lessons.end(),
                               [](const Lesson &item) { return item.id == "06_lists"; });
    check(lesson != app.lessons.end(), "Linked lists lesson is loaded from main");
    int index = std::distance(app.lessons.begin(), lesson);
    h.draw = [&] { app.steps.update_and_draw(app); };
    auto wait = [&] {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(50);
        while (app.steps.job.valid() || app.lesson.busy()) {
            if (std::chrono::steady_clock::now() > deadline)
                throw std::runtime_error("Linked list action timed out");
            h.frame();
        }
        check(app.steps.error.empty(), "Linked list action completes without a debugger error");
    };
    auto primary = [&] {
        h.click(990, GetScreenHeight() - 42);
        wait();
        h.frame();
    };
    app.steps.open(app, index);
    wait();
    h.frame();
    h.frame();
    int controls = std::min(
        210, (int)(65 + app.steps.scene.params.size() * 52 + app.steps.scene.actions.size() / 3 * 38));
    int actions_y = GetScreenHeight() - 94 - controls + 28 + app.steps.scene.params.size() * 50 + 16;
    h.click(90, actions_y);
    check(!app.steps.state.heap.empty() && !app.steps.state.heap.front().freed,
          "Explore reserves memory before node construction");
    h.frame("lists-explore-allocated");
    h.click(275, actions_y);
    check(app.steps.state.heap.front().freed, "Explore gives memory back");
    primary();
    check(app.lessons[index].steps[app.steps.step_index].kind == StepKind::Watch,
          "Explore advances to Watch");
    bool saw_chain = false, saw_cursor = false, saw_free = false;
    for (int i = 0; i < 130 && !app.steps.state.finished; ++i) {
        primary();
        auto chain = list_picture(app.steps.state);
        if (!saw_chain && chain.nodes.size() == 3 && chain.nodes[0].value == "10" &&
            chain.nodes[1].value == "20" && chain.nodes[2].value == "30" &&
            chain.nodes[0].next == chain.nodes[1].address && chain.nodes[1].next == chain.nodes[2].address) {
            saw_chain = true;
            check(app.steps.state.note.find("head->next now points") != std::string::npos,
                  "Watch uses the same arrow notation as the lesson code");
            h.frame("lists-watch-chain");
        }
        auto cursor = std::find_if(chain.references.begin(), chain.references.end(), [](const auto &ref) {
            return ref.name == "p" && ref.known && ref.target;
        });
        if (!saw_cursor && cursor != chain.references.end() && chain.nodes.size() == 3 &&
            cursor->target == chain.nodes[1].address) {
            saw_cursor = true;
            h.frame("lists-watch-cursor");
        }
        if (!saw_free &&
            std::any_of(chain.nodes.begin(), chain.nodes.end(), [](const auto &n) { return n.freed; })) {
            saw_free = true;
            h.frame("lists-watch-freed");
        }
    }
    check(saw_chain && saw_cursor && saw_free, "Watch shows links, the moving cursor and freed nodes");
    check(app.steps.state.finished && describe_difference("10 20 30", app.steps.state.output).empty(),
          "The linked list example prints the measured traversal and finishes");
    h.frame("lists-watch-finished");
    primary();
    app.steps.answer.text = "4";
    primary();
    check(!app.steps.verdict.correct, "The list prediction rejects the first node's value");
    app.steps.answer.text = "7";
    primary();
    check(app.steps.verdict.correct, "The list prediction follows next to the correct value");
    check(list_picture(app.steps.state, "a->next->value").focus_result == "a->next->value = 7",
          "Predict highlights the actual destination field");
    h.frame("lists-predict");
    primary();
    app.steps.choice = 0;
    primary();
    check(app.steps.verdict.correct, "Choose explains NULL termination");
    primary();
    std::iota(app.steps.order.begin(), app.steps.order.end(), 0);
    primary();
    check(app.steps.verdict.correct, "Order checks the list traversal");
    primary();
    app.steps.debugging = true;
    app.steps.code.set_text(app.lesson.editor.text());
    app.steps.launch_debug(app, "start");
    wait();
    bool saw_stack_chain = false;
    for (int i = 0; i < 60 && !app.steps.state.finished; ++i) {
        primary();
        auto chain = list_picture(app.steps.state);
        auto p = std::find_if(chain.references.begin(), chain.references.end(),
                              [](const auto &r) { return r.name == "p" && r.known; });
        if (!saw_stack_chain && chain.nodes.size() == 3 && p != chain.references.end() &&
            p->target == chain.nodes.back().address) {
            saw_stack_chain = true;
            h.frame("lists-find-bug-chain");
        }
    }
    check(saw_stack_chain, "Find the bug draws the real stack-based node chain and its cursor");
    check(describe_difference("1 2", app.steps.state.output).empty(),
          "Find the bug debugger exposes the omitted final node");
    h.frame("lists-find-bug-debug");
    primary();
    check(!app.steps.debugging, "The student can return to editing the faulty traversal");
    auto bug = app.lessons[index].steps[app.steps.step_index];
    app.lesson.editor.set_text(bug.solution);
    app.lesson.editor.lock_template(app.lesson.task.starter_code, bug.edit_from, bug.edit_to);
    primary();
    check(app.lesson.just_completed, "Fixing the loop prints all list nodes");
    primary();
    app.steps.explanation.text = "NULL marks the end, so the loop stops after the last node.";
    primary();
    check(app.steps.submitted, "Explain submits the student's list explanation");
    primary();
    app.lesson.editor.set_text(app.lesson.task.solution);
    app.steps.debugging = true;
    app.steps.code.set_text(app.lesson.editor.text());
    app.steps.launch_debug(app, "start");
    wait();
    bool saw_built_list = false;
    for (int i = 0; i < 120 && !app.steps.state.finished; ++i) {
        primary();
        auto chain = list_picture(app.steps.state);
        if (!saw_built_list && chain.nodes.size() == 3 && chain.nodes[0].value == "3" &&
            chain.nodes[1].value == "2" && chain.nodes[2].value == "1" &&
            chain.nodes[0].next == chain.nodes[1].address && chain.nodes[1].next == chain.nodes[2].address) {
            saw_built_list = true;
            h.frame("lists-practice-debug-chain");
        }
    }
    check(saw_built_list, "Practice Debug reads every link of nodes allocated inside the loop");
    check(app.steps.state.finished && describe_difference("3 2 1", app.steps.state.output).empty(),
          "The student's list prints in reverse order and gives its nodes back");
    primary();
    primary();
    check(app.lesson.just_completed, "List Practice passes all visible and hidden tests");
    h.frame("lists-practice");
    primary();
    check(app.screen == Screen::Course && app.progress.completed.count("06_lists"),
          "The complete linked list journey saves progress");
    SetWindowSize(1100, 650);
    h.frame();
    app.steps.open(app, index);
    app.steps.enter(app, 2);
    app.steps.answer.text = "7";
    primary();
    check(app.steps.verdict.correct, "The list prediction remains usable in a small window");
    h.frame("lists-compact-predict");
    app.steps.shutdown();
    SetWindowSize(1100, 800);
    h.draw = [] {};
    h.frame();
}
void lesson_checks(Harness &h, const std::string &root) {
    App app;
    app.root = root;
    app.lessons = lessons_load("lessons");
    ModalBackdrop backdrop;
    h.draw = [&] {
        if (!app.tutor.visible) {
            if (app.screen == Screen::Course)
                draw_course_screen(app);
            else
                app.steps.update_and_draw(app);
            if (app.tutor.visible)
                backdrop.capture();
        } else
            backdrop.draw();
        app.tutor.update_and_draw();
    };
    auto wait = [&] {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(50);
        while (app.steps.job.valid() || app.lesson.busy()) {
            if (std::chrono::steady_clock::now() >= deadline)
                throw std::runtime_error("Background lesson action timed out");
            h.frame();
        }
    };
    auto primary = [&] {
        h.click(990, GetScreenHeight() - 42);
        wait();
        h.frame();
    };
    for (int lesson = 0; lesson < 4; ++lesson) {
        app.steps.open(app, lesson);
        wait();
        int count = app.lessons[lesson].steps.size();
        for (int index = 0; index < count; ++index) {
            check(app.steps.step_index == index, "Guided lesson remains on the expected step");
            const Step step = app.lessons[lesson].steps[index];
            if (step.kind == StepKind::Watch) {
                for (int line = 0; line < 160 && !app.steps.state.finished; ++line) {
                    primary();
                }
                check(app.steps.state.finished, "Watch finishes");
                if (lesson == 0)
                    check(app.steps.state.output == "Hello!\nI am a program.\n",
                          "Hello output appears line by line");
                h.frame("lesson-" + std::to_string(lesson) + "-watch");
            } else if (step.kind == StepKind::Explore) {
                // Slider track: y = body bottom - controls + title + slider offset.
                int controls = std::min(210, (int)(65 + app.steps.scene.params.size() * 52 +
                                                   app.steps.scene.actions.size() / 3 * 38));
                int y = GetScreenHeight() - 94 - controls + 55;
                h.click(405, y);
                check(app.steps.completed[index], "Explore controls update the scene");
                if (lesson == 1)
                    check(app.steps.picture.branch == "true", "Condition picture shows the chosen branch");
                if (lesson == 3)
                    check(!program_picture_facts(app.steps.state).selected_box.empty(),
                          "Array scene selects a visible box");
                h.frame("lesson-" + std::to_string(lesson) + "-explore");
            } else if (step.kind == StepKind::Predict) {
                app.steps.answer.text = "definitely wrong";
                primary();
                check(!app.steps.verdict.correct, "Predict rejects a wrong answer");
                app.steps.answer.text = lesson == 0   ? "Hi there!"
                                        : lesson == 1 ? "Cold"
                                        : lesson == 2 ? (step.predict_expr.empty() ? "Hi Hi Hi" : "10")
                                                      : "1";
                primary();
                check(app.steps.verdict.correct, "Predict accepts the independently calculated answer");
                if (lesson == 3)
                    check(program_picture_facts(app.steps.state, step.predict_expr).selected_box ==
                              "a[2] = 1",
                          "Array prediction highlights the measured element");
                h.frame("lesson-" + std::to_string(lesson) + "-predict");
            } else if (step.kind == StepKind::Choose) {
                app.steps.choice = step.correct;
                primary();
                check(app.steps.verdict.correct, "Choose checks the selected option");
            } else if (step.kind == StepKind::Order) {
                std::iota(app.steps.order.begin(), app.steps.order.end(), 0);
                primary();
                check(app.steps.verdict.correct, "Order accepts the ordered program");
                h.frame("hello-order");
            } else if (step.kind == StepKind::Modify || step.kind == StepKind::FindBug ||
                       step.kind == StepKind::Practice) {
                if (step.kind != StepKind::Practice) {
                    primary();
                    check(!app.lesson.just_completed, "Starter still fails the task");
                }
                const std::string solution =
                    step.solution.empty() ? app.lessons[lesson].solution : step.solution;
                app.lesson.editor.set_text(solution);
                app.lesson.editor.lock_template(app.lesson.task.starter_code, step.edit_from, step.edit_to);
                primary();
                check(app.lesson.just_completed, "Edited task passes its own checks");
                h.frame("lesson-" + std::to_string(lesson) + "-" + step_kind_name(step.kind));
            } else if (step.kind == StepKind::Explain) {
                app.steps.explanation.text = step.sample_answer;
                primary();
                check(app.steps.submitted, "Explain accepts a submitted explanation");
                check(app.steps.verdict.feedback.find(step.rubric) != std::string::npos,
                      "Explain feedback uses this lesson's ideas");
            }
            primary();
        }
        check(app.screen == Screen::Course, "Guided lesson returns to the path");
        check(app.progress.completed.count(app.lessons[lesson].id), "Whole lesson completion is saved");
    }
    SetWindowSize(1100, 650);
    h.frame();
    h.frame();
    for (int lesson = 0; lesson < 4; ++lesson) {
        app.steps.open(app, lesson);
        wait();
        int prediction = 0;
        while (app.lessons[lesson].steps[prediction].kind != StepKind::Predict)
            ++prediction;
        app.steps.enter(app, prediction);
        h.frame();
        app.steps.answer.text = lesson == 0 ? "Hi there!" : lesson == 1 ? "Cold" : lesson == 2 ? "10" : "1";
        primary();
        check(app.steps.verdict.correct, "Predict remains usable at the minimum window size");
        h.frame("compact-" + std::to_string(lesson) + "-predict");
    }
    app.steps.open(app, 0);
    app.steps.enter(app, 0);
    wait();
    h.frame();
    h.click(990, 36);
    for (int i = 0; i < 18; ++i)
        h.frame();
    check(app.tutor.visible && backdrop.texture.id, "Tutor preserves a native-resolution background");
    check(backdrop.texture.width >= GetScreenWidth(), "Tutor background does not lose display resolution");
    h.frame("tutor-overlay");
    h.press(KEY_ESCAPE);
    check(!app.tutor.visible, "Tutor can close");
    app.steps.shutdown();
    backdrop.release();
}
} // namespace
int main(int argc, char **argv) {
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN | FLAG_WINDOW_RESIZABLE);
    InitWindow(1100, 800, "CodeLens interaction checks");
    SetTargetFPS(120);
    SetExitKey(KEY_NULL);
    check(fonts_load("assets"), "Fonts load");
    char pattern[] = "/tmp/codelens-interactions-XXXXXX";
    char *temporary = mkdtemp(pattern);
    check(temporary, "Create isolated profile");
    std::string root = temporary;
    Harness h;
    const bool console_only = argc > 1 && std::string(argv[1]) == "--console-only";
    const bool lists_only = argc > 1 && std::string(argv[1]) == "--lists-only";
    const int screenshots_arg = console_only || lists_only ? 2 : 1;
    if (argc > screenshots_arg) {
        h.screenshots = argv[screenshots_arg];
        std::filesystem::create_directories(h.screenshots);
    }
    try {
        if (!lists_only) {
            std::filesystem::create_directories(root + "/console");
            console_checks(h, root + "/console");
        }
        if (!console_only && !lists_only) {
            editor_checks(h);
            order_checks(h);
            std::filesystem::create_directories(root + "/migration");
            progress_checks(root + "/migration");
            std::filesystem::create_directories(root + "/lessons");
            lesson_checks(h, root + "/lessons");
        }
        if (!console_only) {
            std::filesystem::create_directories(root + "/lists");
            list_checks(h, root + "/lists");
        }
        h.draw = [] {};
        fonts_unload();
        CloseWindow();
        std::filesystem::remove_all(root);
        std::cout << assertions << " UI interaction checks passed.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAILED: " << error.what() << " (after " << assertions << " checks)\n";
        CloseWindow();
        std::filesystem::remove_all(root);
        return 1;
    }
}
