#include "tutor.h"
#include "services.h"

#include <algorithm>

void TutorPanel::open(const Lesson &context, const std::string &shown_code, const std::string &shown_facts) {
    lesson = context;
    code = shown_code;
    facts = shown_facts;
    answer.clear();
    history.clear();
    slide = 0;
    question = {};
    scroll = 0;
    request = -1;
    visible = true;
    g_focus = Focus::None;
}

void TutorPanel::update_and_draw() {
    if (request >= 0 && ai_poll(request, answer)) {
        history += "Tutor: " + tutor_message(answer) + "\n\n";
        request = -1;
        scroll = 0;
    }
    if (!visible)
        return;
    slide = std::min(1.0f, slide + GetFrameTime() * 8);
    float width = std::min(520.0f, GetScreenWidth() - 48.0f);
    Rectangle r = {GetScreenWidth() - width * slide, 0, width, (float)GetScreenHeight()};
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), {0, 0, 0, 170});
    panel(r, "Ask the tutor");
    if (button({r.x + r.width - 82, r.y + 2, 80, 30}, "Close", true, ButtonStyle::Ghost) ||
        IsKeyPressed(KEY_ESCAPE)) {
        visible = false;
        g_focus = Focus::None;
        return;
    }
    Rectangle body = panel_body(r, true);
    std::string text = history;
    if (request >= 0)
        text += "Thinking…";
    if (text.empty())
        text = ai_available() ? "Ask a question about this lesson. The tutor can see the example and the "
                                "current instruction."
                              : "The tutor is offline. Start Ollama on this computer to ask a question.";
    Rectangle messages = {body.x, body.y, body.width - 8, body.height - 54};
    auto lines = wrap_text(g_fonts.ui, g_fonts.ui_size, text, messages.width);
    scroll_area(messages, lines.size() * (g_fonts.ui_size + 5), scroll);
    BeginScissorMode((int)messages.x, (int)messages.y, (int)messages.width, (int)messages.height);
    draw_wrapped(g_fonts.ui, g_fonts.ui_size, text,
                 {messages.x, messages.y - scroll, messages.width, messages.height}, theme::text);
    EndScissorMode();
    Rectangle field = {body.x, body.y + body.height - 38, body.width - 90, 36};
    bool enter = text_input(field, question, "What would you like to understand?");
    bool send =
        button({field.x + field.width + 8, field.y, 82, 36}, "Ask", request < 0 && !question.text.empty());
    if ((send || enter) && request < 0 && !question.text.empty()) {
        history += "You: " + question.text + "\n\n";
        // Compatibility adapter until ai_ask(AiContext, question) reaches app.h.
        request = ai_chat(lesson, code, facts + "\nConversation:\n" + history, question.text);
        question.text.clear();
    }
}
