#include "list_view.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <map>
#include <set>

namespace {
const Variable *field(const std::vector<Variable> &fields, const std::string &name) {
    for (const auto &value : fields)
        if (value.name == name)
            return &value;
    return nullptr;
}
bool known(const Variable &value) { return !value.value.empty() && value.value != "?"; }
void arrow(Vector2 from, Vector2 to, Color color) {
    DrawLineEx(from, to, 2, color);
    Vector2 direction{to.x - from.x, to.y - from.y};
    float length = std::max(1.0f, std::hypot(direction.x, direction.y));
    direction = {direction.x * 7 / length, direction.y * 7 / length};
    Vector2 base{to.x - direction.x, to.y - direction.y};
    DrawTriangle(to, {base.x + direction.y * .5f, base.y - direction.x * .5f},
                 {base.x - direction.y * .5f, base.y + direction.x * .5f}, color);
}
} // namespace

ProgramState list_debug_snapshot() {
    ProgramState state = debug_state();
    if (state.finished)
        return state;
    bool node_type = false;
    for (const auto &frame : state.stack)
        for (const auto &value : frame.vars)
            if (value.is_pointer && value.type.find("node") != std::string::npos)
                node_type = true;
    if (!node_type)
        return state;
    for (auto &block : state.heap) {
        if (block.freed)
            continue;
        bool fields = field(block.cells, "value") && field(block.cells, "next");
        for (const auto &cell : block.cells)
            fields = fields || (field(cell.children, "value") && field(cell.children, "next"));
        if (fields)
            continue;
        // Some backends expose only blocks directly held by local pointers. Read the other
        // live nodes through the engine's expression evaluator, using recorded allocations.
        std::string node = "((struct node *)" + std::to_string(block.address) + "ULL)";
        std::string value = debug_eval(node + "->value");
        std::string next = debug_eval("(unsigned long long)(" + node + "->next)");
        char *end = nullptr;
        std::strtoll(value.c_str(), &end, 10);
        if (value.empty() || !end || *end)
            continue;
        uint64_t target = std::strtoull(next.c_str(), &end, 0);
        if (next.empty() || !end || *end)
            continue;
        block.cells = {{"value", "int", value, 0, false, 0, {}},
                       {"next", "struct node *", next, 0, true, target, {}}};
    }
    return state;
}

ListPicture list_picture(const ProgramState &state, const std::string &focus) {
    ListPicture picture;
    std::map<uint64_t, ListNodePicture> nodes;
    auto add = [&](uint64_t address, const std::vector<Variable> &fields, bool freed,
                   const std::string &name) {
        const auto *value = field(fields, "value"), *next = field(fields, "next");
        if (!address || !value || !next || !next->is_pointer)
            return;
        nodes[address] = {address,      value->address, next->address, next->points_to,
                          value->value, name,           known(*next),  freed};
    };
    std::function<void(const Variable &, const std::string &)> visit = [&](const Variable &value,
                                                                           const std::string &name) {
        add(value.address, value.children, false, name);
        for (const auto &child : value.children)
            visit(child, name + (child.name.rfind("[", 0) == 0 ? "" : ".") + child.name);
    };
    for (const auto &frame : state.stack)
        for (const auto &value : frame.vars)
            visit(value, value.name);
    bool list_type = !nodes.empty() || state.finished;
    for (const auto &frame : state.stack)
        for (const auto &value : frame.vars)
            if (value.type.find("node") != std::string::npos)
                list_type = true;
    for (const auto &block : state.heap) {
        // GDB exposes struct fields directly; LLDB wraps them in an array element.
        add(block.address, block.cells, block.freed, "");
        for (const auto &cell : block.cells) {
            add(cell.address, cell.children, block.freed, "");
        }
        // A new allocation or a freed allocation may have no readable fields.
        if (list_type && !nodes.count(block.address))
            nodes[block.address] = {block.address, 0, 0, 0, "", "", false, block.freed};
    }
    for (auto frame = state.stack.rbegin(); frame != state.stack.rend(); ++frame)
        for (const auto &value : frame->vars)
            if (value.is_pointer &&
                (value.type.find("node") != std::string::npos || nodes.count(value.points_to)))
                picture.references.push_back({value.name, value.points_to, known(value)});
    std::stable_sort(picture.references.begin(), picture.references.end(), [](const auto &a, const auto &b) {
        if ((a.name == "head") != (b.name == "head"))
            return a.name == "head";
        return a.name < b.name;
    });
    std::set<uint64_t> visited;
    auto chain = [&](uint64_t address) {
        while (address && nodes.count(address) && visited.insert(address).second) {
            const auto &node = nodes.at(address);
            picture.nodes.push_back(node);
            if (node.freed || !node.next_known)
                break;
            address = node.next;
        }
    };
    // Start at an entry node rather than a tail pointer declared earlier in the program.
    for (const auto &ref : picture.references)
        if (ref.known && nodes.count(ref.target)) {
            bool incoming = std::any_of(nodes.begin(), nodes.end(), [&](const auto &entry) {
                return !entry.second.freed && entry.second.next_known && entry.second.next == ref.target;
            });
            if (ref.name == "head" || !incoming)
                chain(ref.target);
        }
    for (const auto &ref : picture.references)
        if (ref.known)
            chain(ref.target);
    for (const auto &node : nodes)
        chain(node.first);

    // Resolve the lesson's pointer expression without executing or guessing values.
    size_t split = focus.find("->");
    if (split != std::string::npos) {
        std::string name = focus.substr(0, split);
        auto ref = std::find_if(picture.references.begin(), picture.references.end(),
                                [&](const auto &r) { return r.name == name && r.known; });
        uint64_t address = ref == picture.references.end() ? 0 : ref->target;
        size_t offset = split + 2;
        while (address && nodes.count(address) && !nodes.at(address).freed) {
            const auto &node = nodes.at(address);
            size_t end = focus.find("->", offset);
            std::string member = focus.substr(offset, end == std::string::npos ? end : end - offset);
            if (member == "value" && end == std::string::npos && !node.value.empty() && node.value != "?") {
                picture.focused_node = address;
                picture.focus_result = focus + " = " + node.value;
                break;
            }
            if (member != "next" || !node.next_known)
                break;
            address = node.next;
            if (end == std::string::npos)
                break;
            offset = end + 2;
        }
    }
    return picture;
}

void ListView::draw(const ListPicture &picture, Rectangle area) {
    if (area.width < 170 || area.height < 70)
        return;
    const float label_height = 26;
    draw_text(g_fonts.ui_bold, 16, "Nodes — each box has a value and a next link", area.x + 8, area.y,
              theme::text);
    Rectangle view{area.x, area.y + label_height, area.width, area.height - label_height};
    constexpr float row = 100, card_height = 72;
    float refs_height =
        picture.references.empty() ? 0 : 20.0f * std::ceil(picture.references.size() / 2.0f) + 8;
    float content = refs_height + (picture.nodes.empty() ? 64 : picture.nodes.size() * row);
    std::map<uint64_t, int> positions;
    for (size_t i = 0; i < picture.nodes.size(); ++i)
        positions[picture.nodes[i].address] = i;
    uint64_t follow = picture.focused_node;
    if (!follow)
        for (const auto &ref : picture.references)
            if (ref.name == "p" && ref.known)
                follow = ref.target;
    if (follow != followed && positions.count(follow)) {
        float top = refs_height + positions[follow] * row;
        if (top < scroll)
            scroll = top;
        if (top + card_height > scroll + view.height)
            scroll = top + card_height - view.height;
    }
    followed = follow;
    scroll_area(view, content, scroll);
    auto node_label = [&](uint64_t address) {
        auto it = positions.find(address);
        if (it == positions.end())
            return std::string("unknown target");
        return "node " + std::to_string(it->second + 1);
    };
    BeginScissorMode(view.x, view.y, view.width, view.height);
    for (size_t i = 0; i < picture.references.size(); ++i) {
        const auto &ref = picture.references[i];
        std::string target = !ref.known    ? "no value yet"
                             : !ref.target ? "NULL (no node)"
                                           : node_label(ref.target);
        auto found = positions.find(ref.target);
        bool freed = found != positions.end() && picture.nodes[found->second].freed;
        if (freed)
            target += " (freed)";
        std::string label = ref.name + " → " + target;
        draw_text(g_fonts.mono, 13, fit_text(g_fonts.mono, 13, label, view.width / 2 - 12),
                  view.x + 8 + (i % 2) * view.width / 2, view.y + (i / 2) * 20 - scroll,
                  freed             ? theme::danger
                  : ref.name == "p" ? theme::success
                                    : theme::accent);
    }
    if (picture.nodes.empty())
        draw_wrapped(g_fonts.ui, 16, "No nodes reserved yet. Next line runs the highlighted instruction.",
                     {view.x + 12, view.y + refs_height + 12 - scroll, view.width - 24, 0}, theme::text_dim);
    for (size_t i = 0; i < picture.nodes.size(); ++i) {
        const auto &node = picture.nodes[i];
        Rectangle card{view.x + 10, view.y + refs_height + i * row - scroll, view.width - 60, card_height};
        bool current =
            std::any_of(picture.references.begin(), picture.references.end(), [&](const auto &ref) {
                return ref.name == "p" && ref.known && ref.target == node.address;
            });
        Color color = node.freed ? theme::text_dim : theme::text;
        bool focus = picture.focused_node == node.address;
        DrawRectangleRounded(card, .15f, 6, focus || current ? theme::accent_soft : theme::panel);
        DrawRectangleRoundedLinesEx(card, .15f, 6, 1,
                                    node.freed ? theme::border
                                    : current  ? theme::success
                                               : theme::accent_dk);
        std::string title = "Node " + std::to_string(i + 1);
        if (!node.name.empty())
            title += " · " + node.name;
        if (node.freed)
            title += " · freed";
        else if (current)
            title += " · p is here";
        draw_text(g_fonts.ui_bold, 14, fit_text(g_fonts.ui_bold, 14, title, card.width - 20), card.x + 10,
                  card.y + 8, color);
        std::string value = node.freed ? "memory given back"
                                       : "value: " + (node.value.empty() || node.value == "?"
                                                          ? std::string("not available yet")
                                                          : node.value);
        draw_text(g_fonts.mono, 15, fit_text(g_fonts.mono, 15, value, card.width - 20), card.x + 10,
                  card.y + 30, color);
        std::string next = !node.next_known ? "next: not available yet"
                           : node.next == 0 ? "next: NULL — end of list"
                                            : "next → " + node_label(node.next);
        if (!node.freed)
            draw_text(g_fonts.mono, 13, fit_text(g_fonts.mono, 13, next, card.width - 20), card.x + 10,
                      card.y + 52, theme::text_dim);
        if (!node.freed && node.next_known && node.next) {
            auto target = positions.find(node.next);
            Color ink = target == positions.end() || picture.nodes[target->second].freed ? theme::danger
                                                                                         : theme::accent;
            if (target != positions.end()) {
                float target_y = view.y + refs_height + target->second * row - scroll;
                if (target->second == (int)i + 1)
                    arrow({card.x + card.width / 2, card.y + card.height},
                          {card.x + card.width / 2, target_y - 2}, ink);
                else {
                    float track = card.x + card.width + 14 + (i % 3) * 6;
                    DrawLineEx({card.x + card.width, card.y + 55}, {track, card.y + 55}, 2, ink);
                    DrawLineEx({track, card.y + 55}, {track, target_y + 14}, 2, ink);
                    arrow({track, target_y + 14}, {card.x + card.width + 1, target_y + 14}, ink);
                }
            }
        }
    }
    EndScissorMode();
}
