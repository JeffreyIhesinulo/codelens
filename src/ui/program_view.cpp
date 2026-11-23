#include "program_view.h"
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <sstream>

namespace {
const Variable *find_variable(const ProgramState &state, const std::string &name) {
    for (auto frame = state.stack.rbegin(); frame != state.stack.rend(); ++frame)
        for (const auto &variable : frame->vars)
            if (variable.name == name)
                return &variable;
    return nullptr;
}
void draw_output(const ProgramState &state, Rectangle area, float &scroll, bool waiting) {
    panel(area, "Program output");
    Rectangle body = panel_body(area, true);
    if (state.output.empty()) {
        std::string message = waiting ? "Running the highlighted line…"
                              : state.finished
                                  ? "The program finished without printing any text."
                                  : "Nothing printed yet. Use Next line to run the highlighted instruction.";
        draw_wrapped(g_fonts.ui, 17, message, {body.x + 6, body.y + 12, body.width - 12, 0}, theme::text_dim);
        return;
    }
    std::vector<std::string> lines;
    std::istringstream input(state.output);
    for (std::string line; std::getline(input, line);) {
        // Wrap by characters so spaces in the program's output are retained.
        const float character = text_width(g_fonts.mono, 18, "M");
        const int limit = std::max(1, (int)((body.width - 18) / character));
        if (line.empty())
            lines.push_back("");
        for (size_t i = 0; i < line.size(); i += limit)
            lines.push_back(line.substr(i, limit));
    }
    scroll_area(body, lines.size() * 27.0f + 12, scroll);
    BeginScissorMode(body.x, body.y, body.width, body.height);
    for (size_t i = 0; i < lines.size(); ++i)
        draw_text(g_fonts.mono, 18, lines[i], body.x + 8, body.y + 8 + i * 27 - scroll, theme::success);
    EndScissorMode();
}
} // namespace

ProgramPictureFacts program_picture_facts(const ProgramState &state, const std::string &focus) {
    ProgramPictureFacts facts;
    for (const auto &frame : state.stack)
        if (!frame.vars.empty())
            facts.has_values = true;
    const Variable *index = find_variable(state, "i");
    int selected = -1;
    if (index && index->value != "?" && !index->value.empty()) {
        char *end = nullptr;
        long value = std::strtol(index->value.c_str(), &end, 10);
        if (end && *end == 0) {
            selected = (int)value;
            facts.counter = "Counter i = " + index->value;
        }
    }
    uint64_t pointer = 0;
    for (const auto &frame : state.stack)
        for (const auto &variable : frame.vars)
            if (variable.is_pointer && variable.points_to)
                pointer = variable.points_to;
    for (const auto &frame : state.stack)
        for (const auto &array : frame.vars) {
            if (array.children.empty() || array.children.front().name.empty() ||
                array.children.front().name.front() != '[')
                continue;
            for (size_t i = 0; i < array.children.size(); ++i) {
                const auto &box = array.children[i];
                if ((pointer && box.address == pointer) || (!pointer && selected == (int)i)) {
                    facts.selected_address = box.address;
                    facts.selected_box =
                        array.name + box.name + " = " + (box.value == "?" ? "no value yet" : box.value);
                    break;
                }
            }
            if (facts.selected_box.empty() && selected >= (int)array.children.size())
                facts.selected_box = "Index i = " + std::to_string(selected) + " is outside boxes 0–" +
                                     std::to_string(array.children.size() - 1) + ".";
        }
    if (!focus.empty()) {
        const Variable *target = find_variable(state, focus);
        size_t open = focus.find('['), close = focus.find(']');
        if (open != std::string::npos && close == focus.size() - 1) {
            auto *array = find_variable(state, focus.substr(0, open));
            if (array)
                for (const auto &child : array->children)
                    if (child.name == focus.substr(open))
                        target = &child;
        }
        if (target) {
            facts.selected_address = target->address;
            facts.selected_box =
                focus + (target->is_pointer ? " points to " : " = ") +
                (target->is_pointer ? (target->points_to ? state_describe_address(state, target->points_to)
                                                         : "nothing (NULL)")
                 : target->value == "?" ? "no value yet"
                                        : target->value);
        }
    }
    return facts;
}
void ProgramView::reset() {
    output_scroll = 0;
    list = {};
    branch.clear();
    previous_note.clear();
}
Rectangle ProgramView::why_button(Rectangle area) {
    return {area.x + area.width - 84, area.y + area.height - 45, 78, 34};
}
void ProgramView::update_and_draw(const ProgramState &state, Rectangle area, ShowLevel level,
                                  const std::string &topic, bool waiting, const std::string &focus) {
    const auto facts = program_picture_facts(state, focus);
    const auto chain = topic == "lists" ? list_picture(state, focus) : ListPicture{};
    const bool show_list = !chain.nodes.empty() || !chain.references.empty();
    if (state.note != previous_note) {
        if (state.note.find("program starts") != std::string::npos)
            branch.clear();
        if (topic.find("condition") != std::string::npos || topic.find("if") != std::string::npos) {
            if (state.note.find("true") != std::string::npos)
                branch = "true";
            else if (state.note.find("false") != std::string::npos)
                branch = "false";
        }
        previous_note = state.note;
    }
    float narration =
        state.note.empty()
            ? 0
            : std::max(58.0f, 24.0f * wrap_text(g_fonts.ui, 16, state.note, area.width - 110).size() + 14);
    narration = std::min(narration, 94.0f);
    Rectangle content = area;
    content.height -= narration;
    float y = content.y;
    if (!branch.empty()) {
        for (int i = 0; i < 2; ++i) {
            std::string value = i == 0 ? "true" : "false";
            Rectangle chip{content.x + i * (content.width + 8) / 2, y, (content.width - 8) / 2, 34};
            DrawRectangleRounded(chip, .2, 6, value == branch ? theme::accent_dk : theme::panel);
            draw_text(g_fonts.ui_bold, 15, "Condition: " + value, chip.x + 12, chip.y + 9,
                      value == branch ? theme::text : theme::text_dim);
        }
        y += 44;
    }
    std::string caption = show_list ? chain.focus_result
                          : !facts.selected_box.empty()
                              ? "Selected box: " + facts.selected_box
                              : (topic.find("loop") != std::string::npos ? facts.counter : "");
    if (!caption.empty()) {
        float height = draw_wrapped(g_fonts.ui_bold, 16, caption, {content.x + 8, y, content.width - 16, 0},
                                    theme::accent, 4);
        y += height + 12;
    }
    content.height -= y - content.y;
    content.y = y;
    const bool memory = facts.has_values || (level >= ShowLevel::Frames && !state.stack.empty()) ||
                        (level == ShowLevel::Heap && !state.heap.empty());
    if (show_list) {
        const float output_height = state.output.empty() ? 0 : 30;
        Rectangle chain_area = content;
        chain_area.height -= output_height ? output_height + 6 : 0;
        list.draw(chain, chain_area);
        if (output_height)
            draw_text(g_fonts.mono, 15,
                      fit_text(g_fonts.mono, 15, "Printed: " + state.output, content.width - 16),
                      content.x + 8, content.y + content.height - 24, theme::success);
    } else if (memory) {
        const bool compact = content.height < 240;
        float output_height = state.output.empty() ? 0
                              : compact            ? 30
                                                   : std::min(136.0f, std::max(80.0f, content.height * .3f));
        int boxes = 0;
        std::function<void(const Variable &)> count = [&](const Variable &value) {
            ++boxes;
            for (const auto &child : value.children)
                count(child);
        };
        for (const auto &frame : state.stack)
            for (const auto &value : frame.vars)
                count(value);
        float desired = 56 + boxes * 35 + (level >= ShowLevel::Frames ? state.stack.size() * 40 : 0);
        for (const auto &block : state.heap)
            desired = std::max(desired, 80.0f + block.cells.size() * 35);
        float available = content.height - output_height - (output_height ? 10 : 0);
        float memory_height = std::min(available, std::max(110.0f, desired));
        float top = content.y + std::max(0.0f, (available - memory_height) / 2);
        draw_memory(state, {content.x, top, content.width, memory_height}, level, facts.selected_address);
        if (output_height) {
            Rectangle output{content.x, content.y + content.height - output_height, content.width,
                             output_height};
            if (compact)
                draw_text(g_fonts.mono, 15,
                          fit_text(g_fonts.mono, 15, "Printed: " + state.output, output.width - 16),
                          output.x + 8, output.y + 6, theme::success);
            else
                draw_output(state, output, output_scroll, waiting);
        }
    } else {
        float desired =
            std::max(170.0f, 70 + 27.0f * std::count(state.output.begin(), state.output.end(), '\n'));
        float height = std::min(content.height, desired);
        Rectangle output{content.x, content.y + std::max(0.0f, (content.height - height) / 2), content.width,
                         height};
        draw_output(state, output, output_scroll, waiting);
    }
    if (narration) {
        Rectangle note{area.x, area.y + area.height - narration + 6, area.width - 100, narration - 8};
        draw_wrapped(g_fonts.ui, 16, state.note, note, state.errors.empty() ? theme::text : theme::danger, 6);
    }
}
