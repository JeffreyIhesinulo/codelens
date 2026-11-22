#include "order_cards.h"
#include <algorithm>
#include <cmath>

bool OrderCards::update_and_draw(Rectangle view, const std::vector<std::string> &lines,
                                 std::vector<int> &order, bool checked, bool enabled, float &scroll) {
    block_release = false;
    if (order.empty())
        return false;
    const Vector2 mouse = GetMousePosition();
    std::vector<float> heights;
    float total = 0;
    for (int index : order) {
        float height =
            std::max(50.0f, 20.0f * wrap_text(g_fonts.mono, 15, lines[index], view.width - 155).size() + 18);
        heights.push_back(height);
        total += height + 8;
    }
    if (active() && (!enabled || IsKeyPressed(KEY_ESCAPE))) {
        dragged = destination = -1;
        block_release = true;
    }
    if (active()) {
        float speed = GetFrameTime() * 260;
        if (mouse.y < view.y + 28)
            scroll -= speed;
        if (mouse.y > view.y + view.height - 28)
            scroll += speed;
    }
    scroll_area(view, total, scroll);
    std::vector<Rectangle> rows;
    float y = view.y - scroll;
    for (float height : heights) {
        rows.push_back({view.x, y, view.width, height});
        y += height + 8;
    }
    if (!active() && enabled && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        CheckCollisionPointRec(mouse, view)) {
        for (size_t i = 0; i < rows.size(); ++i) {
            Rectangle handle = rows[i];
            handle.width -= 116;
            if (CheckCollisionPointRec(mouse, handle)) {
                dragged = i;
                destination = i;
                grab_y = mouse.y - rows[i].y;
                break;
            }
        }
    }
    if (active()) {
        destination = 0;
        for (size_t i = 0; i < rows.size(); ++i)
            if ((int)i != dragged && mouse.y > rows[i].y + rows[i].height / 2)
                ++destination;
        destination = std::clamp(destination, 0, (int)order.size() - 1);
    }
    bool changed = false;
    BeginScissorMode(view.x, view.y, view.width, view.height);
    auto draw_card = [&](Rectangle r, int index, Color fill, bool floating) {
        DrawRectangleRounded(r, .1f, 6, fill);
        if (floating)
            DrawRectangleRoundedLinesEx(r, .1f, 6, 2, theme::accent);
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 2; ++col)
                DrawCircle(r.x + 13 + col * 6, r.y + r.height / 2 - 6 + row * 6, 1.5f, theme::text_dim);
        draw_wrapped(g_fonts.mono, 15, lines[index], {r.x + 34, r.y + 10, r.width - 155, 0}, theme::text, 5);
    };
    for (size_t i = 0; i < rows.size(); ++i) {
        const Rectangle r = rows[i];
        if (!CheckCollisionRecs(r, view))
            continue;
        const bool wrong = checked && lines[order[i]] != lines[i];
        if ((int)i == dragged) {
            DrawRectangleRounded(r, .1f, 6, theme::bg);
            DrawRectangleRoundedLinesEx(r, .1f, 6, 1, theme::accent_dk);
            continue;
        }
        draw_card(r, order[i], wrong ? theme::error_bg : theme::panel, false);
        bool controls = enabled && !active() && !block_release && CheckCollisionPointRec(mouse, view);
        if (button({r.x + r.width - 104, r.y + 7, 44, 34}, "↑", controls && i > 0)) {
            std::swap(order[i], order[i - 1]);
            changed = true;
            checked = false;
        }
        if (button({r.x + r.width - 52, r.y + 7, 44, 34}, "↓", controls && i + 1 < order.size())) {
            std::swap(order[i], order[i + 1]);
            changed = true;
            checked = false;
        }
        if (enabled && !active() && CheckCollisionPointRec(mouse, view) &&
            CheckCollisionPointRec(mouse, {r.x, r.y, r.width - 116, r.height}))
            ui_cursor_hand();
    }
    if (active()) {
        int boundary = destination;
        if (destination > dragged)
            ++boundary;
        float insertion =
            boundary < (int)rows.size() ? rows[boundary].y - 4 : rows.back().y + rows.back().height + 4;
        DrawLineEx({view.x, insertion}, {view.x + view.width, insertion}, 3, theme::accent);
        Rectangle floating = rows[dragged];
        floating.y = mouse.y - grab_y;
        floating.x += 5;
        draw_card(floating, order[dragged], theme::accent_dk, true);
        ui_cursor_hand();
    }
    EndScissorMode();
    if (active() && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        if (CheckCollisionPointRec(mouse, view) && destination != dragged) {
            int item = order[dragged];
            order.erase(order.begin() + dragged);
            order.insert(order.begin() + destination, item);
            changed = true;
        }
        dragged = destination = -1;
        block_release = true;
    }
    return changed;
}
