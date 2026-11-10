#pragma once
#include "app.h"
#include "editor.h"
#include "ui.h"

#include <future>
#include <string>
#include <vector>

struct App;

struct ConsoleLine {
    std::string text;
    Color color;
    bool mono = true;
};

struct ChatMessage {
    bool from_ai;
    std::string text;
};

struct LessonScreen {
    int lesson_index = -1;
    CodeEditor editor;

    // console
    std::vector<ConsoleLine> console;
    float console_scroll = 0;

    // run / check in the background
    std::future<RunResult>  run_job;
    std::future<TestResult> check_job;
    bool busy() const { return run_job.valid() || check_job.valid(); }
    std::string last_error;        // what "Explain error" sends to the AI
    bool just_completed = false;

    // AI
    std::vector<ChatMessage> chat;
    float chat_scroll = 0;
    bool chat_stick_bottom = true;
    int ai_request = -1;
    int hint_level = 0;
    TextInput question;

    float theory_scroll = 0;

    void open(App& app, int index);
    void close(App& app);          // saves the draft
    void update_and_draw(App& app);

private:
    void start_run(App& app);
    void start_check(App& app);
    void poll_jobs(App& app);
    void ask_ai(int request_id, const std::string& shown_text);
    void draw_theory(App& app, Rectangle r);
    void draw_console(Rectangle r);
    void draw_ai(App& app, Rectangle r);
    void print(const std::string& text, Color c, bool mono = true);
};

enum class Screen { Course, Lesson };

struct App {
    std::string root;                // folder with lessons/ and assets/
    std::vector<Lesson> lessons;
    Progress progress;
    Screen screen = Screen::Course;
    LessonScreen lesson;
    float course_scroll = 0;

    std::string progress_path() const { return root + "/progress.txt"; }
    std::string draft_path(const Lesson& l) const {
        return root + "/drafts/" + l.id + (l.lang == Lang::C ? ".c" : ".cpp");
    }
};

void draw_course_screen(App& app);
