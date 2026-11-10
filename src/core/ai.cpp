// AI tutor: talks to a local Ollama server (http://127.0.0.1:11434) over plain HTTP.
// Every request runs on a background thread so the UI never freezes.
#include "app.h"

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <map>
#include <mutex>
#include <netdb.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

static const char* DEFAULT_MODEL = "qwen2.5-coder:3b";

namespace {
struct State {
    std::string host = "127.0.0.1";
    int port = 11434;
    std::string model = DEFAULT_MODEL;
    std::atomic<bool> available{false};
    std::atomic<bool> started{false};
    std::mutex mu;
    std::map<int, std::string> done;   // finished answers
    int next_id = 1;
};
// Intentionally never destroyed: detached worker threads may still finish after main() returns.
State& S() { static State* s = new State(); return *s; }
}

// ---------------------------------------------------------------- HTTP

static int connect_to(const std::string& host, int port, int timeout_sec) {
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0) return -1;
    int fd = -1;
    for (addrinfo* p = res; p; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        timeval tv{timeout_sec, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        close(fd); fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

static std::string dechunk(const std::string& body) {
    std::string out;
    size_t pos = 0;
    while (pos < body.size()) {
        size_t eol = body.find("\r\n", pos);
        if (eol == std::string::npos) break;
        size_t len = std::strtoul(body.substr(pos, eol - pos).c_str(), nullptr, 16);
        if (len == 0) break;
        out += body.substr(eol + 2, len);
        pos = eol + 2 + len + 2;
    }
    return out;
}

// Returns the response body, or "" with err set.
static std::string http_request(const std::string& method, const std::string& path,
                                const std::string& body, int timeout_sec, std::string& err) {
    int fd = connect_to(S().host, S().port, timeout_sec);
    if (fd < 0) { err = "connect"; return ""; }

    std::string req = method + " " + path + " HTTP/1.1\r\n"
                      "Host: " + S().host + "\r\n"
                      "Content-Type: application/json\r\n"
                      "Content-Length: " + std::to_string(body.size()) + "\r\n"
                      "Connection: close\r\n\r\n" + body;
    for (size_t sent = 0; sent < req.size();) {
        ssize_t n = send(fd, req.data() + sent, req.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) { close(fd); err = "send"; return ""; }
        sent += (size_t)n;
    }
    std::string resp;
    char buf[8192];
    for (ssize_t n; (n = recv(fd, buf, sizeof buf, 0)) > 0;) resp.append(buf, (size_t)n);
    close(fd);

    size_t hdr_end = resp.find("\r\n\r\n");
    if (hdr_end == std::string::npos) { err = "timeout or bad response"; return ""; }
    std::string headers = resp.substr(0, hdr_end);
    std::string payload = resp.substr(hdr_end + 4);
    for (char& c : headers) c = (char)std::tolower((unsigned char)c);
    if (headers.find("transfer-encoding: chunked") != std::string::npos) payload = dechunk(payload);
    return payload;
}

// ---------------------------------------------------------------- prompts

static const char* SYSTEM_PROMPT =
    "You are CodeLens, a friendly tutor inside an app that teaches C and C++ to beginners. "
    "Rules: answer in plain English, short (at most 5 sentences unless asked for more). "
    "Never write the complete solution to the student's exercise. "
    "You may show at most 2 lines of code to illustrate a point. "
    "Refer to line numbers when they help. Do not use markdown headings. "
    "The lesson's tests are always correct: never suggest that the test or the checker is wrong. "
    "When a test fails with wrong output, compare the expected and actual output character by character "
    "(punctuation, spaces, capital letters, missing or extra lines) and point to the exact difference. "
    "The app's own diff line ('First difference on line ...') is reliable; build on it.";

// Lesson task + the latest result the student saw, shared by hint and chat.
static std::string context(const Lesson& lesson, const std::string& code, const std::string& last_result) {
    std::string s = "Lesson \"" + lesson.title + "\" (" + std::string(lesson.lang == Lang::C ? "C" : "C++") + ").\n"
                    "Task and theory:\n" + lesson.theory + "\n\n"
                    "Student's code (with line numbers):\n" + code + "\n";
    if (!last_result.empty()) s += "\nLatest result the student got:\n" + last_result + "\n";
    else s += "\nThe student has not run the tests yet, or the last run had no problems.\n";
    return s;
}

static std::string ask(const std::string& user_message) {
    json req = {
        {"model", S().model},
        {"stream", false},
        {"options", {{"temperature", 0.3}}},
        {"messages", json::array({
            {{"role", "system"}, {"content", SYSTEM_PROMPT}},
            {{"role", "user"},   {"content", user_message}},
        })},
    };
    std::string err;
    std::string body = http_request("POST", "/api/chat", req.dump(), 180, err);
    if (!err.empty())
        return "AI is offline. Start Ollama (`ollama serve`) and try again.";
    try {
        json r = json::parse(body);
        if (r.contains("error")) {
            std::string e = r["error"].get<std::string>();
            if (e.find("not found") != std::string::npos)
                return "The AI model is not installed. Run: ollama pull " + S().model;
            return "AI error: " + e;
        }
        return r["message"]["content"].get<std::string>();
    } catch (...) {
        return "AI returned an unexpected response.";
    }
}

static int submit(std::string prompt) {
    int id;
    {
        std::lock_guard<std::mutex> lock(S().mu);
        id = S().next_id++;
    }
    std::thread([id, prompt = std::move(prompt)] {
        std::string answer = ask(prompt);
        std::lock_guard<std::mutex> lock(S().mu);
        S().done[id] = answer;
    }).detach();
    return id;
}

// ---------------------------------------------------------------- public

void ai_init(const std::string& host, int port, const std::string& model) {
    State& s = S();
    s.host = host;
    s.port = port;
    if (!model.empty()) s.model = model;
    else if (const char* env = std::getenv("CODELENS_MODEL")) s.model = env;
    if (s.started.exchange(true)) return;

    // Background health check every 5 seconds.
    std::thread([] {
        for (;;) {
            std::string err;
            http_request("GET", "/api/version", "", 2, err);
            S().available = err.empty();
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }).detach();
}

bool ai_available() { return S().available; }
std::string ai_model() { return S().model; }

int ai_explain_error(const std::string& code, const std::string& errors) {
    return submit(
        "A beginner's program fails. Explain in simple words what the error means and where to look. "
        "Do not rewrite the program.\n\nCode (with line numbers):\n" + code +
        "\n\nErrors:\n" + errors);
}

int ai_hint(const Lesson& lesson, const std::string& code, const std::string& last_result, int level) {
    std::string how =
        level <= 1 ? "Give a single guiding question that helps them find the problem themselves." :
        level == 2 ? "Point out what exactly is wrong or missing, without giving code." :
                     "Describe the fix step by step in words. Still no complete code.";
    return submit(context(lesson, code, last_result) +
                  "\nHint level " + std::to_string(level) + " of 3. " + how);
}

int ai_chat(const Lesson& lesson, const std::string& code, const std::string& last_result,
            const std::string& question) {
    return submit(context(lesson, code, last_result) + "\nStudent asks: " + question);
}

bool ai_poll(int request_id, std::string& answer) {
    std::lock_guard<std::mutex> lock(S().mu);
    auto it = S().done.find(request_id);
    if (it == S().done.end()) return false;
    answer = it->second;
    S().done.erase(it);
    return true;
}
