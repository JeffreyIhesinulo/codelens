// Beginner-friendly compiler messages and glossary entries.
// Patterns were collected from gcc 13 output for the most common beginner mistakes.
#include "app.h"

#include <map>
#include <regex>

namespace {

std::string upper_first(std::string s) {
    if (!s.empty()) s[0] = (char)std::toupper((unsigned char)s[0]);
    return s;
}

// Header a standard function comes from, "" if not a common one.
std::string header_for(const std::string& fn) {
    static const std::map<std::string, std::string> h = {
        {"printf", "stdio.h"}, {"scanf", "stdio.h"}, {"puts", "stdio.h"}, {"putchar", "stdio.h"},
        {"getchar", "stdio.h"}, {"fgets", "stdio.h"}, {"fprintf", "stdio.h"}, {"sprintf", "stdio.h"},
        {"malloc", "stdlib.h"}, {"calloc", "stdlib.h"}, {"realloc", "stdlib.h"}, {"free", "stdlib.h"},
        {"exit", "stdlib.h"}, {"abs", "stdlib.h"}, {"rand", "stdlib.h"}, {"srand", "stdlib.h"}, {"atoi", "stdlib.h"},
        {"strlen", "string.h"}, {"strcpy", "string.h"}, {"strcmp", "string.h"}, {"strcat", "string.h"},
        {"memset", "string.h"}, {"memcpy", "string.h"},
        {"sqrt", "math.h"}, {"pow", "math.h"}, {"fabs", "math.h"},
        {"isdigit", "ctype.h"}, {"isalpha", "ctype.h"}, {"toupper", "ctype.h"}, {"tolower", "ctype.h"},
    };
    auto it = h.find(fn);
    return it == h.end() ? "" : it->second;
}

std::string format_meaning(const std::string& f) {
    static const std::map<std::string, std::string> m = {
        {"d", "whole numbers (int)"}, {"i", "whole numbers (int)"}, {"u", "unsigned whole numbers"},
        {"ld", "long whole numbers (long)"}, {"f", "decimal numbers (double)"}, {"lf", "decimal numbers (double)"},
        {"c", "single characters (char)"}, {"s", "text (char *)"}, {"p", "addresses (pointers)"},
        {"zu", "sizes (size_t)"}, {"x", "whole numbers in hex"},
    };
    auto it = m.find(f);
    return it == m.end() ? "a different type" : it->second;
}

std::string format_for_type(const std::string& t) {
    if (t == "int") return "%d";
    if (t == "double" || t == "float") return "%f";
    if (t == "char") return "%c";
    if (t == "long int") return "%ld";
    if (t.find("char *") != std::string::npos) return "%s";
    if (t.find('*') != std::string::npos) return "%p";
    if (t == "long unsigned int") return "%zu";
    return "";
}

// Explains one diagnostic. Returns false for messages a beginner does not need to see.
// at_line_start: gcc's column points at the first token of the line (so a missing ';' belongs
// to the line before), rather than at the end of the line that lacks it.
bool explain(const std::string& kind, std::string msg, int line, bool at_line_start, FriendlyError& out) {
    std::smatch m;
    out.line = line;
    out.warning = kind == "warning";

    // Clang (Apple's compiler) describes the same mistakes with different words.
    if (std::regex_search(msg, m, std::regex(R"(use of undeclared identifier '(\w+)')"))) msg = "'" + m[1].str() + "' undeclared";
    if (std::regex_search(msg, m, std::regex(R"(call to undeclared (?:library )?function '(\w+)')"))) {
        std::string fn = m[1];
        msg = "implicit declaration of function '" + fn + "'";
        if (fn == "printff") msg += "; did you mean 'printf'";
    }
    if (msg.find("expected ';' at end") != std::string::npos || msg.find("expected ';' after") != std::string::npos) msg = "expected ';' before";
    if (msg == "expected '}'") msg = "expected declaration or statement at end of input";
    if (msg.find("using the result of an assignment as a condition") != std::string::npos) msg = "suggest parentheses around assignment used as truth value";
    if (std::regex_search(msg, m, std::regex(R"(format specifies type '([^']+)' but the argument has type '([^']+)')"))) {
        std::string wanted=m[1], got=m[2], fmt=format_for_type(wanted);
        if(wanted == "int *") fmt="%d";
        msg="format '"+fmt+"' expects argument of type '"+wanted+"', but argument 2 has type '"+got+"'";
    }
    if (msg.find("missing terminating '\"' character") != std::string::npos) {
        out.warning = false;
        out.message = "A piece of text on line " + std::to_string(line) + " is missing its closing quote \".";
        out.hint = "Text goes between two double quotes: \"hello\".";
        return true;
    }
    if (msg == "expected expression") msg = "expected expression before ' }' token";
    // ---- errors
    if (std::regex_search(msg, std::regex(R"(expected (?:',' or )?';'(?: or ',')? before)"))) {
        int at = at_line_start && line > 1 ? line - 1 : line;
        out.message = "A semicolon (;) is missing at the end of line " + std::to_string(at) + ".";
        out.hint = "In C every statement ends with a semicolon, for example: int x = 5;";
        out.line = at;
        return true;
    }
    if (std::regex_search(msg, m, std::regex(R"('(\w+)' undeclared)"))) {
        out.message = upper_first(m[1].str()) + " is used on line " + std::to_string(line) + " but it was never created.";
        out.hint = "Create a variable before using it, for example: int " + m[1].str() +
                   " = 0;  If it already exists, check the spelling (C cares about capital letters).";
        return true;
    }
    if (std::regex_search(msg, m, std::regex(R"(implicit declaration of function '(\w+)'(?:; did you mean '(\w+)')?)"))) {
        std::string fn = m[1], suggestion = m[2], header = header_for(fn);
        out.warning = false;   // gcc 14 makes this an error; treat it as one either way
        if (!suggestion.empty()) {
            out.message = fn + " is not a known function. Did you mean " + suggestion + "?";
            out.hint = "Check the spelling of the function name.";
        } else if (!header.empty()) {
            out.message = "To use " + fn + " you need #include <" + header + "> at the top of the program.";
            out.hint = "#include lines tell the compiler where functions like " + fn + " come from.";
        } else {
            out.message = fn + " is not a known function.";
            out.hint = "Check the spelling, or write the function above main so the compiler knows it.";
        }
        return true;
    }
    if (msg.find("incompatible implicit declaration of built-in function") != std::string::npos) return false;
    if (msg.find("expected declaration or statement at end of input") != std::string::npos) {
        out.message = "A closing brace } is missing at the end of the program.";
        out.hint = "Every { needs a matching }. Count them in main and in every if, for or while.";
        return true;
    }
    if (std::regex_search(msg, m, std::regex(R"(expected '([)\]}])' before)"))) {
        out.message = "A closing " + m[1].str() + " is missing on line " + std::to_string(line) + ".";
        out.hint = "Brackets come in pairs: ( ), [ ], { }.";
        return true;
    }
    if (std::regex_search(msg, m, std::regex(R"(expected expression before '(.+)' token)"))) {
        out.message = "Something is missing on line " + std::to_string(line) + " before " + m[1].str() +
                      ": a value or a variable should be there.";
        out.hint = "For example, int x = ; is missing the value: int x = 5;";
        return true;
    }
    if (msg.find("missing terminating \" character") != std::string::npos) {
        if (out.warning) return false;   // gcc prints it twice, once as a warning
        out.message = "A piece of text on line " + std::to_string(line) + " is missing its closing quote \".";
        out.hint = "Text goes between two double quotes: \"hello\".";
        return true;
    }
    if (msg.find("missing terminating ' character") != std::string::npos ||
        msg.find("character constant too long") != std::string::npos ||
        msg.find("multi-character character constant") != std::string::npos) {
        if (msg.find("missing terminating") != std::string::npos && out.warning) return false;
        out.warning = false;
        out.message = "Single quotes ' ' hold exactly one character, but line " + std::to_string(line) + " has more.";
        out.hint = "Use double quotes for text: \"hello\". Single quotes are for one character: 'h'.";
        return true;
    }
    if (std::regex_search(msg, m, std::regex(R"((?:redefinition|redeclaration) of '(\w+)')"))) {
        out.message = m[1].str() + " is created twice.";
        out.hint = "Create a variable once (int " + m[1].str() + " = 1;) and later just change it (" +
                   m[1].str() + " = 2;).";
        return true;
    }
    if (std::regex_search(msg, m, std::regex(R"(too (few|many) arguments to function '(\w+)')"))) {
        out.message = "The call to " + m[2].str() + " on line " + std::to_string(line) + " passes too " +
                      m[1].str() + " values.";
        out.hint = "Give " + m[2].str() + " exactly as many values as it lists in its definition.";
        return true;
    }
    if (msg.find("stray '\\") != std::string::npos) {
        out.message = "Line " + std::to_string(line) + " contains a character C does not understand, often a curly quote copied from a website.";
        out.hint = "Retype the quotes yourself: \" and ' (straight, not “ ” or ‘ ’).";
        return true;
    }
    if (msg.find("lvalue required as left operand of assignment") != std::string::npos) {
        out.message = "On line " + std::to_string(line) + " the left side of = is not a variable.";
        out.hint = "= puts a value into a variable: x = 5; To compare two values use ==.";
        return true;
    }
    if (msg.find("makes pointer from integer") != std::string::npos ||
        msg.find("makes integer from pointer") != std::string::npos) {
        out.message = "Line " + std::to_string(line) + " mixes a number and an address (pointer).";
        out.hint = "To store the address of x in a pointer write &x, for example: int *p = &x;";
        return true;
    }
    if (std::regex_search(msg, m, std::regex(R"(undefined reference to `(\w+)')"))) {
        if (m[1] == "main") {
            out.message = "The program has no main function.";
            out.hint = "Every C program starts in main. Check the spelling: int main(void) { ... }";
        } else {
            out.message = m[1].str() + " is used but never written.";
            out.hint = "Check the spelling, or write the function " + m[1].str() + ".";
        }
        out.warning = false;
        return true;
    }

    // ---- warnings beginners need
    if (std::regex_search(msg, m, std::regex(R"(format '%(\w+)' expects argument of type '([^']+) \*', but argument \d+ has type '([^']+)')"))
        && m[3].str().find('*') == std::string::npos) {
        out.message = "scanf needs the address of the variable on line " + std::to_string(line) + ".";
        out.hint = "Put & before the variable: scanf(\"%" + m[1].str() + "\", &x);";
        return true;
    }
    if (std::regex_search(msg, m, std::regex(R"(format '%(\w+)' expects argument of type '([^']+)', but argument \d+ has type '([^']+)')"))) {
        std::string want = format_for_type(m[3]);
        out.message = "On line " + std::to_string(line) + ", %" + m[1].str() + " is for " + format_meaning(m[1]) +
                      " but the value is a " + m[3].str() + ".";
        out.hint = want.empty() ? "Use the format that matches the value's type."
                                : "Use " + want + " for a " + m[3].str() + ".";
        return true;
    }
    if (msg.find("suggest parentheses around assignment used as truth value") != std::string::npos) {
        out.message = "Line " + std::to_string(line) + " uses = inside a condition.";
        out.hint = "= puts a value into a variable. To compare, use == : if (x == 5)";
        return true;
    }
    if (msg.find("control reaches end of non-void function") != std::string::npos) {
        out.message = "A function can end without returning a value (line " + std::to_string(line) + ").";
        out.hint = "Make sure every path through the function ends with return ...;";
        return true;
    }
    if (std::regex_search(msg, m, std::regex(R"('(\w+)' (?:is|may be) used uninitialized)"))) {
        out.message = m[1].str() + " is used on line " + std::to_string(line) + " before it gets a value.";
        out.hint = "Give it a value first, for example: int " + m[1].str() + " = 0;";
        return true;
    }
    if (out.warning) return false;   // other warnings: not for beginners

    // ---- any other error: the compiler's words, cleaned up
    out.message = "Line " + std::to_string(line) + ": " + msg;
    out.hint = "";
    return true;
}

}  // namespace

std::vector<FriendlyError> explain_compile_errors(const std::string& output) {
    std::vector<FriendlyError> errors, warnings;
    static const std::regex diag(R"((?:^|\n)[^\n]*?\.(?:c|cpp|cc):(\d+):(?:(\d+):)? (fatal error|error|warning): ([^\n]*))");
    // gcc prints the source line under a diagnostic: "    5 |     printf(...)"
    static const std::regex snippet(R"(^\n?\s*\d+ \| ([^\n]*))");
    static const std::regex linker(R"(undefined reference to `\w+')");

    auto add = [&](const FriendlyError& e) {
        std::vector<FriendlyError>& list = e.warning ? warnings : errors;
        for (const FriendlyError& x : list)
            if (x.line == e.line && x.message == e.message) return;
        list.push_back(e);
    };
    for (std::sregex_iterator it(output.begin(), output.end(), diag), end; it != end; ++it) {
        std::string msg = (*it)[4];
        size_t opt = msg.rfind(" [-W");                 // drop " [-Wformat=]"
        if (opt != std::string::npos) msg.erase(opt);
        int col = (*it)[2].matched ? std::stoi((*it)[2]) : 0;
        bool at_line_start = false;
        std::smatch sm;
        std::string rest = output.substr((size_t)(it->position() + it->length()));
        if (col > 0 && std::regex_search(rest, sm, snippet)) {
            std::string src = sm[1];
            size_t first = src.find_first_not_of(" \t");
            at_line_start = first != std::string::npos && (size_t)col <= first + 1;
        }
        FriendlyError e;
        if (explain((*it)[3] == "warning" ? "warning" : "error", msg, std::stoi((*it)[1]), at_line_start, e)) add(e);
    }
    for (std::sregex_iterator it(output.begin(), output.end(), linker), end; it != end; ++it) {
        FriendlyError e;
        if (explain("error", it->str(), 0, false, e)) add(e);
    }
    if(output.find("\"_main\", referenced from:") != std::string::npos) {
        FriendlyError e; explain("error", "undefined reference to `main'", 0, false, e); add(e);
    }
    errors.insert(errors.end(), warnings.begin(), warnings.end());
    return errors;
}

std::string glossary_lookup(const std::string& token) {
    static const std::map<std::string, std::string> g = {
        {"int", "A whole number, like 7 or -3."},
        {"double", "A number with a decimal point, like 2.5."},
        {"float", "A smaller, less precise decimal number. Prefer double."},
        {"char", "A single character, like 'A' or '7'. Also used for text as an array of chars."},
        {"void", "\"Nothing\": a function that returns no value, or takes no values: int main(void)."},
        {"bool", "true or false (needs #include <stdbool.h> in C)."},
        {"main", "Where every C program starts running."},
        {"printf", "Prints text on the screen. %d, %f, %c and %s are replaced by the values after the text."},
        {"scanf", "Reads input typed by the user into variables. Needs the variable's address: &x."},
        {"puts", "Prints a line of text and moves to the next line."},
        {"#include", "Brings in ready-made functions, for example #include <stdio.h> for printf."},
        {"return", "Ends the function and gives a value back. return 0 in main means \"finished without errors\"."},
        {"if", "Runs the next block only when the condition is true."},
        {"else", "Runs when the if condition was false."},
        {"for", "Repeats a block: for (start; condition; step)."},
        {"while", "Repeats a block as long as the condition is true."},
        {"do", "Like while, but runs the block at least once before checking."},
        {"break", "Leaves the loop (or switch) right away."},
        {"continue", "Skips the rest of this loop round and starts the next one."},
        {"switch", "Chooses one of several cases depending on a value."},
        {"const", "This value cannot be changed after it is set."},
        {"struct", "Groups several values under one name, like a student with an id and a grade."},
        {"sizeof", "How many bytes a type or variable takes in memory."},
        {"NULL", "A pointer that points to nothing. Using *p when p is NULL crashes the program."},
        {"malloc", "Reserves memory on the heap and gives back its address. Free it later with free."},
        {"free", "Gives memory reserved with malloc back. Do not use it after freeing."},
        {"strlen", "The number of characters in a text, not counting the final '\\0'."},
        {";", "Ends a statement. Almost every line of C code ends with one."},
        {"{", "Starts a block of code that belongs together. Closed by }."},
        {"}", "Ends a block of code started by {."},
        {"=", "Puts a value into a variable: x = 5."},
        {"==", "Checks whether two values are equal: if (x == 5)."},
        {"!=", "Checks whether two values are different."},
        {"&&", "\"and\": both conditions must be true."},
        {"||", "\"or\": at least one condition must be true."},
        {"!", "\"not\": turns true into false and false into true."},
        {"++", "Adds 1 to a variable: i++ is the same as i = i + 1."},
        {"--", "Subtracts 1 from a variable."},
        {"%", "The remainder after dividing: 7 % 3 is 1. In printf, % starts a placeholder like %d."},
        {"&", "\"Address of\": &x is where x lives in memory."},
        {"*", "In int *p: p holds an address. In *p: the value at that address. Between numbers: multiply."},
        {"->", "Gets a field through a pointer to a struct: ps->grade is (*ps).grade."},
        {"[]", "Picks one box of an array by its number: a[0] is the first box."},
        {"\\n", "A new line in text."},
        {"%d", "Placeholder in printf/scanf for a whole number (int)."},
        {"%f", "Placeholder in printf for a decimal number (double)."},
        {"%c", "Placeholder for a single character (char)."},
        {"%s", "Placeholder for text (char *)."},
        {"%p", "Placeholder for an address (pointer)."},
    };
    auto it = g.find(token);
    return it == g.end() ? "" : it->second;
}
