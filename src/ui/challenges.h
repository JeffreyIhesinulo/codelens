#pragma once
// Challenges: small retro games against MAXON, the cyber raccoon. Beat a goal, then read how
// the game was made, with links back to the lessons it uses.
#include "ui.h"

#include <set>
#include <string>
#include <vector>

struct App;

struct ChallengeScreen {
    enum class Mode { Intro, List, Play, Won, Lost, Writeup };
    Mode mode = Mode::Intro;
    float t = 0;            // time in the current mode
    int game = -1;          // index into the game list
    int progress = 0, lives = 3;
    std::string taunt;      // MAXON's latest comment
    float taunt_t = 0;
    float scroll = 0;
    std::set<std::string> beaten;
    std::string save_path;
    std::string wallpaper;   // the last game beaten: plays by itself behind the course screen
    bool endless = false;    // after a win: easy games go on until you lose, medium ones until the level is done
    bool cleared = false;    // medium game: the whole level is done

    // game state (one game runs at a time; everything is in an 800 x 500 playfield)
    struct V { float x = 0, y = 0; };
    V ball, vel, p1, p2;                    // Pong / Breakout: ball, velocity, paddles
    std::vector<V> body;                    // Snake: cells; Invaders: aliens; Flappy: pipes (x, gap y)
    std::vector<bool> alive;                // Breakout bricks / Invader aliens
    std::vector<V> shots;                   // Invaders: enemy bullets
    V apple, dir, next_dir, bullet;         // Snake food and direction; Invaders: player bullet
    bool bullet_on = false, waiting = true; // waiting: before the first key / after a lost life
    float timer = 0, step = 0, alien_dx = 40;

    bool picker = false;     // the wallpaper picker on the course screen is open
    static std::vector<std::pair<std::string, std::string>> game_list();   // (key, title) of every game
    void load(const std::string &root);     // beaten games and the wallpaper
    void save() const;
    void open(App &app);                    // from the course screen
    void update_and_draw(App &app);

  private:
    void start(int index);
    void say(const std::string &line);
    void score(App &app);                   // one point: MAXON comments, check the goal
    void miss();                            // lost a life
    void draw_list(App &app);
    void play(App &app);
    void draw_writeup(App &app);
};
