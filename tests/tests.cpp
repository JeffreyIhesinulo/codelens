// Engine unit tests. Run: ./build/tests   (exit code 0 = all passed)
#include "app.h"

#include <arpa/inet.h>
#include <chrono>
#include <cstdio>
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
    int id = ai_chat(l, "int main(){}", "why?");
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
    RUN(test_progress);
    RUN(test_ai_fake_server);

    std::printf("\n%d passed, %d failed\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
