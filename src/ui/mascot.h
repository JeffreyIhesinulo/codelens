#pragma once
// Bit the raccoon (the CodeLens mascot), "liquid glass" speech bubbles, XP and the daily streak.
#include "ui.h"

#include <string>
#include <vector>

// Purple-and-white raccoon head centred on (cx, cy); r = head radius. Blinks on its own.
// mood: 0 normal, 1 happy (closed smiling eyes), 2 thinking (eyes look up).
void draw_raccoon(float cx, float cy, float r, int mood = 0, float alpha = 1);

// A frosted "liquid glass" card: translucent fill, bright rim, soft shadow, a sheen on top.
void draw_glass(Rectangle r, float roundness = 0.3f, float alpha = 1);

// The mascot with a glass speech bubble to its right. Returns the bubble rectangle.
Rectangle mascot_say(float x, float y, float r, const std::string &text, int mood = 0, float max_w = 520);

// XP and streak, saved in rewards.txt next to progress.txt.
struct Rewards {
    int xp = 0;
    int streak = 0;     // days in a row with at least one finished lesson
    int best = 0;
    long last_day = -1; // local day number of the last finished lesson
    std::string path;

    void load(const std::string &file);
    void save() const;
    int step_done();                       // +10 XP; returns XP gained
    int lesson_done();                     // +50 XP and the streak; returns XP gained
    bool streak_alive() const;             // finished something today or yesterday
};

// Small flame and star badges for the course header.
void draw_streak_badges(const Rewards &r, float right, float y);
