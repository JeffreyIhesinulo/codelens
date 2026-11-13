// Lesson files (lessons/*.lesson) use a simple section format, so code can be
// written as-is without JSON escaping. Lines starting with "#!" are comments.
//
// Lesson level (the Practice task — the original format, still valid on its own):
//   @id 03_loops   @title Loops   @topic for loops   @lang c|cpp
//   @theory / @starter / @solution   blocks
//   @test or @hidden_test, then @input / @expected blocks
//
// Steps (optional, in order). Each starts with "@step <kind>" and owns the sections after it:
//   @step explore    @scene <id>                      @text
//   @step watch      @code  [@input]                  @text
//   @step predict    @code  [@input]  @line N  @expr x   @text (the question)
//                    (no @expr = predict the program's output)
//   @step modify     @starter  @solution  @test...    @text (the task)
//   @step find_bug   @starter  @solution  @test...    @text
//   @step explain    [@code]  @rubric (ideas; separated by ';')  @answer (model answer)  @text
//   @step practice   uses the lesson-level task unless it has its own sections
// A lesson without "@step practice" but with lesson-level tests gets a Practice step at the end.
// Inside a step, @input before any @test is the program's stdin.
#include "app.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <sstream>

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Block text: drop leading/trailing empty lines but keep indentation.
static std::string block(const std::string& s) {
    size_t a = 0;
    while (a < s.size()) {
        size_t nl = s.find('\n', a);
        if (nl == std::string::npos) break;
        if (trim(s.substr(a, nl - a)).empty()) a = nl + 1; else break;
    }
    size_t b = s.size();
    while (b > a && (s[b - 1] == '\n' || s[b - 1] == ' ' || s[b - 1] == '\r' || s[b - 1] == '\t')) --b;
    return s.substr(a, b - a);
}

const char* step_kind_name(StepKind k) {
    switch (k) {
        case StepKind::Explore:  return "explore";
        case StepKind::Watch:    return "watch";
        case StepKind::Predict:  return "predict";
        case StepKind::Modify:   return "modify";
        case StepKind::FindBug:  return "find_bug";
        case StepKind::Explain:  return "explain";
        case StepKind::Practice: return "practice";
    }
    return "?";
}

static bool parse_kind(const std::string& s, StepKind& k) {
    for (StepKind c : {StepKind::Explore, StepKind::Watch, StepKind::Predict, StepKind::Modify,
                       StepKind::FindBug, StepKind::Explain, StepKind::Practice})
        if (s == step_kind_name(c)) { k = c; return true; }
    return false;
}

// Checks that a step has what its kind needs. Returns "" if fine.
static std::string validate(const Step& s) {
    switch (s.kind) {
        case StepKind::Explore:  return s.scene.empty() ? "needs @scene" : "";
        case StepKind::Watch:    return s.code.empty() ? "needs @code" : "";
        case StepKind::Predict:
            if (s.code.empty()) return "needs @code";
            if (!s.predict_expr.empty() && s.predict_line <= 0) return "@expr needs @line";
            return "";
        case StepKind::Modify:
        case StepKind::FindBug:
            if (s.starter.empty()) return "needs @starter";
            return s.tests.empty() ? "needs at least one @test" : "";
        case StepKind::Practice:
            return s.tests.empty() ? "needs at least one @test" : "";
        case StepKind::Explain:  return s.text.empty() ? "needs @text (the question)" : "";
    }
    return "";
}

bool lesson_parse(const std::string& text, Lesson& out, std::string& error) {
    out = Lesson{};
    std::istringstream in(text);
    std::string line, section, body;
    int line_no = 0;
    int step = -1;                 // index into out.steps, -1 = lesson level
    int test = -1;                 // index of the current test in the current scope
    bool has_practice_step = false;

    auto tests = [&]() -> std::vector<TestCase>& { return step < 0 ? out.tests : out.steps[step].tests; };
    auto fail = [&](const std::string& msg) { error = "line " + std::to_string(line_no) + ": " + msg; return false; };

    auto flush = [&]() -> bool {
        std::string v = block(body);
        body.clear();
        if (section.empty()) return true;
        std::string code_v = v.empty() ? "" : v + "\n";
        if (section == "input" || section == "expected") {
            if (test >= 0) {
                (section == "input" ? tests()[test].input : tests()[test].expected) = code_v;
            } else if (section == "input" && step >= 0) {
                out.steps[step].input = code_v;   // stdin for Watch / Predict
            } else {
                return fail("@" + section + " before @test");
            }
        } else if (step < 0) {
            if      (section == "theory")   out.theory = v;
            else if (section == "starter")  out.starter_code = code_v;
            else if (section == "solution") out.solution = code_v;
        } else {
            Step& s = out.steps[step];
            if      (section == "text" || section == "theory") s.text = v;
            else if (section == "code")     s.code = code_v;
            else if (section == "starter")  s.starter = code_v;
            else if (section == "solution") s.solution = code_v;
            else if (section == "rubric")   s.rubric = v;
            else if (section == "answer")   s.sample_answer = v;
        }
        return true;
    };

    while (std::getline(in, line)) {
        ++line_no;
        if (line.rfind("#!", 0) == 0) continue;
        if (line.empty() || line[0] != '@') {
            if (!section.empty()) body += line + "\n";
            continue;
        }
        if (!flush()) return false;
        section.clear();
        std::string key = line.substr(1), value;
        size_t sp = key.find_first_of(" \t");
        if (sp != std::string::npos) { value = trim(key.substr(sp)); key = key.substr(0, sp); }
        // inline comment after a value: "@scene array   # sliders for size"
        size_t hash = value.find(" #");
        if (hash != std::string::npos) value = trim(value.substr(0, hash));

        if (key == "step") {
            Step s;
            if (!parse_kind(value, s.kind)) return fail("unknown step kind \"" + value + "\"");
            has_practice_step |= s.kind == StepKind::Practice;
            out.steps.push_back(s);
            step = (int)out.steps.size() - 1;
            test = -1;
        }
        else if (key == "id" && step < 0)    out.id = value;
        else if (key == "topic" && step < 0) out.topic = value;
        else if (key == "title") (step < 0 ? out.title : out.steps[step].title) = value;
        else if (key == "lang" && step < 0) {
            if (value == "c") out.lang = Lang::C;
            else if (value == "cpp" || value == "c++") out.lang = Lang::CPP;
            else return fail("unknown @lang " + value);
        }
        else if (key == "scene" && step >= 0) out.steps[step].scene = value;
        else if (key == "expr"  && step >= 0) out.steps[step].predict_expr = value;
        else if (key == "line"  && step >= 0) {
            out.steps[step].predict_line = std::atoi(value.c_str());
            if (out.steps[step].predict_line <= 0) return fail("@line needs a positive number");
        }
        else if (key == "test" || key == "hidden_test") {
            tests().push_back(TestCase{});
            test = (int)tests().size() - 1;
            tests()[test].hidden = key == "hidden_test";
        }
        else if (key == "theory" || key == "starter" || key == "solution" || key == "input" ||
                 key == "expected" || key == "text" || key == "code" || key == "rubric" || key == "answer") {
            if (step < 0 && (key == "text" || key == "code" || key == "rubric" || key == "answer"))
                return fail("@" + key + " must be inside a @step");
            section = key;
        }
        else return fail("unknown @" + key + (step >= 0 ? " inside a step" : ""));
    }
    if (!flush()) return false;
    if (out.id.empty())    { error = "missing @id"; return false; }
    if (out.title.empty()) { error = "missing @title"; return false; }

    // A practice step without its own content uses the lesson-level task, and vice versa.
    for (Step& s : out.steps) {
        if (s.kind != StepKind::Practice) continue;
        if (s.starter.empty())  s.starter = out.starter_code;
        if (s.solution.empty()) s.solution = out.solution;
        if (s.tests.empty())    s.tests = out.tests;
        if (s.text.empty())     s.text = out.theory;
        if (out.tests.empty()) {
            out.theory = s.text; out.starter_code = s.starter; out.solution = s.solution; out.tests = s.tests;
        }
    }
    // Old format (no steps), or lesson-level task without an explicit practice step: append one.
    if (!has_practice_step && !out.tests.empty()) {
        Step s;
        s.kind = StepKind::Practice;
        s.title = "Practice";
        s.text = out.theory;
        s.starter = out.starter_code;
        s.solution = out.solution;
        s.tests = out.tests;
        out.steps.push_back(s);
    }
    if (out.steps.empty()) { error = "no steps and no tests"; return false; }
    for (size_t i = 0; i < out.steps.size(); ++i) {
        std::string why = validate(out.steps[i]);
        if (!why.empty()) {
            error = "step " + std::to_string(i + 1) + " (" + step_kind_name(out.steps[i].kind) + "): " + why;
            return false;
        }
    }
    return true;
}

std::vector<Lesson> lessons_load(const std::string& dir) {
    std::vector<Lesson> lessons;
    DIR* d = opendir(dir.c_str());
    if (!d) return lessons;
    while (dirent* e = readdir(d)) {
        std::string name = e->d_name;
        if (name.size() < 8 || name.substr(name.size() - 7) != ".lesson") continue;
        std::ifstream f(dir + "/" + name);
        std::stringstream ss; ss << f.rdbuf();
        Lesson l; std::string err;
        if (lesson_parse(ss.str(), l, err)) lessons.push_back(l);
        else std::fprintf(stderr, "lessons: %s: %s\n", name.c_str(), err.c_str());
    }
    closedir(d);
    std::sort(lessons.begin(), lessons.end(), [](const Lesson& a, const Lesson& b) { return a.id < b.id; });
    return lessons;
}

// ---------------------------------------------------------------- progress

Progress progress_load(const std::string& path) {
    Progress p;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (!line.empty()) p.completed.insert(line);
    }
    return p;
}

void progress_save(const std::string& path, const Progress& p) {
    std::ofstream f(path);
    for (const std::string& id : p.completed) f << id << "\n";
}
