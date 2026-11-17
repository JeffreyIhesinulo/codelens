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

### Week 3 (30 Nov – 4 Dec)
- **Grading explanations**: keywords from `@rubric` first (works offline), AI adds feedback and a
  follow-up question; returns `Verdict`.
- Scenes **`array`** (size, values, index, out of bounds) and **`pointer_basics`**
  (`p = &a`, `p = &b`, `*p = 5`, `p = NULL`, `*p` with NULL → error).
- Lessons **4–7**: Loops, Functions (new), Arrays, Pointers.

### Week 4 (7–11 Dec)
- Scene **`heap`** (+ **`recursion`** if time); lessons **8–9** if time.
- **Beginner test** with Ihor: 15 minutes with someone who never programmed; write down where they
  get stuck.

### Week 5 (14–18 Dec)
- Fixes from the test, report, demo video, journal.

## Done checklist
- [ ] Step narration with tests (swap, array, heap, crash, printf)
- [ ] `@show`, `@editable`, `order`, `choose` in the lesson format
- [ ] Friendly errors table + `glossary_lookup`
- [ ] Scene engine + `variables`, `condition`, `array`, `pointer_basics` (+ `heap`, `recursion`)
- [ ] Explanation grading (keywords + AI)
- [ ] Lessons 1–7 (8–9 if time), every step checked by `./build/tests`
- [ ] Beginner test notes in the report
