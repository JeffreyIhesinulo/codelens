#pragma once
// Engine-internal helpers shared between src/core files. Not part of the UI interface (app.h).
#include <string>
#include <vector>

struct ExecResult {
    std::string out, err;
    int  exit_code = -1;
    int  signal = 0;
    bool timed_out = false;
    bool started = false;
};

// Runs argv with stdin_text on stdin, captures stdout/stderr, kills it after timeout_ms.
ExecResult exec_capture(const std::vector<std::string>& argv, const std::string& stdin_text,
                        int timeout_ms, const std::vector<std::string>& env_extra = {});
