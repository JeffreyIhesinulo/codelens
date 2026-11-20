# Plan v3 — Dzheffriei (engine and content, `src/core`, `lessons/`)

Read [PLAN.md](PLAN.md) first: the app is for people who have **never programmed**.

## What changed for you (17 Nov 2026)

1. The debugger must explain every step in **one plain-English sentence** (`ProgramState.note`).
2. Lessons control how much of memory is shown (`@show`) and which lines are editable.
3. Two new step kinds for beginners: **order** and **choose**.
4. **Friendly compiler errors** from a fixed table, before any AI.
5. Explore scenes start with the **first topics** (`variables`, `condition`), not only pointers.
6. Two new topics (**Variables**, **Functions**); lessons 1–3 are rewritten for beginners.
7. A **beginner test** session in week 4.
8. **The local AI tutor is the main feature** (17 Nov, later the same day): every AI request is
   grounded in engine facts, the AI appears in every stage, AI-generated tasks are verified by the
   compiler, and the model choice is backed by a quality check. See PLAN.md, section D.

## Tasks

### Week 1 (16–20 Nov) — one PR that changes `app.h`
- **Step narration**: `describe_step(before, after, code)` fills `ProgramState.note` after every
  `debug_next/step/finish`. Sentences, in this priority:
  - error ("Line 4: crashed — p is NULL, so *p points nowhere")
  - entered or left a function ("Entered swap(a, b)", "Returned to main")
  - created a variable ("Created x and put 3 in it")
  - changed a value ("x changed from 3 to 10")
  - printed something ("Printed: 5 3")
  - heap ("Reserved 12 bytes on the heap (block 1)", "Freed block 1")
  - otherwise "Line 6 ran".
  Uninitialised variables (not yet assigned) are hidden from the snapshot until their line runs.
- **`@show values|frames|addresses|heap`** at lesson and step level → `ShowLevel`.
- **Lesson format additions**: `@step order` (`@code` = the correct program, split into lines),
  `@step choose` (`@option` × n, `@correct N`, `@text`), `@editable A-B` for
  Modify/FindBug/Practice. Add `order_shuffled`, `check_order`.
- **Friendly errors**: `explain_compile_errors(output)` → `FriendlyError{line, message, hint}` for
  the 12–15 most common beginner errors (missing `;`, undeclared name, missing `}`, missing
  `#include <stdio.h>`, `printf` format mismatch, `=` in a condition, missing `return`,
  wrong argument count, string in single quotes, ...). `RunResult.friendly`.
- **`glossary_lookup(token)`**: ~40 entries (`int`, `char`, `printf`, `scanf`, `;`, `{`, `&`, `*`,
  `return`, `if`, `for`, `while`, `NULL`, `malloc`, `free`, ...).
- Tests for all of it. Update `AGENTS.md` change log when the PR lands.

### Week 2 (23–27 Nov)
- **Scene engine**: generic `Scene` (params, actions, code, highlighted line) → `ProgramState`.
- Scenes **`variables`** (create a box, put a value, copy a into b, `x = x + 1`) and **`condition`**
  (slider for `x`, the taken branch of `if (x > 10)` highlighted).
- Lessons **1–3 rewritten** for beginners: 1 What a program is + output, 2 Variables, 3 Conditions.
  Mostly watch / choose / order / edit-one-line; Practice last.

- **AI grounded in engine facts**: one context builder used by every AI call — lesson goal, current
  step, code with line numbers, the step sentence, a short text form of the memory state, friendly
  errors and the test difference. New calls: `ai_ask(context, question)` for any step and
  `ai_why(context)` to explain the last step sentence. Prompts in `prompts/*.txt`.

### Week 3 (30 Nov – 4 Dec)
- **Grading explanations**: keywords from `@rubric` first (works offline), AI adds feedback and a
  follow-up question; returns `Verdict`.
- Scenes **`array`** (size, values, index, out of bounds) and **`pointer_basics`**
  (`p = &a`, `p = &b`, `*p = 5`, `p = NULL`, `*p` with NULL → error).
- Lessons **4–7**: Loops, Functions (new), Arrays, Pointers.
- **AI status and model download**: `ai_status()` (server running? model installed?) and
  `ai_pull_model()` with progress through Ollama's `/api/pull`, for Ihor's setup screen.
- **AI review** after a passed Practice step; **Predict feedback**: after a wrong answer the AI
  explains the misunderstanding using the real value.

### Week 4 (7–11 Dec)
- Scene **`heap`** (+ **`recursion`** if time); lessons **8–9** if time.
- **More practice**: `ai_generate_task(lesson)` → a Practice step (task, starter, reference solution,
  tests). The engine compiles the reference and runs the tests; only a task that passes is shown
  (up to 3 attempts).
- **AI quality check**: `tests/ai_eval/` with 20–30 beginner mistakes and what a good answer must
  mention; a small tool scores each model and prompt. Compare 3B vs 7B; put the table in the report.
- **Beginner test** with Ihor: 15 minutes with someone who never programmed; write down where they
  get stuck.

### Week 5 (14–18 Dec)
- Fixes from the test, report, demo video, journal.

## Done checklist
- [x] Step narration with tests (swap, array, heap, crash, printf)
- [x] `@show`, `@editable`, `order`, `choose` in the lesson format
- [x] Friendly errors table + `glossary_lookup`
- [ ] Scene engine + `variables`, `condition`, `array`, `pointer_basics` (+ `heap`, `recursion`)
- [ ] Explanation grading (keywords + AI)
- [ ] Lessons 1–7 (8–9 if time), every step checked by `./build/tests`
- [ ] Every AI call grounded in engine facts; `ai_ask`, `ai_why`
- [ ] `ai_status`, `ai_pull_model`; AI review; Predict feedback
- [ ] AI-generated practice tasks verified by the compiler
- [ ] AI quality check (models and prompts compared, table in the report)
- [ ] Beginner test notes in the report
