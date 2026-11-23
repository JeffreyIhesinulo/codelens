// Challenges: retro mini-games against MAXON, the cyber raccoon.
#include "challenges.h"
#include "mascot.h"
#include "screens.h"
#include "audio.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace {

struct GameInfo {
    const char *key, *title, *goal, *controls;
    int target;
    bool medium;
    std::vector<const char *> lessons;   // lesson ids used by "How this game was made"
    std::vector<const char *> score_lines, miss_lines;
};

const std::vector<GameInfo> &games() {
    static const std::vector<GameInfo> list = {
        {"pong", "Pong", "Return the ball 10 times", "W / S or Up / Down", 10, false,
         {"02_conditions", "03_loops"},
         {"Lucky bounce.", "My toaster could return that.", "Fine. Means nothing.", "Are you sweating yet?",
          "Halfway. Statistically irrelevant.", "Hmph. Not bad... for a human.", "My fans are spinning. From boredom.",
          "Stop doing that.", "One more and I... no. Won't happen."},
         {"HA! Error 404: paddle not found.", "Missed. Exactly as I calculated.", "Gravity says hi."}},
        {"snake", "Snake", "Eat 10 apples", "Arrow keys or W A S D", 10, false,
         {"04_arrays", "03_loops", "06_lists"},
         {"Crunch. Enjoy it while it lasts.", "Longer snake, bigger crash.", "You know walls exist, right?",
          "That apple was poisoned. Probably.", "Halfway. Your tail is getting nervous.", "Stop eating my apples!",
          "I am rewriting the apple spawner. Right now.", "Your snake is a linked list. And I break lists.",
          "One more? You'll trip over yourself first."},
         {"SPLAT. Snake.exe has stopped working."}},
        {"flappy", "Flappy Bit", "Fly through 10 columns", "Space, Up or click to flap", 10, false,
         {"02_conditions", "04_arrays"},
         {"Flap flap. Pathetic.", "That gap was wide out of pity.", "Gravity is my friend, not yours.",
          "You call that flying?", "Halfway. The next columns are... narrower. Maybe.", "Bit could never.",
          "My sensors detect luck. Only luck.", "Stop dodging my columns!", "Last one. I believe in your crash."},
         {"BONK. Physics wins again."}},
        {"breakout", "Breakout", "Break 25 bricks", "Mouse or Left / Right, Space to launch", 25, true,
         {"04_arrays", "02_conditions", "03_loops"},
         {"That brick was already cracked.", "I have 49 more. Well, fewer now.", "Stop breaking my wall!",
          "Brick by brick. Yawn.", "You are paying for those.", "My wall, my rules. Your ball, your problem.",
          "That one was load-bearing!", "A bouncing ball. How retro. How sad."},
         {"Ball lost. Like your hopes.", "Drop it again. I enjoy this.", "The floor is mine."}},
        {"invaders", "Space Invaders", "Destroy 24 of MAXON's minions", "Left / Right to move, Space to shoot", 24, true,
         {"04_arrays", "03_loops", "02_conditions"},
         {"That minion was on its break!", "I have backups. Many backups.", "Minion down. I felt nothing.",
          "You aim like a random number generator.", "My army grows impatient.", "Stop shooting my friends!",
          "I am recompiling more minions.", "Pew pew. How original."},
         {"Direct hit! On YOU.", "My minions say hello.", "Shields? What shields?"}},
    };
    return list;
}

const Color kRed = {255, 60, 80, 255};

float frand(float a, float b) { return a + (b - a) * (GetRandomValue(0, 10000) / 10000.0f); }

// MAXON with a glass speech bubble to his LEFT (he lives in the top-right corner)
void maxon_say(float right, float y, float r, const std::string &text, float max_w) {
    float bob = std::sin((float)GetTime() * 3) * 1.5f;
    draw_raccoon(right - r, y + bob, r, 0, 1, 1);
    if (text.empty()) return;
    float pad = 12, size = 16;
    auto lines = wrap_text(g_fonts.ui, size, text, max_w - 2 * pad);
    float w = 0;
    for (const auto &l : lines) w = std::max(w, text_width(g_fonts.ui, size, l));
    float lh = std::round(size * text_scale(g_fonts.ui) * 1.35f);
    float h = lines.size() * lh + 2 * pad - 4;
    Rectangle b{right - 2 * r - 16 - (w + 2 * pad), y - h / 2, w + 2 * pad, h};
    draw_glass(b, std::min(0.6f, 28.0f / h));
    DrawRectangleRoundedLinesEx(b, std::min(0.6f, 28.0f / h), 10, 1.2f, ColorAlpha(kRed, 0.45f));
    float ty = b.y + (h - lines.size() * lh) / 2 + (lh - size) / 2 - 1;
    for (const auto &l : lines) {
        draw_text(g_fonts.ui, size, l, b.x + pad, ty, theme::text);
        ty += lh;
    }
}

void scanlines(float w, float h, float alpha) {
    for (int y = 0; y < (int)h; y += 4) DrawRectangle(0, y, (int)w, 1, {0, 0, 0, (unsigned char)(50 * alpha)});
}

bool any_key() { return GetKeyPressed() != 0 || IsMouseButtonPressed(MOUSE_BUTTON_LEFT); }

} // namespace

// ---------------------------------------------------------------- flow

std::vector<std::pair<std::string, std::string>> ChallengeScreen::game_list() {
    std::vector<std::pair<std::string, std::string>> out;
    for (const GameInfo &g : games()) out.push_back({g.key, g.title});
    return out;
}

void ChallengeScreen::load(const std::string &root) {
    save_path = root + "/challenges.txt";
    beaten.clear();
    wallpaper.clear();
    std::ifstream in(save_path);
    for (std::string k; in >> k;) {
        if (k.rfind("wallpaper=", 0) == 0) wallpaper = k.substr(10);
        else beaten.insert(k);
    }
}

void ChallengeScreen::save() const {
    std::ofstream out(save_path);
    for (const auto &k : beaten) out << k << "\n";
    if (!wallpaper.empty()) out << "wallpaper=" << wallpaper << "\n";
}

void ChallengeScreen::open(App &app) {
    load(app.root);
    mode = Mode::Intro;
    t = 0;
    app.screen = Screen::Challenges;
    audio::music("");
    audio::play("maxonTransition");   // Bit turns into MAXON
}

void ChallengeScreen::say(const std::string &line) {
    taunt = line;
    taunt_t = 0;
}

void ChallengeScreen::start(int index) {
    game = index;
    progress = 0;
    endless = cleared = false;
    audio::music("bossMusic" + std::to_string(index % 5 + 1));
    lives = games()[index].key == std::string("snake") || games()[index].key == std::string("flappy") ? 1 : 3;
    mode = Mode::Play;
    t = timer = 0;
    waiting = true;
    bullet_on = false;
    body.clear();
    alive.clear();
    shots.clear();
    std::string k = games()[index].key;
    if (k == "pong") {
        p1 = {30, 210};
        p2 = {758, 210};
        ball = {400, 250};
        vel = {-300, frand(-150, 150)};
        timer = 1.0f;
    } else if (k == "snake") {
        body = {{5, 7}, {4, 7}, {3, 7}};
        dir = next_dir = {1, 0};
        apple = {15, 7};
        step = 0.13f;
    } else if (k == "flappy") {
        ball = {200, 250};
        vel = {0, 0};
        for (int i = 0; i < 4; ++i) {
            body.push_back({620.0f + i * 260, frand(150, 350)});
            alive.push_back(false);   // false = not passed yet
        }
    } else if (k == "breakout") {
        p1 = {350, 470};
        for (int r = 0; r < 5; ++r)
            for (int c = 0; c < 10; ++c) {
                body.push_back({22.0f + c * 76, 50.0f + r * 28});
                alive.push_back(true);
            }
    } else if (k == "invaders") {
        p1 = {400, 465};
        alien_dx = 40;
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 8; ++c) {
                body.push_back({140.0f + c * 66, 60.0f + r * 46});
                alive.push_back(true);
            }
        timer = 1.2f;
    }
    say(std::string("So you want to play ") + games()[index].title + "? Cute.");
}

void ChallengeScreen::score(App &app) {
    const GameInfo &g = games()[game];
    ++progress;
    if (progress >= g.target && !endless) {
        mode = Mode::Won;
        t = 0;
        say("IMPOSSIBLE. Recalculating... recalculating...");
        beaten.insert(g.key);
        wallpaper = g.key;   // the main menu now plays this game in the background
        save();
        app.rewards.xp += 100;
        app.rewards.save();
        audio::music("");
        audio::play("maxonTransition");   // MAXON glitches: he lost
        return;
    }
    if (endless && progress % 10 == 0) {
        say("Still going?! My circuits are melting.");
        return;
    }
    say(g.score_lines[(progress - 1) % g.score_lines.size()]);
}

void ChallengeScreen::miss() {
    const GameInfo &g = games()[game];
    --lives;
    audio::sfx(audio::Sfx::Miss);
    if (lives <= 0) {
        mode = Mode::Lost;
        t = 0;
        say(endless ? "Game over. Your record is safe... for now." : g.miss_lines[0]);
        audio::music("");
        audio::play("maxonWins");
        return;
    }
    say(g.miss_lines[(3 - lives) % g.miss_lines.size()]);
}

void ChallengeScreen::update_and_draw(App &app) {
    t += GetFrameTime();
    taunt_t += GetFrameTime();
    float w = GetScreenWidth(), h = GetScreenHeight();
    if (mode == Mode::Intro) {
        // Bit glitches and turns into MAXON
        float k = std::clamp((t - 0.7f) / 1.1f, 0.0f, 1.0f);
        float cyber = k * k * (3 - 2 * k);
        bool glitch = t > 0.7f && t < 1.9f;
        float jx = glitch ? frand(-7, 7) : 0, jy = glitch ? frand(-4, 4) : 0;
        if (glitch && GetRandomValue(0, 9) < 3) cyber = 1 - cyber;
        if (glitch)
            for (int i = 0; i < 6; ++i)
                DrawRectangle(0, GetRandomValue(0, (int)h), (int)w, GetRandomValue(2, 10), ColorAlpha(kRed, 0.25f));
        draw_raccoon(w / 2 + jx, h / 2 - 60 + jy, 90, t < 0.7f ? 1 : 0, 1, cyber);
        std::string line = t < 0.7f ? "Bit: Wait... something is wrong with my circuits..."
                         : t < 1.9f ? "SYSTEM OVERRIDE"
                                    : "";
        if (!line.empty()) {
            float lw = text_width(g_fonts.ui_bold, 20, line);
            draw_text(g_fonts.ui_bold, 20, line, w / 2 - lw / 2, h / 2 + 70, t < 0.7f ? theme::text : kRed);
        }
        if (t >= 1.9f) {
            std::string name = "MAXON";
            float nw = text_width(g_fonts.title, 64, name);
            float gx = std::fmod(t, 0.5f) < 0.06f ? frand(-6, 6) : 0;
            draw_text(g_fonts.title, 64, name, w / 2 - nw / 2 + gx + 3, h / 2 + 60, ColorAlpha({80, 220, 255, 255}, 0.5f));
            draw_text(g_fonts.title, 64, name, w / 2 - nw / 2 + gx, h / 2 + 60, kRed);
            std::string sub = "Bit is offline. Beat my games, if you can.";
            float sw = text_width(g_fonts.ui, 18, sub);
            draw_text(g_fonts.ui, 18, sub, w / 2 - sw / 2, h / 2 + 140, theme::text_dim);
        }
        scanlines(w, h, 0.6f);
        if (t > 3.2f || (t > 0.4f && any_key())) {
            mode = Mode::List;
            t = 0;
            say("I am MAXON. Pick a game. Any game. You will lose anyway.");
        }
        return;
    }
    if (mode == Mode::List) {
        draw_list(app);
        return;
    }
    if (mode == Mode::Writeup) {
        draw_writeup(app);
        return;
    }
    play(app);   // Play, Won and Lost all show the game
}

// ---------------------------------------------------------------- the list

void ChallengeScreen::draw_list(App &app) {
    float w = GetScreenWidth(), h = GetScreenHeight();
    audio::music("maxonMenuMusic");
    audio::button({w - 74, h - 66, 50, 42});
    if (button({24, 18, 150, 36}, "← Lessons", true, ButtonStyle::Ghost) || IsKeyPressed(KEY_ESCAPE)) {
        app.screen = Screen::Course;
        return;
    }
    draw_text(g_fonts.title, 30, "Challenges", 190, 22, theme::text);
    draw_text(g_fonts.ui, 16, "Retro games against MAXON. Win one, then see how it was made.", 190, 60,
              theme::text_dim);
    maxon_say(w - 30, 64, 40, taunt, std::min(520.0f, w - 620));

    float colw = std::min(560.0f, (w - 120) / 2), gap = 40, x0 = (w - 2 * colw - gap) / 2, y0 = 150;
    const char *heads[] = {"Easy", "Medium"};
    for (int c = 0; c < 2; ++c) {
        float x = x0 + c * (colw + gap), y = y0;
        draw_text(g_fonts.title, 24, heads[c], x, y, c ? kRed : theme::accent);
        y += 46;
        for (size_t i = 0; i < games().size(); ++i) {
            const GameInfo &g = games()[i];
            if (g.medium != (c == 1)) continue;
            Rectangle card{x, y, colw, 104};
            bool hover = CheckCollisionPointRec(GetMousePosition(), card);
            DrawRectangleRounded(card, 0.12f, 8, hover ? theme::panel_hi : theme::panel);
            DrawRectangleRoundedLinesEx(card, 0.12f, 8, 1, hover ? (c ? kRed : theme::accent) : theme::border);
            draw_text(g_fonts.ui_bold, 21, g.title, x + 20, y + 16, theme::text);
            draw_text(g_fonts.ui, 16, std::string("Goal: ") + g.goal, x + 20, y + 48, theme::text);
            draw_text(g_fonts.ui, 14, g.controls, x + 20, y + 74, theme::text_dim);
            bool won = beaten.count(g.key);
            std::string tag = won ? "Beaten" : "Play";
            float tw = text_width(g_fonts.ui_bold, 15, tag);
            Rectangle pill{x + colw - tw - 44, y + 16, tw + 28, 30};
            DrawRectangleRounded(pill, 1, 8, won ? ColorAlpha(theme::success, 0.25f) : theme::accent_soft);
            draw_text(g_fonts.ui_bold, 15, tag, pill.x + 14, pill.y + 6, won ? theme::success : theme::text);
            if (hover) {
                ui_cursor_hand();
                if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) start((int)i);
            }
            y += 120;
        }
    }
    draw_text(g_fonts.ui, 15, "Every win gives +100 XP and a new animated wallpaper for the main menu. Esc leaves a game.",
              x0, h - 48, theme::text_dim);
}

// ---------------------------------------------------------------- the games

void ChallengeScreen::play(App &app) {
    float w = GetScreenWidth(), h = GetScreenHeight();
    const GameInfo &g = games()[game];
    const std::string k = g.key;
    float dt = std::min(GetFrameTime(), 1 / 30.0f);
    bool running = mode == Mode::Play;

    // header: back, title, goal, progress, lives, MAXON
    if (button({24, 18, 120, 36}, "← Back", true, ButtonStyle::Ghost) || (running && IsKeyPressed(KEY_ESCAPE))) {
        mode = Mode::List;
        say("Running away? I'll remember this.");
        return;
    }
    draw_text(g_fonts.title, 26, g.title, 160, 20, theme::text);
    draw_text(g_fonts.ui, 15, std::string(g.goal) + "  ·  " + g.controls, 160, 56, theme::text_dim);
    std::string prog = endless ? std::to_string(progress) : std::to_string(progress) + " / " + std::to_string(g.target);
    draw_text(g_fonts.title, 28, prog, w / 2 - text_width(g_fonts.title, 28, prog) / 2, 24, theme::accent);
    for (int i = 0; i < lives; ++i) DrawCircle((int)(w / 2 + 90 + i * 22), 42, 7, kRed);
    if (endless) draw_text(g_fonts.ui_bold, 14, g.medium ? "Finish the level" : "Endless", w / 2 - 40, 64, theme::accent);
    audio::button({w / 2 + 170, 22, 46, 38});
    maxon_say(w - 24, 48, 28, taunt_t < 4 ? taunt : "", std::min(460.0f, w / 2 - 200));

    // playfield: 800 x 500 logical units, scaled into the window
    Rectangle area{24, 96, w - 48, h - 120};
    float sc = std::min(area.width / 800, area.height / 500);
    float ox = area.x + (area.width - 800 * sc) / 2, oy = area.y + (area.height - 500 * sc) / 2;
    DrawRectangleRounded({ox - 6, oy - 6, 800 * sc + 12, 500 * sc + 12}, 0.02f, 6, theme::panel);
    DrawRectangleRoundedLinesEx({ox - 6, oy - 6, 800 * sc + 12, 500 * sc + 12}, 0.02f, 6, 1, theme::border);
    Vector2 mouse = {(GetMousePosition().x - ox) / sc, (GetMousePosition().y - oy) / sc};
    Camera2D cam{{ox, oy}, {0, 0}, 0, sc};
    BeginScissorMode((int)ox, (int)oy, (int)(800 * sc), (int)(500 * sc));
    BeginMode2D(cam);
    bool up = IsKeyDown(KEY_UP) || IsKeyDown(KEY_W), down = IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S);
    bool left = IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A), right = IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D);
    std::string hint;

    if (k == "pong") {
        if (running) {
            if (up) p1.y -= 440 * dt;
            if (down) p1.y += 440 * dt;
            p1.y = std::clamp(p1.y, 0.0f, 420.0f);
            p2.y += std::clamp(ball.y - 40 - p2.y, -400 * dt, 400 * dt);   // MAXON follows the ball
            p2.y = std::clamp(p2.y, 0.0f, 420.0f);
            if (timer > 0) timer -= dt;
            else {
                ball.x += vel.x * dt;
                ball.y += vel.y * dt;
            }
            if (ball.y < 8) { ball.y = 8; vel.y = std::fabs(vel.y); }
            if (ball.y > 492) { ball.y = 492; vel.y = -std::fabs(vel.y); }
            if (vel.x < 0 && ball.x - 8 <= 42 && ball.x - 8 >= 18 && ball.y >= p1.y - 8 && ball.y <= p1.y + 88) {
                vel.x = std::min(700.0f, std::fabs(vel.x) * 1.06f);
                vel.y = (ball.y - (p1.y + 40)) / 40 * 320;
                ball.x = 50;
                audio::sfx(audio::Sfx::Hit);
                score(app);
            }
            if (vel.x > 0 && ball.x + 8 >= 758) {   // MAXON never misses (he says)
                vel.x = -std::fabs(vel.x);
                ball.x = 750;
                audio::sfx(audio::Sfx::Click);
            }
            if (ball.x < -10) {
                miss();
                ball = {400, 250};
                vel = {-320, frand(-160, 160)};
                timer = 0.9f;
            }
        }
        for (int y = 0; y < 500; y += 28) DrawRectangle(398, y, 4, 14, theme::border);
        DrawRectangleRounded({p1.x, p1.y, 12, 80}, 0.5f, 6, theme::accent);
        DrawRectangleRounded({p2.x, p2.y, 12, 80}, 0.5f, 6, kRed);
        DrawCircleV({ball.x, ball.y}, 8, theme::text);
        if (timer > 0 && running) hint = "Get ready...";
    } else if (k == "snake") {
        const int cols = 25, rows = 15;
        const float cell = 32, top = 10;
        if (running) {
            auto turn = [&](float dx, float dy) {
                if (dir.x != -dx || dir.y != -dy) next_dir = {dx, dy};
                waiting = false;
            };
            if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W)) turn(0, -1);
            if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) turn(0, 1);
            if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A)) turn(-1, 0);
            if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D)) turn(1, 0);
            if (!waiting) timer += dt;
            while (timer >= step && mode == Mode::Play) {
                timer -= step;
                dir = next_dir;
                V head{body[0].x + dir.x, body[0].y + dir.y};
                bool crash = head.x < 0 || head.y < 0 || head.x >= cols || head.y >= rows;
                for (size_t i = 0; i + 1 < body.size() && !crash; ++i)
                    crash = body[i].x == head.x && body[i].y == head.y;
                if (crash) {
                    audio::sfx(audio::Sfx::Boom);
                    miss();
                    break;
                }
                body.insert(body.begin(), head);
                if (head.x == apple.x && head.y == apple.y) {
                    audio::sfx(audio::Sfx::Coin);
                    score(app);
                    step = std::max(0.07f, step - 0.004f);
                    bool free_cell;
                    do {   // a new apple, not on the snake
                        apple = {(float)GetRandomValue(0, cols - 1), (float)GetRandomValue(0, rows - 1)};
                        free_cell = std::none_of(body.begin(), body.end(),
                                                 [&](const V &b) { return b.x == apple.x && b.y == apple.y; });
                    } while (!free_cell);
                } else
                    body.pop_back();
            }
        }
        for (int x = 0; x <= cols; ++x) DrawLine((int)(x * cell), (int)top, (int)(x * cell), (int)(top + rows * cell), {255, 255, 255, 10});
        for (int y = 0; y <= rows; ++y) DrawLine(0, (int)(top + y * cell), (int)(cols * cell), (int)(top + y * cell), {255, 255, 255, 10});
        DrawCircleV({apple.x * cell + 16, top + apple.y * cell + 17}, 11, kRed);
        DrawRectangle((int)(apple.x * cell + 15), (int)(top + apple.y * cell + 2), 3, 7, theme::success);
        for (size_t i = 0; i < body.size(); ++i) {
            Color c = i == 0 ? theme::accent : ColorAlpha(theme::accent, 0.85f - 0.4f * i / body.size());
            DrawRectangleRounded({body[i].x * cell + 2, top + body[i].y * cell + 2, cell - 4, cell - 4}, 0.4f, 6, c);
        }
        if (waiting && running) hint = "Press an arrow key to start";
    } else if (k == "flappy") {
        bool flap = IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W) ||
                    IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
        if (running) {
            if (flap) {
                vel.y = -420;
                waiting = false;
                audio::sfx(audio::Sfx::Flap);
            }
            if (!waiting) {
                vel.y += 1300 * dt;
                ball.y += vel.y * dt;
                for (size_t i = 0; i < body.size(); ++i) {
                    body[i].x -= 210 * dt;
                    if (!alive[i] && body[i].x + 70 < ball.x - 14) {
                        alive[i] = true;
                        audio::sfx(audio::Sfx::Coin);
                        score(app);
                    }
                    if (body[i].x < -80) {   // recycle the column at the back
                        float back = 0;
                        for (const V &p : body) back = std::max(back, p.x);
                        body[i] = {back + 260, frand(140, 360)};
                        alive[i] = false;
                    }
                }
                bool hit = ball.y < 0 || ball.y > 500;
                for (const V &p : body) {
                    Rectangle top{p.x, 0, 70, p.y - 80}, bottom{p.x, p.y + 80, 70, 500 - p.y - 80};
                    if (CheckCollisionCircleRec({ball.x, ball.y}, 13, top) ||
                        CheckCollisionCircleRec({ball.x, ball.y}, 13, bottom))
                        hit = true;
                }
                if (hit && mode == Mode::Play) {
                    audio::sfx(audio::Sfx::Boom);
                    miss();
                }
            }
        }
        for (const V &p : body) {
            DrawRectangle((int)p.x, 0, 70, (int)(p.y - 80), ColorAlpha(theme::success, 0.75f));
            DrawRectangle((int)p.x, (int)(p.y + 80), 70, (int)(500 - p.y - 80), ColorAlpha(theme::success, 0.75f));
            DrawRectangle((int)p.x - 5, (int)(p.y - 98), 80, 18, theme::success);
            DrawRectangle((int)p.x - 5, (int)(p.y + 80), 80, 18, theme::success);
        }
        draw_raccoon(ball.x, ball.y, 16, vel.y < 0 ? 1 : 0);   // Bit is the bird
        if (waiting && running) hint = "Press Space to flap";
    } else if (k == "breakout") {
        if (running) {
            if (left) p1.x -= 560 * dt;
            if (right) p1.x += 560 * dt;
            Vector2 md = GetMouseDelta();
            if ((md.x != 0 || md.y != 0) && mouse.x >= 0 && mouse.x <= 800) p1.x = mouse.x - 50;
            p1.x = std::clamp(p1.x, 0.0f, 700.0f);
            if (waiting) {
                ball = {p1.x + 50, 461};
                if (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_UP) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    waiting = false;
                    vel = {frand(-180, 180), -380};
                    audio::sfx(audio::Sfx::Click);
                }
            } else {
                V before = ball;
                ball.x += vel.x * dt;
                ball.y += vel.y * dt;
                if (ball.x < 7) { ball.x = 7; vel.x = std::fabs(vel.x); }
                if (ball.x > 793) { ball.x = 793; vel.x = -std::fabs(vel.x); }
                if (ball.y < 7) { ball.y = 7; vel.y = std::fabs(vel.y); }
                if (vel.y > 0 && CheckCollisionCircleRec({ball.x, ball.y}, 7, {p1.x, p1.y, 100, 14})) {
                    vel.y = -std::fabs(vel.y);
                    vel.x = (ball.x - (p1.x + 50)) / 50 * 340;
                    ball.y = p1.y - 8;
                    audio::sfx(audio::Sfx::Hit);
                }
                for (size_t i = 0; i < body.size(); ++i) {
                    if (!alive[i]) continue;
                    Rectangle br{body[i].x, body[i].y, 70, 22};
                    if (!CheckCollisionCircleRec({ball.x, ball.y}, 7, br)) continue;
                    alive[i] = false;
                    bool from_side = before.x < br.x || before.x > br.x + br.width;
                    if (from_side) vel.x = -vel.x;
                    else vel.y = -vel.y;
                    audio::sfx(audio::Sfx::Brick);
                    score(app);
                    if (endless && std::none_of(alive.begin(), alive.end(), [](bool a) { return a; })) {
                        cleared = true;   // the whole wall is gone
                        mode = Mode::Won;
                        t = 0;
                        say("My wall... my beautiful wall...");
                        audio::music("");
                        audio::play("maxonTransition");
                        app.rewards.xp += 50;
                        app.rewards.save();
                    }
                    break;
                }
                if (ball.y > 510) {
                    miss();
                    waiting = true;
                }
            }
        }
        const Color rows[] = {kRed, {251, 146, 60, 255}, {250, 204, 21, 255}, theme::success, theme::accent};
        for (size_t i = 0; i < body.size(); ++i)
            if (alive[i]) DrawRectangleRounded({body[i].x, body[i].y, 70, 22}, 0.3f, 4, rows[i / 10]);
        DrawRectangleRounded({p1.x, p1.y, 100, 14}, 0.6f, 6, theme::accent);
        DrawCircleV({ball.x, ball.y}, 7, theme::text);
        if (waiting && running) hint = "Press Space or click to launch";
    } else if (k == "invaders") {
        if (running) {
            if (left) p1.x -= 340 * dt;
            if (right) p1.x += 340 * dt;
            p1.x = std::clamp(p1.x, 24.0f, 776.0f);
            if (IsKeyPressed(KEY_SPACE) && !bullet_on) {
                bullet_on = true;
                bullet = {p1.x, p1.y - 14};
                audio::sfx(audio::Sfx::Shoot);
            }
            if (bullet_on) {
                bullet.y -= 600 * dt;
                if (bullet.y < 0) bullet_on = false;
            }
            int left_alive = (int)std::count(alive.begin(), alive.end(), true);
            float speed = 1 + (32 - left_alive) / 12.0f;
            bool edge = false;
            for (size_t i = 0; i < body.size(); ++i) {
                if (!alive[i]) continue;
                body[i].x += alien_dx * speed * dt;
                if ((body[i].x < 30 && alien_dx < 0) || (body[i].x > 770 && alien_dx > 0)) edge = true;
            }
            if (edge) {
                alien_dx = -alien_dx;
                for (size_t i = 0; i < body.size(); ++i) body[i].y += 16;
            }
            for (size_t i = 0; i < body.size() && bullet_on; ++i)
                if (alive[i] && std::fabs(bullet.x - body[i].x) < 17 && std::fabs(bullet.y - body[i].y) < 14) {
                    alive[i] = false;
                    bullet_on = false;
                    audio::sfx(audio::Sfx::Boom);
                    score(app);
                    if (endless && std::none_of(alive.begin(), alive.end(), [](bool a) { return a; })) {
                        cleared = true;   // the whole wave is gone
                        mode = Mode::Won;
                        t = 0;
                        say("My army... all of it...");
                        audio::music("");
                        audio::play("maxonTransition");
                        app.rewards.xp += 50;
                        app.rewards.save();
                    }
                }
            timer -= dt;
            if (timer <= 0 && left_alive > 0) {   // a random minion shoots
                timer = frand(0.5f, 1.1f);
                int pick = GetRandomValue(0, left_alive - 1);
                for (size_t i = 0; i < body.size(); ++i)
                    if (alive[i] && pick-- == 0) shots.push_back({body[i].x, body[i].y + 14});
            }
            for (auto &s : shots) s.y += 260 * dt;
            for (size_t i = 0; i < shots.size();) {
                bool hit = std::fabs(shots[i].x - p1.x) < 22 && std::fabs(shots[i].y - p1.y) < 12;
                if (hit && mode == Mode::Play) miss();
                if (hit || shots[i].y > 510) shots.erase(shots.begin() + i);
                else ++i;
            }
            for (size_t i = 0; i < body.size(); ++i)
                if (alive[i] && body[i].y > 430 && mode == Mode::Play) {
                    lives = 1;
                    miss();
                    break;
                }
        }
        for (size_t i = 0; i < body.size(); ++i)
            if (alive[i]) draw_raccoon(body[i].x, body[i].y, 13, 0, 1, 1);   // MAXON's minions
        for (const auto &s : shots) DrawRectangle((int)s.x - 2, (int)s.y - 6, 4, 12, kRed);
        if (bullet_on) DrawRectangle((int)bullet.x - 2, (int)bullet.y - 8, 4, 14, theme::accent);
        DrawTriangle({p1.x, p1.y - 14}, {p1.x - 22, p1.y + 10}, {p1.x + 22, p1.y + 10}, theme::accent);
    }
    EndMode2D();
    EndScissorMode();
    if (!hint.empty()) {
        float hw = text_width(g_fonts.ui_bold, 20, hint);
        draw_text(g_fonts.ui_bold, 20, hint, ox + 400 * sc - hw / 2, oy + 380 * sc, theme::text);
    }

    // result cards
    if (mode == Mode::Won || mode == Mode::Lost) {
        bool won = mode == Mode::Won;
        bool first_win = won && !endless;
        DrawRectangle(0, 0, (int)w, (int)h, {0, 0, 0, 150});
        Rectangle card{w / 2 - 290, h / 2 - 225, 580, first_win ? 450.0f : 400.0f};
        draw_glass(card, 0.12f);
        float jx = won && std::fmod(t, 0.4f) < 0.05f ? frand(-5, 5) : 0;
        draw_raccoon(w / 2 + jx, card.y + 80, 52, 0, 1, 1);
        auto centred = [&](const Font &f, float size, const std::string &s, float y, Color c) {
            draw_text(f, size, s, w / 2 - text_width(f, size, s) / 2, y, c);
        };
        std::string title = cleared ? (k == "breakout" ? "Wall cleared!" : "Wave cleared!")
                          : won     ? std::string("You beat MAXON at ") + g.title + "!"
                          : endless ? "Game over. Score: " + std::to_string(progress)
                                    : "MAXON wins.";
        centred(g_fonts.title, 28, title, card.y + 150, won || endless ? theme::text : kRed);
        centred(g_fonts.ui, 17, taunt, card.y + 194, theme::text_dim);
        if (first_win) centred(g_fonts.title, 24, "+100 XP  ·  new wallpaper unlocked", card.y + 228, {250, 204, 21, 255});
        else if (cleared) centred(g_fonts.title, 24, "+50 XP", card.y + 228, {250, 204, 21, 255});
        else if (!won) centred(g_fonts.ui, 17, endless ? "You beat the challenge already. Nice run!" : "Score: " + prog + ". Try again: you were close.",
                               card.y + 230, theme::text);
        float by = card.y + 276;
        if (first_win) {   // easy: endless mode; medium: finish the level
            if (button({w / 2 - 170, by, 340, 44}, g.medium ? "Finish the level" : "Keep playing (endless)", true,
                       ButtonStyle::Primary)) {
                endless = true;
                mode = Mode::Play;
                say(g.medium ? "Fine. Finish it. If you can." : "Endless? You will get tired before I do.");
                audio::music("bossMusic" + std::to_string(game % 5 + 1));
                if (k == "breakout") waiting = true;
            }
            by += 54;
        }
        if (won && button({w / 2 - 170, by, 340, 42}, "How was this game made?", true,
                          first_win ? ButtonStyle::Soft : ButtonStyle::Primary)) {
            mode = Mode::Writeup;
            scroll = 0;
        }
        if (!won && (button({w / 2 - 170, by, 340, 42}, "Try again", true, ButtonStyle::Primary) || IsKeyPressed(KEY_ENTER))) {
            bool keep = endless;
            start(game);
            endless = keep;   // a retry of an endless run stays endless
            return;
        }
        by += 52;
        if (button({w / 2 - 170, by, 165, 40}, won ? "Play again" : "Writeup", won || beaten.count(g.key))) {
            if (won) start(game);
            else {
                mode = Mode::Writeup;
                scroll = 0;
            }
            return;
        }
        if (button({w / 2 + 5, by, 165, 40}, "All challenges", true)) {
            mode = Mode::List;
            say(won ? "Beginner's luck. Pick another one." : "Back so soon?");
        }
    }
}

// ---------------------------------------------------------------- how it was made

void ChallengeScreen::draw_writeup(App &app) {
    float w = GetScreenWidth(), h = GetScreenHeight();
    const GameInfo &g = games()[game];
    static std::string cached_key, text;
    if (cached_key != g.key) {
        cached_key = g.key;
        std::ifstream in(app.root + "/challenges/" + g.key + ".txt");
        std::stringstream ss;
        ss << in.rdbuf();
        text = ss.str().empty() ? "The write-up for this game is missing." : ss.str();
    }
    if (button({24, 18, 190, 36}, "← All challenges", true, ButtonStyle::Ghost) || IsKeyPressed(KEY_ESCAPE)) {
        mode = Mode::List;
        return;
    }
    draw_text(g_fonts.ui, 15, "How this game was made", 230, 18, theme::accent);
    draw_text(g_fonts.title, 28, g.title, 230, 38, theme::text);
    float cw = std::min(980.0f, w - 64), cx = (w - cw) / 2;
    Rectangle page{cx, 96, cw, h - 200};
    DrawRectangleRounded(page, 0.02f, 8, theme::panel);
    DrawRectangleRoundedLinesEx(page, 0.02f, 8, 1, theme::border);
    draw_rich_text({page.x + 32, page.y + 24, page.width - 52, page.height - 40}, text, scroll);
    // links to the lessons this game uses
    float x = cx, y = h - 86;
    draw_text(g_fonts.ui_bold, 15, "Lessons used:", x, y + 10, theme::text_dim);
    x += text_width(g_fonts.ui_bold, 15, "Lessons used:") + 16;
    for (const char *id : g.lessons) {
        for (size_t i = 0; i < app.lessons.size(); ++i) {
            if (app.lessons[i].id != id) continue;
            std::string label = app.lessons[i].title + " →";
            float bw = text_width(g_fonts.ui_bold, g_fonts.ui_size, label) + 36;
            if (button({x, y, bw, 40}, label, true, ButtonStyle::Soft)) {
                app.steps.open(app, (int)i);
                app.steps.reading = !app.lessons[i].reading.empty();   // start with the explanation
                return;
            }
            x += bw + 10;
        }
    }
}
