// One plain-English sentence per debugger step, from the difference between two snapshots:
//   "Line 4: created x = 3."
//   "Line 6: x changed from 3 to 10."
//   "Line 11: called swap(a = &x, b = &y); now inside swap."
//   "Line 12: printed \"5 3\"."
#include "app.h"

#include <cstdio>
#include <cstdlib>
#include <regex>

namespace {

// GDB's way of printing values -> what a beginner expects to read.
std::string show_value(const ProgramState& s, const Variable& v) {
    if (v.is_pointer) {
        if (v.points_to == 0) return "NULL";
        size_t q = v.value.find(" \"");   // char *: 0x55... "hello"
        if (q != std::string::npos) return v.value.substr(q + 1);
        std::string target = state_describe_address(s, v.points_to);
        if (target.rfind("heap block", 0) == 0) return target;   // not "&heap block 1"
        return target.empty() ? "an address" : "&" + target;
    }
    std::smatch m;
    static const std::regex ch(R"(^(-?\d+) '(.*)'$)");          // char: 65 'A'
    if (std::regex_match(v.value, m, ch)) return "'" + m[2].str() + "'";
    static const std::regex dbl(R"(^-?\d+\.\d{7,}$)");          // 3.8999999999999999
    if (std::regex_match(v.value, dbl)) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%g", std::strtod(v.value.c_str(), nullptr));
        return buf;
    }
    return v.value;
}

// "&x" -> "x"; anything else unchanged
std::string target(const std::string& shown) {
    return !shown.empty() && shown[0] == '&' ? shown.substr(1) : shown;
}

const Variable* find(const std::vector<Variable>& vars, const std::string& name) {
    for (const Variable& v : vars) if (v.name == name) return &v;
    return nullptr;
}

bool all_unknown(const Variable& v) {
    if (v.children.empty()) return v.value == "?";
    for (const Variable& c : v.children) if (!all_unknown(c)) return false;
    return true;
}

std::string describe_new(const ProgramState& s, const Variable& v) {
    if (!v.children.empty()) {
        bool array = !v.children[0].name.empty() && v.children[0].name[0] == '[';
        std::string empty = all_unknown(v) ? ", no values yet" : "";
        return array ? "created " + v.name + " (" + std::to_string(v.children.size()) + " boxes" + empty + ")"
                     : "created " + v.name + (empty.empty() ? "" : " (no values yet)");
    }
    if (v.value == "?") return "created " + v.name + " (no value yet)";
    if (v.is_pointer)
        return v.points_to ? "created " + v.name + ", pointing to " + target(show_value(s, v))
                           : "created " + v.name + " = NULL";
    return "created " + v.name + " = " + show_value(s, v);
}

// Differences inside one variable (and its array boxes / struct fields).
void diff_var(const ProgramState& sb, const ProgramState& sa, const Variable& b, const Variable& a,
              const std::string& name, const std::string& where, std::vector<std::string>& out) {
    if (!a.children.empty() && a.children.size() == b.children.size()) {
        for (size_t i = 0; i < a.children.size(); ++i) {
            const Variable& cb = b.children[i];
            const Variable& ca = a.children[i];
            std::string sub = ca.name[0] == '[' ? name + ca.name : name + "." + ca.name;
            diff_var(sb, sa, cb, ca, sub, where, out);
        }
        return;
    }
    if (a.is_pointer) {
        if (a.points_to == b.points_to) return;
        out.push_back(a.points_to ? name + where + " now points to " + target(show_value(sa, a))
                                  : name + where + " is now NULL");
        return;
    }
    std::string vb = show_value(sb, b), va = show_value(sa, a);
    if (vb == va) return;
    if (vb == "?") out.push_back(name + where + " got the value " + va);
    else out.push_back(name + where + " changed from " + vb + " to " + va);
}

std::string call_text(const ProgramState& s, const Frame& f) {
    std::string args;
    for (const Variable& v : f.vars) {
        if (!args.empty()) args += ", ";
        args += v.name + " = " + (v.children.empty() ? show_value(s, v) : "...");
    }
    return f.function + "(" + args + ")";
}

std::vector<std::string> split_lines(const std::string& code) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= code.size()) {
        size_t nl = code.find('\n', start);
        out.push_back(code.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return out;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}

// Text between the first '(' and its matching ')'.
std::string paren_content(const std::string& s) {
    size_t a = s.find('(');
    if (a == std::string::npos) return "";
    int depth = 0;
    for (size_t i = a; i < s.size(); ++i) {
        if (s[i] == '(') ++depth;
        if (s[i] == ')' && --depth == 0) return s.substr(a + 1, i - a - 1);
    }
    return "";
}

// First line after `line` with real code (not empty, not a lone "{").
int next_code_line(const std::vector<std::string>& lines, int line) {
    for (int i = line + 1; i <= (int)lines.size(); ++i) {
        std::string t = trim(lines[i - 1]);
        if (!t.empty() && t != "{") return i;
    }
    return 0;
}

// Sentence for if / while / for / return lines, "" for other lines.
std::string control_fact(const std::vector<std::string>& lines, int line, int next_line, bool same_frame,
                         const std::string& function) {
    if (line <= 0 || line > (int)lines.size()) return "";
    std::string t = trim(lines[line - 1]);
    if (t.rfind("} ", 0) == 0) t = trim(t.substr(1));
    bool entered = same_frame && next_line > 0 && next_line == next_code_line(lines, line);

    if (t.rfind("if", 0) == 0 || t.rfind("else if", 0) == 0) {
        std::string cond = paren_content(t);
        if (cond.empty() || !same_frame) return "";
        return entered ? "checked " + cond + ": true, so the if part runs"
                       : "checked " + cond + ": false, so the if part is skipped";
    }
    if (t.rfind("while", 0) == 0 || t.rfind("for", 0) == 0) {
        std::string head = paren_content(t), cond = head;
        if (t.rfind("for", 0) == 0) {
            size_t a = head.find(';'), b = head.rfind(';');
            if (a != std::string::npos && b != a) cond = trim(head.substr(a + 1, b - a - 1));
        }
        if (cond.empty() || !same_frame) return "";
        return entered ? "checked " + cond + ": true, so the loop body runs"
                       : "checked " + cond + ": false, so the loop ends";
    }
    if (t.rfind("return", 0) == 0) {
        std::string expr = trim(t.substr(6));
        if (!expr.empty() && expr.back() == ';') expr.pop_back();
        if (function == "main") return "main returns" + (expr.empty() ? "" : " " + expr) + ", so the program ends";
        std::string who = function.empty() ? "the function" : function;
        return expr.empty() ? who + " returns" : who + " returns " + expr;
    }
    return "";
}

}  // namespace

std::string describe_step(const ProgramState& before, const ProgramState& after, int line,
                          const std::string& code) {
    std::vector<std::string> facts, gone;
    size_t bd = before.stack.size(), ad = after.stack.size();
    std::vector<std::string> lines = split_lines(code);

    // function calls and returns
    if (ad > bd && ad > 0) {
        facts.push_back("called " + call_text(after, after.stack.back()) + "; now inside " + after.stack.back().function);
    } else if (ad < bd && ad > 0) {
        facts.push_back(before.stack.back().function + " finished; back in " + after.stack.back().function);
    }

    // variables in frames that exist in both snapshots (matched from main upwards)
    size_t common = std::min(bd, ad);
    for (size_t fi = 0; fi < common; ++fi) {
        const Frame& fb = before.stack[fi];
        const Frame& fa = after.stack[fi];
        if (fb.function != fa.function) break;
        bool current = fi + 1 == ad;
        std::string where = current ? "" : " (in " + fa.function + ")";
        for (const Variable& va : fa.vars) {
            const Variable* vb = find(fb.vars, va.name);
            if (!vb) { if (current) facts.push_back(describe_new(after, va)); continue; }
            diff_var(before, after, *vb, va, va.name, where, facts);
        }
        if (current && fi + 1 == bd)
            for (const Variable& vb : fb.vars)
                if (!find(fa.vars, vb.name))
                    gone.push_back(vb.name + " no longer exists (it only lived inside the block that just ended)");
    }

    // heap
    for (size_t i = 0; i < after.heap.size(); ++i) {
        const HeapBlock& h = after.heap[i];
        if (i >= before.heap.size()) {
            facts.push_back("reserved " + std::to_string(h.size) + " bytes on the heap (block " + std::to_string(i + 1) + ")");
            continue;
        }
        const HeapBlock& hb = before.heap[i];
        if (h.freed && !hb.freed) { facts.push_back("freed heap block " + std::to_string(i + 1)); continue; }
        if (h.cells.size() != hb.cells.size()) continue;
        // a write into the block: name it after a pointer to it ("p[0]"), or after the block
        std::string base = "heap block " + std::to_string(i + 1);
        bool named = false;
        for (auto f = after.stack.rbegin(); f != after.stack.rend() && !named; ++f)
            for (const Variable& v : f->vars)
                if (v.is_pointer && v.points_to == h.address) { base = v.name; named = true; break; }
        // a struct node on the heap has its fields as cells: "head->value", "head->next"
        for (size_t c = 0; c < h.cells.size(); ++c) {
            const std::string& cn = h.cells[c].name;
            std::string sub = cn[0] == '[' ? base + cn : (named ? base + "->" + cn : cn + " in " + base);
            diff_var(before, after, hb.cells[c], h.cells[c], sub, "", facts);
        }
    }

    // output
    if (after.output.size() > before.output.size() && after.output.compare(0, before.output.size(), before.output) == 0) {
        std::string printed = after.output.substr(before.output.size());
        while (!printed.empty() && printed.back() == '\n') printed.pop_back();
        for (char& c : printed) if (c == '\n') c = ' ';
        if (printed.size() > 60) printed = printed.substr(0, 57) + "...";
        facts.push_back(printed.empty() ? "printed a line break" : "printed \"" + printed + "\"");
    }

    std::string control = control_fact(lines, line, after.line, ad == bd,
                                       before.stack.empty() ? "" : before.stack.back().function);
    if (!control.empty()) facts.push_back(control);
    facts.insert(facts.end(), gone.begin(), gone.end());

    std::string head = line > 0 ? "Line " + std::to_string(line) + ": " : "";
    if (facts.empty()) {
        if (after.line > 0 && line > 0 && after.line < line && ad == bd)
            return head + "back to line " + std::to_string(after.line) + " (the loop goes round again).";
        return head + "ran; nothing in memory changed.";
    }
    std::string s;
    size_t shown = std::min<size_t>(facts.size(), 3);
    for (size_t i = 0; i < shown; ++i) s += (i ? "; " : "") + facts[i];
    if (facts.size() > shown) s += "; and " + std::to_string(facts.size() - shown) + " more change(s)";
    return head + s + ".";
}
