// Runs a lesson's tests against the student's code.
#include "app.h"

#include <algorithm>
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

static std::vector<std::string> split_lines(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream in(s);
    std::string line;
    while (std::getline(in, line)) out.push_back(line);
    return out;
}

static std::string quote_char(char c) {
    if (c == ' ') return "a space";
    if (c == '\t') return "a tab";
    return std::string("\"") + c + "\"";
}

std::string describe_difference(const std::string& expected, const std::string& got) {
    std::vector<std::string> e = split_lines(normalize(expected)), g = split_lines(normalize(got));
    size_t n = std::max(e.size(), g.size());
    for (size_t i = 0; i < n; ++i) {
        std::string ln = "line " + std::to_string(i + 1);
        if (i >= g.size()) return "Your output is missing " + ln + ": \"" + e[i] + "\".";
        if (i >= e.size()) return "Your output has an extra " + ln + ": \"" + g[i] + "\".";
        const std::string& a = e[i];
        const std::string& b = g[i];
        if (a == b) continue;

        size_t k = 0;
        while (k < a.size() && k < b.size() && a[k] == b[k]) ++k;
        std::string what;
        if (k == b.size())
            what = "missing " + (a.size() - k == 1 ? quote_char(a[k]) : "\"" + a.substr(k) + "\"") + " at the end of the line";
        else if (k == a.size())
            what = "extra " + (b.size() - k == 1 ? quote_char(b[k]) : "\"" + b.substr(k) + "\"") + " at the end of the line";
        else
            what = "expected " + quote_char(a[k]) + " but got " + quote_char(b[k]) + " at column " + std::to_string(k + 1);

        std::string col = std::string(10 + k, ' ') + "^";
        return "First difference on " + ln + ": " + what + ".\n"
               "  expected: " + a + "\n"
               "  got:      " + b + "\n" + col;
    }
    return "";
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
                if (!t.hidden) {
                    std::string diff = describe_difference(t.expected, run.output);
                    if (!diff.empty()) msg += "\n" + diff;
                    msg += "\n\nExpected output:\n" + show(t.expected);
                    msg += "\nYour output:\n" + show(run.output);
                } else {
                    msg += "\n(This is a hidden test.)\nGot:\n" + show(run.output);
                }
            }
            r.first_failure = msg;
            r.first_failed_run = run;
        }
    }
    return r;
}
