#pragma once
#ifdef __APPLE__
#include "app.h"
#include <sys/types.h>

class LldbSession {
  public:
    ~LldbSession() { stop(); }
    bool start(const std::string &code, Lang lang, const std::string &input, std::string &error);
    bool exec(const std::string &command);
    bool run_to_line(int line);
    std::string eval(const std::string &expression);
    bool active() const { return pid_ > 0; }
    void stop();
    ProgramState state;

  private:
    pid_t pid_ = -1;
    int input_ = -1, output_ = -1;
    std::string directory_, buffered_, code_;
    int steps_ = 0;
    bool exchange(const std::string &command, std::string &response, bool interrupt = false);
    int target_pid_ = -1;
    bool timed_out_ = false;
};
#endif
