# Plan v3 — Ihor (UI, `src/ui`)

Read [PLAN.md](PLAN.md) first: the app is for people who have **never programmed**, and the
interface must always show what to do next.

## What changed for you (17 Nov 2026)

1. The lesson screen is no longer "theory | editor | AI" all at once. A lesson becomes a **guided
   sequence of steps**, one at a time, full width, with a "what to do now" bar and one main button.
   The current three-panel screen survives only as the **Practice** step.
2. `draw_memory` needs **levels** (`values`, `frames`, `addresses`, `heap`) so beginners first see
   only named boxes with values.
3. New screens: **welcome** (first launch) and the **course map as a path** with a Continue button.
4. New step UIs: **order** (drag shuffled lines into order) and **choose** (pick one option).
5. The editor gets **locked lines** (template greyed out, only marked lines editable).
6. **Glossary tooltips** when hovering keywords in code.
7. Every debugger step shows the engine's sentence (`ProgramState.note`) under the visualizer.
8. **The local AI tutor is our main feature** (added later on 17 Nov): an "Ask the tutor" button on
   every screen, a **Why?** button next to every step sentence, an **AI setup screen** with model
   download, and a **More practice** button. See PLAN.md, section D.

Your existing work (`memview.cpp`, `slider`, `action_buttons`, `text_area`, `--memory-preview`)
is kept and reused: the visualizer is the centre of Explore and Understand.

## Reference screen (what "beginner-friendly" means here)

Every student screen follows the checklist in [AGENTS.md](../AGENTS.md#checklist-for-every-student-facing-screen).
Example — the Explore step of the Arrays lesson:

```
← Lessons   6. Arrays   ● ● ○ ○                                  [Ask the tutor]
┌──────────────────────────────────────────────────────────────────────────────┐
│ Step 2 of 4 · Explore                                                        │
│ Move the slider and watch which box the arrow p points to.                   │
│ An array is several boxes in a row with one name. Numbering starts at 0.     │
└──────────────────────────────────────────────────────────────────────────────┘
┌ Code ─────────────────────────────┐ ┌──────────────────────────────────────┐
│ int a[5] = {10, 20, 99, 40, 50};  │ │   a — 5 whole numbers                │
│ int *p = &a[4];      ← highlighted│ │  [a[0]][a[1]][a[2]][a[3]][a[4]]      │
│                                   │ │    10    20    99    40   (50)       │
│ Box number  ─────────●  4         │ │                          ↑           │
│                                   │ │                     p → a[4]         │
└───────────────────────────────────┘ └──────────────────────────────────────┘
 💡 p now points to a[4]. That box holds 50.                         [Next →]
```

- No Stack/Heap headers, no addresses (this lesson uses `@show values`).
- The slider label says what it is ("Box number"), and the highlighted code line changes with it.
- The sentence at the bottom changes after every move (`ProgramState.note`).
- No Notes box. The tutor button sends the step, the code and the current state.

## Interfaces you will use

Already in `src/core/app.h` (main):

```cpp
bool debug_start(const std::string& code, Lang lang, const std::string& stdin_text, std::string& error);
bool debug_next(); bool debug_step(); bool debug_finish(); bool debug_run_to_line(int line);
const ProgramState& debug_state();      // .note = sentence for the last step, .line = next line
bool debug_active(); void debug_stop();
Verdict check_prediction(const Step& step, Lang lang, const std::string& answer);
struct Step { StepKind kind; std::string title, text, code, input, starter, solution, scene; ... };
// Lesson::steps holds every step in order; a lesson without @step has one Practice step.
```

**Added in Dzheffriei's week-1 PR (17 Nov)** — now in `app.h`:

```cpp
enum class ShowLevel { Values, Frames, Addresses, Heap };   // Lesson::show, Step::show
// StepKind gains Order and Choose
// Step gains:
//   std::vector<std::string> lines;     // Order: lines in the correct order
//   std::vector<std::string> options;   // Choose: answers
//   int correct = -1;                   // Choose: index of the right option
//   int edit_from = 0, edit_to = 0;     // Modify/FindBug/Practice: editable lines (1-based), 0 = all
//   (first edit_from-1 and last (lines - edit_to) lines locked; the middle may grow or shrink)
std::vector<int> order_shuffled(const Step& s);                    // a shuffled start order
bool check_order(const Step& s, const std::vector<int>& order);
Verdict check_choice(const Step& s, int index);                    // Choose steps
// ProgramState::note: one sentence after every debugger step; a value "?" = no value yet
struct FriendlyError { int line; std::string message, hint; };
// RunResult gains std::vector<FriendlyError> friendly;  (show these instead of raw compiler text)
std::string glossary_lookup(const std::string& token);             // "" if unknown
// Scene gains std::string code; scene_state(...).line = line to highlight
```

**Planned AI calls** (Dzheffriei, weeks 2–4; names may still change in his PR):

```cpp
int  ai_ask(const AiContext& ctx, const std::string& question);   // any step; ctx built by the engine
int  ai_why(const AiContext& ctx);                                // explain the last step sentence
bool ai_poll(int request_id, std::string& answer);               // already exists
struct AiStatus { bool server_running; bool model_installed; std::string model; };
AiStatus ai_status();
bool ai_pull_model(float& progress /* 0..1 */);                  // download in the background
int  ai_generate_task(const Lesson& lesson);                      // "More practice", verified by the engine
```

## Tasks

### Follow-up (23 Nov)
- Linked lists: a chain of nodes with values and next links, head/p labels, NULL termination,
  freed-node indicators and focus on pointer expressions such as `a->next->value`.
- Use engine snapshots for heap nodes and stack-based nodes in Find the bug. Support both
  GDB's direct struct fields and LLDB's nested element layout; scroll to the focused node.
- If a snapshot omits fields of a live node, read them through `debug_eval` on the worker;
  rendering never evaluates expressions or guesses node values.
- Verify all steps of `06_lists` with the real debugger and an isolated progress/draft profile.

At Ihor's request, heap narration now uses arrow notation for a single struct with either
debugger layout. Regression cases retain indexing for real arrays and arrays of structs.

### Follow-up (22 Nov)
- Order cards support dragging, insertion feedback and edge scrolling; arrow buttons remain available.
- The editor supports mouse/keyboard selection, copy/cut/paste, undo/redo and whole-line deletion,
  while preserving locked template lines. Shortcuts are listed in the README.
- The expanded Hello, Conditions, Loops and Arrays lessons use output, branch, counter and selected
  array-box views. Explain feedback uses each step's rubric.
- Preserve completed tasks when new steps are inserted; keep drafts intact. Draw normal screens at
  the display's native resolution and capture the background only while the tutor is open.
- UI interaction checks live in `src/ui/checks/`; they use an isolated temporary profile.

### Week 1 (16–20 Nov)
- Keep `--memory-preview` as your developer tool; do not polish it for students.
- **`draw_memory(state, area, ShowLevel level)`**
  - `Values`: one box per variable, `name = value`. No addresses, no frame headers, no heap column.
  - `Frames`: frame per function, current one marked.
  - `Addresses`: addresses on hover, pointer arrows.
  - `Heap`: heap column.
  - Done when: `--memory-preview` has a level switch, and the swap fixture at `Values` shows only
    `x`, `y`, `a`, `b`, `t` as boxes, with no hex anywhere.
- **Welcome screen** (first launch only; store a flag next to `progress.txt`). It explains in three
  short lines what Explore / Understand / Practice mean and has one button: **Start lesson 1**.
- **Guided lesson screen skeleton** (`step_screen.cpp`):
  - Top bar: ← Lessons · lesson title · step dots (done / current / locked).
  - "What to do now" bar: `Step 3 of 8 · Predict · <step.text first line>`.
  - Body area per step kind (placeholders for now) and the main button bottom-right (Next / Check).
  - Practice = the existing three-panel `LessonScreen`.
  - Build it on `lessons/05_pointers.lesson`, which already has every step kind.
  - Done when: you can click through all steps of that lesson.

### Week 2 (23–27 Nov)
- **Watch step**: code on the left (read-only, current line highlighted), `draw_memory` on the
  right, `debug_state().note` under it in large text. Buttons: Next line, Step into, Step out,
  Restart. Keys: F10, F11, Shift+F11.
- **Predict step**: code with line `predict_line` highlighted, the question, an answer field,
  Check. After checking: show the `Verdict` feedback and run the code to that line so the
  visualizer shows the real value.
- **Order step**: shuffled lines as cards; drag to reorder (or up/down buttons); Check →
  `check_order`; wrong lines turn red.
- **Choose step**: options as large buttons; right/wrong colour + explanation.
- **Locked template in the editor**: lines outside `edit_from..edit_to` drawn dimmed and not
  editable; the cursor cannot enter them.
- **Friendly errors** in the console: the `FriendlyError` sentence first, raw compiler text behind a
  "Show details" toggle.
- **Ask the tutor** button on every step screen (opens a slide-in chat; until `ai_ask` lands use
  `ai_chat`) and a **Why?** button next to the step sentence in Watch and Predict.

### Week 3 (30 Nov – 4 Dec)
- **Explore step** (generic for every scene): sliders for `scene.params`, `action_buttons` for
  `scene.actions`, the scene code with its highlighted line, `draw_memory` of
  `scene_state(...)`, and the note under it.
- **Modify / FindBug**: the editor with locked lines + Run/Check + a **Debug** button that opens the
  student's code in the Watch view.
- **Explain**: question, `text_area`, Submit; feedback card; "Show model answer" after submitting.
- **Course map as a path**: topics top-to-bottom, three small stage indicators each, the next topic
  highlighted, a big **Continue** button.
- **Lesson notes drawer** in Practice: the lesson's Explore/Understand texts, reachable at any time.
- **AI setup screen**: shown when `ai_status()` says the server or the model is missing. Plain steps
  ("1. Install Ollama from ollama.com 2. Press Download model"), a Download button with a progress
  bar (`ai_pull_model`), and "Continue without AI". Reachable later from the course screen.

### Week 4 (7–11 Dec)
- **Glossary tooltips**: hovering a token in code (editor and read-only views) shows
  `glossary_lookup(token)` in a small card.
- **AI tutor**: in Explore/Understand an "Ask the tutor" button opens a slide-in panel; in Practice
  it stays a panel.
- **More practice** button after a passed Practice step: shows "Writing a new task…" while
  `ai_generate_task` runs, then opens the new task as a Practice step.
- **Animations**: a changed value flashes, a new frame slides in, a pointer arrow moves smoothly.
- **Beginner test** together with Dzheffriei; polish the UI from what you see.

### Week 5 (14–18 Dec)
- Fixes from the test, presentation, demo video, journal.

## Done checklist
- [ ] Every student screen passes the AGENTS.md checklist (goal, terms explained, purpose of each control, what happened, code line, no empty space, tutor button, one main button)
- [x] `draw_memory` levels + switch in `--memory-preview`
- [x] Welcome screen
- [x] Guided lesson screen: bar, step dots, main button
- [x] Watch with live debugger + narration, F10/F11
- [x] Predict, Order, Choose
- [x] Locked template in the editor; friendly errors in the console
- [x] Explore (generic), Modify/FindBug with Debug, Explain
- [x] Course map path + Continue; lesson notes drawer
- [x] Glossary tooltips; AI tutor button; animations
- [x] Ask the tutor + Why? on every step; AI setup screen with model download; More practice

Technical UI items are implemented; scene/AI adapters will be reconciled with Jeffrey's later APIs.
The complete-beginner screen review and real-model quality check still need team validation.
