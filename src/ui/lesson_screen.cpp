// Lesson screen: theory | code editor + console | AI tutor.
#include "screens.h"
#include "services.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

static bool ready(const std::future<RunResult>& f) {
    return f.valid() && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}
static bool ready(const std::future<TestResult>& f) {
    return f.valid() && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

static std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f) return "";
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

static std::string first_lines(const std::string& s, int n) {
    std::string out;
    std::istringstream in(s);
    std::string line;
    for (int i = 0; i < n && std::getline(in, line); ++i) out += line + "\n";
    return out;
}

// ---------------------------------------------------------------- open / close

void LessonScreen::open(App& app, int index) {
    lesson_index = index;
    task = app.lessons[index];
    marks_lesson_complete = true;
    draft_file = app.draft_path(task);
    edit_from = edit_to = 0;
    const Lesson& l = task;
    std::string draft = read_file(draft_file);
    editor.set_text(draft.empty() ? l.starter_code : draft);
    details.clear(); show_details = false;
    console.clear();
    console_scroll = theory_scroll = chat_scroll = 0;
    chat.clear();
    chat.push_back({true, "Hi, I'm Bit, your tutor for \"" + l.title + "\". Press Run to try your code, Check to test it. "
                          "Stuck? Ask me a question or press Hint."});
    chat_stick_bottom = true;
    ai_request = -1;
    hint_level = 0;
    last_error.clear();
    just_completed = false;
    question.text.clear();
    g_focus = Focus::Editor;
    print("Press Run to compile and run your program, or Check to run the tests.", theme::text_dim, false);
}

void LessonScreen::open_task(App& app, int index, const Step& step, int step_number) {
    open(app, index);
    marks_lesson_complete = step.kind == StepKind::Practice;
    task.title = step.title.empty() ? task.title : step.title;
    // Fix-the-bug and modify steps show only their own instruction; the lesson's Practice task
    // (e.g. "read n and print the largest") belongs to the Practice step and would contradict it.
    if (!step.text.empty()) task.theory = marks_lesson_complete ? step.text + "\n\n" + app.lessons[index].theory : step.text;
    if (!step.starter.empty()) task.starter_code = step.starter;
    if (!step.solution.empty()) task.solution = step.solution;
    if (!step.tests.empty()) task.tests = step.tests;
    if (!marks_lesson_complete) draft_file = app.root + "/drafts/" + task.id + "_step_" + std::to_string(step_number) + (task.lang == Lang::C ? ".c" : ".cpp");
    auto draft = read_file(draft_file);
    editor.set_text(draft.empty() ? task.starter_code : draft);
    edit_from=step.edit_from; edit_to=step.edit_to;
    editor.lock_template(task.starter_code, edit_from, edit_to);
}

void LessonScreen::close(App& app) {
    if (lesson_index < 0) return;
    mkdir((app.root + "/drafts").c_str(), 0755);
    std::ofstream(draft_file) << editor.text();
}

void LessonScreen::print(const std::string& text, Color c, bool mono) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) console.push_back({line, c, mono});
}

// ---------------------------------------------------------------- run / check

void LessonScreen::start_run(App& app) {
    const Lesson& l = task;
    just_completed = false;
    close(app);   // save draft
    std::string input;
    for (const TestCase& t : l.tests) if (!t.hidden) { input = t.input; break; }

    console.clear();
    console_scroll = 0;
    details.clear();
    show_details = false;
    print("Compiling and running…", theme::text_dim, false);
    if (!input.empty()) print("Input (from the first test):\n" + input, theme::text_dim);
    std::string code = editor.text();
    Lang lang = l.lang;
    run_job = std::async(std::launch::async, [code, input, lang] { return run_code(code, input, lang); });
}

void LessonScreen::start_check(App& app) {
    just_completed = false;
    close(app);
    console.clear();
    console_scroll = 0;
    details.clear();
    show_details = false;
    print("Running the tests…", theme::text_dim, false);
    Lesson l = task;
    std::string code = editor.text();
    check_job = std::async(std::launch::async, [l, code] { return check_code(l, code); });
}

// Reports compile errors / crashes / memory errors. Returns true if there was a problem.
static bool report_problems(LessonScreen& s, const RunResult& r) {
    s.editor.error_lines.clear();
    s.details.clear();
    for (const auto& diagnostic : {r.compile_errors, r.stderr_text}) {
        const auto first = diagnostic.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) continue;
        const auto last = diagnostic.find_last_not_of(" \t\r\n");
        if (!s.details.empty()) s.details += "\n\n";
        s.details += diagnostic.substr(first, last - first + 1);
    }
    s.show_details = false;
    for (const auto& e : r.friendly) {
        s.console.push_back({e.message, e.warning ? theme::warning : theme::danger, false});
        if (!e.hint.empty()) s.console.push_back({e.hint, theme::text, false});
    }
    if (!r.compiled) {
        if (r.friendly.empty()) s.console.push_back({"This code could not be compiled. Ask the tutor or open the details.", theme::danger, false});
        for (int ln : r.error_lines) s.editor.error_lines.insert(ln);
        s.last_error = "Compiler output:\n" + r.compile_errors;
        return true;
    }
    if (!r.memory_errors.empty()) {
        for (const MemoryError& e : r.memory_errors) {
            std::string where = e.line ? " (line " + std::to_string(e.line) + ")" : "";
            s.console.push_back({"Memory error" + where + ": " + e.details, theme::danger, false});
            if (e.line) s.editor.error_lines.insert(e.line);
        }
        s.last_error = "AddressSanitizer report:\n" + first_lines(r.stderr_text, 30);
        return true;
    }
    if (r.timed_out) {
        s.console.push_back({"Stopped: the program did not finish in 3 seconds. Is there an infinite loop?", theme::danger, false});
        s.last_error = "The program was killed after 3 seconds (timeout).";
        return true;
    }
    if (r.crashed) {
        s.console.push_back({"The program crashed.", theme::danger, false});
        s.last_error = "The program crashed.\n" + first_lines(r.stderr_text, 20);
        return true;
    }
    return false;
}

void LessonScreen::poll_jobs(App& app) {
    if (ready(run_job)) {
        RunResult r = run_job.get();
        console.clear();
        console_scroll = 0;
        last_error.clear();
        if (r.compiled) {
            print(r.output.empty() ? "(no output)" : r.output, r.output.empty() ? theme::text_dim : theme::text);

        }
        if (!report_problems(*this, r) && r.compiled)
            print("Exit code " + std::to_string(r.exit_code), theme::text_dim, false);
    }
    if (ready(check_job)) {
        TestResult t = check_job.get();
        const Lesson& l = task;
        console.clear();
        console_scroll = 0;
        details.clear();
        show_details = false;
        last_error.clear();
        if (t.total > 0 && t.passed == t.total) {
            editor.error_lines.clear();
            print("✓ All " + std::to_string(t.total) + " tests passed. Well done!", theme::success, false);
            if (marks_lesson_complete && !app.progress.completed.count(l.id)) {
                app.progress.completed.insert(l.id);
                progress_save(app.progress_path(), app.progress);
            }
            just_completed = true;
        } else {
            print("Passed " + std::to_string(t.passed) + " of " + std::to_string(t.total) + " tests.", theme::warning, false);
            if (!report_problems(*this, t.first_failed_run)) {
                print(t.first_failure, theme::text);
                last_error = "Test failure:\n" + t.first_failure;
            } else {
                print(t.first_failure, theme::text_dim, false);
            }
        }
    }
}

// ---------------------------------------------------------------- AI

void LessonScreen::ask_ai(int id, const std::string& shown_text) {
    chat.push_back({false, shown_text});
    ai_request = id;
    chat_stick_bottom = true;
}

// ---------------------------------------------------------------- drawing

void LessonScreen::draw_theory(App&, Rectangle r) {
    const Lesson& l = task;
    panel(r, "Lesson " + std::to_string(lesson_index + 1));
    Rectangle body = panel_body(r, true);
    body.width -= 8;

    BeginScissorMode((int)body.x - 4, (int)body.y - 4, (int)body.width + 8, (int)body.height + 8);
    float y = body.y - theory_scroll;
    y += draw_wrapped(g_fonts.title, 24, l.title, {body.x, y, body.width, 0}, theme::text) + 10;

    // The task first, in its own highlighted card: "Your task:" in the lesson text, or the whole
    // text for a fix-the-bug / modify step. What the lesson explained comes below it.
    std::string recap = l.theory, todo;
    size_t mark = recap.find("Your task:");
    if (mark != std::string::npos) {
        todo = recap.substr(mark + 10);
        recap = recap.substr(0, mark);
    } else {
        todo = recap;
        recap.clear();
    }
    while (!todo.empty() && (todo[0] == '\n' || todo[0] == ' ')) todo.erase(0, 1);
    {
        float pad = 14, inner = body.width - 2 * pad;
        float h = 34 + draw_rich_block(0, 0, inner, todo, false);
        const TestCase *example = nullptr;
        for (const TestCase &t : l.tests)
            if (!t.hidden) { example = &t; break; }
        std::string ex;
        if (example) {
            ex = (example->input.empty() ? "" : "Input:\n" + example->input) + "Expected output:\n" + example->expected;
            h += 30 + (std::count(ex.begin(), ex.end(), '\n') + 1) * (g_fonts.mono_size + 5);
        }
        Rectangle card{body.x, y, body.width, h + pad};
        DrawRectangleRounded(card, 0.04f, 8, theme::accent_soft);
        DrawRectangleRoundedLinesEx(card, 0.04f, 8, 1.5f, theme::accent);
        draw_text(g_fonts.ui_bold, 18, "Your task", card.x + pad, card.y + 10, theme::accent);
        float cy = card.y + 40;
        cy += draw_rich_block(card.x + pad, cy, inner, todo);
        if (example) {
            cy += 8;
            draw_text(g_fonts.ui_bold, 15, "Example", card.x + pad, cy, theme::text_dim);
            cy += 24;
            std::istringstream exin(ex);
            for (std::string el; std::getline(exin, el); cy += g_fonts.mono_size + 5)
                draw_text(g_fonts.mono, g_fonts.mono_size, el, card.x + pad,
                          cy, !el.empty() && el.back() == ':' ? theme::text_dim : theme::str);
        }
        y += card.height + 18;
    }
    if (!recap.empty())
        y += draw_wrapped(g_fonts.ui_bold, 17, "Lesson recap", {body.x, y, body.width, 0}, theme::text_dim) + 8;

    // Paragraphs; lines indented by 4+ spaces form code blocks.
    std::istringstream in(recap);
    std::string line, para, code;
    auto flush_para = [&] {
        if (para.empty()) return;
        y += draw_wrapped(g_fonts.ui, g_fonts.ui_size, para, {body.x, y, body.width, 0}, theme::text, 5) + 8;
        para.clear();
    };
    auto flush_code = [&] {
        if (code.empty()) return;
        float lh = g_fonts.mono_size + 5;
        int n = (int)std::count(code.begin(), code.end(), '\n');
        Rectangle box = {body.x, y, body.width, n * lh + 16};
        DrawRectangleRounded(box, 0.06f, 6, theme::bg);
        std::istringstream cin(code);
        std::string cl;
        float cy = y + 8;
        while (std::getline(cin, cl)) {
            draw_text(g_fonts.mono, g_fonts.mono_size, cl.size() >= 4 ? cl.substr(4) : cl, box.x + 10, cy, theme::str);
            cy += lh;
        }
        y += box.height + 10;
        code.clear();
    };
    while (std::getline(in, line)) {
        if (line.rfind("    ", 0) == 0) { flush_para(); code += line + "\n"; }
        else if (line.empty()) {
            if (!code.empty()) { /* blank line inside a code block ends it only if next isn't code */ code += "\n"; }
            else flush_para();
        } else {
            // trailing blank lines inside a code block
            while (code.size() >= 2 && code.substr(code.size() - 2) == "\n\n") code.pop_back();
            flush_code();
            // A line break inside a sentence (the lesson file is wrapped at ~90 columns) is a
            // space; after . : ! ? or before a "- " list item it stays a line break.
            bool sentence_end = !para.empty() && std::string(".:!?").find(para.back()) != std::string::npos;
            bool list_item = line.rfind("- ", 0) == 0;
            para += para.empty() ? line : ((sentence_end || list_item) ? "\n" : " ") + line;
        }
    }
    while (code.size() >= 2 && code.substr(code.size() - 2) == "\n\n") code.pop_back();
    flush_para();
    flush_code();

    int hidden = 0;
    for (const TestCase& t : l.tests) hidden += t.hidden;
    y += 10;
    y += draw_wrapped(g_fonts.ui, g_fonts.ui_size - 2,
                      std::to_string(l.tests.size()) + " tests (" + std::to_string(hidden) + " hidden)",
                      {body.x, y, body.width, 0}, theme::text_dim);
    EndScissorMode();

    float content_h = y + theory_scroll - body.y + 10;
    scroll_area(body, content_h, theory_scroll);
}

void LessonScreen::draw_console(Rectangle r) {
    panel(r, "Console");
    Rectangle body = panel_body(r, true);
    if (!details.empty()) {
        if (button({r.x+r.width-125,r.y+2,123,29},show_details?"Hide details":"Show details",true,ButtonStyle::Ghost)) {
            show_details = !show_details;
            console_scroll = 0;
        }
    }
    body.width -= 8;
    float lh_mono = g_fonts.mono_size + 4, lh_ui = g_fonts.ui_size + 5;

    // wrap all lines first to know the height
    struct Row { std::string text; Color c; bool mono; };
    std::vector<Row> rows;
    for (const ConsoleLine& cl : console) {
        const Font& f = cl.mono ? g_fonts.mono : g_fonts.ui;
        float fs = cl.mono ? g_fonts.mono_size : g_fonts.ui_size;
        for (const std::string& w : wrap_text(f, fs, cl.text, body.width)) rows.push_back({w, cl.color, cl.mono});
    }
    if (show_details) for(const auto& line:wrap_text(g_fonts.mono,g_fonts.mono_size,details,body.width)) rows.push_back({line,theme::text_dim,true});
    float h = 0;
    for (const Row& row : rows) h += row.mono ? lh_mono : lh_ui;
    scroll_area(body, h + 10, console_scroll);

    BeginScissorMode((int)body.x, (int)body.y - 4, (int)body.width + 4, (int)body.height + 8);
    float y = body.y - console_scroll;
    for (const Row& row : rows) {
        draw_text(row.mono ? g_fonts.mono : g_fonts.ui, row.mono ? g_fonts.mono_size : g_fonts.ui_size,
                  row.text, body.x, y, row.c);
        y += row.mono ? lh_mono : lh_ui;
    }
    if (busy()) {
        int dots = (int)(GetTime() * 3) % 4;
        draw_text(g_fonts.ui, g_fonts.ui_size, std::string(dots, '.'), body.x, y, theme::text_dim);
    }
    EndScissorMode();
}

void LessonScreen::draw_ai(App&, Rectangle r) {
    const Lesson& l = task;
    panel(r, "AI tutor");
    bool on = ai_available();
    std::string st = on ? ai_model() : "offline";
    float sw = text_width(g_fonts.ui, 14, st);
    DrawCircle((int)(r.x + r.width - sw - 22), (int)(r.y + 17), 4, on ? theme::success : theme::warning);
    draw_text(g_fonts.ui, 14, st, r.x + r.width - sw - 12, r.y + 10, theme::text_dim);

    Rectangle body = panel_body(r, true);
    float controls_h = 36 + 8 + 36;
    Rectangle msgs = {body.x, body.y, body.width - 8, body.height - controls_h - 12};

    // ---- messages (shared bubbles, see tutor.cpp)
    draw_chat(msgs, chat, ai_request >= 0, chat_scroll, chat_stick_bottom);

    // ---- controls
    float cy = body.y + body.height - controls_h;
    bool waiting = ai_request >= 0;
    float half = (body.width - 8) / 2;
    std::string hint_label = "Hint " + std::to_string(std::min(hint_level + 1, 3)) + "/3";
    if (button({body.x, cy, half, 36}, "Explain error", !waiting && !last_error.empty())) {
        ask_ai(ai_explain_error(editor.numbered_text(), last_error), "Explain this error, please.");
    }
    if (button({body.x + half + 8, cy, half, 36}, hint_label, !waiting)) {
        hint_level = std::min(hint_level + 1, 3);
        ask_ai(ai_hint(l, editor.numbered_text(), last_error, hint_level), "Give me a hint (level " + std::to_string(hint_level) + ").");
    }
    Rectangle in_r = {body.x, cy + 44, body.width - 76, 36};
    bool enter = text_input(in_r, question, "Ask about your code…");
    bool ask = button({body.x + body.width - 68, cy + 44, 68, 36}, "Ask", !waiting && !question.text.empty(), ButtonStyle::Primary);
    if ((enter || ask) && !waiting && !question.text.empty()) {
        ask_ai(ai_chat(l, editor.numbered_text(), last_error, question.text), question.text);
        question.text.clear();
    }

    std::string answer;
    if (ai_request >= 0 && ai_poll(ai_request, answer)) {
        chat.push_back({true, tutor_message(answer)});
        ai_request = -1;
        chat_stick_bottom = true;
    }
}

void LessonScreen::check(App& app) {
    if (!busy()) start_check(app);
}

void LessonScreen::update_and_draw(App& app, Rectangle area) {
    poll_jobs(app);
    const Lesson& l = task;
    const float m = 12;
    const float top = area.y + 46;
    float bx = area.x + area.width;
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL) || IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER);
    bx -= 90;
    if (button({bx, area.y, 90, 36}, "Reset", !busy())) {
        just_completed = false;
        editor.set_text(l.starter_code);
        editor.lock_template(l.starter_code, edit_from, edit_to);
        console.clear();
        console_scroll = 0;
        details.clear();
        show_details = false;
        print("Code reset to the starting template.", theme::text_dim, false);
    }
    if (ctrl && IsKeyPressed(KEY_ENTER) && !busy())
        start_check(app);
    bx -= 98;
    if (button({bx, area.y, 90, 36}, "▶ Run", !busy()) || (ctrl && IsKeyPressed(KEY_R) && !busy()))
        start_run(app);


    // ---- layout: theory | editor/console | AI
    float theory_w = std::clamp(area.width * 0.27f, 250.0f, 420.0f);
    float ai_w = std::clamp(area.width * 0.25f, 250.0f, 400.0f);
    float mid_x = area.x + theory_w + m, mid_w = area.width - theory_w - ai_w - 2 * m;
    float avail_h = area.y + area.height - top;
    float console_h = std::max(150.0f, avail_h * 0.30f);

    draw_theory(app, {area.x, top, theory_w, avail_h});
    editor.update_and_draw({mid_x, top, mid_w, avail_h - console_h - m});
    draw_console({mid_x, top + avail_h - console_h, mid_w, console_h});
    draw_ai(app, {area.x + area.width - ai_w, top, ai_w, avail_h});
}
