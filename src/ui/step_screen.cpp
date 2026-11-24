#include "screens.h"
#include "memview.h"
#include "services.h"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <sstream>

namespace {
template <class T> bool ready(const std::future<T> &job) {
    return job.valid() && job.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}
bool editing(StepKind kind) {
    return kind == StepKind::Practice || kind == StepKind::Modify || kind == StepKind::FindBug;
}
std::string name(StepKind kind) {
    std::string s = step_kind_name(kind);
    if (kind == StepKind::FindBug)
        return "Find the bug";
    if (!s.empty())
        s[0] = std::toupper(s[0]);
    return s;
}
// The first line of the step's own text is its instruction; the rest explains it.
std::string first_line(const std::string &text) { return text.substr(0, text.find('\n')); }
std::string rest_of(const std::string &text) {
    size_t nl = text.find('\n');
    if (nl == std::string::npos)
        return "";
    std::string rest = text.substr(nl + 1);
    for (char &c : rest)
        if (c == '\n')
            c = ' ';
    return rest;
}
std::string goal(const Step &step) {
    if (!step.text.empty() && step.kind != StepKind::Practice)
        return first_line(step.text);
    switch (step.kind) {
    case StepKind::Explore:
        return "Try the controls and watch how the picture and code change.";
    case StepKind::Watch:
        return "Choose Next line to run one line. Step into follows a function call.";
    case StepKind::Predict:
        return step.text.substr(0, step.text.find('\n'));
    case StepKind::Order:
        return "Drag the cards into order, then choose Check.";
    case StepKind::Choose:
        return step.text.substr(0, step.text.find('\n'));
    case StepKind::Explain:
        return "Explain the example in your own words, then choose Submit.";
    default:
        return "Edit the code, try Run, then choose Check to test your answer.";
    }
}
void text_card(Rectangle r, const std::string &text, Color color = theme::text, float *offset = nullptr) {
    panel(r);
    auto body = panel_body(r, false);
    auto lines = wrap_text(g_fonts.ui, 17, text, body.width - 6);
    float local = 0;
    float &scroll = offset ? *offset : local;
    scroll_area(body, lines.size() * 22, scroll);
    BeginScissorMode(body.x, body.y, body.width, body.height);
    draw_wrapped(g_fonts.ui, 17, text, {body.x, body.y - scroll, body.width - 8, 0}, color, 5);
    EndScissorMode();
}
void show_code(CodeEditor &code, Rectangle area, int line) {
    panel(area, "Code — hover a word for help");
    code.read_only = true;
    if (code.highlighted_line != line) {
        code.highlighted_line = line;
        code.scroll_y = std::max(0.0f, (line - 4) * 23.0f);
    }
    code.update_and_draw(panel_body(area, true));
}
std::string facts(const Step &step, const ProgramState &state) {
    return "Step: " + name(step.kind) + "\n" + step.text + "\nLast action: " + state.note +
           "\nMeasured state:\n" + state_to_json(state);
}
} // namespace

void StepScreen::shutdown() {
    if (job.valid()) {
        job.wait();
        job.get();
    }
    if (generated_job.valid()) {
        generated_job.wait();
        generated_job.get();
    }
    debug_stop();
}
void StepScreen::open(App &app, int index) {
    if (index < 0 || index >= (int)app.lessons.size())
        return;
    if (loaded && lesson_index >= 0 && editing(app.lessons[lesson_index].steps[step_index].kind))
        app.lesson.close(app);
    shutdown();
    loaded = false;
    app.reconcile_steps();
    lesson_index = index;
    generated_task = false;
    completed.assign(app.lessons[index].steps.size(), false);
    for (size_t i = 0; i < completed.size(); ++i)
        completed[i] = app.step_progress[app.lessons[index].id].count(i) ||
                       app.progress.completed.count(app.lessons[index].id);
    furthest = 0;
    while (furthest < (int)completed.size() && completed[furthest])
        ++furthest;
    furthest = std::min(furthest, (int)completed.size() - 1);
    app.screen = Screen::Lesson;
    enter(app, furthest);
    // first visit of a lesson: start with its explanation
    reading = app.auto_reading && !app.lessons[index].reading.empty() &&
              std::none_of(completed.begin(), completed.end(), [](bool b) { return b; });
    reading_scroll = 0;
}
void StepScreen::enter(App &app, int index) {
    if (job.valid() || generated_job.valid())
        return;
    if (loaded && editing(app.lessons[lesson_index].steps[step_index].kind))
        app.lesson.close(app);
    debug_stop();
    generated_task = false;
    order_cards = {};
    step_index = index;
    loaded = true;
    debugging = notes = model_answer = submitted = explored = revealed = false;
    text_scroll = code_scroll = feedback_scroll = 0;
    answer = {};
    explanation = {};
    verdict = {};
    error.clear();
    state = {};
    picture.reset();
    actions.clear();
    choice = -1;
    explain_request = -1;
    const auto &l = app.lessons[lesson_index];
    focus_answer = l.steps[index].kind == StepKind::Predict;   // ready to type straight away
    const auto &s = l.steps[step_index];
    order = s.kind == StepKind::Order ? order_shuffled(s) : std::vector<int>{};
    code.set_text(!s.code.empty() ? s.code : !s.starter.empty() ? s.starter : l.starter_code);
    code.read_only = true;
    if (s.kind == StepKind::Explore) {
        scene = ui_scene_get(s.scene);
        if (scene.id.empty())
            error = "This scene is not available yet. Ask your tutor for help.";
        else {
            state = ui_scene_state(scene, actions);
            code.set_text(scene.code);
        }
    }
    if (editing(s.kind))
        app.lesson.open_task(app, lesson_index, s, step_index);
    if (s.kind == StepKind::Watch)
        launch_debug(app, "start");
    app.resume_lesson = lesson_index;
    app.resume_step = step_index;
    app.save_steps();
    g_focus = Focus::None;
}
void StepScreen::mark_complete(App &app) {
    if (generated_task)
        return;
    const std::string &id = app.lessons[lesson_index].id;
    bool changed = !completed[step_index];
    bool all_before = std::all_of(completed.begin(), completed.end(), [](bool value) { return value; });
    completed[step_index] = true;
    app.step_progress[id].insert(step_index);
    furthest = std::max(furthest, std::min(step_index + 1, (int)completed.size() - 1));
    bool all = std::all_of(completed.begin(), completed.end(), [](bool value) { return value; });
    bool was_complete = app.progress.completed.count(id);
    if (all)
        app.progress.completed.insert(id);
    else
        app.progress.completed.erase(id);
    if (was_complete != all)
        progress_save(app.progress_path(), app.progress);
    if (changed || was_complete != all)
        app.save_steps();
    // rewards: every new step gives XP; finishing a lesson the first time adds to the streak
    if (changed)
        app.rewards.step_done();
    if (all && !all_before) {   // (Practice may already have marked the lesson complete itself)
        app.celebration_xp = app.rewards.lesson_done() + 10;
        if (app.guides)
            app.celebration = app.lessons[lesson_index].title;
    }
}
void StepScreen::launch_debug(App &app, const std::string &operation) {
    if (job.valid())
        return;
    const auto &l = app.lessons[lesson_index];
    const auto &s = l.steps[step_index];
    std::string source = debugging ? app.lesson.editor.text() : s.code;
    std::string input = s.input;
    if (debugging && !app.lesson.task.tests.empty())
        input = app.lesson.task.tests.front().input;
    Lang lang = l.lang;
    const bool lists = l.topic == "lists";
    error.clear();
    if (operation == "start")
        picture.reset();
    job = std::async(std::launch::async, [source, input, lang, operation, lists] {
        Result r;
        bool success = false;
        if (operation == "start")
            success = debug_start(source, lang, input, r.error);
        else if (operation == "into")
            success = debug_step();
        else if (operation == "out")
            success = debug_finish();
        else
            success = debug_next();
        r.state = lists ? list_debug_snapshot() : debug_state();
        if (!success && r.error.empty() && !r.state.finished)
            r.error = "The debugger could not complete this action. Choose Restart to try again.";
        return r;
    });
}

void StepScreen::update_and_draw(App &app) {
    if (lesson_index < 0 || lesson_index >= (int)app.lessons.size() ||
        app.lessons[lesson_index].steps.empty()) {
        app.screen = Screen::Course;
        return;
    }
    const Lesson &l = app.lessons[lesson_index];
    const Step &s = l.steps[step_index];
    if (ready(job)) {
        auto result = job.get();
        state = result.state;
        error = result.error;
        if (result.has_verdict) {
            verdict = result.verdict;
            submitted = true;
            if (verdict.correct)
                mark_complete(app);
        }
    }
    if (ready(generated_job)) {
        auto task = generated_job.get();
        if (!task.id.empty()) {
            app.lesson.close(app);
            app.lesson.open(app, lesson_index);
            app.lesson.task = task;
            app.lesson.draft_file = app.root + "/drafts/" + task.id + (task.lang == Lang::C ? ".c" : ".cpp");
            app.lesson.marks_lesson_complete = false;
            app.lesson.editor.set_text(task.starter_code);
            generated_task = true;
            generated_status =
                "New task checked against its reference solution. Write your answer, then Check.";
        } else
            generated_status = "The tutor's task could not be verified. It was not opened. Try again.";
    }
    if (explain_request >= 0) {
        std::string response;
        if (ai_poll(explain_request, response)) {
            verdict.feedback = tutor_message(response);
            explain_request = -1;
        }
    }
    app_busy = editing(s.kind) && app.lesson.busy();
    bool waiting = busy();
    const bool navigation = !waiting && !order_cards.active();
    float w = GetScreenWidth(), h = GetScreenHeight(), m = 24;
    if (reading) {   // "Read the lesson": the topic explained, before or between the steps
        draw_text(g_fonts.ui, 15, "Read the lesson", 32, 22, theme::accent);
        draw_text(g_fonts.title, 28, l.title, 32, 44, theme::text);
        if (button({w - 190, 20, 158, 36}, "Ask the tutor", true, ButtonStyle::Soft))
            app.tutor.open(l, "", "The student is reading the lesson explanation:\n" + l.reading);
        float cw = std::min(980.0f, w - 64), cx = (w - cw) / 2;
        Rectangle page{cx, 96, cw, h - 180};
        DrawRectangleRounded(page, 0.02f, 8, theme::panel);
        DrawRectangleRoundedLinesEx(page, 0.02f, 8, 1, theme::border);
        draw_rich_text({page.x + 32, page.y + 24, page.width - 52, page.height - 40}, l.reading, reading_scroll);
        if (button({w - 32 - 230, h - 63, 230, 40},
                   std::none_of(completed.begin(), completed.end(), [](bool b) { return b; }) ? "Start the steps →"
                                                                                           : "Back to the step",
                   true, ButtonStyle::Primary) ||
            (IsKeyPressed(KEY_ESCAPE) && !app.tutor.visible)) {
            reading = false;
            reading_scroll = 0;
        }
        if (button({32, h - 63, 160, 40}, "← Lessons", true, ButtonStyle::Ghost)) {
            reading = false;
            app.screen = Screen::Course;
        }
        return;
    }
    if (notes) {
        draw_text(g_fonts.title, 27, "Lesson notes — " + l.title, 32, 26, theme::text);
        std::string text;
        for (const auto &step : l.steps)
            if (!editing(step.kind))
                text += name(step.kind) + ": " + step.title + "\n" + step.text + "\n\n";
        text += l.theory;
        {   // same formatting as the lesson page: headings, code, lists, **bold**
            Rectangle page{32, 80, w - 64, h - 160};
            DrawRectangleRounded(page, 0.02f, 8, theme::panel);
            DrawRectangleRoundedLinesEx(page, 0.02f, 8, 1, theme::border);
            std::string rich;
            for (const auto &step : l.steps)
                if (!editing(step.kind))
                    rich += "## " + name(step.kind) + ": " + step.title + "\n" + step.text + "\n\n";
            rich += "## Practice\n" + l.theory;
            draw_rich_text({page.x + 24, page.y + 18, page.width - 40, page.height - 30}, rich, text_scroll);
        }
        if (!l.reading.empty() && button({w - 420, h - 62, 188, 40}, "Read the lesson", true)) {
            notes = false;
            reading = true;
            reading_scroll = 0;
            return;
        }
        if (button({w - 220, h - 62, 188, 40}, "Back to my code", true, ButtonStyle::Primary)) {
            notes = false;
            text_scroll = 0;
        }
        if (button({w - 190, 20, 158, 36}, "Ask the tutor", true, ButtonStyle::Soft))
            app.tutor.open(l, app.lesson.editor.text(), "Lesson notes:\n" + text);
        return;
    }
    if (button({m, 18, 120, 36}, "← Lessons", navigation, ButtonStyle::Ghost) ||
        (IsKeyPressed(KEY_ESCAPE) && !waiting && !order_cards.active() &&
         !(g_focus == Focus::Editor &&
           ((editing(s.kind) && !debugging ? app.lesson.editor.has_selection() : code.has_selection()))))) {
        if (editing(s.kind))
            app.lesson.close(app);
        debug_stop();
        app.screen = Screen::Course;
        return;
    }
    draw_text(g_fonts.ui_bold, 20, fit_text(g_fonts.ui_bold, 20, l.title, w - 860), m + 134, 25, theme::text);
    float spacing = std::min(23.0f, 240.0f / l.steps.size()), start = w - 420 - spacing * l.steps.size();
    for (size_t i = 0; i < l.steps.size(); ++i) {
        Vector2 p{start + i * spacing, 36};
        DrawCircleV(p, 5,
                    (int)i == step_index ? theme::accent
                    : completed[i]       ? theme::success
                                         : theme::border);
        if (navigation && (completed[i] || (int)i <= furthest) &&
            CheckCollisionPointRec(GetMousePosition(), {p.x - 9, p.y - 12, 18, 24})) {
            ui_cursor_hand();
            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && (int)i != step_index) {
                enter(app, i);
                return;
            }
        }
    }
    if (!l.reading.empty() &&
        button({w - m - 164 - 12 - 190, 18, 190, 36}, "Read the lesson", navigation, ButtonStyle::Ghost)) {
        reading = true;
        reading_scroll = 0;
        return;
    }
    if (button({w - m - 164, 18, 164, 36}, "Ask the tutor", navigation, ButtonStyle::Soft))
        app.tutor.open(generated_task ? app.lesson.task : l,
                       editing(s.kind) ? app.lesson.editor.numbered_text() : code.numbered_text(),
                       facts(s, state) +
                           (editing(s.kind) ? "\nLatest result: " + app.lesson.last_error : ""));
    // instruction card: tinted so the "what to do now" sentence stands out
    DrawRectangleRounded({m, 70, w - 2 * m, 125}, 0.08f, 8, theme::accent_soft);
    DrawRectangleRoundedLinesEx({m, 70, w - 2 * m, 125}, 0.08f, 8, 1, theme::accent_dk);
    draw_text(g_fonts.ui, 15,
              "Step " + std::to_string(step_index + 1) + " of " + std::to_string(l.steps.size()) + " · " +
                  name(s.kind),
              m + 16, 82, theme::accent);
    draw_wrapped(g_fonts.ui_bold, 18, goal(s), {m + 16, 107, w - 2 * m - 32, 50}, theme::text);
    // Under the instruction: the lesson's explanation for this step, else a short usage hint.
    std::string terms = s.kind == StepKind::Practice ? "" : rest_of(s.text);
    if (terms.empty())
        terms = s.kind == StepKind::Watch
                    ? "Next line runs the highlighted instruction. Restart starts this example again."
                : s.kind == StepKind::Order
                    ? "Drag a card to move it, or use the arrows. Check tests the whole order."
                : editing(s.kind) && s.edit_from > 0 && !generated_task
                    ? "Dim lines are fixed. Edit the brighter lines; the editable area can grow."
                    : "";
    if (!terms.empty()) {
        auto lines = wrap_text(g_fonts.ui, 14, terms, w - 2 * m - 32);
        for (size_t i = 0; i < lines.size() && i < 2; ++i) {
            std::string line = lines[i];
            if (i == 1 && lines.size() > 2)
                line = fit_text(g_fonts.ui, 14, line + " " + lines[2], w - 2 * m - 32);
            draw_text(g_fonts.ui, 14, line, m + 16, 150 + i * 19.0f, theme::text_dim);
        }
    }
    bool write = editing(s.kind) && !debugging;
    const bool guess = s.kind == StepKind::Predict || s.kind == StepKind::Choose || s.kind == StepKind::Order;
    const bool try_again = guess && submitted && !verdict.correct && !revealed;
    std::string feedback = !error.empty() ? error
                           : !submitted   ? ""
                           : try_again && s.kind == StepKind::Predict
                               ? "Not quite. Read the code once more, line by line, and try again. Stuck? Press Show answer."
                           : try_again && s.kind == StepKind::Choose
                               ? "Not quite. Try another option, or press Show answer."
                           : revealed && s.kind == StepKind::Predict && !verdict.correct
                               ? "The answer is: " + verdict.expected + "\nType it in and press Check to go on.\n\n" +
                                     verdict.feedback
                               : verdict.feedback;
    float feedback_height =
        feedback.empty() || write
            ? 0
            : std::min(108.0f, 20.0f + 22.0f * wrap_text(g_fonts.ui, 17, feedback, w - 2 * m - 40).size());
    Rectangle body{m, 211, w - 2 * m, h - 305 - (feedback_height ? feedback_height + 10 : 0)};
    if (write) {
        Rectangle taskarea = body;
        if (button({body.x, body.y, 145, 34}, "Lesson notes", !waiting, ButtonStyle::Ghost)) {
            notes = true;
            text_scroll = 0;
            return;
        }
        if (button({body.x + 156, body.y, 145, 34}, "Debug my code", !waiting, ButtonStyle::Ghost)) {
            debugging = true;
            code.set_text(app.lesson.editor.text());
            launch_debug(app, "start");
            return;
        }

        app.lesson.update_and_draw(app, taskarea);
        if (app.lesson.just_completed && !generated_task)
            mark_complete(app);
    } else if (s.kind == StepKind::Order) {
        Rectangle cards = body;
        cards.width = std::min(880.0f, body.width);
        cards.x += (body.width - cards.width) / 2;
        if (order_cards.update_and_draw(cards, s.lines, order, submitted, !waiting, text_scroll)) {
            submitted = false;
            verdict = {};
        }
    } else if (s.kind == StepKind::Choose) {
        float left = body.width * .43f;
        show_code(code, {body.x, body.y, left, body.height}, 0);
        Rectangle options{body.x + left + 18, body.y, body.width - left - 18, body.height};
        scroll_area(options, s.options.size() * 75.0f, text_scroll);
        BeginScissorMode(options.x, options.y, options.width, options.height);
        for (size_t i = 0; i < s.options.size(); ++i) {
            Rectangle r{options.x, options.y + i * 75 - text_scroll, options.width, 62};
            if (submitted && (int)i == choice)
                DrawRectangleRec(r, verdict.correct ? theme::success : theme::danger);
            if (button({r.x + 3, r.y + 3, r.width - 6, r.height - 6},
                       fit_text(g_fonts.mono, 16, s.options[i], r.width - 35), !waiting,
                       choice == (int)i ? ButtonStyle::Primary : ButtonStyle::Normal)) {
                choice = i;
                submitted = false;
                verdict = {};
            }
        }
        EndScissorMode();
    } else if (s.kind == StepKind::Explain) {
        float half = (body.width - 18) / 2;
        Rectangle left{body.x, body.y, half, body.height};
        std::string question =
            s.text + "\n\n" +
            (model_answer
                 ? s.sample_answer
                 : "Use your own words. After submitting, compare your explanation with a model answer.");
        text_card(left, question, theme::text, &text_scroll);
        if (model_answer == false && submitted &&
            button({left.x + 14, left.y + left.height - 48, left.width - 28, 34}, "Show model answer"))
            model_answer = true;
        Rectangle field{body.x + half + 18, body.y, half, body.height};
        panel(field, "Your explanation");
        text_area(panel_body(field, true), explanation, "Why does the code work this way?");
    } else {
        bool exploring = s.kind == StepKind::Explore;
        float controls =
            exploring ? std::min(210.0f, 65.0f + scene.params.size() * 52 + scene.actions.size() / 3 * 38)
            : s.kind == StepKind::Predict ? 120
                                          : 52;
        Rectangle left{body.x, body.y, body.width * .46f, body.height},
            right{body.x + body.width * .46f + 18, body.y, body.width * .54f - 18, body.height};
        float code_h = std::max(80.0f, left.height - controls - 12);
        if (s.kind == StepKind::Predict) {   // no tall empty code box: fit the code, answer right below
            int n = 1 + (int)std::count(s.code.begin(), s.code.end(), '\n');
            code_h = std::min(code_h, n * std::round(g_fonts.mono_size * 1.45f) + 70.0f);
        }
        show_code(code, {left.x, left.y, left.width, code_h},
                  s.kind == StepKind::Predict && !(submitted && (verdict.correct || revealed)) ? s.predict_line
                                                                                             : state.line);
        float y = s.kind == StepKind::Predict ? left.y + code_h + 26 : left.y + left.height - controls;
        if (exploring) {
            draw_text(g_fonts.ui, 14, fit_text(g_fonts.ui, 14, scene.title, left.width), left.x, y,
                      theme::text_dim);
            y += 28;
            bool changed = false;
            for (auto &p : scene.params) {
                if (slider({left.x, y, left.width, 37}, p.name, p.value, p.min, p.max))
                    changed = true;
                y += 50;
            }
            for (size_t i = 0; i < scene.actions.size(); ++i) {
                float bw = (left.width - 16) / 3;
                Rectangle r{left.x + (i % 3) * (bw + 8), y + (i / 3) * 39, bw, 32};
                if (button(r, scene.actions[i], !waiting)) {
                    if (scene.actions[i] == "Reset")
                        actions.clear();
                    else {
                        actions.push_back(scene.actions[i]);
                        if (actions.size() > 100)
                            actions.erase(actions.begin());
                    }
                    changed = true;
                }
            }
            if (changed) {
                state = ui_scene_state(scene, actions);
                code.set_text(scene.code);
                explored = true;
                mark_complete(app);
            }
        } else if (s.kind == StepKind::Predict) {
            Rectangle card{left.x, y - 10, left.width, 112};
            Color edge = submitted ? (verdict.correct ? theme::success : theme::warning) : theme::accent;
            DrawRectangleRounded(card, 0.12f, 8, theme::accent_soft);
            DrawRectangleRoundedLinesEx(card, 0.12f, 8, 2, edge);
            draw_text(g_fonts.ui_bold, 16,
                      s.predict_expr.empty() ? "Your answer: what will the program print?"
                                             : "Your answer: what is " + s.predict_expr + "?",
                      card.x + 16, card.y + 14, theme::text);
            std::string previous_answer = answer.text;
            if (focus_answer && !app.tutor.visible) {
                answer.focused = true;
                g_focus = Focus::Input;
                focus_answer = false;
            }
            text_input({card.x + 16, card.y + 46, card.width - 32, 48}, answer,
                       s.predict_expr.empty() ? "Type the printed text. Several lines? Put a space between them"
                                              : "Type a value here, then press Check");
            if (answer.text != previous_answer) {
                submitted = false;
                verdict = {};
            }
            if (submitted && (verdict.correct || revealed))
                draw_text(g_fonts.ui, 14, "The picture shows what the program really did.", left.x, y + 114,
                          theme::text_dim);
        } else {
            float control_x = left.x;
            const bool functions = s.show >= ShowLevel::Frames;
            if (functions) {
                if (button({control_x, y, 110, 34}, "Step into", !waiting && !state.finished))
                    launch_debug(app, "into");
                if (button({control_x + 118, y, 110, 34}, "Step out", !waiting && !state.finished))
                    launch_debug(app, "out");
                control_x += 236;
            }
            if (button({control_x, y, 100, 34}, "Restart", !waiting))
                launch_debug(app, "start");
            if (debugging && button({control_x + 108, y, 135, 34}, "Back to code", !waiting)) {
                debugging = false;
                debug_stop();
                return;
            }
            if (!waiting && !state.finished) {
                if (IsKeyPressed(KEY_F10))
                    launch_debug(app, "next");
                if (functions && IsKeyPressed(KEY_F11))
                    launch_debug(app,
                                 IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT) ? "out" : "into");
            }
        }
        bool show_picture = exploring || s.kind == StepKind::Watch || debugging ||
                            (submitted && (verdict.correct || revealed || s.kind != StepKind::Predict));
        if (show_picture && error.empty()) {
            picture.update_and_draw(state, right, s.show, l.topic, waiting,
                                    s.kind == StepKind::Predict ? s.predict_expr : "");
            if (!state.note.empty() &&
                button(ProgramView::why_button(right), "Why?", !waiting, ButtonStyle::Ghost)) {
                app.tutor.open(l, code.numbered_text(), facts(s, state));
                app.tutor.request = ai_chat(
                    l, code.numbered_text(), facts(s, state),
                    "Explain why the last action produced this measured result. Do not invent values.");
            }
        } else
        {
            if (!error.empty())
                text_card(right, error, theme::text_dim);
            else {   // placeholder: centred, not a lonely line at the top of an empty box
                panel(right);
                std::string head = submitted ? "Not yet" : "Your prediction first";
                std::string body = submitted ? "When your answer is right, or you press Show answer, this picture "
                                               "shows what the program really did."
                                             : "After Check, this picture shows what the program really did, "
                                               "line by line.";
                float tw = std::min(right.width - 80, 520.0f);
                auto lines = wrap_text(g_fonts.ui, 17, body, tw);
                float total = 34 + lines.size() * 26.0f;
                float ty = right.y + (right.height - total) / 2;
                float hw = text_width(g_fonts.ui_bold, 22, head);
                draw_text(g_fonts.ui_bold, 22, head, right.x + (right.width - hw) / 2, ty, theme::text);
                ty += 40;
                for (const auto &line : lines) {
                    float lw = text_width(g_fonts.ui, 17, line);
                    draw_text(g_fonts.ui, 17, line, right.x + (right.width - lw) / 2, ty, theme::text_dim);
                    ty += 26;
                }
            }
        }
    }
    waiting = busy();
    if (feedback_height) {
        Color color = !error.empty()                ? theme::danger
                      : verdict.correct             ? theme::success
                      : s.kind == StepKind::Explain ? theme::text
                                                    : theme::warning;
        text_card({m, h - 94 - feedback_height, w - 2 * m, feedback_height}, feedback, color,
                  &feedback_scroll);
    }
    if (app.guides) {   // Bit the raccoon cheers and gives one small tip at a time
        static const char *praise[] = {"Great job! That's exactly right.", "Nice! You're getting the hang of it.",
                                       "Yes! Keep that streak going."};
        std::string tip;
        int mood = 0;
        bool right = submitted && verdict.correct;
        if (right || (write && app.lesson.just_completed)) {
            tip = praise[step_index % 3];
            mood = 1;
        } else if (try_again)
            tip = "Almost! Look again, line by line. Stuck? Show answer is right there.";
        else if (write)
            tip = "Run often. An error is a clue, not a failure. Ask me if you get stuck!";
        else if (s.kind == StepKind::Watch)
            tip = state.finished ? "Done! Press Next when you're ready." : "Press Next line and watch the picture change.";
        else if (s.kind == StepKind::Explore)
            tip = "Play with it! Nothing can break here.";
        else if (s.kind == StepKind::Predict)
            tip = "Think first, then type. Read the code from the top.";
        else if (s.kind == StepKind::Choose)
            tip = "Read every option before you pick one.";
        else if (s.kind == StepKind::Order)
            tip = "What has to happen first? Start there.";
        else if (s.kind == StepKind::Explain) {
            tip = "Use your own words. Two short sentences are enough.";
            mood = 2;
        }
        float room = (w - m - 185 - 12 - 165 - 24) - (m + 118 + 56);
        if (!tip.empty() && room > 220)
            mascot_say(m + 118, h - 43, 20, tip, mood, std::min(560.0f, room));
    }
    if (button({m, h - 63, 100, 40}, "← Back", step_index > 0 && navigation, ButtonStyle::Ghost)) {
        enter(app, step_index - 1);
        return;
    }
    if (try_again && button({w - m - 185 - 12 - 165, h - 63, 165, 40}, "Show answer", navigation)) {
        revealed = true;
        if (s.kind == StepKind::Order) {   // put the cards in the right order; Check then passes
            for (size_t i = 0; i < order.size(); ++i) order[i] = (int)i;
            submitted = false;
            verdict = {};
            verdict.feedback = "";
        }
    }
    bool passed = write ? app.lesson.just_completed : verdict.correct;
    bool watch = (s.kind == StepKind::Watch || debugging);
    if (watch && state.finished && error.empty() && !debugging) {
        mark_complete(app);
        passed = true;
    }
    std::string label = passed ? "Next →" : "Check";
    if (s.kind == StepKind::Explore)
        label = "Next →";
    if (s.kind == StepKind::Explain)
        label = submitted ? "Next →" : "Submit";
    if (watch && !state.finished)
        label = "Next line";
    if (watch && state.finished && debugging)
        label = "Back to code";
    if (passed && step_index + 1 == (int)l.steps.size())
        label = "Finish lesson";
    bool enabled = !waiting && !order_cards.active() && !order_cards.block_release;
    if (s.kind == StepKind::Explore)
        enabled = enabled && (explored || completed[step_index]);
    if (s.kind == StepKind::Predict && !passed)
        enabled = enabled && !answer.text.empty();
    if (s.kind == StepKind::Choose && !passed)
        enabled = enabled && choice >= 0;
    if (s.kind == StepKind::Explain && !submitted)
        enabled = enabled && explanation.text.size() >= 15;
    if (watch && !error.empty())
        enabled = false;
    if (button({w - m - 185, h - 63, 185, 40}, label, enabled, ButtonStyle::Primary)) {
        if (watch && debugging && state.finished) {
            debugging = false;
            debug_stop();
            return;
        }
        if (watch && !state.finished) {
            launch_debug(app, "next");
            return;
        }
        if (passed || (s.kind == StepKind::Explain && submitted) || s.kind == StepKind::Explore) {
            mark_complete(app);
            if (step_index + 1 < (int)l.steps.size()) {
                enter(app, step_index + 1);
                return;
            }
            app.lesson.close(app);
            debug_stop();
            app.screen = Screen::Course;
        } else if (write)
            app.lesson.check(app);
        else if (s.kind == StepKind::Choose) {
            verdict = check_choice(s, choice);
            submitted = true;
            if (verdict.correct)
                mark_complete(app);
        } else if (s.kind == StepKind::Order) {
            verdict.correct = check_order(s, order);
            verdict.feedback = verdict.correct ? "The lines are in the right order."
                                               : "The red cards are out of place. Move them and try again.";
            submitted = true;
            if (verdict.correct)
                mark_complete(app);
        } else if (s.kind == StepKind::Predict) {
            Step copy = s;
            Lang lang = l.lang;
            std::string value = answer.text;
            const bool lists = l.topic == "lists";
            job = std::async(std::launch::async, [copy, lang, value, lists] {
                Result r;
                r.verdict = check_prediction(copy, lang, value);
                r.has_verdict = true;
                if (debug_start(copy.code, lang, copy.input, r.error)) {
                    if (copy.predict_line) {
                        if (debug_state().line != copy.predict_line)
                            debug_run_to_line(copy.predict_line);
                        debug_next();
                    } else {
                        for (int i = 0; i < 2000 && debug_active(); ++i)
                            debug_next();
                    }
                    r.state = lists ? list_debug_snapshot() : debug_state();
                    debug_stop();
                }
                return r;
            });
        } else if (s.kind == StepKind::Explain) {
            submitted = true;
            verdict.correct = false;
            verdict.feedback = "Compare your explanation with the model answer. Key ideas: " + s.rubric;
            if (ai_available())
                explain_request = ai_chat(
                    l, s.code,
                    "Question: " + s.text + "\nKey ideas: " + s.rubric + "\nReference: " + s.sample_answer,
                    "Give brief feedback on this explanation. Do not claim a numerical grade.\n" +
                        explanation.text);
        }
    }
    if (write && app.lesson.just_completed) {
        if (button({w - m - 385, h - 63, 185, 40}, "More practice", !waiting && ai_available())) {
            Lesson original = l;
            generated_job = std::async(std::launch::async, [original] {
                std::string error;
                return generate_practice(original, error);
            });
            generated_status = "Writing a new task and checking its reference answer…";
        }
    }
    if (!generated_status.empty())
        draw_text(g_fonts.ui, 14, fit_text(g_fonts.ui, 14, generated_status, w - 2 * m - 210), m, h - 94,
                  theme::text_dim);
}
