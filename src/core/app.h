#pragma once
// CodeLens — shared interface between the engine (src/core) and the UI (src/ui).
// The UI calls ONLY these functions. The engine never includes raylib.
// Changes to this file: separate PR, both agree.

#include <cstdint>
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

// One step of a lesson. A topic goes Explore -> Understand (Watch/Predict/Modify/FindBug/Explain) -> Practice.
enum class StepKind { Explore, Watch, Predict, Modify, FindBug, Explain, Practice };

struct Step {
    StepKind kind = StepKind::Practice;
    std::string title;
    std::string text;           // explanation, task or question shown to the student
    std::string code;           // Watch / Predict / Explain: the program to look at
    std::string input;          // stdin for Watch / Predict
    std::string starter;        // Modify / FindBug / Practice: code the student starts with
    std::string solution;       // Modify / FindBug / Practice: reference, never shown
    std::string scene;          // Explore: scene id
    int predict_line = 0;       // Predict: "value of <expr> after line N runs"
    std::string predict_expr;   // Predict: "" = predict the program's output instead
    std::string rubric;         // Explain: key ideas, separated by ';'
    std::string sample_answer;  // Explain: model answer shown after submitting
    std::vector<TestCase> tests;
};

const char* step_kind_name(StepKind k);   // "explore", "watch", ...

struct Lesson {
    std::string id;            // "03_loops" — also the file name
    std::string title;
    std::string topic;
    Lang lang = Lang::C;
    // Practice task (the original lesson format)
    std::string theory;
    std::string starter_code;
    std::string solution;      // reference solution, never shown to the student
    std::vector<TestCase> tests;
    // All steps in order. A lesson without @step sections has one Practice step.
    std::vector<Step> steps;
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

// Explains where two outputs first differ, e.g. missing "." at the end of line 2.
// Ignores trailing spaces and trailing empty lines. "" if equal.
std::string describe_difference(const std::string& expected, const std::string& got);

// ---------------------------------------------------------------- Memory state
// What the memory visualizer draws. Filled by the debugger (real programs)
// or by scene_state (Explore scenes).
struct Variable {
    std::string name;
    std::string type;          // "int", "int *", "int [5]", "struct point"
    std::string value;         // as GDB prints it; "" for arrays/structs (see children)
    uint64_t address = 0;
    bool is_pointer = false;
    uint64_t points_to = 0;    // 0 = NULL
    std::vector<Variable> children;   // array elements ("[0]", "[1]"...) or struct fields
};

struct Frame {
    std::string function;
    int line = 0;
    std::vector<Variable> vars;   // arguments first, then locals
};

struct HeapBlock {
    uint64_t address = 0;
    size_t size = 0;              // bytes
    bool freed = false;
    std::vector<Variable> cells;  // contents when the pointing type is known (e.g. int[3])
};

struct ProgramState {
    std::vector<Frame> stack;     // [0] = main, back() = the current function
    std::vector<HeapBlock> heap;
    std::string output;           // everything the program printed so far
    int line = 0;                 // next line to run (0 = not in the student's file)
    bool finished = false;
    int exit_code = 0;
    std::vector<MemoryError> errors;
    std::string note;             // one sentence: what just happened
};

std::string  state_to_json(const ProgramState& s);
bool         state_from_json(const std::string& json, ProgramState& out, std::string& error);
// Finds what an address points at: "x", "a[2]", "heap block 1", or "" if nothing known.
std::string  state_describe_address(const ProgramState& s, uint64_t address);

// ---------------------------------------------------------------- Debugger
// Step-by-step execution through GDB (C programs). One session at a time.
// Not thread-safe: call from one thread (the UI thread is fine: each call takes milliseconds).
bool debug_start(const std::string& code, Lang lang, const std::string& stdin_text, std::string& error);
bool debug_next();      // run the current line, stepping over function calls
bool debug_step();      // run the current line, entering function calls
bool debug_finish();    // run until the current function returns
bool debug_run_to_line(int line);
std::string debug_eval(const std::string& expr);   // value of a C expression in the current frame, "" on error
const ProgramState& debug_state();
bool debug_active();
void debug_stop();

// ---------------------------------------------------------------- Explore scenes
struct SceneParam { std::string name; int min = 0, max = 0, value = 0; };
struct Scene {
    std::string id, title;
    std::vector<SceneParam> params;      // sliders
    std::vector<std::string> actions;    // buttons, e.g. "p = &a", "free(p)"
};
Scene        scene_get(const std::string& id);   // id "" if unknown
ProgramState scene_state(const Scene& s, const std::vector<std::string>& actions_done);

// ---------------------------------------------------------------- Checking answers
struct Verdict {
    bool correct = false;
    std::string expected;   // the right answer
    std::string feedback;   // short explanation for the student
};
// The right answer is computed by running step.code, never typed by the lesson author.
Verdict check_prediction(const Step& step, Lang lang, const std::string& answer);

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
// last_result: the latest compile errors / test failure / memory error shown to the student ("" if none).
int  ai_hint(const Lesson& lesson, const std::string& code, const std::string& last_result, int level);  // level 1..3
int  ai_chat(const Lesson& lesson, const std::string& code, const std::string& last_result,
             const std::string& question);
bool ai_poll(int request_id, std::string& answer);
