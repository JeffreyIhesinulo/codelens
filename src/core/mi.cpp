#include "mi.h"

#include <cctype>

const MIValue* MIValue::get(const std::string& key) const {
    for (const auto& kv : items)
        if (kv.first == key) return &kv.second;
    return nullptr;
}

std::string MIValue::text(const std::string& key) const {
    const MIValue* v = get(key);
    return v && v->kind == String ? v->str : "";
}

std::string mi_parse_cstring(const std::string& s, size_t& i) {
    std::string out;
    if (i >= s.size() || s[i] != '"') return out;
    ++i;
    while (i < s.size() && s[i] != '"') {
        char c = s[i++];
        if (c != '\\' || i >= s.size()) { out += c; continue; }
        char e = s[i++];
        switch (e) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            default:
                if (e >= '0' && e <= '7') {   // octal escape, e.g. \303
                    int v = e - '0';
                    for (int k = 0; k < 2 && i < s.size() && s[i] >= '0' && s[i] <= '7'; ++k) v = v * 8 + (s[i++] - '0');
                    out += (char)v;
                } else {
                    out += e;
                }
        }
    }
    if (i < s.size()) ++i;   // closing quote
    return out;
}

static bool parse_value(const std::string& s, size_t& i, MIValue& out);

// key=value[,key=value...] until `end` (or end of string when end == 0)
static bool parse_results(const std::string& s, size_t& i, MIValue& out, char end) {
    while (i < s.size() && s[i] != end) {
        size_t eq = s.find('=', i);
        if (eq == std::string::npos) return false;
        std::string key = s.substr(i, eq - i);
        i = eq + 1;
        MIValue v;
        if (!parse_value(s, i, v)) return false;
        out.items.emplace_back(key, std::move(v));
        if (i < s.size() && s[i] == ',') ++i;
    }
    return true;
}

static bool parse_value(const std::string& s, size_t& i, MIValue& out) {
    if (i >= s.size()) return false;
    char c = s[i];
    if (c == '"') {
        out.kind = MIValue::String;
        out.str = mi_parse_cstring(s, i);
        return true;
    }
    if (c == '{') {
        out.kind = MIValue::Tuple;
        ++i;
        if (!parse_results(s, i, out, '}')) return false;
        if (i < s.size()) ++i;
        return true;
    }
    if (c == '[') {
        out.kind = MIValue::List;
        ++i;
        while (i < s.size() && s[i] != ']') {
            char d = s[i];
            if (d == '"' || d == '{' || d == '[') {          // list of values
                MIValue v;
                if (!parse_value(s, i, v)) return false;
                out.items.emplace_back("", std::move(v));
            } else {                                          // list of results: frame={...}
                size_t eq = s.find('=', i);
                if (eq == std::string::npos) return false;
                std::string key = s.substr(i, eq - i);
                i = eq + 1;
                MIValue v;
                if (!parse_value(s, i, v)) return false;
                out.items.emplace_back(key, std::move(v));
            }
            if (i < s.size() && s[i] == ',') ++i;
        }
        if (i < s.size()) ++i;
        return true;
    }
    return false;
}

bool mi_parse_line(const std::string& raw, MIRecord& out) {
    out = MIRecord{};
    std::string line = raw;
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
    if (line.rfind("(gdb)", 0) == 0) { out.type = '('; return true; }

    size_t i = 0;
    while (i < line.size() && std::isdigit((unsigned char)line[i])) ++i;
    if (i > 0) out.token = std::stoi(line.substr(0, i));
    if (i >= line.size()) return false;

    char t = line[i];
    if (t == '~' || t == '@' || t == '&') {
        out.type = t;
        ++i;
        out.stream = mi_parse_cstring(line, i);
        return true;
    }
    if (t != '^' && t != '*' && t != '=' && t != '+') return false;
    out.type = t;
    ++i;
    size_t comma = line.find(',', i);
    out.cls = line.substr(i, comma == std::string::npos ? std::string::npos : comma - i);
    out.results.kind = MIValue::Tuple;
    if (comma == std::string::npos) return true;
    i = comma + 1;
    return parse_results(line, i, out.results, 0);
}
