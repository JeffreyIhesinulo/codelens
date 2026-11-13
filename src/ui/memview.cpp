#include "memview.h"
#include "ui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace {
constexpr float gap = 6;
constexpr float pad = 9;
constexpr float row_height = 29;
constexpr float font_size = 15;
constexpr float arrow_space = 44;

enum class BoxKind { Frame, Variable, Cell, Array, Structure, Block };

struct Box {
    Rectangle rect;
    std::string label;
    BoxKind kind;
    uint64_t address = 0;
    size_t size = 0;
    bool pointer = false;
    uint64_t points_to = 0;
    bool freed = false;
    bool error = false;
    bool in_array = false;
};

struct Column {
    Rectangle view;
    std::vector<Box> boxes;
    float y = 0;

    Rectangle row() const {
        return {view.x, y, view.width - arrow_space, row_height};
    }
};

std::string hex_address(uint64_t address) {
    char text[19];
    std::snprintf(text, sizeof text, "0x%llx", (unsigned long long)address);
    return text;
}

bool array_elements(const std::vector<Variable>& children) {
    return !children.empty() && !children.front().name.empty() && children.front().name.front() == '[';
}

void add_variable(std::vector<Box>& boxes, const Variable& var, Rectangle r, bool cell = false);

float add_children(std::vector<Box>& boxes, const std::vector<Variable>& children,
                   Rectangle r, bool array) {
    const int columns = array ? std::max(1, (int)((r.width + gap) / 62)) : 1;
    const float width = (r.width - (columns - 1) * gap) / columns;
    float row_bottom = r.y;
    for (size_t i = 0; i < children.size(); ++i) {
        int col = (int)(i % columns);
        if (i > 0 && col == 0) r.y = row_bottom + gap;
        Rectangle child = {r.x + col * (width + gap), r.y, width, row_height};
        size_t index = boxes.size();
        add_variable(boxes, children[i], child, array);
        row_bottom = std::max(row_bottom, r.y + boxes[index].rect.height);
    }
    return row_bottom;
}

void add_variable(std::vector<Box>& boxes, const Variable& var, Rectangle r, bool cell) {
    const bool aggregate = !var.children.empty();
    const bool array = array_elements(var.children);
    const std::string value = var.is_pointer ? (var.points_to ? hex_address(var.points_to) : "NULL") : var.value;
    std::string label = var.name;
    if (!cell || aggregate) label += ": " + var.type;
    if (!aggregate) label += " = " + value;
    BoxKind kind = BoxKind::Variable;
    if (aggregate) kind = array ? BoxKind::Array : BoxKind::Structure;
    else if (cell) kind = BoxKind::Cell;

    const size_t index = boxes.size();
    // Very deep aggregates can still show their name, type and address in the available width.
    const bool expanded = aggregate && r.width >= 65;
    if (cell && !aggregate) r.height = 47;
    Box box{r, label, kind};
    box.address = var.address;
    box.pointer = var.is_pointer;
    box.points_to = var.points_to;
    box.in_array = cell;
    boxes.push_back(box);
    if (!expanded) return;

    Rectangle inner = {r.x + pad, r.y + r.height, r.width - 2 * pad, 0};
    float bottom = add_children(boxes, var.children, inner, array);
    boxes[index].rect.height = bottom + pad - r.y;
}

void add_frame(Column& column, const Frame& frame, bool current, bool error) {
    Rectangle r = column.row();
    std::string label = frame.function + "()";
    if (frame.line > 0) label += "   ·   line " + std::to_string(frame.line);
    if (current) label += "   ·   current";
    const size_t index = column.boxes.size();
    Box box{r, label, BoxKind::Frame};
    box.error = error;
    column.boxes.push_back(box);
    if (!frame.vars.empty()) {
        Rectangle inner = {r.x + pad, r.y + r.height + 2, r.width - 2 * pad, 0};
        float bottom = add_children(column.boxes, frame.vars, inner, false);
        column.boxes[index].rect.height = bottom + pad - r.y;
    }
    column.y += column.boxes[index].rect.height + 12;
}

void add_block(Column& column, const HeapBlock& block, size_t number, bool error) {
    Rectangle r = column.row();
    std::string label = "Block " + std::to_string(number) + "   ·   " + std::to_string(block.size) + " B";
    if (block.freed) label += "   ·   freed";
    const size_t index = column.boxes.size();
    Box box{r, label, BoxKind::Block};
    box.address = block.address;
    box.size = block.size;
    box.freed = block.freed;
    box.error = error;
    column.boxes.push_back(box);
    if (!block.freed && !block.cells.empty()) {
        Rectangle inner = {r.x + pad, r.y + r.height + 2, r.width - 2 * pad, 0};
        float bottom = add_children(column.boxes, block.cells, inner, array_elements(block.cells));
        column.boxes[index].rect.height = bottom + pad - r.y;
    }
    column.y += column.boxes[index].rect.height + 12;
}

void draw_box(const Box& box) {
    Rectangle r = box.rect;
    const bool group = box.kind == BoxKind::Frame || box.kind == BoxKind::Block;
    const bool aggregate = box.kind == BoxKind::Array || box.kind == BoxKind::Structure;
    const Font& font = group ? g_fonts.ui_bold : g_fonts.mono;
    Color fill = (group || aggregate) ? theme::panel_hi : theme::bg;
    if (box.error) fill = theme::error_bg;
    Color border = box.error ? theme::danger : theme::border;
    Color ink = box.freed ? theme::text_dim : (box.pointer ? theme::accent : theme::text);

    DrawRectangleRounded(r, 0.18f, 6, fill);
    DrawRectangleRoundedLinesEx(r, 0.18f, 6, 1, border);
    float x = r.x + pad, y = r.y + 6;
    if (box.kind == BoxKind::Cell) {
        const size_t split = box.label.find(" = ");
        draw_text(font, 13, fit_text(font, 13, box.label.substr(0, split), r.width - 2 * pad),
                  x, y, theme::text_dim);
        draw_text(font, font_size, fit_text(font, font_size, box.label.substr(split + 3), r.width - 2 * pad),
                  x, y + 17, ink);
    } else {
        std::string label = fit_text(font, font_size, box.label, r.width - 2 * pad);
        draw_text(font, font_size, label, x, y, ink);
        if (box.freed) {
            float strike_y = y + font_size / 2 + 1;
            DrawLineEx({x, strike_y}, {x + text_width(font, font_size, label), strike_y}, 1, ink);
        }
    }
}

void clip(Rectangle area) {
    BeginScissorMode((int)area.x, (int)area.y, (int)area.width, (int)area.height);
}

void draw_column(Column& column, float& offset, const std::string& empty) {
    Rectangle view = column.view;
    float content = std::max(0.0f, column.y - gap);
    scroll_area(view, content, offset);
    clip(view);
    if (column.boxes.empty())
        draw_text(g_fonts.ui, font_size, empty, view.x, view.y + 6, theme::text_dim);
    for (Box& box : column.boxes) {
        box.rect.y += view.y - offset;
        if (CheckCollisionRecs(box.rect, view)) draw_box(box);
    }
    EndScissorMode();
}

int target_priority(BoxKind kind) {
    // A heap base labels the allocation, an array base labels element zero,
    // and a struct base labels the whole struct even when its first field shares the address.
    if (kind == BoxKind::Block) return 3;
    if (kind == BoxKind::Structure) return 2;
    if (kind == BoxKind::Array) return 0;
    return 1;
}

void index_addresses(const Column& column, std::map<uint64_t, const Box*>& targets) {
    for (const Box& box : column.boxes) {
        if (box.address == 0) continue;
        auto [it, inserted] = targets.emplace(box.address, &box);
        if (!inserted && target_priority(box.kind) > target_priority(it->second->kind)) it->second = &box;
    }
}

const Box* pointer_target(uint64_t address, const std::map<uint64_t, const Box*>& targets,
                          const Column& heap) {
    auto exact = targets.find(address);
    if (exact != targets.end()) return exact->second;
    // An interior address can still identify the allocation even without a decoded cell.
    for (const Box& box : heap.boxes) {
        if (box.kind == BoxKind::Block && box.address != 0 &&
            address >= box.address && address - box.address < box.size)
            return &box;
    }
    return nullptr;
}

Vector2 anchor(Rectangle r, bool right, bool heading = false) {
    return {r.x + (right ? r.width : 0), r.y + (heading ? row_height : r.height) / 2};
}

void draw_arrow(Vector2 start, Vector2 p1, Vector2 p2, Vector2 end, Color color) {
    Vector2 last = start;
    constexpr int segments = 28;
    for (int i = 1; i <= segments; ++i) {
        float t = (float)i / segments, u = 1 - t;
        Vector2 next = {u*u*u*start.x + 3*u*u*t*p1.x + 3*u*t*t*p2.x + t*t*t*end.x,
                        u*u*u*start.y + 3*u*u*t*p1.y + 3*u*t*t*p2.y + t*t*t*end.y};
        DrawLineEx(last, next, 2, color);
        last = next;
    }
    Vector2 back = {p2.x - end.x, p2.y - end.y};
    float scale = 7 / std::hypot(back.x, back.y);
    back = {back.x * scale, back.y * scale};
    Vector2 base = {end.x + back.x, end.y + back.y};
    DrawTriangle(end, {base.x - back.y * 0.53f, base.y + back.x * 0.53f},
                 {base.x + back.y * 0.53f, base.y - back.x * 0.53f}, color);
}

bool visible_in(Vector2 point, const Column& column, const Column& other) {
    return CheckCollisionPointRec(point, column.view) || CheckCollisionPointRec(point, other.view);
}

void draw_pointers(const Column& column, const Column& stack, const Column& heap,
                   const std::map<uint64_t, const Box*>& targets) {
    for (const Box& box : column.boxes) {
        if (!box.pointer) continue;
        Vector2 start = anchor(box.rect, true);
        if (!CheckCollisionPointRec(start, column.view)) continue;
        const Box* target = box.points_to ? pointer_target(box.points_to, targets, heap) : nullptr;
        if (!target) {
            Vector2 end = {start.x + 20, start.y};
            Color color = box.error ? theme::danger : theme::text_dim;
            draw_arrow(start, {start.x + 6, start.y}, {end.x - 6, end.y}, end, color);
            draw_text(g_fonts.ui_bold, font_size, box.points_to ? "?" : "∅", end.x + 4, end.y - 8, color);
            continue;
        }
        const bool to_right = target->rect.x > start.x;
        const bool to_left = target->rect.x + target->rect.width < box.rect.x;
        const Color color = target->freed ? theme::warning : theme::accent;
        if (!to_right && !to_left && target->in_array && target->rect.y + target->rect.height < box.rect.y) {
            // Enter a cell from below so the arrow doesn't cross its neighbors.
            start = {box.rect.x + box.rect.width / 2, box.rect.y};
            Vector2 end = {target->rect.x + target->rect.width / 2, target->rect.y + target->rect.height};
            if (!visible_in(end, stack, heap)) continue;
            float bend = (start.y - end.y) * 0.55f;
            draw_arrow(start, {start.x, start.y - bend}, {end.x, end.y + bend}, end, color);
            continue;
        }
        if (to_left) start = anchor(box.rect, false);
        Vector2 end = anchor(target->rect, !to_right, true);
        if (!visible_in(end, stack, heap)) continue;
        const bool cross = to_right || to_left;
        float bend = cross ? std::clamp(std::abs(end.x - start.x) * 0.55f, 10.0f, 90.0f) : 42;
        if (target == &box) {
            start.y += 5;
            end.y -= 6;
        }
        draw_arrow(start, {start.x + (to_left ? -bend : bend), start.y},
                   {end.x + (to_right ? -bend : bend), end.y}, end, color);
    }
}

void draw_address(const Column& column) {
    Vector2 mouse = GetMousePosition();
    if (!CheckCollisionPointRec(mouse, column.view)) return;
    for (auto it = column.boxes.rbegin(); it != column.boxes.rend(); ++it) {
        if (it->address == 0 || !CheckCollisionPointRec(mouse, it->rect)) continue;
        std::string text = hex_address(it->address);
        float width = text_width(g_fonts.mono, font_size, text) + 16;
        float x = std::clamp(mouse.x + 12, 0.0f, std::max(0.0f, GetScreenWidth() - width));
        float y = mouse.y + 24;
        if (y + row_height > GetScreenHeight()) y = mouse.y - row_height;
        Rectangle tip = {x, y, width, row_height};
        DrawRectangleRec(tip, theme::bg);
        DrawRectangleLinesEx(tip, 1, theme::accent);
        draw_text(g_fonts.mono, font_size, text, x + 8, y + 6, theme::text);
        return;
    }
}
}

void draw_memory(const ProgramState& state, Rectangle area) {
    if (area.width < 320 || area.height < 120) return;
    static float stack_scroll = 0, heap_scroll = 0;
    const float gutter = 14;
    const float stack_width = std::floor((area.width - gutter) * 0.55f);
    Rectangle stack_panel = {area.x, area.y, stack_width, area.height};
    Rectangle heap_panel = {area.x + stack_width + gutter, area.y, area.width - stack_width - gutter, area.height};
    if (!state.errors.empty()) {
        stack_panel.height -= 36;
        heap_panel.height -= 36;
        const MemoryError& error = state.errors.front();
        std::string text = error.kind;
        if (error.line > 0) text += " on line " + std::to_string(error.line);
        if (!error.details.empty()) text += " · " + error.details;
        if (state.errors.size() > 1) text += " · +" + std::to_string(state.errors.size() - 1) + " more";
        Rectangle warning = {area.x, area.y + area.height - 28, area.width, 28};
        DrawRectangleRec(warning, theme::error_bg);
        draw_text(g_fonts.ui, font_size, fit_text(g_fonts.ui, font_size, text, warning.width - 20),
                  warning.x + 10, warning.y + 5, theme::danger);
    }
    panel(stack_panel, "Stack");
    panel(heap_panel, "Heap");
    Column stack{panel_body(stack_panel, true), {}}, heap{panel_body(heap_panel, true), {}};
    for (size_t i = state.stack.size(); i > 0; --i) {
        const Frame& frame = state.stack[i - 1];
        const bool current = i == state.stack.size();
        bool error = current && frame.line > 0 &&
                     std::any_of(state.errors.begin(), state.errors.end(), [&](const MemoryError& e) {
                         return e.line == frame.line;
                     });
        add_frame(stack, frame, current, error);
    }
    const bool leak = std::any_of(state.errors.begin(), state.errors.end(), [](const MemoryError& e) {
        return e.kind == "memory-leak";
    });
    for (size_t i = 0; i < state.heap.size(); ++i)
        add_block(heap, state.heap[i], i + 1, leak && state.finished && !state.heap[i].freed);

    // A short stack rests at the bottom. A tall stack starts at the top and scrolls.
    float stack_shift = std::max(0.0f, stack.view.height - stack.y);
    for (Box& box : stack.boxes) box.rect.y += stack_shift;
    draw_column(stack, stack_scroll, "No stack frames");
    draw_column(heap, heap_scroll, "No heap allocations");

    std::map<uint64_t, const Box*> targets;
    index_addresses(stack, targets);
    index_addresses(heap, targets);
    Rectangle view = {stack.view.x, stack.view.y,
                      heap.view.x + heap.view.width - stack.view.x, stack.view.height};
    clip(view);
    draw_pointers(stack, stack, heap, targets);
    draw_pointers(heap, stack, heap, targets);
    EndScissorMode();
    draw_address(stack);
    draw_address(heap);
}
