#include "services.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <sstream>
#include <thread>
#include <sys/wait.h>
#include <unistd.h>
#include <nlohmann/json.hpp>

namespace {
Variable number(std::string name, int value, uint64_t address) {
    return {std::move(name), "int", std::to_string(value), address, false, 0, {}};
}
int param(const Scene &scene, const std::string &name, int fallback = 0) {
    for (const auto &p : scene.params)
        if (p.name == name)
            return p.value;
    return fallback;
}
std::string endpoint(const std::string &path) {
    const char *host = std::getenv("CODELENS_OLLAMA_HOST");
    const char *port = std::getenv("CODELENS_OLLAMA_PORT");
    return "http://" + std::string(host ? host : "127.0.0.1") + ":" + (port ? port : "11434") + path;
}
struct StatusStore {
    std::mutex mutex;
    TutorStatus value;
    std::atomic<bool> checking{false};
};
StatusStore &store() {
    static auto *s = new StatusStore;
    return *s;
}
// Arguments are passed directly to execvp, without a shell.
std::string command(const std::vector<std::string> &args) {
    int pipefd[2];
    if (pipe(pipefd) != 0)
        return "";
    pid_t pid = fork();
    if (pid == 0) {
        dup2(pipefd[1], 1);
        dup2(pipefd[1], 2);
        close(pipefd[0]);
        close(pipefd[1]);
        std::vector<char *> argv;
        for (const auto &a : args)
            argv.push_back(const_cast<char *>(a.c_str()));
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
    }
    close(pipefd[1]);
    std::string out;
    char data[4096];
    if (pid > 0) {
        for (ssize_t n; (n = read(pipefd[0], data, sizeof data)) > 0;)
            if (out.size() < 65536)
                out.append(data, n);
    }
    close(pipefd[0]);
    if (pid > 0)
        waitpid(pid, nullptr, 0);
    return out;
}
} // namespace

Scene ui_scene_get(const std::string &id) {
    Scene actual = scene_get(id);
    if (!actual.id.empty())
        return actual;
    Scene s;
    s.id = id;
    if (id == "array") {
        s.title = "Choose a box in the array";
        s.params = {{"Box number", 0, 4, 0}};
        s.actions = {"Reset"};
    } else if (id == "pointer_basics") {
        s.title = "Choose what p points to";
        s.params = {{"Value in a", 0, 20, 3}};
        s.actions = {"p = &a", "p = &b", "*p = 5", "p = NULL", "Reset"};
    } else if (id == "variables") {
        s.title = "A variable is a named box for a value";
        s.params = {{"Value in x", -10, 20, 3}};
        s.actions = {"Reset"};
    } else if (id == "condition") {
        s.title = "An if chooses which block runs";
        s.params = {{"Value in x", 0, 20, 3}};
        s.actions = {"Reset"};
    } else if (id == "heap") {
        s.title = "Reserve memory, use it, then give it back";
        s.params = {{"Number of boxes", 1, 8, 3}};
        s.actions = {"malloc", "free(p)", "Reset"};
    } else if (id == "recursion") {
        s.title = "A function can call itself";
        s.params = {{"Number of calls", 1, 6, 3}};
        s.actions = {"Reset"};
    } else
        s.id.clear();
    return s;
}

ProgramState ui_scene_state(Scene &s, const std::vector<std::string> &actions) {
    if (!scene_get(s.id).id.empty())
        return scene_state(s, actions);
    ProgramState st;
    Frame f;
    f.function = "main";
    if (s.id == "array") {
        int i = param(s, "Box number");
        const int values[] = {10, 20, 99, 40, 50};
        Variable a{"a", "int [5]", "", 0x1000, false, 0, {}};
        for (int n = 0; n < 5; ++n)
            a.children.push_back(number("[" + std::to_string(n) + "]", values[n], 0x1000 + n * 4));
        f.vars = {a, {"p", "int *", "", 0x2000, true, 0x1000 + (uint64_t)i * 4, {}}};
        s.code = "int a[5] = {10, 20, 99, 40, 50};\nint *p = &a[" + std::to_string(i) + "];";
        st.line = 2;
        st.note =
            "p now points to a[" + std::to_string(i) + "]. That box holds " + std::to_string(values[i]) + ".";
    } else if (s.id == "pointer_basics") {
        int a = param(s, "Value in a", 3), b = 7;
        uint64_t target = 0x1000;
        s.code = "int a = " + std::to_string(a) + ", b = 7;\nint *p = &a;";
        st.line = 2;
        for (const auto &action : actions) {
            if (action == "Reset") {
                a = param(s, "Value in a", 3);
                b = 7;
                target = 0x1000;
                s.code = "int a = " + std::to_string(a) + ", b = 7;\nint *p = &a;";
                st.errors.clear();
                st.line = 2;
                continue;
            }
            s.code += "\n" + action + ";";
            ++st.line;
            if (action == "p = &a")
                target = 0x1000;
            if (action == "p = &b")
                target = 0x1004;
            if (action == "p = NULL")
                target = 0;
            if (action == "*p = 5") {
                if (target == 0x1000)
                    a = 5;
                else if (target == 0x1004)
                    b = 5;
                else
                    st.errors.push_back(
                        {"SEGV", st.line, "p has no target. Assign an address before using *p."});
            }
        }
        f.vars = {
            number("a", a, 0x1000), number("b", b, 0x1004), {"p", "int *", "", 0x2000, true, target, {}}};
        st.note = st.errors.empty() ? (target ? "p points to " + std::string(target == 0x1000 ? "a" : "b") +
                                                    ". *p uses that box's value."
                                              : "p has no target. It is NULL.")
                                    : st.errors.back().details;
    } else if (s.id == "variables" || s.id == "condition") {
        int x = param(s, "Value in x", 3);
        f.vars = {number("x", x, 0x1000)};
        s.code = "int x = " + std::to_string(x) + ";";
        st.line = 1;
        st.note = "The box named x holds " + std::to_string(x) + ".";
        if (s.id == "condition") {
            s.code += "\nif (x >= 10) {\n    puts(\"large\");\n} else {\n    puts(\"small\");\n}";
            st.line = x >= 10 ? 3 : 5;
            st.output = x >= 10 ? "large\n" : "small\n";
            st.note = "x >= 10 is " + std::string(x >= 10 ? "true" : "false") + ". This branch prints " +
                      (x >= 10 ? "large." : "small.");
        }
    } else if (s.id == "heap") {
        int count = param(s, "Number of boxes", 3);
        bool allocated = false, freed = false;
        s.code = "#include <stdlib.h>\nint *p = NULL;";
        st.line = 2;
        for (const auto &a : actions) {
            if (a == "Reset") {
                allocated = freed = false;
                s.code = "#include <stdlib.h>\nint *p = NULL;";
                st.line = 2;
                continue;
            }
            if (a == "malloc") {
                allocated = true;
                freed = false;
                s.code += "\np = malloc(" + std::to_string(count) + " * sizeof(int));";
            } else {
                if (allocated)
                    freed = true;
                s.code += "\nfree(p);\np = NULL;";
            }
        }
        st.line = (int)std::count(s.code.begin(), s.code.end(), '\n') + 1;
        f.vars = {{"p", "int *", "", 0x1000, true, allocated && !freed ? uint64_t(0x4000) : uint64_t(0), {}}};
        if (allocated) {
            HeapBlock b{0x4000, (size_t)count * 4, freed, {}};
            for (int i = 0; i < count; ++i)
                b.cells.push_back(
                    {"[" + std::to_string(i) + "]", "int", "?", 0x4000 + (uint64_t)i * 4, false, 0, {}});
            st.heap = {b};
        }
        st.note = freed       ? "free gave the reserved memory back. p now has no target."
                  : allocated ? "malloc reserved " + std::to_string(count) +
                                    " whole-number boxes. They have no values yet."
                              : "Press malloc to reserve boxes that outlive a function call.";
    } else if (s.id == "recursion") {
        int n = param(s, "Number of calls", 3);
        s.code = "void count(int n) {\n    if (n > 1) count(n - 1);\n}\nint main(void) {\n    count(" +
                 std::to_string(n) + ");\n}";
        st.line = 2;
        st.stack.push_back(f);
        for (int i = n; i > 0; --i)
            st.stack.push_back({"count", 2, {number("n", i, 0x1000 + (uint64_t)(n - i) * 16)}});
        st.note = "Each call has its own n. The newest call has n = 1.";
        return st;
    }
    f.line = st.line;
    st.stack.push_back(f);
    return st;
}

TutorStatus tutor_status() {
    std::lock_guard<std::mutex> lock(store().mutex);
    return store().value;
}
void tutor_refresh() {
    if (store().checking.exchange(true))
        return;
    std::thread([] {
        TutorStatus status;
        status.checked = true;
        auto response = command({"curl", "-sS", "--max-time", "3", endpoint("/api/tags")});
        try {
            auto json = nlohmann::json::parse(response);
            status.server = json.contains("models");
            for (const auto &m : json.at("models")) {
                std::string name = m.value("name", "");
                if (name == ai_model() || name == ai_model() + ":latest")
                    status.installed = true;
            }
        } catch (...) {
        }
        {
            std::lock_guard<std::mutex> lock(store().mutex);
            status.downloading = store().value.downloading;
            status.progress = store().value.progress;
            status.message = store().value.message;
            store().value = status;
        }
        store().checking = false;
    }).detach();
}
void tutor_download() {
    {
        std::lock_guard<std::mutex> lock(store().mutex);
        if (store().value.downloading)
            return;
        store().value.downloading = true;
        store().value.message = "Downloading the tutor model. You can keep learning.";
    }
    std::thread([] {
        int pipefd[2];
        if (pipe(pipefd) != 0) {
            std::lock_guard<std::mutex> lock(store().mutex);
            store().value.downloading = false;
            store().value.message = "Could not start the download. Try again.";
            return;
        }
        std::string body = nlohmann::json({{"model", ai_model()}, {"stream", true}}).dump();
        pid_t pid = fork();
        if (pid == 0) {
            dup2(pipefd[1], 1);
            dup2(pipefd[1], 2);
            close(pipefd[0]);
            close(pipefd[1]);
            execlp("curl", "curl", "-sS", "-N", "--max-time", "3600", "-H", "Content-Type: application/json",
                   "-d", body.c_str(), endpoint("/api/pull").c_str(), (char *)nullptr);
            _exit(127);
        }
        close(pipefd[1]);
        bool success = false;
        std::string failure = "Download did not finish. Check Ollama and try again.";
        if (pid > 0) {
            FILE *f = fdopen(pipefd[0], "r");
            char *line = nullptr;
            size_t cap = 0;
            while (getline(&line, &cap, f) > 0) {
                try {
                    auto j = nlohmann::json::parse(line);
                    std::lock_guard<std::mutex> lock(store().mutex);
                    auto &s = store().value;
                    s.message = j.value("status", "");
                    if (j.contains("error"))
                        failure = j["error"];
                    uint64_t total = j.value("total", (uint64_t)0);
                    if (total)
                        s.progress = (float)j.value("completed", (uint64_t)0) / total;
                    if (s.message == "success")
                        success = true;
                } catch (...) {
                }
            }
            free(line);
            fclose(f);
            waitpid(pid, nullptr, 0);
        } else
            close(pipefd[0]);
        {
            std::lock_guard<std::mutex> lock(store().mutex);
            auto &s = store().value;
            s.downloading = false;
            s.installed = success;
            s.progress = success ? 1 : 0;
            s.message = success ? "The tutor is ready." : failure;
        }
        tutor_refresh();
    }).detach();
}
Lesson validated_practice(const std::string &response, const Lesson &original, std::string &error) {
    Lesson task;
    try {
        size_t first = response.find('{'), last = response.rfind('}');
        if (first == std::string::npos || last == std::string::npos)
            throw std::runtime_error("Missing task data.");
        auto j = nlohmann::json::parse(response.substr(first, last - first + 1));
        task.id = original.id + "_extra";
        task.lang = original.lang;
        task.title = j.at("title");
        task.topic = original.topic;
        task.theory = j.at("text");
        task.starter_code = j.at("starter");
        task.solution = j.at("solution");
        for (const auto &t : j.at("tests"))
            task.tests.push_back({t.at("input"), t.at("expected"), false});
        if (task.tests.size() < 2 || task.tests.size() > 8 || task.starter_code.size() > 16000 ||
            task.solution.size() > 16000)
            throw std::runtime_error("The task has invalid limits.");
        auto result = check_code(task, task.solution);
        if (result.total == 0 || result.passed != result.total)
            throw std::runtime_error(
                "The reference answer failed the compiler or tests. The task was rejected.");
        Step step;
        step.kind = StepKind::Practice;
        step.text = task.theory;
        step.starter = task.starter_code;
        step.tests = task.tests;
        task.steps = {step};
    } catch (const std::exception &e) {
        error = e.what();
        return {};
    }
    return task;
}

Lesson generate_practice(const Lesson &original, std::string &error) {
    // Authoring has its own prompt: the ordinary tutor must never reveal a full solution.
    nlohmann::json request = {
        {"model", ai_model()},
        {"stream", false},
        {"format", "json"},
        {"messages",
         nlohmann::json::array(
             {{{"role", "system"},
               {"content",
                "You author small C exercises for complete beginners. Return ONLY JSON with string title, "
                "text, starter, solution, and tests: an array of at least two objects with string input and "
                "expected. The solution must be a complete program that reads stdin and prints exactly the "
                "expected result for every test. The starter leaves the central task unfinished. Never "
                "include unsafe system calls, file access or networking."}},
              {{"role", "user"},
               {"content",
                "Write a new exercise about " + original.topic +
                    ". It should be different from this completed exercise: " + original.theory}}})}};
    auto response = command({"curl", "-sS", "--max-time", "180", "-H", "Content-Type: application/json", "-d",
                             request.dump(), endpoint("/api/chat")});
    try {
        auto json = nlohmann::json::parse(response);
        if (json.contains("error")) {
            error = json["error"];
            return {};
        }
        return validated_practice(json.at("message").at("content"), original, error);
    } catch (const std::exception &) {
        error = "The tutor could not create a task. Check its setup and try again.";
        return {};
    }
}

std::string tutor_message(std::string answer) {
    if (answer.find("AI is offline") != std::string::npos ||
        answer.find("model is not installed") != std::string::npos)
        return "The tutor is not ready yet. Open Tutor setup from the learning path, then return to this "
               "lesson. Your work is saved.";
    return answer;
}
