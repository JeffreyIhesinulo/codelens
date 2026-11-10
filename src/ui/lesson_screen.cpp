// Lesson screen: theory | code editor + console | AI tutor.
#include "screens.h"

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
    const Lesson& l = app.lessons[index];
    std::string draft = read_file(app.draft_path(l));
    editor.set_text(draft.empty() ? l.starter_code : draft);
    console.clear();
    console_scroll = theory_scroll = chat_scroll = 0;
    chat.clear();
    chat.push_back({true, "Hi! I'm your tutor for \"" + l.title + "\". Press Run to try your code, Check to test it. "
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

void LessonScreen::close(App& app) {
    if (lesson_index < 0) return;
    mkdir((app.root + "/drafts").c_str(), 0755);
    std::ofstream(app.draft_path(app.lessons[lesson_index])) << editor.text();
}

void LessonScreen::print(const std::string& text, Color c, bool mono) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) console.push_back({line, c, mono});
}

// ---------------------------------------------------------------- run / check

void LessonScreen::start_run(App& app) {
    const Lesson& l = app.lessons[lesson_index];
    close(app);   // save draft
    std::string input;
    for (const TestCase& t : l.tests) if (!t.hidden) { input = t.input; break; }

    console.clear();
    console_scroll = 0;
    print("Compiling and running…", theme::text_dim, false);
    if (!input.empty()) print("Input (from the first test):\n" + input, theme::text_dim);
    std::string code = editor.text();
    Lang lang = l.lang;
    run_job = std::async(std::launch::async, [code, input, lang] { return run_code(code, input, lang); });
}

void LessonScreen::start_check(App& app) {
    close(app);
    console.clear();
    console_scroll = 0;
    print("Running the tests…", theme::text_dim, false);
    Lesson l = app.lessons[lesson_index];
    std::string code = editor.text();
    check_job = std::async(std::launch::async, [l, code] { return check_code(l, code); });
}

// Reports compile errors / crashes / memory errors. Returns true if there was a problem.
static bool report_problems(LessonScreen& s, const RunResult& r) {
    s.editor.error_lines.clear();
    if (!r.compiled) {
        s.console.push_back({"Compilation failed:", theme::danger, false});
        std::istringstream in(r.compile_errors);
        std::string line;
        while (std::getline(in, line)) s.console.push_back({line, theme::danger, true});
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
        last_error.clear();
        if (r.compiled) {
            print(r.output.empty() ? "(no output)" : r.output, r.output.empty() ? theme::text_dim : theme::text);
            if (!r.compile_errors.empty()) print("Warnings:\n" + r.compile_errors, theme::warning);
        }
        if (!report_problems(*this, r) && r.compiled)
            print("Exit code " + std::to_string(r.exit_code), theme::text_dim, false);
    }
    if (ready(check_job)) {
        TestResult t = check_job.get();
        const Lesson& l = app.lessons[lesson_index];
        console.clear();
        last_error.clear();
        if (t.passed == t.total) {
            editor.error_lines.clear();
            print("✓ All " + std::to_string(t.total) + " tests passed. Lesson complete!", theme::success, false);
            if (!app.progress.completed.count(l.id)) {
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

void LessonScreen::draw_theory(App& app, Rectangle r) {
    const Lesson& l = app.lessons[lesson_index];
    panel(r, "Lesson " + std::to_string(lesson_index + 1));
    Rectangle body = panel_body(r, true);
    body.width -= 8;

    BeginScissorMode((int)body.x - 4, (int)body.y - 4, (int)body.width + 8, (int)body.height + 8);
    float y = body.y - theory_scroll;
    y += draw_wrapped(g_fonts.title, 24, l.title, {body.x, y, body.width, 0}, theme::text) + 10;

    // Paragraphs; lines indented by 4+ spaces form code blocks.
    std::istringstream in(l.theory);
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
            para += (para.empty() ? "" : "\n") + line;
        }
    }
    while (code.size() >= 2 && code.substr(code.size() - 2) == "\n\n") code.pop_back();
    flush_para();
    flush_code();

    // visible test as an example
    for (const TestCase& t : l.tests) {
        if (t.hidden) continue;
        y += 6;
        draw_text(g_fonts.ui_bold, g_fonts.ui_size, "Example", body.x, y, theme::text_dim);
        y += g_fonts.ui_size + 8;
        std::string ex = (t.input.empty() ? "" : "Input:\n" + t.input) + "Expected output:\n" + t.expected;
        y += draw_wrapped(g_fonts.mono, g_fonts.mono_size, ex, {body.x, y, body.width, 0}, theme::text_dim, 5);
        break;
    }
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
    float h = 0;
    for (const Row& row : rows) h += row.mono ? lh_mono : lh_ui;

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
    scroll_area(body, h + 10, console_scroll);
}

void LessonScreen::draw_ai(App& app, Rectangle r) {
    const Lesson& l = app.lessons[lesson_index];
    panel(r, "AI tutor");
    bool on = ai_available();
    std::string st = on ? ai_model() : "offline";
    float sw = text_width(g_fonts.ui, 14, st);
    DrawCircle((int)(r.x + r.width - sw - 22), (int)(r.y + 17), 4, on ? theme::success : theme::warning);
    draw_text(g_fonts.ui, 14, st, r.x + r.width - sw - 12, r.y + 10, theme::text_dim);

    Rectangle body = panel_body(r, true);
    float controls_h = 36 + 8 + 36;
    Rectangle msgs = {body.x, body.y, body.width - 8, body.height - controls_h - 12};

    // ---- messages
    const float pad = 10, gap = 10;
    float bubble_w = msgs.width - 20;
    std::vector<std::vector<std::string>> wrapped;
    float total = 0;
    for (const ChatMessage& m : chat) {
        wrapped.push_back(wrap_text(g_fonts.ui, g_fonts.ui_size, m.text, bubble_w - 2 * pad));
        total += wrapped.back().size() * (g_fonts.ui_size + 5) + 2 * pad + gap;
    }
    if (ai_request >= 0) total += g_fonts.ui_size + 2 * pad + gap;
    if (chat_stick_bottom) { chat_scroll = std::max(0.0f, total - msgs.height); chat_stick_bottom = false; }

    BeginScissorMode((int)msgs.x, (int)msgs.y, (int)msgs.width + 4, (int)msgs.height);
    float y = msgs.y - chat_scroll;
    for (size_t i = 0; i < chat.size(); ++i) {
        const ChatMessage& m = chat[i];
        float h = wrapped[i].size() * (g_fonts.ui_size + 5) + 2 * pad;
        float bx = m.from_ai ? msgs.x : msgs.x + 20;
        DrawRectangleRounded({bx, y, bubble_w, h}, 0.12f, 6, m.from_ai ? theme::panel_hi : theme::accent_dk);
        float ty = y + pad;
        for (const std::string& line : wrapped[i]) {
            draw_text(g_fonts.ui, g_fonts.ui_size, line, bx + pad, ty, theme::text);
            ty += g_fonts.ui_size + 5;
        }
        y += h + gap;
    }
    if (ai_request >= 0) {
        int dots = (int)(GetTime() * 3) % 4;
        draw_text(g_fonts.ui, g_fonts.ui_size, "Thinking" + std::string(dots, '.'), msgs.x + pad, y + pad, theme::text_dim);
    }
    EndScissorMode();
    scroll_area(msgs, total, chat_scroll);

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
        ask_ai(ai_hint(l, editor.numbered_text(), hint_level), "Give me a hint (level " + std::to_string(hint_level) + ").");
    }
    Rectangle in_r = {body.x, cy + 44, body.width - 76, 36};
    bool enter = text_input(in_r, question, "Ask about your code…");
    bool ask = button({body.x + body.width - 68, cy + 44, 68, 36}, "Ask", !waiting && !question.text.empty(), ButtonStyle::Primary);
    if ((enter || ask) && !waiting && !question.text.empty()) {
        ask_ai(ai_chat(l, editor.numbered_text(), question.text), question.text);
        question.text.clear();
    }

    std::string answer;
    if (ai_request >= 0 && ai_poll(ai_request, answer)) {
        chat.push_back({true, answer});
        ai_request = -1;
        chat_stick_bottom = true;
    }
}

void LessonScreen::update_and_draw(App& app) {
    poll_jobs(app);
    const Lesson& l = app.lessons[lesson_index];
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    float m = 12, top = 56;

    // ---- top bar
    if (button({m, 10, 120, 36}, "← Lessons", !busy(), ButtonStyle::Ghost) ||
        (IsKeyPressed(KEY_ESCAPE) && !busy())) {
        close(app);
        app.screen = Screen::Course;
        return;
    }
    std::string title = std::to_string(lesson_index + 1) + ". " + l.title + (l.lang == Lang::C ? "  ·  C" : "  ·  C++");
    draw_text(g_fonts.ui_bold, g_fonts.ui_size + 1, title, m + 136, 18, theme::text);
    if (app.progress.completed.count(l.id))
        draw_text(g_fonts.ui, g_fonts.ui_size, "✓ completed", m + 150 + text_width(g_fonts.ui_bold, g_fonts.ui_size + 1, title),
                  19, theme::success);

    float bx = W - m;
    bool has_next = lesson_index + 1 < (int)app.lessons.size();
    if (just_completed && has_next) {
        bx -= 150;
        if (button({bx, 10, 150, 36}, "Next lesson →", !busy(), ButtonStyle::Primary)) {
            close(app);
            open(app, lesson_index + 1);
            return;
        }
        bx -= 8;
    }
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    bx -= 90;
    if (button({bx, 10, 90, 36}, "Reset", !busy())) {
        editor.set_text(l.starter_code);
        console.clear();
        print("Code reset to the starting template.", theme::text_dim, false);
    }
    bx -= 98;
    if (button({bx, 10, 90, 36}, "Check", !busy(), ButtonStyle::Primary) || (ctrl && IsKeyPressed(KEY_ENTER) && !busy()))
        start_check(app);
    bx -= 98;
    if (button({bx, 10, 90, 36}, "▶ Run", !busy()) || (ctrl && IsKeyPressed(KEY_R) && !busy()))
        start_run(app);

    // ---- layout: theory | editor/console | AI
    float theory_w = std::clamp(W * 0.27f, 280.0f, 420.0f);
    float ai_w = std::clamp(W * 0.25f, 280.0f, 400.0f);
    float mid_x = m + theory_w + m, mid_w = W - mid_x - ai_w - 2 * m;
    float avail_h = H - top - m;
    float console_h = std::max(150.0f, avail_h * 0.30f);

    draw_theory(app, {m, top, theory_w, avail_h});
    editor.update_and_draw({mid_x, top, mid_w, avail_h - console_h - m});
    draw_console({mid_x, top + avail_h - console_h, mid_w, console_h});
    draw_ai(app, {W - ai_w - m, top, ai_w, avail_h});
}
