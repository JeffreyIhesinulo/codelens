#include "screens.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>

namespace {
std::string step_key(const Lesson &lesson, const Step &step) {
    std::string data = std::to_string((int)step.kind) + "|" + step.title + "|" + step.text + "|" + step.code +
                       "|" + step.starter + "|" + step.scene + "|" + step.input;
    data += "|" + std::to_string(step.predict_line) + "|" + step.predict_expr + "|" +
            std::to_string(step.correct);
    for (const auto &line : step.lines)
        data += "|line:" + line;
    for (const auto &option : step.options)
        data += "|option:" + option;
    for (const auto &test : step.tests)
        data += "|test:" + test.input + "|" + test.expected;
    if (step.kind == StepKind::Practice) {
        data += "|" + lesson.starter_code;
        for (const auto &test : lesson.tests)
            data += "|test:" + test.input + "|" + test.expected;
    }
    // A stable digest works across operating systems and C++ standard libraries.
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char byte : data) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    std::ostringstream out;
    out << std::hex << hash;
    return out.str();
}
} // namespace
void App::load_steps() {
    try {
        std::ifstream file(root + "/steps.json");
        nlohmann::json json;
        file >> json;
        resume_lesson = json.value("lesson", 0);
        resume_step = json.value("step", 0);
        if (json.contains("completed"))
            for (auto it = json["completed"].begin(); it != json["completed"].end(); ++it)
                for (const auto &index : it.value())
                    step_progress[it.key()].insert(index.get<int>());
        if (json.contains("step_keys"))
            saved_step_keys = json["step_keys"].get<decltype(saved_step_keys)>();
    } catch (...) {
    }
    reconcile_steps();
}
void App::reconcile_steps() {
    bool changed = false, course_changed = false;
    for (const auto &lesson : lessons) {
        std::vector<std::string> keys;
        for (const auto &step : lesson.steps)
            keys.push_back(step_key(lesson, step));
        auto &done = step_progress[lesson.id];
        auto old = saved_step_keys.find(lesson.id);
        if (old != saved_step_keys.end() && old->second != keys) {
            std::set<std::string> passed;
            for (int index : done)
                if (index >= 0 && index < (int)old->second.size())
                    passed.insert(old->second[index]);
            done.clear();
            for (size_t i = 0; i < keys.size(); ++i)
                if (passed.count(keys[i]))
                    done.insert(i);
            changed = true;
        } else if (old == saved_step_keys.end()) {
            // Earlier releases saved only a Practice completion for the first four lessons.
            // Keep that completed task, while exposing the newly added learning steps.
            if (progress.completed.count(lesson.id) && keys.size() > 1 && done.size() <= 1 &&
                (done.empty() || done.count(0))) {
                done.clear();
                for (size_t i = 0; i < lesson.steps.size(); ++i)
                    if (lesson.steps[i].kind == StepKind::Practice)
                        done.insert(i);
            }
            changed = true;
        }
        saved_step_keys[lesson.id] = std::move(keys);
        for (auto it = done.begin(); it != done.end();)
            if (*it < 0 || *it >= (int)lesson.steps.size())
                it = done.erase(it);
            else
                ++it;
        if (progress.completed.count(lesson.id) && done.size() != lesson.steps.size()) {
            progress.completed.erase(lesson.id);
            course_changed = true;
        }
    }
    if (course_changed)
        progress_save(progress_path(), progress);
    if (changed)
        save_steps();
}
void App::save_steps() const {
    nlohmann::json done = nlohmann::json::object();
    for (const auto &item : step_progress)
        done[item.first] = item.second;
    std::string temporary = root + "/steps.json.tmp";
    std::ofstream file(temporary);
    file << nlohmann::json({{"lesson", resume_lesson},
                            {"step", resume_step},
                            {"completed", done},
                            {"step_keys", saved_step_keys}})
                .dump(2);
    file.close();
    if (file)
        std::rename(temporary.c_str(), (root + "/steps.json").c_str());
}
