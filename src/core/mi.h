#pragma once
// Parser for GDB's machine interface (MI) output, e.g.
//   12^done,stack=[frame={level="0",func="main",file="main.c",line="5"}]
//   *stopped,reason="end-stepping-range",frame={...}
//   ~"text printed by gdb\n"
#include <string>
#include <utility>
#include <vector>

struct MIValue {
    enum Kind { String, Tuple, List } kind = String;
    std::string str;                                      // String
    std::vector<std::pair<std::string, MIValue>> items;   // Tuple and List (list items have key "" unless named)

    const MIValue* get(const std::string& key) const;     // nullptr if missing
    std::string    text(const std::string& key) const;    // "" if missing or not a string
};

struct MIRecord {
    char type = 0;        // '^' result, '*' exec async, '=' notify, '+' status, '~' console, '@' target, '&' log, '(' prompt
    int token = -1;
    std::string cls;      // "done", "running", "error", "stopped", ...
    MIValue results;      // Tuple of key=value
    std::string stream;   // for '~', '@', '&'
};

// Parses one line of MI output. Returns false for lines that are not MI (e.g. program output).
bool mi_parse_line(const std::string& line, MIRecord& out);
// Parses a C string literal starting at s[i] == '"'; advances i past the closing quote.
std::string mi_parse_cstring(const std::string& s, size_t& i);
