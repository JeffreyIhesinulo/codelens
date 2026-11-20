#include "screens.h"
#include <fstream>
#include <nlohmann/json.hpp>

void App::load_steps() {
    try {
        std::ifstream file(root + "/steps.json");
        nlohmann::json json;
        file >> json;
        resume_lesson = json.value("lesson", 0);
        resume_step = json.value("step", 0);
        if (json.contains("completed"))
            for (auto it = json["completed"].begin(); it != json["completed"].end(); ++it)
                for (const auto &n : it.value())
                    step_progress[it.key()].insert(n.get<int>());
    } catch (...) {
    }
}
void App::save_steps() const {
    nlohmann::json done = nlohmann::json::object();
    for (const auto &item : step_progress)
        done[item.first] = item.second;
    std::string temporary = root + "/steps.json.tmp";
    std::ofstream file(temporary);
    file << nlohmann::json({{"lesson", resume_lesson}, {"step", resume_step}, {"completed", done}}).dump(2);
    file.close();
    if (file)
        std::rename(temporary.c_str(), (root + "/steps.json").c_str());
}
