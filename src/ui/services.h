#pragma once
// UI adapters for the scene and AI contracts not yet supplied by the engine.
// Remove these adapters when the corresponding app.h interfaces land.
#include "app.h"
#include <string>

Scene ui_scene_get(const std::string &id);
ProgramState ui_scene_state(Scene &scene, const std::vector<std::string> &actions);
std::string tutor_message(std::string answer);
struct TutorStatus {
    bool checked = false;
    bool server = false, installed = false, downloading = false;
    float progress = 0;
    std::string message;
};
TutorStatus tutor_status();
void tutor_refresh();
void tutor_download();
Lesson generate_practice(const Lesson &original, std::string &error);
Lesson validated_practice(const std::string &json, const Lesson &original, std::string &error);
