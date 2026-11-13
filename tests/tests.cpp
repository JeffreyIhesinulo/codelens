// Engine unit tests. Run: ./build/tests   (exit code 0 = all passed)
#include "app.h"
#include "mi.h"

#include <arpa/inet.h>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

static int g_failed = 0, g_passed = 0;

#define CHECK(cond) do { \
    if (cond) ++g_passed; \
    else { ++g_failed; std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

#define TEST(name) static void name()
#define RUN(name)  do { std::printf("%s\n", #name); name(); } while (0)

// ---------------------------------------------------------------- run_code

TEST(test_hello) {
    RunResult r = run_code("#include <stdio.h>\nint main(void){ printf(\"hi\\n\"); return 0; }\n", "", Lang::C);
    CHECK(r.compiled);
    CHECK(r.output == "hi\n");
    CHECK(r.exit_code == 0);
    CHECK(!r.timed_out && !r.crashed);
}

TEST(test_stdin) {
    RunResult r = run_code("#include <stdio.h>\nint main(void){ int a,b; scanf(\"%d %d\",&a,&b); printf(\"%d\\n\",a+b); }\n",
                           "2 40\n", Lang::C);
    CHECK(r.output == "42\n");
}

TEST(test_cpp) {
    RunResult r = run_code("#include <iostream>\n#include <vector>\nint main(){ std::vector<int> v{1,2,3}; std::cout << v.size() << \"\\n\"; }\n",
                           "", Lang::CPP);
    CHECK(r.compiled);
    CHECK(r.output == "3\n");
}

TEST(test_compile_error_lines) {
    RunResult r = run_code("#include <stdio.h>\nint main(void) {\n    int x = 5\n    return 0;\n}\n", "", Lang::C);
    CHECK(!r.compiled);
    CHECK(r.compile_errors.find("main.c:") != std::string::npos);
    CHECK(r.compile_errors.find("/tmp/codelens-") == std::string::npos);   // temp path hidden
    CHECK(!r.error_lines.empty());
}

TEST(test_timeout) {
    auto t0 = std::chrono::steady_clock::now();
    RunResult r = run_code("int main(void){ while(1){} }\n", "", Lang::C, 1000);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(r.compiled);
    CHECK(r.timed_out);
    CHECK(ms < 8000);
}

TEST(test_program_ignores_stdin) {
    // exits immediately without reading a big input: must not kill us with SIGPIPE
    std::string big(200000, 'x');
    RunResult r = run_code("int main(void){ return 0; }\n", big, Lang::C);
    CHECK(r.compiled);
    CHECK(r.exit_code == 0);
}

TEST(test_heap_overflow) {
    RunResult r = run_code(
        "#include <stdlib.h>\n"
        "int main(void) {\n"
        "    int *a = malloc(3 * sizeof(int));\n"
        "    a[3] = 1;\n"
        "    free(a);\n"
        "    return 0;\n"
        "}\n", "", Lang::C);
    CHECK(r.compiled);
    CHECK(!r.memory_errors.empty());
    if (!r.memory_errors.empty()) {
        CHECK(r.memory_errors[0].kind == "heap-buffer-overflow");
        CHECK(r.memory_errors[0].line == 4);
    }
}

TEST(test_use_after_free) {
    RunResult r = run_code(
        "#include <stdlib.h>\n"
        "#include <stdio.h>\n"
        "int main(void) {\n"
        "    int *p = malloc(sizeof(int));\n"
        "    free(p);\n"
        "    printf(\"%d\\n\", *p);\n"
        "    return 0;\n"
        "}\n", "", Lang::C);
    CHECK(!r.memory_errors.empty());
    if (!r.memory_errors.empty()) {
        CHECK(r.memory_errors[0].kind == "heap-use-after-free");
        CHECK(r.memory_errors[0].line == 6);
    }
}

TEST(test_null_deref) {
    RunResult r = run_code(
        "#include <stddef.h>\n"
        "int main(void) {\n"
        "    int *p = NULL;\n"
        "    return *p;\n"
        "}\n", "", Lang::C);
    CHECK(!r.memory_errors.empty());
    if (!r.memory_errors.empty()) CHECK(r.memory_errors[0].line == 4);
}

TEST(test_leak) {
    RunResult r = run_code(
        "#include <stdlib.h>\n"
        "int main(void) {\n"
        "    void *p = malloc(40);\n"
        "    p = 0;\n"
        "    return 0;\n"
        "}\n", "", Lang::C);
    bool found = false;
    for (const MemoryError& e : r.memory_errors)
        if (e.kind == "memory-leak") { found = true; CHECK(e.line == 3); }
    // LeakSanitizer does not work under ptrace/some containers; only warn.
    if (!found) std::printf("  note: leak not detected (LeakSanitizer unavailable here?)\n");
}

// ---------------------------------------------------------------- lessons

TEST(test_lesson_parse) {
    const char* text =
        "@id 99_x\n@title X\n@lang cpp\n"
        "@theory\nline one\n\n    indented\n"
        "@starter\nint main(){}\n"
        "@test\n@input\n1 2\n@expected\n3\n"
        "@hidden_test\n@expected\nok\n";
    Lesson l; std::string err;
    CHECK(lesson_parse(text, l, err));
    CHECK(l.id == "99_x");
    CHECK(l.lang == Lang::CPP);
    CHECK(l.theory == "line one\n\n    indented");
    CHECK(l.tests.size() == 2);
    CHECK(l.tests[0].input == "1 2\n");
    CHECK(l.tests[0].expected == "3\n");
    CHECK(!l.tests[0].hidden && l.tests[1].hidden);
    CHECK(l.tests[1].input.empty());

    Lesson bad;
    CHECK(!lesson_parse("@title no id\n@test\n@expected\n1\n", bad, err));
    CHECK(!lesson_parse("@id a\n@title a\n@lang rust\n", bad, err));
}

TEST(test_all_lessons_solvable) {
    std::vector<Lesson> lessons = lessons_load(LESSONS_DIR);
    CHECK(lessons.size() >= 5);
    for (const Lesson& l : lessons) {
        TestResult good = check_code(l, l.solution);
        if (good.passed != good.total)
            std::printf("  lesson %s: solution failed: %s\n", l.id.c_str(), good.first_failure.c_str());
        CHECK(good.total > 0 && good.passed == good.total);

        TestResult starter = check_code(l, l.starter_code);
        CHECK(starter.passed < starter.total);   // starter code must not already pass
    }
}

TEST(test_all_lesson_steps_valid) {
    for (const Lesson& l : lessons_load(LESSONS_DIR)) {
        for (size_t i = 0; i < l.steps.size(); ++i) {
            const Step& st = l.steps[i];
            std::string where = l.id + " step " + std::to_string(i + 1) + " (" + step_kind_name(st.kind) + ")";
            if (st.kind == StepKind::Predict) {
                // the right answer can be computed, and giving it back counts as correct
                Verdict v = check_prediction(st, l.lang, "");
                Verdict again = check_prediction(st, l.lang, v.expected);
                if (!again.correct) std::printf("  %s: %s\n", where.c_str(), v.feedback.c_str());
                CHECK(!v.expected.empty() && again.correct);
            }
            if (st.kind == StepKind::FindBug || st.kind == StepKind::Modify) {
                Lesson tmp = l; tmp.tests = st.tests;
                TestResult good = check_code(tmp, st.solution), bad = check_code(tmp, st.starter);
                if (good.passed != good.total) std::printf("  %s: solution fails: %s\n", where.c_str(), good.first_failure.c_str());
                CHECK(good.passed == good.total);
                CHECK(bad.passed < bad.total);   // the starter really has a bug / missing change
            }
            if (st.kind == StepKind::Watch) {
                std::string err;
                CHECK(debug_start(st.code, l.lang, st.input, err));
                debug_stop();
            }
        }
    }
}

TEST(test_check_messages) {
    Lesson l; std::string err;
    lesson_parse("@id t\n@title t\n@test\n@input\n2\n@expected\n4\n@hidden_test\n@input\n5\n@expected\n10\n", l, err);

    TestResult wrong = check_code(l, "#include <stdio.h>\nint main(void){int n;scanf(\"%d\",&n);printf(\"%d\\n\",n+2);}\n");
    CHECK(wrong.passed == 1);   // 2+2 == 4 passes, 5+2 != 10 fails
    CHECK(wrong.first_failure.find("Test 2") != std::string::npos);
    CHECK(wrong.first_failure.find("hidden") != std::string::npos);
    CHECK(wrong.first_failure.find("Input") == std::string::npos);   // hidden input not shown

    TestResult broken = check_code(l, "int main(void) { return }\n");
    CHECK(broken.passed == 0);
    CHECK(broken.first_failure.find("does not compile") != std::string::npos);
}

TEST(test_describe_difference) {
    // the bug from lesson 1: missing "." at the end of line 2
    std::string d = describe_difference("Hello, CodeLens!\nI am learning C.\n", "Hello, CodeLens!\nI am learning C");
    CHECK(d.find("line 2") != std::string::npos);
    CHECK(d.find("missing \".\" at the end") != std::string::npos);

    d = describe_difference("Hello\n", "hello\n");
    CHECK(d.find("expected \"H\" but got \"h\" at column 1") != std::string::npos);

    d = describe_difference("a b\n", "a  b\n");
    CHECK(d.find("expected \"b\" but got a space at column 3") != std::string::npos);

    d = describe_difference("1\n2\n", "1\n");
    CHECK(d.find("missing line 2") != std::string::npos);

    d = describe_difference("1\n", "1\n2\n");
    CHECK(d.find("extra line 2") != std::string::npos);

    CHECK(describe_difference("ok\n", "ok   \n\n").empty());   // trailing spaces / lines ignored

    // the message the student sees contains the explanation
    Lesson l; std::string err;
    lesson_parse("@id t\n@title t\n@test\n@expected\nHello, CodeLens!\nI am learning C.\n", l, err);
    TestResult r = check_code(l, "#include <stdio.h>\nint main(void){ printf(\"Hello, CodeLens!\\nI am learning C\"); return 0; }\n");
    CHECK(r.passed == 0);
    CHECK(r.first_failure.find("missing \".\" at the end of the line") != std::string::npos);
}

TEST(test_progress) {
    Progress p;
    p.completed = {"01_hello", "03_loops"};
    progress_save("/tmp/codelens_test_progress.txt", p);
    Progress q = progress_load("/tmp/codelens_test_progress.txt");
    CHECK(q.completed == p.completed);
    CHECK(progress_load("/tmp/does/not/exist").completed.empty());
}

// ---------------------------------------------------------------- AI (fake Ollama)

// Minimal fake Ollama: answers every request with a chunked JSON response.
static void fake_ollama(int listen_fd, int requests) {
    for (int i = 0; i < requests; ++i) {
        int c = accept(listen_fd, nullptr, nullptr);
        if (c < 0) return;
        char buf[65536];
        std::string req;
        ssize_t n;
        while ((n = recv(c, buf, sizeof buf, 0)) > 0) {
            req.append(buf, (size_t)n);
            size_t h = req.find("\r\n\r\n");
            if (h == std::string::npos) continue;
            size_t cl = req.find("Content-Length: ");
            size_t len = cl == std::string::npos ? 0 : std::stoul(req.substr(cl + 16));
            if (req.size() >= h + 4 + len) break;
        }
        std::string body = req.find("/api/chat") != std::string::npos
            ? R"({"message":{"role":"assistant","content":"Check line 4: you read past the end."},"done":true})"
            : R"({"version":"0.0.0"})";
        char size_hex[16];
        std::snprintf(size_hex, sizeof size_hex, "%zx", body.size());
        std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                           "Transfer-Encoding: chunked\r\n\r\n" + std::string(size_hex) + "\r\n" + body + "\r\n0\r\n\r\n";
        send(c, resp.data(), resp.size(), MSG_NOSIGNAL);
        close(c);
    }
}

TEST(test_ai_fake_server) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    CHECK(bind(fd, (sockaddr*)&addr, sizeof addr) == 0);
    socklen_t len = sizeof addr;
    getsockname(fd, (sockaddr*)&addr, &len);
    listen(fd, 8);
    int port = ntohs(addr.sin_port);

    std::thread server(fake_ollama, fd, 2);   // 1 health check + 1 chat
    ai_init("127.0.0.1", port, "test-model");
    CHECK(ai_model() == "test-model");

    Lesson l; l.title = "Arrays";
    int id = ai_chat(l, "int main(){}", "Test 1 failed", "why?");
    std::string answer;
    bool got = false;
    for (int i = 0; i < 100 && !got; ++i) {
        got = ai_poll(id, answer);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    CHECK(got);
    CHECK(answer == "Check line 4: you read past the end.");
    CHECK(ai_available());
    CHECK(!ai_poll(id, answer));   // an answer is delivered once

    server.join();
    close(fd);
}


// ---------------------------------------------------------------- GDB/MI parser

TEST(test_mi_parser) {
    MIRecord r;
    CHECK(mi_parse_line("12^done,stack=[frame={level=\"0\",func=\"swap\",file=\"main.c\",line=\"5\"},"
                        "frame={level=\"1\",func=\"main\",file=\"main.c\",line=\"12\"}]", r));
    CHECK(r.type == '^' && r.token == 12 && r.cls == "done");
    const MIValue* st = r.results.get("stack");
    CHECK(st && st->kind == MIValue::List && st->items.size() == 2);
    if (st && st->items.size() == 2) {
        CHECK(st->items[0].first == "frame");
        CHECK(st->items[0].second.text("func") == "swap");
        CHECK(st->items[1].second.text("line") == "12");
    }

    CHECK(mi_parse_line("*stopped,reason=\"exited\",exit-code=\"01\"", r));
    CHECK(r.type == '*' && r.cls == "stopped" && r.results.text("exit-code") == "01");

    CHECK(mi_parse_line("~\"hello \\\"world\\\"\\n\"", r));
    CHECK(r.type == '~' && r.stream == "hello \"world\"\n");

    CHECK(mi_parse_line("5^done,variables=[{name=\"x\",type=\"int\",value=\"3\"},{name=\"a\",type=\"int [3]\"}]", r));
    const MIValue* vars = r.results.get("variables");
    CHECK(vars && vars->items.size() == 2);
    if (vars && vars->items.size() == 2) {
        CHECK(vars->items[0].second.text("value") == "3");
        CHECK(vars->items[1].second.get("value") == nullptr);
    }

    CHECK(mi_parse_line("(gdb) ", r) && r.type == '(');
    CHECK(mi_parse_line("^done", r) && r.cls == "done");
    CHECK(!mi_parse_line("5 3", r));   // program output, not MI
}

// ---------------------------------------------------------------- steps

TEST(test_lesson_steps) {
    const char* text =
        "@id 07_x\n@title X\n@lang c\n"
        "@theory\nPractice task text\n@starter\nint main(void){}\n"
        "@test\n@input\n1\n@expected\n2\n"
        "@step explore\n@scene array   # sliders\n@text\nPlay with it.\n"
        "@step watch\n@title Watch swap\n@code\nint main(void){ return 0; }\n@input\n5\n"
        "@step predict\n@code\nint main(void){ int x = 1; x++; return 0; }\n@line 1\n@expr x\n@text\nWhat is x?\n"
        "@step find_bug\n@starter\nbuggy\n@solution\nfixed\n@test\n@expected\nok\n"
        "@step explain\n@text\nWhy?\n@rubric\naddress; copy\n@answer\nBecause.\n"
        "@step practice\n";
    Lesson l; std::string err;
    CHECK(lesson_parse(text, l, err));
    if (!err.empty()) std::printf("  parse error: %s\n", err.c_str());
    CHECK(l.steps.size() == 6);
    if (l.steps.size() == 6) {
        CHECK(l.steps[0].kind == StepKind::Explore && l.steps[0].scene == "array");
        CHECK(l.steps[1].title == "Watch swap" && l.steps[1].input == "5\n");
        CHECK(l.steps[2].predict_line == 1 && l.steps[2].predict_expr == "x" && l.steps[2].text == "What is x?");
        CHECK(l.steps[3].kind == StepKind::FindBug && l.steps[3].tests.size() == 1 && l.steps[3].tests[0].expected == "ok\n");
        CHECK(l.steps[4].rubric == "address; copy" && l.steps[4].sample_answer == "Because.");
        CHECK(l.steps[5].kind == StepKind::Practice);
        CHECK(l.steps[5].tests.size() == 1 && l.steps[5].text == "Practice task text");   // filled from lesson level
    }
    CHECK(l.title == "X");   // step @title does not overwrite the lesson title

    // old format -> one practice step
    Lesson old;
    CHECK(lesson_parse("@id a\n@title A\n@starter\nx\n@test\n@expected\n1\n", old, err));
    CHECK(old.steps.size() == 1 && old.steps[0].kind == StepKind::Practice && old.steps[0].starter == "x\n");

    Lesson bad;
    CHECK(!lesson_parse("@id a\n@title A\n@step dance\n", bad, err));
    CHECK(err.find("unknown step kind") != std::string::npos);
    CHECK(!lesson_parse("@id a\n@title A\n@step predict\n@expr x\n@code\nint main(){}\n", bad, err));
    CHECK(err.find("@line") != std::string::npos);
    CHECK(!lesson_parse("@id a\n@title A\n@step explore\n", bad, err));
    CHECK(!lesson_parse("@id a\n@title A\n@code\nx\n", bad, err));   // @code outside a step
}

// ---------------------------------------------------------------- ProgramState JSON

TEST(test_state_json) {
    ProgramState s;
    Frame f; f.function = "main"; f.line = 7;
    Variable x; x.name = "x"; x.type = "int"; x.value = "5"; x.address = 0x7ffffffde00cULL;
    Variable p; p.name = "p"; p.type = "int *"; p.value = "0x7ffffffde00c"; p.address = 0x7ffffffde010ULL;
    p.is_pointer = true; p.points_to = x.address;
    Variable a; a.name = "a"; a.type = "int [2]"; a.address = 0x7ffffffde020ULL;
    Variable a0; a0.name = "[0]"; a0.value = "1"; a0.address = 0x7ffffffde020ULL;
    Variable a1; a1.name = "[1]"; a1.value = "2"; a1.address = 0x7ffffffde024ULL;
    a.children = {a0, a1};
    f.vars = {x, p, a};
    s.stack.push_back(f);
    HeapBlock h; h.address = 0x55555555a2a0ULL; h.size = 12; h.freed = true;
    s.heap.push_back(h);
    s.errors.push_back({"heap-use-after-free", 9, "used after free"});
    s.note = "hi";

    ProgramState back; std::string err;
    CHECK(state_from_json(state_to_json(s), back, err));
    CHECK(back.stack.size() == 1 && back.stack[0].vars.size() == 3);
    CHECK(back.stack[0].vars[1].points_to == 0x7ffffffde00cULL);   // 64-bit address survives
    CHECK(back.stack[0].vars[2].children.size() == 2);
    CHECK(back.heap.size() == 1 && back.heap[0].freed && back.heap[0].size == 12);
    CHECK(back.errors.size() == 1 && back.errors[0].line == 9);
    CHECK(back.note == "hi");

    CHECK(state_describe_address(s, 0x7ffffffde00cULL) == "x");
    CHECK(state_describe_address(s, 0x7ffffffde024ULL) == "a[1]");
    CHECK(state_describe_address(s, 0x7ffffffde020ULL) == "a[0]");
    CHECK(state_describe_address(s, 0x55555555a2a4ULL) == "heap block 1 (freed)");
    CHECK(state_describe_address(s, 0) == "NULL");
    CHECK(!state_from_json("{not json", back, err));
}

// ---------------------------------------------------------------- debugger

static const Variable* find_var(const ProgramState& s, const std::string& name) {
    for (auto f = s.stack.rbegin(); f != s.stack.rend(); ++f)
        for (const Variable& v : f->vars) if (v.name == name) return &v;
    return nullptr;
}

TEST(test_debugger_swap) {
    const char* code =
        "#include <stdio.h>\n"                       // 1
        "void swap(int *a, int *b) {\n"              // 2
        "    int t = *a;\n"                          // 3
        "    *a = *b;\n"                             // 4
        "    *b = t;\n"                              // 5
        "}\n"                                        // 6
        "int main(void) {\n"                         // 7
        "    int x = 3, y = 5;\n"                    // 8
        "    swap(&x, &y);\n"                        // 9
        "    printf(\"%d %d\\n\", x, y);\n"          // 10
        "    return 0;\n"                            // 11
        "}\n";
    std::string err;
    CHECK(debug_start(code, Lang::C, "", err));
    if (!err.empty()) { std::printf("  %s\n", err.c_str()); return; }
    CHECK(debug_state().line == 8);
    CHECK(debug_next());
    CHECK(debug_state().line == 9);
    CHECK(debug_step());                                   // into swap
    const ProgramState& s = debug_state();
    CHECK(s.stack.size() == 2 && s.stack.back().function == "swap");
    const Variable* a = find_var(s, "a");
    CHECK(a && a->is_pointer && state_describe_address(s, a->points_to) == "x");
    CHECK(debug_finish());                                 // back to main
    CHECK(debug_state().stack.size() == 1 && debug_state().line == 10);
    const Variable* x = find_var(debug_state(), "x");
    CHECK(x && x->value == "5");
    CHECK(debug_next());
    CHECK(debug_state().output == "5 3\n");
    while (debug_active()) debug_next();
    CHECK(debug_state().finished && debug_state().exit_code == 0);
    debug_stop();
}

TEST(test_debugger_heap_and_leak) {
    const char* code =
        "#include <stdlib.h>\n"                      // 1
        "int main(void) {\n"                         // 2
        "    int *a = malloc(3 * sizeof(int));\n"    // 3
        "    a[0] = 7; a[1] = 8; a[2] = 9;\n"        // 4
        "    int *b = malloc(16);\n"                 // 5
        "    free(a);\n"                             // 6
        "    return 0;\n"                            // 7
        "}\n";
    std::string err;
    CHECK(debug_start(code, Lang::C, "", err));
    CHECK(debug_run_to_line(5));
    const ProgramState& s = debug_state();
    CHECK(s.heap.size() == 1);
    if (s.heap.size() == 1) {
        CHECK(s.heap[0].size == 12 && !s.heap[0].freed);
        CHECK(s.heap[0].cells.size() == 3 && s.heap[0].cells[2].value == "9");
    }
    CHECK(debug_run_to_line(7));
    CHECK(debug_state().heap.size() == 2 && debug_state().heap[0].freed && !debug_state().heap[1].freed);
    while (debug_active()) debug_next();
    bool leak = false;
    for (const MemoryError& e : debug_state().errors) leak |= e.kind == "memory-leak";
    CHECK(leak);   // b was never freed
    debug_stop();
}

TEST(test_debugger_segfault) {
    const char* code =
        "#include <stddef.h>\n"
        "int main(void) {\n"
        "    int *p = NULL;\n"
        "    *p = 5;\n"
        "    return 0;\n"
        "}\n";
    std::string err;
    CHECK(debug_start(code, Lang::C, "", err));
    while (debug_active()) debug_next();
    const ProgramState& s = debug_state();
    CHECK(s.finished);
    CHECK(!s.errors.empty() && s.errors[0].kind == "SEGV" && s.errors[0].line == 4);
    debug_stop();
}

TEST(test_debugger_infinite_loop) {
    const char* code =
        "void spin(void) {\n"        // 1
        "    while (1) {}\n"         // 2
        "}\n"                        // 3
        "int main(void) {\n"         // 4
        "    spin();\n"              // 5
        "    return 0;\n"            // 6
        "}\n";
    std::string err;
    CHECK(debug_start(code, Lang::C, "", err));
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 3 && debug_active(); ++i) debug_next();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(ms < 9000);
    // "next" over spin() never returns: the debugger interrupts it and stops with a note
    CHECK(debug_state().finished);
    CHECK(debug_state().note.find("infinite loop") != std::string::npos);
    debug_stop();
}

TEST(test_debugger_recursion_and_stdin) {
    const char* code =
        "#include <stdio.h>\n"                                       // 1
        "int fact(int n) {\n"                                        // 2
        "    if (n <= 1) return 1;\n"                                // 3
        "    return n * fact(n - 1);\n"                              // 4
        "}\n"                                                        // 5
        "int main(void) {\n"                                         // 6
        "    int n;\n"                                               // 7
        "    scanf(\"%d\", &n);\n"                                   // 8
        "    printf(\"%d\\n\", fact(n));\n"                          // 9
        "    return 0;\n"                                            // 10
        "}\n";
    std::string err;
    CHECK(debug_start(code, Lang::C, "4\n", err));
    CHECK(debug_run_to_line(3));           // first time: n = 4
    CHECK(debug_run_to_line(3));           // n = 3
    CHECK(debug_run_to_line(3));           // n = 2
    CHECK(debug_run_to_line(3));           // n = 1
    const ProgramState& s = debug_state();
    CHECK(s.stack.size() == 5);            // main + fact(4..1)
    const Variable* n = s.stack.empty() ? nullptr : (s.stack.back().vars.empty() ? nullptr : &s.stack.back().vars[0]);
    CHECK(n && n->value == "1");
    while (debug_active()) debug_next();
    CHECK(debug_state().output == "24\n");
    debug_stop();
}

TEST(test_debugger_compile_error) {
    std::string err;
    CHECK(!debug_start("int main(void) { return }\n", Lang::C, "", err));
    CHECK(err.find("main.c") != std::string::npos);
    CHECK(err.find("/tmp/") == std::string::npos);
    CHECK(!debug_active());
}

TEST(test_fixtures_load) {
    for (const char* name : {"swap", "array", "struct", "heap", "leak", "recursion", "null_crash"}) {
        std::ifstream f(std::string(FIXTURES_DIR) + "/" + name + ".json");
        std::stringstream ss; ss << f.rdbuf();
        ProgramState s; std::string err;
        bool ok = state_from_json(ss.str(), s, err);
        if (!ok) std::printf("  %s: %s\n", name, err.c_str());
        CHECK(ok && !s.note.empty());
    }
    std::ifstream f(std::string(FIXTURES_DIR) + "/swap.json");
    std::stringstream ss; ss << f.rdbuf();
    ProgramState s; std::string err;
    state_from_json(ss.str(), s, err);
    const Variable* a = find_var(s, "a");
    CHECK(a && state_describe_address(s, a->points_to) == "x");
}

// ---------------------------------------------------------------- predictions

TEST(test_check_prediction) {
    Step s;
    s.kind = StepKind::Predict;
    s.code =
        "#include <stdio.h>\n"                       // 1
        "int main(void) {\n"                         // 2
        "    int x = 3;\n"                           // 3
        "    int *p = &x;\n"                         // 4
        "    *p = 10;\n"                             // 5
        "    char c = 'A' + 2;\n"                    // 6
        "    printf(\"%d %c\\n\", x, c);\n"          // 7
        "    return 0;\n"                            // 8
        "}\n";
    s.predict_line = 5; s.predict_expr = "x";
    CHECK(check_prediction(s, Lang::C, "10").correct);
    Verdict wrong = check_prediction(s, Lang::C, "3");
    CHECK(!wrong.correct && wrong.expected == "10");
    CHECK(wrong.feedback.find("after line 5") != std::string::npos);

    s.predict_line = 4; s.predict_expr = "p";      // pointers: answer with the target
    CHECK(check_prediction(s, Lang::C, "&x").correct);
    CHECK(check_prediction(s, Lang::C, "x").correct);
    CHECK(!check_prediction(s, Lang::C, "3").correct);

    s.predict_line = 6; s.predict_expr = "c";      // chars: 'C', C or 67
    CHECK(check_prediction(s, Lang::C, "'C'").correct);
    CHECK(check_prediction(s, Lang::C, "67").correct);

    s.predict_expr = "";                           // predict the output
    CHECK(check_prediction(s, Lang::C, "10 C").correct);
    Verdict out = check_prediction(s, Lang::C, "10 A");
    CHECK(!out.correct && out.feedback.find("column 4") != std::string::npos);
}

int main() {
    RUN(test_hello);
    RUN(test_stdin);
    RUN(test_cpp);
    RUN(test_compile_error_lines);
    RUN(test_timeout);
    RUN(test_program_ignores_stdin);
    RUN(test_heap_overflow);
    RUN(test_use_after_free);
    RUN(test_null_deref);
    RUN(test_leak);
    RUN(test_lesson_parse);
    RUN(test_all_lessons_solvable);
    RUN(test_check_messages);
    RUN(test_describe_difference);
    RUN(test_progress);
    RUN(test_ai_fake_server);
    RUN(test_mi_parser);
    RUN(test_lesson_steps);
    RUN(test_state_json);
    RUN(test_debugger_swap);
    RUN(test_debugger_heap_and_leak);
    RUN(test_debugger_segfault);
    RUN(test_debugger_infinite_loop);
    RUN(test_debugger_recursion_and_stdin);
    RUN(test_debugger_compile_error);
    RUN(test_check_prediction);
    RUN(test_all_lesson_steps_valid);
    RUN(test_fixtures_load);

    std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
