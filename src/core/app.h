#pragma once
// CodeLens — shared interface between the engine (src/core) and the UI (src/ui).
// The UI calls ONLY these functions. The engine never includes raylib.
// Changes to this file: separate PR, both agree.

#include <string>
#include <vector>
#include <set>

// ---------------------------------------------------------------- Run
enum class Lang { C, CPP };

struct MemoryError {
    std::string kind;      // "heap-buffer-overflow", "memory-leak", "runtime-error", ...
    int line = 0;          // line in the student's file, 0 if unknown
    std::string details;   // one human-readable line
};

struct RunResult {
    bool compiled = false;
    std::string compile_errors;      // raw compiler output
    std::vector<int> error_lines;    // lines with compile errors (for highlighting)
    std::string output;              // stdout
    std::string stderr_text;         // stderr (sanitizer reports etc.)
    int  exit_code = 0;
    bool timed_out = false;
    bool crashed = false;            // killed by a signal (segfault, abort)
    std::vector<MemoryError> memory_errors;
};

// Compiles with -g -O0 and AddressSanitizer/UBSan, runs with a time limit.
RunResult run_code(const std::string& code, const std::string& stdin_text,
                   Lang lang, int timeout_ms = 3000);

// ---------------------------------------------------------------- Lessons
struct TestCase {
    std::string input;
    std::string expected;
    bool hidden = false;
};

struct Lesson {
    std::string id;            // "03_loops" — also the file name
    std::string title;
    std::string topic;
    Lang lang = Lang::C;
    std::string theory;
    std::string starter_code;
    std::string solution;      // reference solution, never shown to the student
    std::vector<TestCase> tests;
};

std::vector<Lesson> lessons_load(const std::string& dir);   // sorted by id
bool lesson_parse(const std::string& text, Lesson& out, std::string& error);

// ---------------------------------------------------------------- Tests
struct TestResult {
    int passed = 0;
    int total = 0;
    std::string first_failure;   // message for the student, empty if all passed
    RunResult first_failed_run;  // the run that failed (for errors/highlighting)
};

TestResult check_code(const Lesson& lesson, const std::string& code);

// ---------------------------------------------------------------- Progress
struct Progress {
    std::set<std::string> completed;   // lesson ids
};
Progress progress_load(const std::string& path);
void     progress_save(const std::string& path, const Progress& p);

// ---------------------------------------------------------------- AI
// All AI calls are asynchronous: they return a request id immediately.
// Call ai_poll(id, answer) every frame; it returns true once the answer is ready.
void ai_init(const std::string& host = "127.0.0.1", int port = 11434,
             const std::string& model = "");   // "" -> $CODELENS_MODEL or default
bool ai_available();                           // is Ollama reachable? (cached, cheap)
std::string ai_model();

int  ai_explain_error(const std::string& code, const std::string& errors);
int  ai_hint(const Lesson& lesson, const std::string& code, int level);   // level 1..3
int  ai_chat(const Lesson& lesson, const std::string& code, const std::string& question);
bool ai_poll(int request_id, std::string& answer);
