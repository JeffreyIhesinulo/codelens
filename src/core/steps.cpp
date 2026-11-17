// Checking Order and Choose steps.
#include "app.h"

#include <algorithm>
#include <numeric>

// Small deterministic generator so a step always starts in the same shuffled order
// (stable for tests and for a student who comes back to the step).
static uint32_t next_rand(uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

std::vector<int> order_shuffled(const Step& step) {
    int n = (int)step.lines.size();
    std::vector<int> order(n);
    std::iota(order.begin(), order.end(), 0);
    if (n < 2) return order;

    uint32_t seed = 2166136261u;   // FNV-1a over the lines
    for (const std::string& l : step.lines)
        for (char c : l) { seed ^= (unsigned char)c; seed *= 16777619u; }
    if (seed == 0) seed = 1;

    for (int attempt = 0; attempt < 20; ++attempt) {
        for (int i = n - 1; i > 0; --i) std::swap(order[i], order[next_rand(seed) % (i + 1)]);
        if (!check_order(step, order)) return order;
    }
    std::rotate(order.begin(), order.begin() + 1, order.end());   // all lines identical except one: still differs
    return order;
}

bool check_order(const Step& step, const std::vector<int>& order) {
    if (order.size() != step.lines.size()) return false;
    std::vector<bool> used(order.size(), false);
    for (size_t i = 0; i < order.size(); ++i) {
        int k = order[i];
        if (k < 0 || k >= (int)step.lines.size() || used[k]) return false;
        used[k] = true;
        // compare text, ignoring indentation: two "}" lines are interchangeable
        auto strip = [](const std::string& s) {
            size_t a = s.find_first_not_of(' ');
            return a == std::string::npos ? std::string() : s.substr(a);
        };
        if (strip(step.lines[k]) != strip(step.lines[i])) return false;
    }
    return true;
}

Verdict check_choice(const Step& step, int index) {
    Verdict v;
    if (step.correct >= 0 && step.correct < (int)step.options.size()) v.expected = step.options[step.correct];
    v.correct = index == step.correct;
    std::string why = step.sample_answer.empty() ? "" : " " + step.sample_answer;
    v.feedback = v.correct ? "Right." + why : "Not quite. The answer is: " + v.expected + "." + why;
    return v;
}
