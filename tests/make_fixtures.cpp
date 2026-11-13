// Generates tests/fixtures/*.json: real ProgramState snapshots taken with the debugger.
// The UI uses them to build the memory visualizer before wiring it to a live session.
// Run: ./build/make_fixtures tests/fixtures
#include "app.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

struct Fixture {
    const char* name;
    const char* about;
    const char* code;
    const char* input;
    int stop_line;        // run to this line (0 = run to the end)
    int extra_nexts;      // then this many "next"s
    int steps_into = 0;   // "step" this many times after reaching stop_line
};

static const Fixture FIXTURES[] = {
    {"swap", "Inside swap(): a and b point at main's x and y.",
     "#include <stdio.h>\n"
     "void swap(int *a, int *b) {\n"
     "    int t = *a;\n"
     "    *a = *b;\n"
     "    *b = t;\n"
     "}\n"
     "int main(void) {\n"
     "    int x = 3, y = 5;\n"
     "    swap(&x, &y);\n"
     "    printf(\"%d %d\\n\", x, y);\n"
     "    return 0;\n"
     "}\n", "", 9, 2, 1},

    {"array", "An int array and a pointer to one of its elements.",
     "int main(void) {\n"
     "    int a[5] = {10, 20, 30, 40, 50};\n"
     "    int *p = &a[2];\n"
     "    *p = 99;\n"
     "    return 0;\n"
     "}\n", "", 5, 0},

    {"struct", "A struct with a char array field, a pointer to it, and a string.",
     "#include <string.h>\n"
     "struct student { int id; double grade; char name[8]; };\n"
     "int main(void) {\n"
     "    struct student s = {42, 3.5, \"Ann\"};\n"
     "    struct student *ps = &s;\n"
     "    char word[6] = \"hello\";\n"
     "    ps->grade = 3.9;\n"
     "    return 0;\n"
     "}\n", "", 8, 0},

    {"heap", "A live heap block (int[3]) and a freed block with a dangling pointer.",
     "#include <stdlib.h>\n"
     "int main(void) {\n"
     "    int *nums = malloc(3 * sizeof(int));\n"
     "    nums[0] = 1; nums[1] = 2; nums[2] = 3;\n"
     "    int *old = malloc(4 * sizeof(int));\n"
     "    free(old);\n"
     "    free(nums);\n"
     "    return 0;\n"
     "}\n", "", 7, 0},

    {"leak", "The program finished but one heap block was never freed.",
     "#include <stdlib.h>\n"
     "int main(void) {\n"
     "    int *p = malloc(10 * sizeof(int));\n"
     "    p = NULL;\n"
     "    return 0;\n"
     "}\n", "", 0, 0},

    {"recursion", "fact(4) at its deepest call: five frames on the stack.",
     "#include <stdio.h>\n"
     "int fact(int n) {\n"
     "    if (n <= 1) return 1;\n"
     "    return n * fact(n - 1);\n"
     "}\n"
     "int main(void) {\n"
     "    printf(\"%d\\n\", fact(4));\n"
     "    return 0;\n"
     "}\n", "", 3, 0},

    {"null_crash", "Writing through a NULL pointer: segmentation fault on line 4.",
     "#include <stddef.h>\n"
     "int main(void) {\n"
     "    int *p = NULL;\n"
     "    *p = 5;\n"
     "    return 0;\n"
     "}\n", "", 4, 1},
};

int main(int argc, char** argv) {
    std::string dir = argc > 1 ? argv[1] : "tests/fixtures";
    std::ofstream readme(dir + "/README.md");
    readme << "# ProgramState fixtures\n\n"
              "Real snapshots taken with the CodeLens debugger (`./build/make_fixtures " << dir << "`).\n"
              "Load one with `state_from_json` to build the memory visualizer without a live session.\n"
              "Addresses are hex strings. `stack[0]` is `main`.\n\n";
    int failures = 0;
    for (const Fixture& f : FIXTURES) {
        std::string err;
        if (!debug_start(f.code, Lang::C, f.input, err)) {
            std::fprintf(stderr, "%s: %s\n", f.name, err.c_str());
            ++failures;
            continue;
        }
        if (f.stop_line) {
            if (f.name == std::string("recursion"))
                for (int i = 0; i < 4; ++i) debug_run_to_line(f.stop_line);   // deepest: n == 1
            else
                debug_run_to_line(f.stop_line);
            for (int i = 0; i < f.steps_into; ++i) debug_step();
            for (int i = 0; i < f.extra_nexts; ++i) debug_next();
        } else {
            while (debug_active()) debug_next();
        }
        ProgramState s = debug_state();
        s.note = f.about;
        std::ofstream(dir + "/" + f.name + ".json") << state_to_json(s) << "\n";
        debug_stop();

        readme << "## " << f.name << ".json\n\n" << f.about << "\n\n```c\n" << f.code << "```\n\n";
        std::printf("%-11s line %-2d frames %zu heap %zu errors %zu\n", f.name, s.line,
                    s.stack.size(), s.heap.size(), s.errors.size());
    }
    return failures ? 1 : 0;
}
