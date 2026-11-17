# AGENTS.md — instructions for AI coding agents

Read this file before changing anything. Then read [docs/PLAN.md](docs/PLAN.md) and the plan
for the person you work for: [docs/plan-ihor.md](docs/plan-ihor.md) (UI) or
[docs/plan-jeffrey.md](docs/plan-jeffrey.md) (engine and lessons).

## Project

CodeLens: a desktop app (C++17 + raylib) that teaches C to **people who have never programmed**.
Each topic goes Explore (interactive memory picture) → Understand (watch code run line by line,
predict, fix a bug, explain) → Practice (write code that passes tests, with an AI tutor).

## Ownership — do not cross it

| Path | Owner | Branch |
|---|---|---|
| `src/core/`, `lessons/`, `tests/` | Dzheffriei (engine, content) | `dzheffriei` |
| `src/ui/`, `assets/` | Ihor (interface) | `ihor` |
| `src/core/app.h` | **shared contract** — changed only in a separate PR both people approve | — |
| `docs/`, `AGENTS.md`, `README.md`, `CMakeLists.txt` | both; keep changes small and say why in the PR | — |

- The UI calls the engine **only** through `src/core/app.h`. The engine never includes raylib.
- If you need something the engine does not provide: do not edit `src/core`. Write a placeholder
  in `src/ui` and describe the missing function in the PR description.
- Work on your owner's branch. Merge into `main` only through a pull request.
  After any merge into `main`: `git pull origin main` into the working branch.

## Build and test

```
sudo apt install build-essential cmake gdb libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev
cmake -B build && cmake --build build -j
./build/tests                         # must print "N passed, 0 failed" before any PR
./build/codelens                      # the app
./build/codelens --memory-preview     # visualizer on tests/fixtures
./build/make_fixtures tests/fixtures  # regenerate memory snapshots
```

## Language

**Everything is in English**: UI text, lessons, AI tutor prompts and answers, error messages,
code comments, commit messages, PR descriptions and docs. The team may discuss in other
languages, but nothing in the repository or the app is in another language.

## Design rules (the audience is a complete beginner)

- One instruction and one main button visible at any time ("what do I do now?").
- Never show something before the lesson has explained it: no hex addresses before the pointers
  topic, no heap before dynamic memory (`ShowLevel`).
- Plain English, short sentences; no raw compiler output as the first thing the student sees.
- Do not add features outside the plan without asking the person you work for.

### Checklist for every student-facing screen
A screen is not done until all of these are true:
1. **Goal**: a sentence at the top says what to do ("Move the slider and watch which box p points to").
2. **Terms explained**: every term on screen is either already taught or explained in one line
   right there ("An array is several boxes in a row with one name; numbering starts at 0").
   Never show bare words like Stack, Heap, Snapshot, Fixture, `int [5]` or hex addresses to a beginner.
3. **Purpose of every control**: each slider, button or text field says what it changes and what
   to look at. If it has no purpose for the student, remove it (example: a "Notes" box).
4. **What just happened**: one sentence after every action ("p now points to a[4], which holds 50").
5. **Code connection**: the code that matches the picture is visible, with the relevant line highlighted.
6. **No empty space**: hide empty columns; centre small content; size panels to their content.
7. **Help is one click away**: an "Ask the tutor" button on every screen; the question is sent
   with the current step, code and state as context.
8. **One main button** (Next / Check / Run) in the same place on every screen.

`--memory-preview` is a **developer tool** for checking the visualizer on fixtures. It does not
need to follow this checklist and is never shown to students. Student screens are built in the
guided lesson screen.

## Plan change log

Newest first. When the plan changes, add an entry here and update `docs/`.

### 2026-11-17 (update) — screen checklist
Dzheffriei reviewed the `--memory-preview` window as if he were a beginner: lots of empty space,
unexplained words (Array, Stack, Heap, Snapshot), a slider with no instruction, a Notes box with no
purpose, and no way to ask the AI. Added the **Checklist for every student-facing screen** above.
`--memory-preview` stays a developer tool. The first Explore screen to build is the array scene
described in docs/plan-ihor.md ("Reference screen").

### 2026-11-17 — v3: beginner-first and guided UI
Reason: the app was still hard for someone with no programming knowledge; opening a lesson
showed theory, an editor and an AI panel at once, and it was unclear where to learn what the
task asks for.

What changes:
- **Lesson screen** → a guided sequence of steps, one at a time, with a "what to do now" bar and
  one main button. The old three-panel screen is now only the Practice step. (Ihor)
- **Welcome screen** on first launch and the **course map as a path** with Continue. (Ihor)
- **`draw_memory` levels** `Values / Frames / Addresses / Heap`, chosen by `@show` in the lesson. (both)
- **Step narration**: the engine writes one sentence per debugger step into `ProgramState.note`;
  the UI shows it under the visualizer. (Dzheffriei → Ihor)
- New step kinds **order** and **choose**; **locked template lines** in the editor (`@editable`). (both)
- **Friendly compiler errors** and **glossary tooltips**. (Dzheffriei: data; Ihor: display)
- Explore scenes also for the first topics (`variables`, `condition`).
- Topics are now: output, variables, conditions, loops, functions, arrays, pointers, dynamic memory*, recursion* (*if time).
- A **beginner test** with a real person in week 4.

Proposed `app.h` additions (arrive with Dzheffriei's week-1 PR; until then use placeholders):
`ShowLevel`, `StepKind::Order/Choose`, `Step::lines/options/correct/edit_from/edit_to`,
`order_shuffled`, `check_order`, `FriendlyError` + `RunResult::friendly`, `glossary_lookup`,
`Scene::code`. Exact signatures: [docs/plan-ihor.md](docs/plan-ihor.md#interfaces-you-will-use).

Next for Ihor: `draw_memory` levels → welcome screen → guided lesson screen skeleton on
`lessons/05_pointers.lesson` (see docs/plan-ihor.md, week 1).

### 2026-11-13 — v2: Explore → Understand → Practice
The lecturer said a "task + tests" app is a LeetCode copy and does not teach enough. Every topic
got three stages. Engine: GDB debugger, lesson steps, computed Predict answers, fixtures.
UI: memory visualizer (done in `memview.cpp`), widgets.

### 2026-11-10 — v1: MVP
Lessons, code editor, Run/Check with sanitizers and tests, AI tutor through Ollama.
