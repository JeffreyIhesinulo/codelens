# CodeLens

An AI-assisted interactive app for learning C and C++, written in C++ with raylib.

Students read a short lesson, write code in the built-in editor, run it, and check it
against tests. An AI tutor (a local LLM through Ollama) explains compiler errors,
gives hints without revealing the solution, and answers questions about the code.
Every program is built with AddressSanitizer and UBSan, so memory bugs are reported
with the exact line instead of silently producing garbage.

## Features (MVP)

- Course screen with progress
- Lesson screen: theory with code samples, code editor, console, AI tutor
- Code editor: syntax highlighting, line numbers, auto-indent, auto-closing braces, paste
- **Run** compiles and runs the code (3 s time limit) using the first test's input
- **Check** runs all tests, including hidden ones, and explains the first failure
- Compile errors and memory errors are highlighted in the editor
- AI tutor: *Explain error*, *Hint* (3 levels), free questions; works offline too (AI just shows as offline)
- Drafts and progress are saved automatically

## Requirements (Linux)

```
sudo apt install build-essential cmake gdb \
    libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev
```

For the AI tutor, install [Ollama](https://ollama.com) and pull a small code model:

```
ollama pull qwen2.5-coder:3b
ollama serve            # if it is not already running as a service
```

Use another model with `CODELENS_MODEL=llama3.2:3b ./build/codelens`.

## Build and run

```
cmake -B build
cmake --build build -j
./build/codelens
./build/tests           # engine unit tests
```

Engine and tests only (no raylib): `cmake -B build -DBUILD_APP=OFF`.

Shortcuts: `Ctrl+R` run, `Ctrl+Enter` check, `Esc` back to lessons, `Shift+wheel` horizontal scroll.

## Project structure

```
src/core/     engine — no raylib (Jeffrey)
  app.h         shared interface between engine and UI
  runner.cpp    compile + run in a child process, time limit, sanitizer parsing
  checker.cpp   runs lesson tests
  lessons.cpp   lesson file parser, progress
  ai.cpp        Ollama client, background requests, prompts
src/ui/       raylib interface (Igor)
  main.cpp, ui.h, widgets.cpp     window, theme, fonts, buttons, panels, text input
  editor.cpp                       code editor
  course_screen.cpp                lesson list
  lesson_screen.cpp                theory | editor + console | AI tutor
lessons/      *.lesson files
tests/        engine unit tests
assets/fonts/ DejaVu fonts (free license, see LICENSE-DejaVu.txt)
```

The UI only calls functions from `src/core/app.h`. Changing `app.h` is a separate PR that both of us approve.

## Lesson format

Lessons are plain text so code can be written without escaping:

```
@id     03_loops
@title  Loops
@topic  for loops
@lang   c            # or cpp

@theory
Text. Lines indented by 4 spaces are shown as code.

@starter
...code the student starts with...

@solution
...reference solution (never shown; unit tests check that it passes)...

@test
@input
5
@expected
15

@hidden_test
@input
0
@expected
0
```

`./build/tests` verifies that every lesson's solution passes its tests and the starter code does not.

## Workflow

- `main` always builds.
- One branch per task: `core/...`, `ui/...`, `lesson/...`.
- Merge to `main` only through a pull request reviewed by the other person.

## Roadmap

- Step-by-step debugger (GDB/MI) and the memory visualizer: stack, heap, pointer arrows
- AI code review after a lesson is completed
- AI-generated practice tasks, verified by compiling a reference solution before showing them
- Lessons 6–12 (strings, structs, dynamic memory, recursion, C++ classes, STL)
- Editor: mouse selection, copy selection, undo
