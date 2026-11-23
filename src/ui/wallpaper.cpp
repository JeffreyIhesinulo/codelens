// Animated wallpapers: the last game you beat plays itself behind the course screen.
#include "screens.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

struct P { float x, y; };

float rnd(float a, float b) { return a + (b - a) * (GetRandomValue(0, 10000) / 10000.0f); }

struct Demo {
    std::string key;
    P ball{400, 250}, vel{260, 140}, a{30, 210}, b{758, 210};
    std::vector<P> items;      // snake cells / pipes / bricks / aliens
    std::vector<bool> on;
    P dir{1, 0}, apple{15, 7};
    float timer = 0, dx = 40;

    void reset(const std::string &k) {
        *this = Demo{};
        key = k;
        if (k == "snake") {
            items = {{5, 7}, {4, 7}, {3, 7}};
        } else if (k == "flappy") {
            for (int i = 0; i < 4; ++i) items.push_back({620.0f + i * 260, rnd(150, 350)});
            ball = {200, 250};
            vel = {0, 0};
        } else if (k == "breakout") {
            for (int r = 0; r < 5; ++r)
                for (int c = 0; c < 10; ++c) {
                    items.push_back({22.0f + c * 76, 50.0f + r * 28});
                    on.push_back(true);
                }
            ball = {400, 400};
            vel = {200, -330};
            a = {350, 470};
        } else if (k == "invaders") {
            for (int r = 0; r < 4; ++r)
                for (int c = 0; c < 8; ++c) {
                    items.push_back({140.0f + c * 66, 60.0f + r * 46});
                    on.push_back(true);
                }
            a = {400, 465};
            ball = {0, -10};   // the ship's bullet
        }
    }

    void step(float dt) {
        if (key == "pong") {   // both paddles follow the ball: an endless rally
            ball.x += vel.x * dt;
            ball.y += vel.y * dt;
            if (ball.y < 8 || ball.y > 492) vel.y = -vel.y;
            if (ball.x < 50 && vel.x < 0) vel.x = -vel.x;
            if (ball.x > 750 && vel.x > 0) vel.x = -vel.x;
            a.y += std::clamp(ball.y - 40 - a.y, -300 * dt, 300 * dt);
            b.y += std::clamp(ball.y - 40 - b.y, -300 * dt, 300 * dt);
        } else if (key == "snake") {   // the snake walks to the apple, one axis at a time
            timer += dt;
            if (timer < 0.12f) return;
            timer = 0;
            P head = items[0];
            P want = head.x != apple.x ? P{apple.x > head.x ? 1.0f : -1.0f, 0} : P{0, apple.y > head.y ? 1.0f : -1.0f};
            if (want.x == -dir.x && want.y == -dir.y) want = {dir.y, dir.x};
            dir = want;
            P next{head.x + dir.x, head.y + dir.y};
            bool hit = next.x < 0 || next.y < 0 || next.x >= 25 || next.y >= 15;
            for (size_t i = 0; i + 1 < items.size() && !hit; ++i) hit = items[i].x == next.x && items[i].y == next.y;
            if (hit || items.size() > 60) { reset(key); return; }
            items.insert(items.begin(), next);
            if (next.x == apple.x && next.y == apple.y)
                apple = {(float)GetRandomValue(0, 24), (float)GetRandomValue(0, 14)};
            else
                items.pop_back();
        } else if (key == "flappy") {   // Bit flaps whenever he drops below the next gap
            float gap = 250;
            for (const P &p : items)
                if (p.x + 70 > ball.x - 14) { gap = p.y; break; }
            if (ball.y > gap + 20 && vel.y > 0) vel.y = -380;
            vel.y += 1300 * dt;
            ball.y += vel.y * dt;
            for (P &p : items) {
                p.x -= 200 * dt;
                if (p.x < -80) {
                    float back = 0;
                    for (const P &q : items) back = std::max(back, q.x);
                    p = {back + 260, rnd(160, 340)};
                }
            }
            std::sort(items.begin(), items.end(), [](const P &l, const P &r) { return l.x < r.x; });
        } else if (key == "breakout") {   // the paddle follows the ball; the wall comes back when it is empty
            ball.x += vel.x * dt;
            ball.y += vel.y * dt;
            if (ball.x < 7 || ball.x > 793) vel.x = -vel.x;
            if (ball.y < 7) vel.y = std::fabs(vel.y);
            a.x = std::clamp(ball.x - 50, 0.0f, 700.0f);
            if (ball.y > 462 && vel.y > 0) { vel.y = -std::fabs(vel.y); vel.x = rnd(-260, 260); }
            for (size_t i = 0; i < items.size(); ++i)
                if (on[i] && CheckCollisionCircleRec({ball.x, ball.y}, 7, {items[i].x, items[i].y, 70, 22})) {
                    on[i] = false;
                    vel.y = -vel.y;
                    break;
                }
            if (std::none_of(on.begin(), on.end(), [](bool v) { return v; })) reset(key);
        } else if (key == "invaders") {   // the ship hunts the nearest minion
            bool edge = false;
            int left = 0;
            for (size_t i = 0; i < items.size(); ++i) {
                if (!on[i]) continue;
                ++left;
                items[i].x += dx * dt;
                if ((items[i].x < 30 && dx < 0) || (items[i].x > 770 && dx > 0)) edge = true;
            }
            if (edge) {
                dx = -dx;
                for (P &p : items) p.y += 12;
            }
            if (left == 0 || items[0].y > 300) { reset(key); return; }
            float target = a.x;
            for (int i = (int)items.size() - 1; i >= 0; --i)
                if (on[i]) { target = items[i].x; break; }
            a.x += std::clamp(target - a.x, -260 * dt, 260 * dt);
            if (ball.y < 0 && std::fabs(target - a.x) < 6) ball = {a.x, a.y - 14};
            if (ball.y >= 0) {
                ball.y -= 520 * dt;
                for (size_t i = 0; i < items.size(); ++i)
                    if (on[i] && std::fabs(ball.x - items[i].x) < 17 && std::fabs(ball.y - items[i].y) < 14) {
                        on[i] = false;
                        ball.y = -10;
                    }
            }
        }
    }

    void draw() const {
        const Color red = {255, 60, 80, 255};
        if (key == "pong") {
            for (int y = 0; y < 500; y += 28) DrawRectangle(398, y, 4, 14, theme::border);
            DrawRectangleRounded({a.x, a.y, 12, 80}, 0.5f, 6, theme::accent);
            DrawRectangleRounded({b.x, b.y, 12, 80}, 0.5f, 6, red);
            DrawCircleV({ball.x, ball.y}, 8, theme::text);
        } else if (key == "snake") {
            DrawCircleV({apple.x * 32 + 16, 10 + apple.y * 32 + 17}, 11, red);
            for (size_t i = 0; i < items.size(); ++i)
                DrawRectangleRounded({items[i].x * 32 + 2, 10 + items[i].y * 32 + 2, 28, 28}, 0.4f, 6,
                                     ColorAlpha(theme::accent, 1 - 0.5f * i / items.size()));
        } else if (key == "flappy") {
            for (const P &p : items) {
                DrawRectangle((int)p.x, 0, 70, (int)(p.y - 80), theme::success);
                DrawRectangle((int)p.x, (int)(p.y + 80), 70, (int)(420 - p.y), theme::success);
            }
            draw_raccoon(ball.x, ball.y, 16, vel.y < 0 ? 1 : 0);
        } else if (key == "breakout") {
            const Color rows[] = {red, {251, 146, 60, 255}, {250, 204, 21, 255}, theme::success, theme::accent};
            for (size_t i = 0; i < items.size(); ++i)
                if (on[i]) DrawRectangleRounded({items[i].x, items[i].y, 70, 22}, 0.3f, 4, rows[i / 10]);
            DrawRectangleRounded({a.x, a.y, 100, 14}, 0.6f, 6, theme::accent);
            DrawCircleV({ball.x, ball.y}, 7, theme::text);
        } else if (key == "invaders") {
            for (size_t i = 0; i < items.size(); ++i)
                if (on[i]) draw_raccoon(items[i].x, items[i].y, 13, 0, 1, 1);
            if (ball.y >= 0) DrawRectangle((int)ball.x - 2, (int)ball.y - 8, 4, 14, theme::accent);
            DrawTriangle({a.x, a.y - 14}, {a.x - 22, a.y + 10}, {a.x + 22, a.y + 10}, theme::accent);
        }
    }
};

} // namespace

// Fills the window behind the course screen; dimmed so the cards stay readable.
void draw_wallpaper(const std::string &key) {
    static Demo demo;
    if (key.empty()) return;
    if (demo.key != key) demo.reset(key);
    demo.step(std::min(GetFrameTime(), 1 / 30.0f));
    float w = GetScreenWidth(), h = GetScreenHeight();
    float sc = std::max(w / 800, h / 500);   // cover the whole window
    Camera2D cam{{(w - 800 * sc) / 2, (h - 500 * sc) / 2}, {0, 0}, 0, sc};
    BeginMode2D(cam);
    demo.draw();
    EndMode2D();
    DrawRectangle(0, 0, (int)w, (int)h, ColorAlpha(theme::bg, 0.72f));   // dim it
}
