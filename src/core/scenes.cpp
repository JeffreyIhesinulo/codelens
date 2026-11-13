// Explore scenes: build a ProgramState from slider values and button presses, without compiling.
// Planned for week 3 (array, pointer_basics, heap, recursion). Until then: no scenes.
#include "app.h"

Scene scene_get(const std::string& /*id*/) {
    return Scene{};
}

ProgramState scene_state(const Scene& s, const std::vector<std::string>& /*actions_done*/) {
    ProgramState st;
    st.note = "Scene \"" + s.id + "\" is not implemented yet.";
    return st;
}
