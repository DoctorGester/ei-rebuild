// The read trace for the mod-folder checks: see harness_modtrace.h.
#include "harness_modtrace.h"

#include <cstdlib>
#include <mutex>
#include <set>

namespace ei::verify::modtrace {
namespace {

bool g_enabled = false;
std::mutex g_lock;
std::set<std::string> g_seen, g_written;   // lower case, backslashes
std::string g_mod, g_game, g_overlay;

// A path as the trace keeps it: lower case (ASCII), backslashes, no trailing one.
std::string Normal(std::string path) {
    for (char& c : path) {
        if (c == '/') c = '\\';
        else if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    while (path.size() > 1 && path.back() == '\\') path.pop_back();
    return path;
}

} // namespace

void Enable() { g_enabled = true; }
bool Enabled() { return g_enabled; }

void SetRoots(const std::string& mod, const std::string& game, const std::string& overlay) {
    g_mod = Normal(mod);
    g_game = Normal(game);
    g_overlay = Normal(overlay);
}

void NoteRead(const std::string& mapped) {
    if (!g_enabled || mapped.empty()) return;
    const std::string path = Normal(mapped);
    std::lock_guard<std::mutex> lock(g_lock);
    g_seen.insert(path);
}

void NoteWrite(const std::string& mapped) {
    if (!g_enabled || mapped.empty()) return;
    const std::string path = Normal(mapped);
    std::lock_guard<std::mutex> lock(g_lock);
    g_written.insert(path);
}

bool Seen(const std::string& where, const std::string& path, bool written, std::string* error) {
    const std::string relative = Normal(path);
    std::lock_guard<std::mutex> lock(g_lock);
    const std::set<std::string>& seen = written ? g_written : g_seen;
    if (where == "mod") {
        if (g_mod.empty()) {
            *error = "no mod folder (the scenario has no mod line)";
            return false;
        }
        return seen.count(g_mod + "\\" + relative) != 0;
    }
    if (where == "game") {
        // The game's folder, or the overlay's copy of its file (a file the run wrote).
        return seen.count(g_game + "\\" + relative) != 0 || seen.count(g_overlay + "\\" + relative) != 0;
    }
    *error = "'" + where + "' is not mod or game";
    return false;
}

std::string ReadStep(const std::string& verb, const std::string& argument) {
    const std::string usage = verb + " " + argument + ": not <mod|game> <path> yes|no";
    const std::size_t a = argument.find(' ');
    const std::size_t b = argument.rfind(' ');
    if (a == std::string::npos || b == a) return usage;
    const std::string where = argument.substr(0, a), path = argument.substr(a + 1, b - a - 1), want = argument.substr(b + 1);
    if (want != "yes" && want != "no") return usage;
    std::string error;
    const bool seen = Seen(where, path, verb == "wrote", &error);
    if (!error.empty()) return verb + " " + argument + ": " + error;
    if (seen == (want == "yes")) return {};
    const bool written = verb == "wrote";
    const char* did = seen ? (written ? "wrote" : "touched") : (written ? "did not write" : "did not touch");
    return verb + " " + where + " " + path + ": the game " + did + " it, expected " +
           (want == "yes" ? "it to" : "it not to");
}

bool ParseWaitRead(const std::string& argument, unsigned long* seconds, std::string* where, std::string* path) {
    char* rest = nullptr;
    *seconds = std::strtoul(argument.c_str(), &rest, 10);
    if (!rest || *rest != ' ') return false;
    const std::string tail = rest + 1;
    const std::size_t space = tail.find(' ');
    if (space == std::string::npos) return false;
    *where = tail.substr(0, space);
    *path = tail.substr(space + 1);
    return !path->empty();
}

} // namespace ei::verify::modtrace
