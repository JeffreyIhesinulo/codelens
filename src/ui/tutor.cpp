#include "tutor.h"
#include "services.h"
#include "mascot.h"

#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------- chat bubbles

namespace {

const float kPad = 14, kGap = 14, kLabel = 20;

float line_h() { return std::round(g_fonts.ui_size * 1.45f); }

// One word (or a piece of `code`) placed on a line.
struct Piece {
    std::string text;
    bool code = false, bold = false;
    bool block = false;     // a line of a code block (4-space indent): drawn on a full-width strip
    float size = 0;         // 0 = default size; headings are larger
    float x = 0, w = 0;
    int line = 0;
};

struct Layout {
    std::vector<Piece> pieces;
    std::vector<float> line_y;   // top of each line, relative to the text block
    float height = 0, width = 0; // width of the widest line
    float max_w = 0;
};

const Font &font_of(const Piece &p) { return p.code ? g_fonts.mono : p.bold ? g_fonts.ui_bold : g_fonts.ui; }
float size_of(const Piece &p) { return p.size > 0 ? p.size : p.code ? g_fonts.mono_size + 1 : g_fonts.ui_size; }

// Lays out the message: blank line = new paragraph, "- " / "* " / "1. " = list item,
// `x` = code, **x** = bold. Words wrap at max_w.
Layout layout(const std::string &text, float max_w) {
    Layout out;
    out.max_w = max_w;
    float space = text_width(g_fonts.ui, g_fonts.ui_size, " ");
    float x = 0, y = 0, indent = 0;
    int line = 0;
    auto new_line = [&](float extra) {
        y += line_h() + extra;
        ++line;
        x = indent;
        out.line_y.push_back(y);
    };
    out.line_y.push_back(0);

    size_t start = 0;
    bool first_para = true;
    while (start <= text.size()) {
        size_t nl = text.find('\n', start);
        std::string row = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        start = nl == std::string::npos ? text.size() + 1 : nl + 1;
        size_t a = row.find_first_not_of(' ');
        if (a == std::string::npos) {   // blank line: paragraph gap (once)
            if (x > indent || !first_para) {
                if (x > 0) new_line(8);
                first_para = true;
            }
            indent = 0;
            x = 0;
            continue;
        }
        bool code_row = a >= 4;   // indented by 4+ spaces: a line of a code block
        if (x > 0) new_line(0);   // a single newline still breaks the line
        first_para = false;
        indent = 0;
        if (code_row) {
            Piece p;
            p.text = row.substr(4);
            p.code = p.block = true;
            p.x = 14;
            p.line = line;
            p.w = text_width(g_fonts.mono, size_of(p), p.text);
            out.pieces.push_back(p);
            x = 1;   // the next row starts a new line
            continue;
        }
        row = row.substr(a);
        if (row.rfind("## ", 0) == 0) {   // heading
            if (line > 0) { y += 10; out.line_y[line] = y; }
            Piece p;
            p.text = row.substr(3);
            p.bold = true;
            p.size = g_fonts.ui_size + 4;
            p.line = line;
            p.w = text_width(font_of(p), size_of(p), p.text);
            out.pieces.push_back(p);
            y += 6;
            x = 1;
            continue;
        }
        // list item: draw a bullet and indent the rest
        bool bullet = row.rfind("- ", 0) == 0 || row.rfind("* ", 0) == 0;
        size_t dot = row.find(". ");
        bool numbered = dot != std::string::npos && dot > 0 && dot <= 2 &&
                        std::all_of(row.begin(), row.begin() + dot, ::isdigit);
        if (bullet || numbered) {
            std::string mark = bullet ? "•" : row.substr(0, dot + 1);
            row = row.substr(bullet ? 2 : dot + 2);
            Piece p;
            p.text = mark;
            p.bold = numbered;
            p.x = 4;
            p.line = line;
            p.w = text_width(font_of(p), size_of(p), mark);
            out.pieces.push_back(p);
            indent = 22;
            x = indent;
        }
        // split into words, tracking `code` and **bold**
        bool code = false, bold = false;
        std::string word;
        auto flush = [&](bool space_after) {
            if (word.empty()) return;
            Piece p;
            p.text = word;
            p.code = code;
            p.bold = bold && !code;
            p.w = text_width(font_of(p), size_of(p), word);
            if (x > indent && x + p.w > max_w) new_line(0);
            p.x = x;
            p.line = line;
            out.pieces.push_back(p);
            x += p.w + (space_after ? space : 0);
            out.width = std::max(out.width, x);
            word.clear();
        };
        for (size_t i = 0; i < row.size(); ++i) {
            char c = row[i];
            if (c == '`') { flush(false); code = !code; continue; }
            if (!code && c == '*' && i + 1 < row.size() && row[i + 1] == '*') { flush(false); bold = !bold; ++i; continue; }
            if (c == ' ') {
                if (word.empty()) { if (x > indent) x += space; }   // space after `code` or **bold**
                else flush(true);
                continue;
            }
            word += c;
        }
        flush(true);
    }
    out.height = y + line_h();
    for (const Piece &p : out.pieces) out.width = std::max(out.width, p.x + p.w);
    return out;
}

void draw_layout(const Layout &l, float x0, float y0, Color text) {
    for (const Piece &p : l.pieces) {
        float x = x0 + p.x, y = y0 + l.line_y[p.line];
        if (p.block) {
            DrawRectangle((int)x0, (int)y - 2, (int)l.max_w, (int)line_h(), theme::bg);
            draw_text(g_fonts.mono, size_of(p), p.text, x, y + 3, theme::text);
            continue;
        }
        if (p.code) {
            float pad = 3;
            DrawRectangleRounded({x - pad, y - 1, p.w + 2 * pad, line_h() - 4}, 0.3f, 4, theme::bg);
            draw_text(g_fonts.mono, size_of(p), p.text, x, y + 2, theme::str);
        } else {
            draw_text(font_of(p), size_of(p), p.text, x, y + 1, p.text == "•" ? theme::accent : text);
        }
    }
}

} // namespace

float draw_rich_block(float x, float y, float width, const std::string &text, bool draw) {
    Layout l = layout(text, width);
    if (draw) draw_layout(l, x, y, theme::text);
    return l.height;
}

float draw_rich_text(Rectangle area, const std::string &text, float &scroll) {
    Layout l = layout(text, area.width - 16);
    BeginScissorMode((int)area.x, (int)area.y, (int)area.width, (int)area.height);
    draw_layout(l, area.x, area.y - scroll, theme::text);
    EndScissorMode();
    scroll_area(area, l.height + 20, scroll);
    return l.height;
}

float draw_chat(Rectangle area, const std::vector<ChatMessage> &messages, bool thinking, float &scroll,
                bool &stick_bottom) {
    float max_bubble = std::max(120.0f, area.width * 0.86f);
    std::vector<Layout> layouts;
    float total = 0;
    for (const ChatMessage &m : messages) {
        layouts.push_back(layout(m.text, max_bubble - 2 * kPad));
        total += layouts.back().height + 2 * kPad + kLabel + kGap;
    }
    if (thinking) total += line_h() + 2 * kPad + kLabel + kGap;
    if (stick_bottom) {
        scroll = std::max(0.0f, total - area.height);
        stick_bottom = false;
    }

    BeginScissorMode((int)area.x, (int)area.y, (int)area.width + 4, (int)area.height);
    float y = area.y - scroll;
    auto bubble = [&](bool from_ai, float w, float h) {
        float bx = from_ai ? area.x : area.x + area.width - w;
        if (from_ai) {   // Bit the raccoon is the tutor
            draw_raccoon(bx + 9, y + 9, 8);
            draw_text(g_fonts.ui_bold, 13, "Bit · tutor", bx + 24, y + 1, theme::accent);
        } else
            draw_text(g_fonts.ui_bold, 13, "You", bx + w - text_width(g_fonts.ui_bold, 13, "You") - 2, y,
                      theme::text_dim);
        Rectangle b{bx, y + kLabel, w, h};
        DrawRectangleRounded(b, std::min(0.5f, 24.0f / std::max(h, 1.0f)), 8, from_ai ? theme::panel_hi : theme::accent_dk);
        if (from_ai) DrawRectangleRounded({b.x, b.y + 8, 3, b.height - 16}, 1, 4, theme::accent);
        return b;
    };
    for (size_t i = 0; i < messages.size(); ++i) {
        const Layout &l = layouts[i];
        bool ai = messages[i].from_ai;
        float w = ai ? max_bubble : std::min(max_bubble, l.width + 2 * kPad);
        float h = l.height + 2 * kPad;
        Rectangle b = bubble(ai, w, h);
        draw_layout(l, b.x + kPad + (ai ? 4 : 0), b.y + kPad, theme::text);
        y += h + kLabel + kGap;
    }
    if (thinking) {
        Rectangle b = bubble(true, 86, line_h() + 2 * kPad);
        for (int d = 0; d < 3; ++d) {
            float phase = (float)GetTime() * 5 - d * 0.7f;
            float lift = std::max(0.0f, std::sin(phase)) * 4;
            DrawCircle((int)(b.x + 26 + d * 16), (int)(b.y + b.height / 2 - lift), 4,
                       ColorAlpha(theme::accent, 0.5f + 0.5f * lift / 4));
        }
    }
    EndScissorMode();
    scroll_area(area, total, scroll);
    return total;
}

// ---------------------------------------------------------------- slide-in tutor panel

void TutorPanel::open(const Lesson &context, const std::string &shown_code, const std::string &shown_facts) {
    lesson = context;
    code = shown_code;
    facts = shown_facts;
    answer.clear();
    history.clear();
    messages.clear();
    slide = 0;
    question = {};
    scroll = 0;
    request = -1;
    visible = true;
    g_focus = Focus::None;
}

void TutorPanel::update_and_draw() {
    if (request >= 0 && ai_poll(request, answer)) {
        std::string reply = tutor_message(answer);
        history += "Tutor: " + reply + "\n\n";
        messages.push_back({true, reply});
        request = -1;
        stick_bottom = true;
    }
    if (!visible)
        return;
    slide = std::min(1.0f, slide + GetFrameTime() * 8);
    float width = std::min(540.0f, GetScreenWidth() - 48.0f);
    Rectangle r = {GetScreenWidth() - width * slide, 0, width, (float)GetScreenHeight()};
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), {0, 0, 0, 170});
    DrawRectangleRec(r, theme::panel);
    DrawLineEx({r.x, r.y}, {r.x, r.y + r.height}, 2, theme::accent_dk);

    // header: title, where the tutor runs, Close
    Rectangle head{r.x, r.y, r.width, 72};
    DrawRectangleRec(head, theme::accent_soft);
    draw_raccoon(head.x + 44, head.y + 40, 24, request >= 0 ? 2 : 0);
    draw_text(g_fonts.ui_bold, 20, "Ask Bit, your tutor", head.x + 82, head.y + 14, theme::text);
    bool on = ai_available();
    DrawCircle((int)(head.x + 87), (int)(head.y + 51), 4, on ? theme::success : theme::warning);
    draw_text(g_fonts.ui, 14, on ? "Runs on your computer · private" : "Offline: start Ollama to ask questions",
              head.x + 98, head.y + 43, theme::text_dim);
    if (button({head.x + head.width - 96, head.y + 18, 80, 34}, "Close", true, ButtonStyle::Ghost) ||
        IsKeyPressed(KEY_ESCAPE)) {
        visible = false;
        g_focus = Focus::None;
        return;
    }

    Rectangle body{r.x + 20, head.y + head.height + 16, r.width - 40, r.height - head.height - 32};
    Rectangle area{body.x, body.y, body.width - 8, body.height - 64};
    if (messages.empty() && request < 0) {
        std::vector<ChatMessage> intro{
            {true, on ? "Hi, I'm Bit! Ask me anything about this step. I can see the example, the instruction and what the "
                        "program just did.\n\nFor example:\n- Why did this line change the value?\n- What does `\\n` do?"
                      : "The tutor is offline. Start Ollama on this computer, then ask your question here."}};
        bool no = false;
        float s = 0;
        draw_chat(area, intro, false, s, no);
    } else {
        draw_chat(area, messages, request >= 0, scroll, stick_bottom);
    }

    Rectangle field = {body.x, body.y + body.height - 44, body.width - 96, 44};
    bool enter = text_input(field, question, "What would you like to understand?");
    bool send = button({field.x + field.width + 10, field.y, 86, 44}, "Ask", request < 0 && !question.text.empty(),
                       ButtonStyle::Primary);
    if ((send || enter) && request < 0 && !question.text.empty()) {
        history += "You: " + question.text + "\n\n";
        messages.push_back({false, question.text});
        stick_bottom = true;
        // Compatibility adapter until ai_ask(AiContext, question) reaches app.h.
        request = ai_chat(lesson, code, facts + "\nConversation:\n" + history, question.text);
        question.text.clear();
    }
}
