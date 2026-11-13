// Compiles and runs student code in a child process with a time limit.
#include "app.h"
#include "internal.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <poll.h>
#include <regex>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

static const size_t MAX_OUTPUT = 64 * 1024;

// Runs argv with stdin_text on stdin. Captures stdout/stderr. Kills after timeout_ms.
ExecResult exec_capture(const std::vector<std::string>& argv,
                        const std::string& stdin_text, int timeout_ms,
                        const std::vector<std::string>& env_extra) {
    ExecResult r;
    int in_pipe[2], out_pipe[2], err_pipe[2];
    if (pipe(in_pipe) || pipe(out_pipe) || pipe(err_pipe)) return r;

    pid_t pid = fork();
    if (pid < 0) return r;
    if (pid == 0) {
        setpgid(0, 0);   // own process group so we can kill everything it spawns
        dup2(in_pipe[0], 0);
        dup2(out_pipe[1], 1);
        dup2(err_pipe[1], 2);
        close(in_pipe[0]); close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        close(err_pipe[0]); close(err_pipe[1]);
        for (const std::string& e : env_extra) putenv(strdup(e.c_str()));
        std::vector<char*> args;
        for (const std::string& a : argv) args.push_back(const_cast<char*>(a.c_str()));
        args.push_back(nullptr);
        execvp(args[0], args.data());
        _exit(127);
    }
    r.started = true;
    close(in_pipe[0]); close(out_pipe[1]); close(err_pipe[1]);

    // stdin: student inputs are small, but write non-blocking anyway.
    fcntl(in_pipe[1], F_SETFL, O_NONBLOCK);
    size_t written = 0;
    int in_fd = in_pipe[1];
    if (stdin_text.empty()) { close(in_fd); in_fd = -1; }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    bool out_open = true, err_open = true;
    char buf[4096];

    while (out_open || err_open) {
        int left = (int)std::chrono::duration_cast<std::chrono::milliseconds>(
                       deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0) { r.timed_out = true; break; }

        pollfd fds[3]; int n = 0;
        int i_out = -1, i_err = -1, i_in = -1;
        if (out_open) { fds[n] = {out_pipe[0], POLLIN, 0}; i_out = n++; }
        if (err_open) { fds[n] = {err_pipe[0], POLLIN, 0}; i_err = n++; }
        if (in_fd >= 0) { fds[n] = {in_fd, POLLOUT, 0}; i_in = n++; }
        if (poll(fds, n, left) < 0) { if (errno == EINTR) continue; break; }

        if (i_in >= 0 && fds[i_in].revents) {
            ssize_t w = write(in_fd, stdin_text.data() + written, stdin_text.size() - written);
            if (w > 0) written += (size_t)w;
            if (w < 0 && errno != EAGAIN) written = stdin_text.size();
            if (written >= stdin_text.size()) { close(in_fd); in_fd = -1; }
        }
        auto drain = [&](int idx, int fd, std::string& dst, bool& open) {
            if (idx < 0 || !fds[idx].revents) return;
            ssize_t k = read(fd, buf, sizeof buf);
            if (k <= 0) { open = false; return; }
            if (dst.size() < MAX_OUTPUT) dst.append(buf, (size_t)k);
        };
        drain(i_out, out_pipe[0], r.out, out_open);
        drain(i_err, err_pipe[0], r.err, err_open);
    }
    if (in_fd >= 0) close(in_fd);
    close(out_pipe[0]); close(err_pipe[0]);

    if (r.timed_out) kill(-pid, SIGKILL);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    if (WIFEXITED(status))   r.exit_code = WEXITSTATUS(status);
    if (WIFSIGNALED(status)) r.signal = WTERMSIG(status);
    if (r.out.size() >= MAX_OUTPUT) r.out += "\n[output truncated]\n";
    return r;
}

// ---------------------------------------------------------------- parsing

// "main.c:5:3: error: ..." -> 5
static std::vector<int> parse_error_lines(const std::string& compiler_out, const std::string& file) {
    std::vector<int> lines;
    std::regex re(file + R"(:(\d+):\d+: (?:fatal )?error)");
    for (std::sregex_iterator it(compiler_out.begin(), compiler_out.end(), re), end; it != end; ++it) {
        int ln = std::stoi((*it)[1]);
        if (std::find(lines.begin(), lines.end(), ln) == lines.end()) lines.push_back(ln);
    }
    return lines;
}

// First stack frame that points into the student's file: "... main.c:12"
static int first_line_in(const std::string& text, size_t from, const std::string& file) {
    std::regex re("/" + file + R"(:(\d+))");
    std::smatch m;
    std::string rest = text.substr(from);
    if (std::regex_search(rest, m, re)) return std::stoi(m[1]);
    return 0;
}

static std::vector<MemoryError> parse_sanitizers(const std::string& err, const std::string& file) {
    std::vector<MemoryError> out;

    // AddressSanitizer: "ERROR: AddressSanitizer: heap-buffer-overflow on address ..."
    std::regex asan(R"(ERROR: AddressSanitizer: ([a-z\-]+))");
    for (std::sregex_iterator it(err.begin(), err.end(), asan), end; it != end; ++it) {
        MemoryError e;
        e.kind = (*it)[1];
        e.line = first_line_in(err, (size_t)it->position(), file);
        if (e.kind == "SEGV") e.details = "Segmentation fault: invalid memory access (often a NULL or wild pointer)";
        else e.details = e.kind + " detected by AddressSanitizer";
        out.push_back(e);
    }

    // LeakSanitizer: "Direct leak of 40 byte(s) in 1 object(s) allocated from:"
    std::regex leak(R"((Direct|Indirect) leak of (\d+) byte)");
    for (std::sregex_iterator it(err.begin(), err.end(), leak), end; it != end; ++it) {
        MemoryError e;
        e.kind = "memory-leak";
        e.line = first_line_in(err, (size_t)it->position(), file);
        e.details = std::string((*it)[2]) + " bytes allocated but never freed";
        out.push_back(e);
    }

    // UBSan: "main.c:7:9: runtime error: signed integer overflow: ..."
    std::regex ub(file + R"(:(\d+):\d+: runtime error: ([^\n]*))");
    for (std::sregex_iterator it(err.begin(), err.end(), ub), end; it != end; ++it) {
        MemoryError e;
        e.kind = "runtime-error";
        e.line = std::stoi((*it)[1]);
        e.details = (*it)[2];
        out.push_back(e);
    }
    return out;
}

// Removes the temp directory path so the student sees "main.c:5" instead of "/tmp/codelens-XXXX/main.c:5".
static void strip_paths(std::string& s, const std::string& dir) {
    std::string needle = dir + "/";
    for (size_t p; (p = s.find(needle)) != std::string::npos;) s.erase(p, needle.size());
}

// ---------------------------------------------------------------- public

RunResult run_code(const std::string& code, const std::string& stdin_text, Lang lang, int timeout_ms) {
    RunResult res;
    signal(SIGPIPE, SIG_IGN);   // a program that exits early must not kill the app via its stdin pipe

    char tmpl[] = "/tmp/codelens-XXXXXX";
    if (!mkdtemp(tmpl)) { res.compile_errors = "Could not create a temporary directory"; return res; }
    std::string dir = tmpl;
    std::string file = lang == Lang::C ? "main.c" : "main.cpp";
    std::string src = dir + "/" + file, bin = dir + "/program";
    { std::ofstream(src) << code; }

    std::vector<std::string> cc = lang == Lang::C
        ? std::vector<std::string>{"gcc", "-std=c11"}
        : std::vector<std::string>{"g++", "-std=c++17"};
    for (const char* f : {"-g", "-O0", "-Wall", "-fsanitize=address,undefined",
                          "-fno-omit-frame-pointer", "-fdiagnostics-color=never"})
        cc.push_back(f);
    cc.push_back(src);
    cc.push_back("-o");
    cc.push_back(bin);
    if (lang == Lang::C) cc.push_back("-lm");

    ExecResult comp = exec_capture(cc, "", 20000);
    res.compile_errors = comp.err;
    strip_paths(res.compile_errors, dir);
    if (!comp.started || comp.exit_code != 0) {
        if (comp.exit_code == 127) res.compile_errors = "Compiler not found. Install gcc/g++.";
        res.error_lines = parse_error_lines(res.compile_errors, file);
        res.compiled = false;
    } else {
        res.compiled = true;
        ExecResult run = exec_capture({bin}, stdin_text, timeout_ms,
            {"ASAN_OPTIONS=detect_leaks=1:abort_on_error=0:symbolize=1",
             "UBSAN_OPTIONS=print_stacktrace=1"});
        res.output      = run.out;
        res.stderr_text = run.err;
        strip_paths(res.stderr_text, dir);
        res.exit_code = run.exit_code;
        res.timed_out = run.timed_out;
        res.crashed   = run.signal != 0 && !run.timed_out;
        res.memory_errors = parse_sanitizers(run.err, file);
    }

    unlink(src.c_str());
    unlink(bin.c_str());
    rmdir(dir.c_str());
    return res;
}
