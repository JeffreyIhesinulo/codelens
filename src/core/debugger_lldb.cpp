#ifdef __APPLE__
#include "debugger_lldb.h"
#include "internal.h"
#include "mac_memory.h"

#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace {}

bool LldbSession::exchange(const std::string &command, std::string &response, bool interrupt) {
    const std::string line = command + "\n";
    for (size_t sent = 0; sent < line.size();) {
        ssize_t count = write(input_, line.data() + sent, line.size() - sent);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            return false;
        sent += count;
    }
    timed_out_ = false;
    auto started = std::chrono::steady_clock::now();
    auto deadline = started + std::chrono::seconds(12);
    while (std::chrono::steady_clock::now() < deadline) {
        size_t end = buffered_.find('\n');
        if (end != std::string::npos) {
            std::string row = buffered_.substr(0, end);
            buffered_.erase(0, end + 1);
            const std::string prefix = "CODELENS_JSON ";
            if (row.rfind(prefix, 0) == 0) {
                response = row.substr(prefix.size());
                return true;
            }
            continue;
        }
        if (interrupt && !timed_out_ && target_pid_ > 0 &&
            std::chrono::steady_clock::now() - started > std::chrono::seconds(3)) {
            timed_out_ = true;
            kill(target_pid_, SIGSTOP);
        }
        pollfd descriptor{output_, POLLIN, 0};
        if (poll(&descriptor, 1, 200) < 0 && errno != EINTR)
            return false;
        if (!(descriptor.revents & (POLLIN | POLLHUP)))
            continue;
        char data[16384];
        ssize_t count = read(output_, data, sizeof data);
        if (count <= 0)
            return false;
        buffered_.append(data, count);
    }
    return false;
}

bool LldbSession::start(const std::string &code, Lang lang, const std::string &input, std::string &error) {
    stop();
    state = {};
    code_ = code;
    steps_ = 0;
    signal(SIGPIPE, SIG_IGN);
    char pattern[] = "/tmp/codelens-lldb-XXXXXX";
    if (!mkdtemp(pattern)) {
        error = "Could not create a temporary directory.";
        return false;
    }
    directory_ = pattern;
    const std::string file = lang == Lang::C ? "main.c" : "main.cpp";
    std::ofstream(directory_ + "/" + file) << mac_memory::declarations << "\n#line 1 \"" << file << "\"\n"
                                           << code;
    std::ofstream(directory_ + "/tracker.c") << mac_memory::tracker;
    std::ofstream(directory_ + "/stdin.txt") << input;
    auto compiled = exec_capture(
        {"clang", "-g", "-O0", "-c", directory_ + "/tracker.c", "-o", directory_ + "/tracker.o"}, "", 20000);
    if (compiled.exit_code == 0)
        compiled = exec_capture({lang == Lang::C ? "clang" : "clang++", "-g", "-O0", "-fno-stack-protector",
                                 "-fno-omit-frame-pointer", directory_ + "/" + file,
                                 directory_ + "/tracker.o", "-o", directory_ + "/program"},
                                "", 20000);
    if (compiled.exit_code != 0) {
        error = compiled.err;
        stop();
        return false;
    }
    int incoming[2], outgoing[2];
    if (pipe(incoming) != 0) {
        error = "Could not open a debugger pipe.";
        stop();
        return false;
    }
    if (pipe(outgoing) != 0) {
        close(incoming[0]);
        close(incoming[1]);
        error = "Could not open a debugger pipe.";
        stop();
        return false;
    }
    pid_ = fork();
    if (pid_ == 0) {
        setpgid(0, 0);
        setenv("PYTHONDONTWRITEBYTECODE", "1", 1);
        dup2(incoming[0], 0);
        dup2(outgoing[1], 1);
        dup2(outgoing[1], 2);
        close(incoming[0]);
        close(incoming[1]);
        close(outgoing[0]);
        close(outgoing[1]);
        std::string load = "command script import " CODELENS_CORE_ROOT "/lldb_bridge.py";
        execl("/usr/bin/xcrun", "xcrun", "lldb", "--no-lldbinit", "--batch", "-o", load.c_str(), "-o",
              "script lldb_bridge.run()", (char *)nullptr);
        _exit(127);
    }
    close(incoming[0]);
    close(outgoing[1]);
    input_ = incoming[1];
    output_ = outgoing[0];
    if (pid_ < 0) {
        error = "Could not start LLDB.";
        stop();
        return false;
    }
    std::string response;
    if (!exchange(
            json({{"action", "start"}, {"directory", directory_}, {"file", file}, {"code", code}}).dump(),
            response)) {
        error = "LLDB did not respond. Install Apple's Command Line Tools with xcode-select --install.";
        stop();
        return false;
    }
    auto result = json::parse(response);
    if (result.contains("error")) {
        error = result["error"];
        stop();
        return false;
    }
    if (!state_from_json(result["state"].dump(), state, error)) {
        stop();
        return false;
    }
    target_pid_ = result.value("pid", -1);
    state.note = "The program starts. Next line to run: " + std::to_string(state.line) + ".";
    return true;
}

bool LldbSession::exec(const std::string &command) {
    if (!active() || state.finished)
        return false;
    if (++steps_ > 2000) {
        state.finished = true;
        state.note = "Stopped after 2000 steps.";
        return false;
    }
    ProgramState before = state;
    std::string response, error;
    if (!exchange(json({{"action", command}}).dump(), response, true)) {
        state.finished = true;
        state.note = "The debugger stopped responding.";
        return false;
    }
    auto result = json::parse(response);
    if (result.contains("error")) {
        state.note = result["error"];
        return false;
    }
    if (!state_from_json(result["state"].dump(), state, error))
        return false;
    if (timed_out_) {
        state.finished = true;
        state.errors.clear();
        state.note = "This line did not finish in 3 seconds. Is there an infinite loop?";
    }
    if (state.note.find("infinite loop") == std::string::npos && state.errors.empty())
        state.note = describe_step(before, state, before.line, code_);
    return true;
}

bool LldbSession::run_to_line(int line) {
    if (!active() || state.finished)
        return false;
    // Already stopped at that line in main: a breakpoint there would never be hit again.
    if (state.line == line && state.stack.size() == 1)
        return true;
    std::string response, error;
    if (!exchange(json({{"action", "line"}, {"line", line}}).dump(), response, true))
        return false;
    auto result = json::parse(response);
    return result.contains("state") && state_from_json(result["state"].dump(), state, error);
}

std::string LldbSession::eval(const std::string &expression) {
    if (!active() || state.finished)
        return "";
    std::string response;
    if (!exchange(json({{"action", "eval"}, {"expression", expression}}).dump(), response))
        return "";
    return json::parse(response).value("value", "");
}

void LldbSession::stop() {
    if (pid_ > 0) {
        std::string response;
        if (!exchange("{\"action\":\"stop\"}", response) && target_pid_ > 0)
            kill(target_pid_, SIGKILL);
        kill(-pid_, SIGTERM);
        for (int i = 0; i < 30; ++i) {
            if (waitpid(pid_, nullptr, WNOHANG) == pid_) {
                pid_ = -1;
                break;
            }
            usleep(10000);
        }
        if (pid_ > 0) {
            kill(-pid_, SIGKILL);
            waitpid(pid_, nullptr, 0);
        }
    }
    pid_ = -1;
    target_pid_ = -1;
    if (input_ >= 0)
        close(input_);
    if (output_ >= 0)
        close(output_);
    input_ = output_ = -1;
    buffered_.clear();
    if (!directory_.empty()) {
        std::error_code error;
        std::filesystem::remove_all(directory_, error);
        directory_.clear();
    }
}
#endif
