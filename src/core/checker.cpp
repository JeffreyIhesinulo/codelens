// Runs a lesson's tests against the student's code.
#include "app.h"

#include <sstream>

// Compare ignoring trailing spaces on each line and trailing empty lines.
static std::string normalize(const std::string& s) {
    std::istringstream in(s);
    std::string line, out;
    std::vector<std::string> lines;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r'))
            line.pop_back();
        lines.push_back(line);
    }
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
    for (size_t i = 0; i < lines.size(); ++i) out += lines[i] + (i + 1 < lines.size() ? "\n" : "");
    return out;
}

static std::string show(const std::string& s) {
    std::string n = normalize(s);
    return n.empty() ? "(nothing)" : n;
}

TestResult check_code(const Lesson& lesson, const std::string& code) {
    TestResult r;
    r.total = (int)lesson.tests.size();

    for (size_t i = 0; i < lesson.tests.size(); ++i) {
        const TestCase& t = lesson.tests[i];
        RunResult run = run_code(code, t.input, lesson.lang);
        std::string label = "Test " + std::to_string(i + 1);

        if (!run.compiled) {
            r.first_failure = "Your code does not compile. Press Run to see the errors.";
            r.first_failed_run = run;
            return r;   // no point running the other tests
        }

        std::string why;
        if (run.timed_out)                 why = "the program did not finish in 3 seconds (infinite loop?)";
        else if (!run.memory_errors.empty()) why = "memory error: " + run.memory_errors[0].details;
        else if (run.crashed)              why = "the program crashed";
        else if (normalize(run.output) != normalize(t.expected)) {
            why = "wrong output";
        }

        if (why.empty()) { ++r.passed; continue; }
        if (r.first_failure.empty()) {
            std::string msg = label + " failed: " + why + ".";
            if (why == "wrong output") {
                if (!t.hidden && !normalize(t.input).empty()) msg += "\nInput:    " + show(t.input);
                if (!t.hidden) msg += "\nExpected: " + show(t.expected);
                else msg += "\n(This is a hidden test.)";
                msg += "\nGot:      " + show(run.output);
            }
            r.first_failure = msg;
            r.first_failed_run = run;
        }
    }
    return r;
}
