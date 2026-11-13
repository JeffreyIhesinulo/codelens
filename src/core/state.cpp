// ProgramState <-> JSON (fixtures, saved snapshots) and address lookup.
#include "app.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

// Addresses are stored as hex strings: JSON numbers lose precision above 2^53.
static std::string hex(uint64_t a) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "0x%llx", (unsigned long long)a);
    return buf;
}
static uint64_t unhex(const json& j) {
    if (j.is_number_unsigned()) return j.get<uint64_t>();
    if (!j.is_string()) return 0;
    return std::strtoull(j.get<std::string>().c_str(), nullptr, 16);
}

static json var_to_json(const Variable& v) {
    json j = {{"name", v.name}, {"type", v.type}, {"value", v.value}, {"address", hex(v.address)}};
    if (v.is_pointer) { j["is_pointer"] = true; j["points_to"] = hex(v.points_to); }
    if (!v.children.empty()) {
        j["children"] = json::array();
        for (const Variable& c : v.children) j["children"].push_back(var_to_json(c));
    }
    return j;
}

static Variable var_from_json(const json& j) {
    Variable v;
    v.name = j.value("name", "");
    v.type = j.value("type", "");
    v.value = j.value("value", "");
    v.address = unhex(j.value("address", json("0")));
    v.is_pointer = j.value("is_pointer", false);
    v.points_to = unhex(j.value("points_to", json("0")));
    if (j.contains("children"))
        for (const json& c : j["children"]) v.children.push_back(var_from_json(c));
    return v;
}

std::string state_to_json(const ProgramState& s) {
    json j;
    j["stack"] = json::array();
    for (const Frame& f : s.stack) {
        json jf = {{"function", f.function}, {"line", f.line}, {"vars", json::array()}};
        for (const Variable& v : f.vars) jf["vars"].push_back(var_to_json(v));
        j["stack"].push_back(jf);
    }
    j["heap"] = json::array();
    for (const HeapBlock& h : s.heap) {
        json jh = {{"address", hex(h.address)}, {"size", h.size}, {"freed", h.freed}, {"cells", json::array()}};
        for (const Variable& c : h.cells) jh["cells"].push_back(var_to_json(c));
        j["heap"].push_back(jh);
    }
    j["output"] = s.output;
    j["line"] = s.line;
    j["finished"] = s.finished;
    j["exit_code"] = s.exit_code;
    j["errors"] = json::array();
    for (const MemoryError& e : s.errors)
        j["errors"].push_back({{"kind", e.kind}, {"line", e.line}, {"details", e.details}});
    j["note"] = s.note;
    return j.dump(2);
}

bool state_from_json(const std::string& text, ProgramState& out, std::string& error) {
    out = ProgramState{};
    try {
        json j = json::parse(text);
        for (const json& jf : j.value("stack", json::array())) {
            Frame f;
            f.function = jf.value("function", "");
            f.line = jf.value("line", 0);
            for (const json& v : jf.value("vars", json::array())) f.vars.push_back(var_from_json(v));
            out.stack.push_back(f);
        }
        for (const json& jh : j.value("heap", json::array())) {
            HeapBlock h;
            h.address = unhex(jh.value("address", json("0")));
            h.size = jh.value("size", (size_t)0);
            h.freed = jh.value("freed", false);
            for (const json& c : jh.value("cells", json::array())) h.cells.push_back(var_from_json(c));
            out.heap.push_back(h);
        }
        out.output = j.value("output", "");
        out.line = j.value("line", 0);
        out.finished = j.value("finished", false);
        out.exit_code = j.value("exit_code", 0);
        for (const json& je : j.value("errors", json::array()))
            out.errors.push_back({je.value("kind", ""), je.value("line", 0), je.value("details", "")});
        out.note = j.value("note", "");
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

// ---------------------------------------------------------------- address lookup

static bool is_array(const Variable& v) {
    return !v.children.empty() && !v.children[0].name.empty() && v.children[0].name[0] == '[';
}

// Name of the variable, array element or struct field at address a ("" if none).
// Arrays name the element (p = a points at "a[0]"); structs name themselves (p = &pt is "pt").
static std::string find_in(const Variable& v, uint64_t a, const std::string& name) {
    if (!is_array(v) && v.address == a) return name;
    for (const Variable& c : v.children) {
        std::string sub = name + (c.name[0] == '[' ? c.name : "." + c.name);
        std::string r = find_in(c, a, sub);
        if (!r.empty()) return r;
    }
    if (v.address == a) return name;
    return "";
}

std::string state_describe_address(const ProgramState& s, uint64_t a) {
    if (a == 0) return "NULL";
    for (auto f = s.stack.rbegin(); f != s.stack.rend(); ++f)
        for (const Variable& v : f->vars) {
            std::string r = find_in(v, a, v.name);
            if (!r.empty()) return r;
        }
    for (size_t i = 0; i < s.heap.size(); ++i) {
        const HeapBlock& h = s.heap[i];
        if (a >= h.address && a < h.address + std::max<size_t>(h.size, 1))
            return "heap block " + std::to_string(i + 1) + (h.freed ? " (freed)" : "");
    }
    return "";
}
