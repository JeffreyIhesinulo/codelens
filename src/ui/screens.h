#pragma once
#include "app.h"
#include "editor.h"
#include "order_cards.h"
#include "program_view.h"
#include "ui.h"
#include "tutor.h"
#include "mascot.h"

#include <future>
#include <map>
#include <string>
#include <vector>

struct App;

struct ConsoleLine {
    std::string text;
    Color color;
    bool mono = true;
};

struct LessonScreen {
    int lesson_index = -1;
    Lesson task;
    std::string draft_file;
    bool marks_lesson_complete = true;
    int edit_from = 0, edit_to = 0;
    std::string details;
    bool show_details = false;
    CodeEditor editor;

    // console
    std::vector<ConsoleLine> console;
    float console_scroll = 0;

    // run / check in the background
    std::future<RunResult> run_job;
    std::future<TestResult> check_job;
    bool busy() const { return run_job.valid() || check_job.valid(); }
    std::string last_error; // what "Explain error" sends to the AI
    bool just_completed = false;

    // AI
    std::vector<ChatMessage> chat;
    float chat_scroll = 0;
    bool chat_stick_bottom = true;
    int ai_request = -1;
    int hint_level = 0;
    TextInput question;

    float theory_scroll = 0;

    void open(App &app, int index);
    void open_task(App &app, int index, const Step &step, int step_number);
    void close(App &app); // saves the draft
    void update_and_draw(App &app, Rectangle area);
    void check(App &app);

  private:
    void start_run(App &app);
    void start_check(App &app);
    void poll_jobs(App &app);
    void ask_ai(int request_id, const std::string &shown_text);
    void draw_theory(App &app, Rectangle r);
    void draw_console(Rectangle r);
    void draw_ai(App &app, Rectangle r);
    void print(const std::string &text, Color c, bool mono = true);
};

struct StepScreen {
    int lesson_index = -1;
    int step_index = 0;
    int furthest = 0;
    float text_scroll = 0;
    float code_scroll = 0;
    float feedback_scroll = 0;
    std::vector<bool> completed;
    CodeEditor code;
    TextInput answer;
    TextArea explanation;
    Verdict verdict;
    std::vector<int> order;
    OrderCards order_cards;
    int choice = -1;
    bool submitted = false, model_answer = false, debugging = false, notes = false;
    bool revealed = false;   // the student pressed Show answer after a wrong try
    bool focus_answer = false;
    bool reading = false;   // "Read the lesson" page is open
    float reading_scroll = 0;
    bool loaded = false, explored = false;
    Scene scene;
    std::vector<std::string> actions;
    ProgramState state;
    ProgramView picture;
    struct Result {
        ProgramState state;
        Verdict verdict;
        std::string error;
        bool has_verdict = false;
    };
    std::future<Result> job;
    std::string error, generated_status;
    int explain_request = -1;
    std::future<Lesson> generated_job;
    bool generated_task = false;
    bool busy() const { return job.valid() || generated_job.valid() || app_busy; }
    bool app_busy = false;

    void open(App &app, int index);
    void enter(App &app, int index);
    void mark_complete(App &app);
    void launch_debug(App &app, const std::string &operation);
    void shutdown();
    void update_and_draw(App &app);
};

enum class Screen { Welcome, Course, Lesson, AiSetup };

struct App {
    std::string root; // folder with lessons/ and assets/
    std::vector<Lesson> lessons;
    Progress progress;
    Screen screen = Screen::Course;
    LessonScreen lesson;
    StepScreen steps;
    TutorPanel tutor;
    float course_scroll = 0;
    std::string welcome_error;
    std::map<std::string, std::set<int>> step_progress;
    std::map<std::string, std::vector<std::string>> saved_step_keys;
    int resume_lesson = 0, resume_step = 0;
    bool ai_setup_checked = false;
    bool auto_reading = false;   // open "Read the lesson" when a lesson is started for the first time
    bool guides = false;         // Bit the raccoon: tips, the tour and the lesson-complete card (off in tests)
    Rewards rewards;
    int tour_step = -1;          // first-run tour on the course screen; -1 = not showing
    std::string celebration;     // title of the lesson just finished: show the reward card
    int celebration_xp = 0;

    std::string progress_path() const { return root + "/progress.txt"; }
    std::string welcome_path() const { return root + "/welcome.txt"; }
    void load_steps();
    void reconcile_steps();
    void save_steps() const;
    std::string draft_path(const Lesson &l) const {
        return root + "/drafts/" + l.id + (l.lang == Lang::C ? ".c" : ".cpp");
    }
};

void draw_course_screen(App &app);
void draw_welcome_screen(App &app);
void draw_celebration(App &app);   // "Lesson complete" card with XP and streak
void draw_logo(float cx, float cy, float r, float alpha = 1);
void draw_wordmark(float cx, float y, float size, float alpha = 1);
bool draw_splash_screen(float t);   // true when finished
void draw_ai_setup_screen(App &app);
