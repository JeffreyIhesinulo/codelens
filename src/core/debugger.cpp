// Step-by-step execution through GDB's machine interface (gdb --interpreter=mi2).
//
// The student's program is compiled with -g -O0 together with a tiny heap tracker
// (malloc/free are wrapped with -Wl,--wrap), so after every step we can read
// the stack (frames, variables, addresses) and the heap (blocks, freed or not).
#include "app.h"
#include "internal.h"
#include "mi.h"
#ifdef __APPLE__
#include "debugger_lldb.h"
#endif

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <poll.h>
#include <regex>
#include <signal.h>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>

#ifndef __APPLE__
// Linked into every program run under the debugger. Records every malloc/free
// made by the student's code (not by libc itself) in a global array GDB can read.
static const char* TRACKER_SOURCE = R"(
#include <stddef.h>
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void  __real_free(void *);
struct cl_block { void *addr; size_t size; int freed; };
struct cl_block cl_heap[256];
int cl_heap_n = 0;
static void cl_add(void *p, size_t n) {
    if (p && cl_heap_n < 256) { cl_heap[cl_heap_n].addr = p; cl_heap[cl_heap_n].size = n; cl_heap[cl_heap_n].freed = 0; cl_heap_n++; }
}
static void cl_mark_freed(void *p) {
    for (int i = cl_heap_n - 1; i >= 0; i--)
        if (cl_heap[i].addr == p && !cl_heap[i].freed) { cl_heap[i].freed = 1; return; }
}
void *__wrap_malloc(size_t n) { void *p = __real_malloc(n); cl_add(p, n); return p; }
void *__wrap_calloc(size_t a, size_t b) { void *p = __real_calloc(a, b); cl_add(p, a * b); return p; }
void *__wrap_realloc(void *old, size_t n) { void *p = __real_realloc(old, n); if (old) cl_mark_freed(old); cl_add(p, n); return p; }
void  __wrap_free(void *p) { if (p) cl_mark_freed(p); __real_free(p); }
/* unbuffered stdout so the output file is up to date after every step */
__attribute__((constructor)) static void cl_init(void) { setvbuf(stdout, NULL, _IONBF, 0); }
/* final heap state, read by the debugger after the program exits */
__attribute__((destructor)) static void cl_done(void) {
    FILE *f = fopen("cl_heap.txt", "w");
    if (!f) return;
    for (int i = 0; i < cl_heap_n; i++)
        fprintf(f, "%p %zu %d\n", cl_heap[i].addr, cl_heap[i].size, cl_heap[i].freed);
    fclose(f);
}
#ifdef __cplusplus
}
#endif
)";

// GDB Python command "cl-decls": prints "name line is_arg" for every variable visible in the
// selected frame, so variables whose declaration has not run yet can be hidden.
static const char* GDB_HELPER = R"(
import gdb
class ClDecls(gdb.Command):
    def __init__(self):
        super().__init__("cl-decls", gdb.COMMAND_USER)
    def invoke(self, arg, from_tty):
        try:
            b = gdb.selected_frame().block()
        except RuntimeError:
            gdb.write("CLDECL\n")
            return
        out = []
        while b is not None:
            for s in b:
                if s.is_variable or s.is_argument:
                    out.append("%s %d %d" % (s.name, s.line, 1 if s.is_argument else 0))
            if b.function is not None:
                break
            b = b.superblock
        gdb.write("CLDECL " + ";".join(out) + "\n")
ClDecls()
)";

static const int STEP_TIMEOUT_MS = 3000;
static const int MAX_STEPS = 2000;
#endif

namespace {

uint64_t first_hex(const std::string& s) {
    size_t p = s.find("0x");
    if (p == std::string::npos) return 0;
    return std::strtoull(s.c_str() + p, nullptr, 16);
}

#ifndef __APPLE__
std::string read_file(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

// Splits a GDB aggregate value "{1, 2, {3, 4}, x = 5}" into its top-level parts.
std::vector<std::string> split_aggregate(const std::string& v) {
    std::vector<std::string> parts;
    if (v.size() < 2 || v.front() != '{') return parts;
    int depth = 0;
    bool in_str = false;
    std::string cur;
    for (size_t i = 1; i + 1 < v.size(); ++i) {
        char c = v[i];
        if (in_str) {
            cur += c;
            if (c == '\\' && i + 1 < v.size()) cur += v[++i];
            else if (c == '"' || c == '\'') in_str = false;
            continue;
        }
        if (c == '"' || c == '\'') in_str = true;
        if (c == '{') ++depth;
        if (c == '}') --depth;
        if (c == ',' && depth == 0) {
            size_t a = cur.find_first_not_of(' ');
            parts.push_back(a == std::string::npos ? "" : cur.substr(a));
            cur.clear();
            continue;
        }
        cur += c;
    }
    size_t a = cur.find_first_not_of(' ');
    if (a != std::string::npos) parts.push_back(cur.substr(a));
    return parts;
}

#endif

#ifdef __APPLE__
using Session = LldbSession;
#else
class Session {
public:
    ~Session() { stop(); }

    bool start(const std::string& code, Lang lang, const std::string& input, std::string& error);
    bool exec(const std::string& cmd);            // -exec-next, -exec-step, ...
    bool run_to_line(int line);
    std::string eval(const std::string& expr, int frame = -1);   // "" on error
    std::string type_of(const std::string& expr, int frame);      // "int", "char [8]", "" on error
    std::map<std::string, std::pair<int, bool>> declarations(int frame);   // name -> (line, is_argument)
    void stop();
    bool active() const { return pid_ > 0; }
    ProgramState state;

private:
    pid_t pid_ = -1;
    int to_gdb_ = -1, from_gdb_ = -1;
    std::string buf_;
    int token_ = 1;
    int steps_ = 0;
    bool have_decls_ = false;      // GDB has Python and loaded cl-decls
    int leave_depth_ = 0;          // guards the "finish out of library code" loop
    // Variables declared without a value ("int x;"): key "depth#function#name" -> the garbage
    // they held when they appeared. Shown as "?" until the value changes.
    std::map<std::string, std::string> uninit_;
    std::string console_;          // text GDB printed (~ records) during the last command
    std::string dir_, file_;
    std::vector<std::string> lines_;   // the student's source, for small checks
    std::string code_;

    static std::vector<std::string> split_code_lines(const std::string& code) {
        std::vector<std::string> out;
        std::istringstream in(code);
        std::string l;
        while (std::getline(in, l)) out.push_back(l);
        return out;
    }
    // "int x;" or "int a[5];" on that line declares name without giving it a value
    bool declared_without_value(int line, const std::string& name) const {
        if (line <= 0 || line > (int)lines_.size()) return false;
        const std::string& l = lines_[line - 1];
        std::regex no_init("\\b" + name + "\\s*(\\[[^\\]]*\\])?\\s*[;,)]");
        std::regex init("\\b" + name + "\\s*(\\[[^\\]]*\\])?\\s*=");
        return std::regex_search(l, no_init) && !std::regex_search(l, init);
    }
    static std::string signature(const Variable& v) {
        std::string s = v.value;
        for (const Variable& c : v.children) s += "|" + signature(c);
        return s;
    }
    static void mark_unknown(Variable& v) {
        if (v.type.rfind("std::", 0) == 0) return;   // std::string s; std::vector<int> v; start empty, not garbage
        if (v.children.empty()) v.value = "?";
        for (Variable& c : v.children) mark_unknown(c);
    }
    bool is_function_header(int line) const {
        if (line <= 0 || line > (int)lines_.size()) return false;
        static const std::regex re(R"(^\s*[A-Za-z_][\w\s\*]*\([^;]*\)\s*\{\s*$)");
        return std::regex_match(lines_[line - 1], re);
    }

    int  send(const std::string& cmd);
    bool read_line(std::string& line, int timeout_ms);
    bool wait_result(int token, MIRecord& out, int timeout_ms = 5000);
    bool wait_stopped(MIRecord& out, int timeout_ms);
    bool command(const std::string& cmd, MIRecord* out = nullptr);
    void handle_stop(const MIRecord& stop);
    void refresh();
    Variable read_variable(const MIValue& v, int level);
    void fill_heap_cells();
    // GDB may report "main.c" or "/tmp/codelens-dbg-XXXX/main.c"
    bool is_user_file(const std::string& f) const {
        return f == file_ || (f.size() > file_.size() && f.compare(f.size() - file_.size() - 1, std::string::npos, "/" + file_) == 0);
    }
};

// ---------------------------------------------------------------- GDB I/O

int Session::send(const std::string& cmd) {
    int t = token_++;
    std::string line = std::to_string(t) + cmd + "\n";
    if (write(to_gdb_, line.data(), line.size()) < 0) return -1;
    return t;
}

bool Session::read_line(std::string& line, int timeout_ms) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    for (;;) {
        size_t nl = buf_.find('\n');
        if (nl != std::string::npos) {
            line = buf_.substr(0, nl);
            buf_.erase(0, nl + 1);
            return true;
        }
        int left = (int)std::chrono::duration_cast<std::chrono::milliseconds>(
                       deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0) return false;
        pollfd p{from_gdb_, POLLIN, 0};
        int r = poll(&p, 1, left);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return false;
        char tmp[8192];
        ssize_t n = read(from_gdb_, tmp, sizeof tmp);
        if (n <= 0) return false;
        buf_.append(tmp, (size_t)n);
    }
}

bool Session::wait_result(int token, MIRecord& out, int timeout_ms) {
    std::string line;
    while (read_line(line, timeout_ms)) {
        MIRecord r;
        if (!mi_parse_line(line, r)) continue;
        if (r.type == '~') console_ += r.stream;
        if (r.type == '^' && r.token == token) { out = r; return true; }
    }
    return false;
}

bool Session::wait_stopped(MIRecord& out, int timeout_ms) {
    std::string line;
    while (read_line(line, timeout_ms)) {
        MIRecord r;
        if (mi_parse_line(line, r) && r.type == '*' && r.cls == "stopped") { out = r; return true; }
    }
    return false;
}

bool Session::command(const std::string& cmd, MIRecord* out) {
    console_.clear();
    int t = send(cmd);
    if (t < 0) return false;
    MIRecord r;
    if (!wait_result(t, r)) return false;
    if (out) *out = r;
    return r.cls == "done" || r.cls == "running" || r.cls == "connected";
}

// ---------------------------------------------------------------- lifecycle

bool Session::start(const std::string& code, Lang lang, const std::string& input, std::string& error) {
    stop();
    signal(SIGPIPE, SIG_IGN);
    state = ProgramState{};
    steps_ = 0;
    uninit_.clear();

    char tmpl[] = "/tmp/codelens-dbg-XXXXXX";
    if (!mkdtemp(tmpl)) { error = "Could not create a temporary directory"; return false; }
    dir_ = tmpl;
    file_ = lang == Lang::C ? "main.c" : "main.cpp";
    { std::ofstream(dir_ + "/" + file_) << code; }
    { std::ofstream(dir_ + "/cl_tracker.c") << TRACKER_SOURCE; }
    { std::ofstream(dir_ + "/stdin.txt") << input; }
    { std::ofstream(dir_ + "/cl_helper.py") << GDB_HELPER; }
    lines_ = split_code_lines(code);
    code_ = code;

    std::vector<std::string> cc = lang == Lang::C
        ? std::vector<std::string>{"gcc", "-std=c11"} : std::vector<std::string>{"g++", "-std=c++17"};
    // no stack protector: its setup code is attributed to the "{" line and would be the first stop
    for (const char* f : {"-g", "-O0", "-fno-omit-frame-pointer", "-fno-stack-protector",
                          "-fdiagnostics-color=never"}) cc.push_back(f);
    cc.push_back(dir_ + "/" + file_);
    cc.push_back(dir_ + "/cl_tracker.c");
    cc.push_back("-o");
    cc.push_back(dir_ + "/program");
    cc.push_back("-Wl,--wrap=malloc,--wrap=free,--wrap=calloc,--wrap=realloc");
    cc.push_back("-lm");
    ExecResult comp = exec_capture(cc, "", 20000);
    if (comp.exit_code != 0) {
        error = comp.err.empty() ? "Compilation failed" : comp.err;
        for (size_t p; (p = error.find(dir_ + "/")) != std::string::npos;) error.erase(p, dir_.size() + 1);
        return false;
    }

    int in_pipe[2], out_pipe[2];
    if (pipe(in_pipe) || pipe(out_pipe)) { error = "pipe failed"; return false; }
    pid_ = fork();
    if (pid_ < 0) { error = "fork failed"; return false; }
    if (pid_ == 0) {
        setpgid(0, 0);
        dup2(in_pipe[0], 0);
        dup2(out_pipe[1], 1);
        dup2(out_pipe[1], 2);
        close(in_pipe[0]); close(in_pipe[1]); close(out_pipe[0]); close(out_pipe[1]);
        if (chdir(dir_.c_str()) != 0) _exit(126);
        execlp("gdb", "gdb", "--interpreter=mi2", "-q", "-nx", "program", (char*)nullptr);
        _exit(127);
    }
    close(in_pipe[0]); close(out_pipe[1]);
    to_gdb_ = in_pipe[1];
    from_gdb_ = out_pipe[0];

    for (const char* setup : {"-gdb-set confirm off", "-gdb-set width 0", "-gdb-set height 0",
                              "-gdb-set print pretty off", "-gdb-set print repeats 1000",
                              "-gdb-set print elements 200", "-gdb-set print symbol off",
                              "-gdb-set print null-stop on",
                              "-exec-arguments < stdin.txt > out.txt 2>&1",
                              "-break-insert -f main"}) {
        if (!command(setup)) {
            error = "GDB is not available. Install it: sudo apt install gdb";
            stop();
            return false;
        }
    }
    // Optional: needs GDB built with Python (Ubuntu's is). Without it all variables are shown.
    have_decls_ = command("-interpreter-exec console \"source cl_helper.py\"");
    MIRecord r;
    if (!command("-exec-run", &r)) { error = "Could not start the program"; stop(); return false; }
    MIRecord stopped;
    if (!wait_stopped(stopped, STEP_TIMEOUT_MS)) { error = "The program did not reach main"; stop(); return false; }
    handle_stop(stopped);
    // Some compilers still stop on the "int main(void) {" line: move to the first statement.
    if (!state.finished && is_function_header(state.line)) exec("-exec-next");
    state.note = "The program is ready. Line " + std::to_string(state.line) + " runs next.";
    return true;
}

void Session::stop() {
    if (pid_ > 0) {
        if (to_gdb_ >= 0) {
            std::string q = "-gdb-exit\n";
            (void)!write(to_gdb_, q.data(), q.size());
        }
        for (int i = 0; i < 50; ++i) {                 // up to 0.5 s to exit cleanly
            if (waitpid(pid_, nullptr, WNOHANG) == pid_) { pid_ = -1; break; }
            usleep(10000);
        }
        if (pid_ > 0) { kill(-pid_, SIGKILL); waitpid(pid_, nullptr, 0); }
    }
    pid_ = -1;
    if (to_gdb_ >= 0) close(to_gdb_);
    if (from_gdb_ >= 0) close(from_gdb_);
    to_gdb_ = from_gdb_ = -1;
    buf_.clear();
    if (!dir_.empty()) {
        for (const char* f : {"/main.c", "/main.cpp", "/cl_tracker.c", "/program", "/stdin.txt", "/out.txt", "/cl_heap.txt",
                              "/cl_helper.py"})
            unlink((dir_ + f).c_str());
        rmdir(dir_.c_str());
        dir_.clear();
    }
}

// ---------------------------------------------------------------- stepping

bool Session::exec(const std::string& cmd) {
    if (!active() || state.finished) return false;
    if (++steps_ > MAX_STEPS) {
        state.finished = true;
        state.note = "Stopped after " + std::to_string(MAX_STEPS) + " steps.";
        return false;
    }
    ProgramState before = state;
    MIRecord r;
    if (!command(cmd, &r)) return false;
    MIRecord stopped;
    if (!wait_stopped(stopped, STEP_TIMEOUT_MS)) {
        // Probably an infinite loop inside this line: interrupt the program.
        kill(pid_, SIGINT);
        if (!wait_stopped(stopped, 1000)) { stop(); state.finished = true; return false; }
        refresh();
        state.finished = true;
        state.note = "This line did not finish in 3 seconds. Is there an infinite loop?";
        return true;
    }
    handle_stop(stopped);
    // Plain-English sentence about what the line did (crashes and the end set their own note).
    if (!state.finished) state.note = describe_step(before, state, before.line, code_);
    return true;
}

bool Session::run_to_line(int line) {
    if (!active() || state.finished) return false;
    // Already stopped at that line (e.g. the first line of main): a breakpoint there would
    // never be hit again and the program would run to the end.
    if (state.line == line && !state.stack.empty() && state.stack.size() == 1) return true;
    if (!command("-break-insert -t " + file_ + ":" + std::to_string(line))) return false;
    bool ok = exec("-exec-continue");
    if (ok && !state.finished) state.note = "Ran the program up to line " + std::to_string(line) + ".";
    return ok;
}

void Session::handle_stop(const MIRecord& stop) {
    std::string reason = stop.results.text("reason");
    const MIValue* frame = stop.results.get("frame");
    std::string file = frame ? frame->text("file") : "";

    if (reason == "exited-normally" || reason == "exited" || reason == "exited-signalled") {
        state.finished = true;
        state.exit_code = reason == "exited" ? std::atoi(stop.results.text("exit-code").c_str()) : 0;
        state.stack.clear();
        state.line = 0;
        if (active()) {
            state.output = read_file(dir_ + "/out.txt");
            // Final heap from the tracker; keep the cells we already read for blocks we know.
            std::istringstream in(read_file(dir_ + "/cl_heap.txt"));
            std::vector<HeapBlock> heap;
            std::string addr;
            size_t size;
            int freed;
            while (in >> addr >> size >> freed) {
                HeapBlock h;
                h.address = first_hex(addr);
                h.size = size;
                h.freed = freed != 0;
                for (const HeapBlock& old : state.heap)
                    if (old.address == h.address && old.size == h.size) h.cells = old.cells;
                heap.push_back(h);
            }
            if (in.eof() || !heap.empty()) state.heap = heap;
        }
        int leaks = 0;
        for (const HeapBlock& h : state.heap) leaks += !h.freed;
        state.note = "The program finished (exit code " + std::to_string(state.exit_code) + ").";
        if (leaks) {
            state.note += " " + std::to_string(leaks) + " heap block(s) were never freed: a memory leak.";
            for (const HeapBlock& h : state.heap)
                if (!h.freed) state.errors.push_back({"memory-leak", 0, std::to_string(h.size) + " bytes never freed"});
        }
        return;
    }
    if (reason == "signal-received") {
        std::string sig = stop.results.text("signal-name");
        refresh();
        state.finished = true;
        std::string what = sig == "SIGSEGV" ? "Segmentation fault: the program used an invalid pointer"
                         : sig == "SIGFPE"  ? "Arithmetic error (division by zero?)"
                         : sig == "SIGABRT" ? "The program aborted"
                         : "The program received " + sig;
        state.errors.push_back({sig == "SIGSEGV" ? "SEGV" : sig, state.line, what});
        state.note = what + (state.line ? " on line " + std::to_string(state.line) + "." : ".");
        return;
    }
    // Stopped outside the student's file:
    //  - "step" went into a library function (printf with debug info installed): finish it and
    //    come back to the student's code;
    //  - main has returned into the C library: run to the end.
    if (!is_user_file(file)) {
        MIRecord fr;
        bool user_below = false;
        if (command("-stack-list-frames", &fr))
            if (const MIValue* st = fr.results.get("stack"))
                for (const auto& kv : st->items) user_below |= is_user_file(kv.second.text("file"));
        if (user_below && ++leave_depth_ < 20) {
            MIRecord r, again;
            if (command("-exec-finish", &r) && wait_stopped(again, STEP_TIMEOUT_MS)) handle_stop(again);
        } else {
            exec("-exec-continue");
        }
        leave_depth_ = 0;
        return;
    }
    refresh();
}

// ---------------------------------------------------------------- reading state

std::string Session::eval(const std::string& expr, int frame) {
    std::string escaped;
    for (char c : expr) { if (c == '"' || c == '\\') escaped += '\\'; escaped += c; }
    std::string cmd = "-data-evaluate-expression ";
    if (frame >= 0) cmd += "--thread 1 --frame " + std::to_string(frame) + " ";
    MIRecord r;
    if (!command(cmd + "\"" + escaped + "\"", &r)) return "";
    return r.results.text("value");
}

std::string Session::type_of(const std::string& expr, int frame) {
    std::string escaped;
    for (char c : expr) { if (c == '"' || c == '\\') escaped += '\\'; escaped += c; }
    MIRecord r;
    if (!command("-var-create --thread 1 --frame " + std::to_string(frame) + " - * \"" + escaped + "\"", &r)) return "";
    std::string type = r.results.text("type");
    std::string name = r.results.text("name");
    if (!name.empty()) command("-var-delete " + name);
    return type;
}

std::map<std::string, std::pair<int, bool>> Session::declarations(int frame) {
    std::map<std::string, std::pair<int, bool>> out;
    if (!have_decls_) return out;
    if (!command("-stack-select-frame " + std::to_string(frame))) return out;
    if (!command("-interpreter-exec console \"cl-decls\"")) return out;
    size_t p = console_.find("CLDECL");
    if (p == std::string::npos) return out;
    std::string list = console_.substr(p + 6);
    std::istringstream in(list);
    std::string item;
    while (std::getline(in, item, ';')) {
        std::istringstream f(item);
        std::string name; int line = 0, arg = 0;
        if (f >> name >> line >> arg) out.emplace(name, std::make_pair(line, arg != 0));   // innermost block wins
    }
    command("-stack-select-frame 0");
    return out;
}

Variable Session::read_variable(const MIValue& mv, int level) {
    Variable v;
    v.name = mv.text("name");
    v.type = mv.text("type");
    v.value = mv.text("value");
    v.address = first_hex(eval("&(" + v.name + ")", level));

    bool aggregate = mv.get("value") == nullptr;   // --simple-values omits arrays/structs
    if (!aggregate && !v.type.empty() && v.type.back() == '*') {
        v.is_pointer = true;
        v.points_to = first_hex(v.value);
    }
    if (aggregate) {
        std::string full = eval(v.name, level);
        std::vector<std::string> parts = split_aggregate(full);
        bool is_array = v.type.find('[') != std::string::npos;
        uint64_t elem = is_array ? std::strtoull(eval("sizeof((" + v.name + ")[0])", level).c_str(), nullptr, 10) : 0;
        for (size_t i = 0; i < parts.size() && i < 64; ++i) {
            Variable c;
            if (is_array) {
                c.name = "[" + std::to_string(i) + "]";
                c.value = parts[i];
                c.address = v.address + i * elem;
                c.type = v.type.substr(0, v.type.find('['));
                while (!c.type.empty() && c.type.back() == ' ') c.type.pop_back();
            } else {
                size_t eq = parts[i].find(" = ");
                if (eq == std::string::npos) continue;
                c.name = parts[i].substr(0, eq);
                c.value = parts[i].substr(eq + 3);
                std::string field = "(" + v.name + ")." + c.name;
                c.address = first_hex(eval("&(" + field + ")", level));
                c.type = type_of(field, level);
            }
            if (!c.type.empty() && c.type.back() == '*') { c.is_pointer = true; c.points_to = first_hex(c.value); }
            v.children.push_back(c);
        }
        // char arrays print as a string ("Ann"), not as {...}: keep that as the value
        if (parts.empty()) v.value = full;
        else v.value.clear();
    }
    // C++: GDB's pretty printer shows "std::vector of length 3, capacity 4 = {3, 8, 1}".
    // Show it like an array: one box per element, and a short type name.
    static const std::regex vec(R"(^std::(vector|deque|list|array)(?: of length (\d+))?(?:, capacity \d+)?(?: = (\{.*\}))?$)");
    std::smatch m;
    if (v.children.empty() && std::regex_match(v.value, m, vec)) {
        std::string kind = m[1].str();
        size_t lt = v.type.find('<');
        std::string elem;
        if (lt != std::string::npos) {   // std::vector<int, std::allocator<int> > -> int
            int depth = 0;
            for (size_t i = lt + 1; i < v.type.size(); ++i) {
                char c = v.type[i];
                if (c == '<') ++depth;
                if (c == '>' && depth-- == 0) break;
                if (c == ',' && depth == 0) break;
                elem += c;
            }
        }
        while (!elem.empty() && elem.back() == ' ') elem.pop_back();
        if (!elem.empty()) v.type = "std::" + kind + "<" + elem + ">";
        if (m[3].matched)
            for (const std::string& part : split_aggregate(m[3].str())) {
                Variable c;
                c.name = "[" + std::to_string(v.children.size()) + "]";
                c.type = elem;
                c.value = part;
                v.children.push_back(c);
            }
        v.value = v.children.empty() ? "empty" : "";
    }
    return v;
}

void Session::fill_heap_cells() {
    // For a pointer that points at the start of a live block, show the block's contents
    // using the pointer's type: int *p = malloc(3 * sizeof(int)) -> int[3].
    for (HeapBlock& h : state.heap) {
        if (h.freed || h.size == 0) continue;
        for (size_t fi = 0; fi < state.stack.size() && h.cells.empty(); ++fi) {
            int level = (int)(state.stack.size() - 1 - fi);
            for (const Variable& v : state.stack[fi].vars) {
                if (!v.is_pointer || v.points_to != h.address) continue;
                if (v.type.find("void") != std::string::npos || v.type.find("char") != std::string::npos) continue;
                uint64_t esz = std::strtoull(eval("sizeof(*(" + v.name + "))", level).c_str(), nullptr, 10);
                if (esz == 0 || esz > h.size) continue;
                size_t n = std::min<size_t>(h.size / esz, 32);
                std::string elem_type = v.type.substr(0, v.type.size() - 1);
                while (!elem_type.empty() && elem_type.back() == ' ') elem_type.pop_back();
                std::vector<std::string> parts = n == 1
                    ? std::vector<std::string>{eval("*(" + v.name + ")", level)}
                    : split_aggregate(eval("*(" + v.name + ")@" + std::to_string(n), level));
                // a single struct (a list node): its fields become the cells, "value" and "next"
                if (n == 1 && parts.size() == 1 && !parts[0].empty() && parts[0][0] == '{' &&
                    elem_type.find('[') == std::string::npos) {
                    for (const std::string& part : split_aggregate(parts[0])) {
                        size_t eq = part.find(" = ");
                        if (eq == std::string::npos) continue;
                        Variable c;
                        c.name = part.substr(0, eq);
                        c.value = part.substr(eq + 3);
                        std::string field = "(*(" + v.name + "))." + c.name;
                        c.address = first_hex(eval("&(" + field + ")", level));
                        c.type = type_of(field, level);
                        if (!c.type.empty() && c.type.back() == '*') { c.is_pointer = true; c.points_to = first_hex(c.value); }
                        h.cells.push_back(c);
                    }
                    if (!h.cells.empty()) break;
                }
                for (size_t i = 0; i < parts.size(); ++i) {
                    Variable c;
                    c.name = "[" + std::to_string(i) + "]";
                    c.type = elem_type;
                    c.value = parts[i];
                    c.address = h.address + i * esz;
                    if (!c.type.empty() && c.type.back() == '*') { c.is_pointer = true; c.points_to = first_hex(c.value); }
                    h.cells.push_back(c);
                }
                break;
            }
        }
    }
}

void Session::refresh() {
    std::vector<MemoryError> keep_errors = state.errors;
    ProgramState s;
    s.errors = keep_errors;
    s.output = read_file(dir_ + "/out.txt");

    MIRecord r;
    if (command("-stack-list-frames", &r)) {
        const MIValue* stack = r.results.get("stack");
        std::vector<std::pair<int, Frame>> frames;   // (level, frame), innermost first
        if (stack)
            for (const auto& kv : stack->items) {
                const MIValue& f = kv.second;
                if (!is_user_file(f.text("file"))) continue;
                Frame fr;
                fr.function = f.text("func");
                fr.line = std::atoi(f.text("line").c_str());
                frames.emplace_back(std::atoi(f.text("level").c_str()), fr);
            }
        for (size_t idx = 0; idx < frames.size(); ++idx) {
            auto& lf = frames[idx];
            size_t depth = frames.size() - 1 - idx;   // 0 = main
            const Frame* old = depth < state.stack.size() && state.stack[depth].function == lf.second.function
                             ? &state.stack[depth] : nullptr;
            auto was_visible = [&](const std::string& name) {
                if (!old) return false;
                for (const Variable& v : old->vars) if (v.name == name) return true;
                return false;
            };
            std::map<std::string, std::pair<int, bool>> decls = declarations(lf.first);   // name -> (line, is_arg)
            MIRecord vr;
            if (command("-stack-list-variables --thread 1 --frame " + std::to_string(lf.first) + " --simple-values", &vr))
                if (const MIValue* vars = vr.results.get("variables"))
                    for (const auto& kv : vars->items) {
                        std::string name = kv.second.text("name");
                        auto d = decls.find(name);
                        bool is_arg = d != decls.end() && d->second.second;
                        bool visible_before = was_visible(name);
                        // Hide variables whose declaration line has not run yet: before it runs
                        // they only hold leftover garbage, which confuses beginners. Once shown,
                        // a variable stays (a for-loop's i is declared on the line we come back to).
                        if (d != decls.end() && !is_arg && d->second.first >= lf.second.line && !visible_before) continue;
                        Variable v = read_variable(kv.second, lf.first);

                        std::string key = std::to_string(depth) + "#" + lf.second.function + "#" + name;
                        if (!visible_before && !is_arg && d != decls.end() && declared_without_value(d->second.first, name))
                            uninit_[key] = signature(v);
                        auto u = uninit_.find(key);
                        if (u != uninit_.end()) {
                            if (u->second == signature(v)) mark_unknown(v);
                            else uninit_.erase(u);   // it got a value
                        }
                        lf.second.vars.push_back(v);
                    }
        }
        // forget "no value yet" marks of frames that returned
        for (auto it = uninit_.begin(); it != uninit_.end();)
            it = (size_t)std::atoi(it->first.c_str()) >= frames.size() ? uninit_.erase(it) : std::next(it);
        for (auto it = frames.rbegin(); it != frames.rend(); ++it) s.stack.push_back(it->second);
        if (!frames.empty()) s.line = frames.front().second.line;
    }

    // heap blocks recorded by the tracker
    int n = std::atoi(eval("cl_heap_n").c_str());
    for (int i = 0; i < n && i < 256; ++i) {
        std::string b = eval("cl_heap[" + std::to_string(i) + "]");
        std::smatch m;
        static const std::regex re(R"(addr = (0x[0-9a-f]+), size = (\d+), freed = (\d+))");
        if (std::regex_search(b, m, re)) {
            HeapBlock h;
            h.address = std::strtoull(m[1].str().c_str(), nullptr, 16);
            h.size = std::strtoull(m[2].str().c_str(), nullptr, 10);
            h.freed = m[3] == "1";
            s.heap.push_back(h);
        }
    }
    state = s;
    fill_heap_cells();
}

#endif
Session g_ui;   // the session the UI drives

}  // namespace

// ---------------------------------------------------------------- public API

bool debug_start(const std::string& code, Lang lang, const std::string& stdin_text, std::string& error) {
    return g_ui.start(code, lang, stdin_text, error);
}
bool debug_next()   { return g_ui.exec("-exec-next"); }
bool debug_step()   { return g_ui.exec("-exec-step"); }
bool debug_finish() {
    // -exec-finish from main would leave the student's code; treat it as "run to the end".
    if (g_ui.state.stack.size() <= 1) return g_ui.exec("-exec-continue");
    return g_ui.exec("-exec-finish");
}
bool debug_run_to_line(int line) { return g_ui.run_to_line(line); }
std::string debug_eval(const std::string& expr) { return g_ui.active() ? g_ui.eval(expr) : ""; }
const ProgramState& debug_state() { return g_ui.state; }
bool debug_active() { return g_ui.active() && !g_ui.state.finished; }
void debug_stop() { g_ui.stop(); }

// ---------------------------------------------------------------- predictions

static std::string lower_trim(std::string s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n;");
    s = a == std::string::npos ? "" : s.substr(a, b - a + 1);
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

Verdict check_prediction(const Step& step, Lang lang, const std::string& answer) {
    Verdict v;
    if (step.predict_expr.empty()) {   // predict the output
        RunResult r = run_code(step.code, step.input, lang);
        v.expected = r.output;
        std::string diff = describe_difference(r.output, answer);
        // The answer box has one line: accept output of several lines typed with spaces
        // ("Hi there 5") or with \n ("Hi there\n5").
        auto flat = [](std::string t) {
            for (size_t p; (p = t.find("\\n")) != std::string::npos;) t.replace(p, 2, " ");
            std::string out;
            for (char c : t) {
                bool space = c == ' ' || c == '\n' || c == '\t' || c == '\r';
                if (space) { if (!out.empty() && out.back() != ' ') out += ' '; }
                else out += c;
            }
            while (!out.empty() && out.back() == ' ') out.pop_back();
            return out;
        };
        std::string bare = flat(answer);   // "Cold" typed with quotes is still Cold
        if (bare.size() >= 2 && bare.front() == '"' && bare.back() == '"') bare = bare.substr(1, bare.size() - 2);
        if (!diff.empty() && r.compiled && (flat(r.output) == flat(answer) || flat(r.output) == bare)) diff.clear();
        v.correct = r.compiled && diff.empty();
        v.feedback = v.correct ? "Right, that is exactly what the program prints."
                               : "Not quite. " + diff;
        return v;
    }

    Session s;
    std::string err;
    if (!s.start(step.code, lang, step.input, err)) { v.feedback = "Could not run the example: " + err; return v; }
    if (!s.run_to_line(step.predict_line) || s.state.finished) {
        v.feedback = "The program never reached line " + std::to_string(step.predict_line) + ".";
        return v;
    }
    s.exec("-exec-next");   // run line N itself
    std::string raw = s.eval(step.predict_expr);
    if (raw.empty()) { v.feedback = "Could not evaluate " + step.predict_expr + "."; return v; }

    // What counts as the right answer, in the forms a student would write.
    std::vector<std::string> accepted;
    std::string shown = raw;
    size_t q = raw.find(" \"");
    if (raw.rfind("0x", 0) == 0 && q != std::string::npos) {          // char *: 0x55... "hello"
        shown = raw.substr(q + 1);
        accepted = {lower_trim(shown), lower_trim(shown.substr(1, shown.size() - 2))};
    } else if (raw.rfind("0x", 0) == 0 || raw.find("*) 0x") != std::string::npos) {   // pointer
        uint64_t a = first_hex(raw);
        std::string target = state_describe_address(s.state, a);
        shown = target.empty() ? raw : (a ? "&" + target : "NULL");
        accepted = {lower_trim(raw)};
        if (!target.empty()) {
            accepted.push_back(lower_trim(target));
            accepted.push_back(lower_trim("&" + target));
        }
        if (a == 0) { accepted.push_back("null"); accepted.push_back("0"); accepted.push_back("nullptr"); }
    } else {
        std::smatch m;
        static const std::regex ch(R"(^(-?\d+) '(.*)'$)");                 // char: 65 'A'
        if (std::regex_match(raw, m, ch)) {
            shown = "'" + m[2].str() + "' (" + m[1].str() + ")";
            accepted = {m[1].str(), lower_trim(m[2].str()), lower_trim("'" + m[2].str() + "'")};
        } else {
            accepted = {lower_trim(raw)};
        }
    }
    v.expected = shown;
    std::string a = lower_trim(answer);
    v.correct = std::find(accepted.begin(), accepted.end(), a) != accepted.end();
    std::string where = "after line " + std::to_string(step.predict_line) + " runs, " + step.predict_expr + " is " + shown;
    v.feedback = v.correct ? "Right: " + where + "." : "Not quite: " + where + ".";
    return v;
}
