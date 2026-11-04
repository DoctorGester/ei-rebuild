// Recorded play (harness_record.h).
#include "harness_record.h"

#include "harness_clock.h"
#include "platform/harness_platform.h"
#include "platform/log.h"
#include "system/harness_system.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <sstream>

namespace ei::verify::record {
namespace {

using platform::Event;
using platform::EventType;
using platform::MouseButton;

bool g_asked = false;
bool g_live = false;
FILE* g_file = nullptr;
const unsigned* g_frames = nullptr;
std::atomic<int>* g_mouseX = nullptr;
std::atomic<int>* g_mouseY = nullptr;
// The harness's mouse as the lines left it (the harness's own start, -1: the window's centre).
int g_writtenX = -1, g_writtenY = -1;
// Live play's pacing: the game's clock and the wall clock when it began (rebased when the game ran ahead).
bool g_paced = false;
std::uint32_t g_gameStart = 0, g_realStart = 0;

unsigned Frame() { return g_frames ? *g_frames : 0; }

const char* ButtonName(MouseButton button) {
    switch (button) {
    case MouseButton::Left: return "left";
    case MouseButton::Right: return "right";
    case MouseButton::Middle: return "middle";
    case MouseButton::X1: return "x1";
    case MouseButton::X2: return "x2";
    }
    return "left";
}

bool ButtonOf(const std::string& name, MouseButton* button) {
    static const struct { const char* name; MouseButton button; } kButtons[] = {
        {"left", MouseButton::Left}, {"right", MouseButton::Right}, {"middle", MouseButton::Middle},
        {"x1", MouseButton::X1}, {"x2", MouseButton::X2}};
    for (const auto& b : kButtons)
        if (name == b.name) {
            *button = b.button;
            return true;
        }
    return false;
}

// Text as one word: bytes outside ! .. ~ and % itself as %XX.
std::string Escaped(const std::string& text) {
    std::string out;
    for (const unsigned char c : text) {
        if (c > ' ' && c <= '~' && c != '%') {
            out += static_cast<char>(c);
        } else {
            char hex[4];
            std::snprintf(hex, sizeof(hex), "%%%02X", c);
            out += hex;
        }
    }
    return out;
}

std::string Unescaped(const std::string& word) {
    std::string out;
    for (std::size_t i = 0; i < word.size(); ++i) {
        if (word[i] == '%' && i + 2 < word.size()) {
            out += static_cast<char>(std::strtoul(word.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        } else {
            out += word[i];
        }
    }
    return out;
}

void Line(const std::string& text) {
    if (g_file) std::fprintf(g_file, "at %u %s\n", Frame(), text.c_str());
}

// The harness's mouse written when it moved since the lines last placed it; true when it now stands at `at`
// (written with the event that brings it there).
bool CursorTo(const Event* event) {
    const int x = g_mouseX ? g_mouseX->load() : -1, y = g_mouseY ? g_mouseY->load() : -1;
    if (x == g_writtenX && y == g_writtenY) return false;
    g_writtenX = x;
    g_writtenY = y;
    if (event && event->x == x && event->y == y) return true;
    Line("event cursor " + std::to_string(x) + " " + std::to_string(y));
    return false;
}

bool IsMouse(EventType type) {
    return type == EventType::MouseMove || type == EventType::MouseDown || type == EventType::MouseUp ||
           type == EventType::Wheel;
}

} // namespace

void Ask(bool live) {
    g_asked = true;
    g_live = g_live || live;
}
bool Asked() { return g_asked; }
bool Live() { return g_live; }

void Init(const unsigned* frames, std::atomic<int>* mouseX, std::atomic<int>* mouseY) {
    g_frames = frames;
    g_mouseX = mouseX;
    g_mouseY = mouseY;
}

std::string Begin(const std::vector<std::string>& header, const std::string& source) {
    g_file = sys::OpenFile(ei::OutputPath("recorded.txt"), "w");
    if (!g_file) return "recordinput: recorded.txt cannot be created in the run folder";
    // The scenario's path from the checkout's tests folder on (no machine's folders in a committed file).
    std::string shown = source;
    std::replace(shown.begin(), shown.end(), '\\', '/');
    if (const std::size_t tests = shown.rfind("tests/scenarios/"); tests != std::string::npos) shown.erase(0, tests);
    std::fprintf(g_file,
                 "# Recorded by the harness from %s (tests/verify/harness_record.h):\n"
                 "# each input event and each step that acts on the game at the frame it came in.\n"
                 "recorded\n",
                 shown.c_str());
    for (const std::string& line : header) std::fprintf(g_file, "%s\n", line.c_str());
    std::fflush(g_file);
    Log("harness: recording the run's input into recorded.txt%s", g_live ? " (live play)" : "");
    return "";
}

bool On() { return g_file != nullptr; }

StepKind Classify(const std::string& verb) {
    static const std::set<std::string> kInput = {
        "key", "keydown", "keyup", "click", "rclick", "move", "wheel", "dblclick", "rdblclick", "ldown", "lup", "rdown",
        "rup", "clickregion", "rclickregion", "dblclickregion", "moveregion", "clickunit", "clickground", "lookat", "hold",
        "release", "typeunicode", "mousedrag", "realinput"};
    static const std::set<std::string> kSkip = {
        "wait", "frames", "waitmenu", "waitwindow", "waitdialogue", "talk", "waitunit", "waitidle", "walkto", "waitdead",
        "waitaward", "awardfrom", "waitline", "waitexpect", "waitfor", "waitzone", "waitpresent", "check", "expect",
        "reject", "report", "groundview", "hudstate", "minimap", "consoleline"};
    static const std::set<std::string> kKept = {
        "newgame", "load", "console", "zone", "screen", "speed", "frametime", "focus", "windowsize", "chat", "campputon",
        "freeze", "unfreeze", "digest", "dump", "quit", "shadows", "masterhost", "hit"};
    if (kInput.count(verb)) return StepKind::Input;
    if (kSkip.count(verb)) return StepKind::Skip;
    if (kKept.count(verb)) return StepKind::Kept;
    return StepKind::Refused;
}

void Step(const std::string& verb, const std::string& argument) {
    if (!g_file) return;
    CursorTo(nullptr);
    Line(argument.empty() ? verb : verb + " " + argument);
}

void Queued(const Event& e, bool typesCharacter) {
    if (!g_file) return;
    const bool cursor = IsMouse(e.type) ? CursorTo(&e) : (CursorTo(nullptr), false);
    char text[160];
    switch (e.type) {
    case EventType::KeyDown:
    case EventType::KeyUp:
        std::snprintf(text, sizeof(text), "event key %s %#x %#x %u%s%s", e.type == EventType::KeyDown ? "down" : "up", e.key,
                      e.scan, unsigned(e.modifiers), e.repeat ? " repeat" : "", typesCharacter ? "" : " raw");
        break;
    case EventType::Text:
        Line("event text " + std::to_string(e.scan) + " " + Escaped(e.text));
        return;
    case EventType::MouseMove: std::snprintf(text, sizeof(text), "event move %d %d", e.x, e.y); break;
    case EventType::MouseDown:
        std::snprintf(text, sizeof(text), "event down %s %d %d %d", ButtonName(e.button), e.x, e.y, e.clicks);
        break;
    case EventType::MouseUp:
        std::snprintf(text, sizeof(text), "event up %s %d %d %d", ButtonName(e.button), e.x, e.y, e.clicks);
        break;
    case EventType::Wheel: std::snprintf(text, sizeof(text), "event wheel %d %d %.9g", e.x, e.y, e.wheel); break;
    default:
        Log("harness: recordinput: an event of type %d is not recorded", static_cast<int>(e.type));
        return;
    }
    Line(std::string(text) + (cursor ? " c" : ""));
}

void Motion(int dx, int dy) {
    if (g_file && (dx || dy)) Line("event motion " + std::to_string(dx) + " " + std::to_string(dy));
}

void EndPass() {
    if (!g_file) return;
    CursorTo(nullptr);
    std::fflush(g_file);
    if (!g_live) return;
    // At most one frame's wait a pass: a game that ran far ahead (a load's sleeps take no time) is not caught up with.
    constexpr int kMostWait = 50;
    const std::uint32_t game = clock::Milliseconds(), real = sys::TickCount();
    if (!g_paced) {
        g_paced = true;
        g_gameStart = game;
        g_realStart = real;
        return;
    }
    const int ahead = static_cast<int>((game - g_gameStart) - (real - g_realStart));
    if (ahead > 0) sys::SleepMs(static_cast<unsigned>((std::min)(ahead, kMostWait)));
    if (ahead > kMostWait) g_realStart = real + kMostWait - (game - g_gameStart);
}

void End() {
    if (!g_file) return;
    std::fclose(g_file);
    g_file = nullptr;
    Log("harness: recorded.txt written");
}

std::string Replay(const std::string& argument) {
    std::istringstream in(argument);
    std::vector<std::string> w;
    for (std::string word; in >> word;) w.push_back(word);
    const bool cursor = !w.empty() && w.back() == "c";
    if (cursor) w.pop_back();
    auto number = [&](std::size_t i) { return i < w.size() ? static_cast<int>(std::strtol(w[i].c_str(), nullptr, 0)) : 0; };
    const std::string bad = "event: cannot read '" + argument + "'";
    if (w.empty()) return bad;
    Event e;
    if (w[0] == "cursor" && w.size() == 3) {
        *g_mouseX = number(1);
        *g_mouseY = number(2);
        return "";
    }
    if (w[0] == "motion" && w.size() == 3) {
        harnessplatform::AddMotion(number(1), number(2));
        return "";
    }
    bool typesCharacter = true;
    if (w[0] == "key" && w.size() >= 5 && (w[1] == "down" || w[1] == "up")) {
        e.type = w[1] == "down" ? EventType::KeyDown : EventType::KeyUp;
        e.key = number(2);
        e.scan = number(3);
        e.modifiers = static_cast<std::uint8_t>(number(4));
        for (std::size_t i = 5; i < w.size(); ++i) {
            if (w[i] == "repeat") e.repeat = true;
            else if (w[i] == "raw") typesCharacter = false;
            else return bad;
        }
    } else if (w[0] == "text" && w.size() == 3) {
        e.type = EventType::Text;
        e.scan = number(1);
        e.text = Unescaped(w[2]);
    } else if (w[0] == "move" && w.size() == 3) {
        e.type = EventType::MouseMove;
        e.x = number(1);
        e.y = number(2);
    } else if ((w[0] == "down" || w[0] == "up") && w.size() == 5) {
        e.type = w[0] == "down" ? EventType::MouseDown : EventType::MouseUp;
        if (!ButtonOf(w[1], &e.button)) return bad;
        e.x = number(2);
        e.y = number(3);
        e.clicks = number(4);
    } else if (w[0] == "wheel" && w.size() == 4) {
        e.type = EventType::Wheel;
        e.x = number(1);
        e.y = number(2);
        e.wheel = std::strtof(w[3].c_str(), nullptr);
    } else {
        return bad;
    }
    if (cursor) {
        *g_mouseX = e.x;
        *g_mouseY = e.y;
    }
    harnessplatform::Queue(e, typesCharacter);
    return "";
}

} // namespace ei::verify::record
