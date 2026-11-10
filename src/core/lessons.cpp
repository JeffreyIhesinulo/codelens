// Lesson files (lessons/*.lesson) use a simple section format, so code can be
// written as-is without JSON escaping:
//
//   @id       03_loops
//   @title    Loops
//   @topic    loops
//   @lang     c
//   @theory
//   ...text...
//   @starter
//   ...code the student starts with...
//   @solution
//   ...reference solution...
//   @test            (or @hidden_test)
//   @input
//   5
//   @expected
//   15
//
// Lines starting with "#!" are comments.
#include "app.h"

#include <algorithm>
#include <cstdio>
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

bool lesson_parse(const std::string& text, Lesson& out, std::string& error) {
    out = Lesson{};
    std::istringstream in(text);
    std::string line, section, body;
    TestCase* test = nullptr;
    int line_no = 0;

    auto flush = [&]() -> bool {
        std::string v = block(body);
        if      (section == "theory")   out.theory = v;
        else if (section == "starter")  out.starter_code = v + "\n";
        else if (section == "solution") out.solution = v + "\n";
        else if (section == "input" || section == "expected") {
            if (!test) { error = "@" + section + " before @test"; return false; }
            (section == "input" ? test->input : test->expected) = v.empty() ? "" : v + "\n";
        }
        body.clear();
        return true;
    };

    while (std::getline(in, line)) {
        ++line_no;
        if (line.rfind("#!", 0) == 0) continue;
        if (!line.empty() && line[0] == '@') {
            if (!flush()) return false;
            std::string key = line.substr(1), value;
            size_t sp = key.find_first_of(" \t");
            if (sp != std::string::npos) { value = trim(key.substr(sp)); key = key.substr(0, sp); }

            if      (key == "id")    out.id = value;
            else if (key == "title") out.title = value;
            else if (key == "topic") out.topic = value;
            else if (key == "lang") {
                if (value == "c") out.lang = Lang::C;
                else if (value == "cpp" || value == "c++") out.lang = Lang::CPP;
                else { error = "line " + std::to_string(line_no) + ": unknown @lang " + value; return false; }
            }
            else if (key == "test" || key == "hidden_test") {
                out.tests.push_back(TestCase{});
                test = &out.tests.back();
                test->hidden = key == "hidden_test";
            }
            else if (key == "theory" || key == "starter" || key == "solution" ||
                     key == "input" || key == "expected") {
                section = key;
                continue;
            }
            else { error = "line " + std::to_string(line_no) + ": unknown @" + key; return false; }
            section.clear();
        } else if (!section.empty()) {
            body += line + "\n";
        }
    }
    if (!flush()) return false;
    if (out.id.empty())    { error = "missing @id"; return false; }
    if (out.title.empty()) { error = "missing @title"; return false; }
    if (out.tests.empty()) { error = "no tests"; return false; }
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
