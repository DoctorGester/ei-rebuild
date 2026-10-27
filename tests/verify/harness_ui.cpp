// The interface's two paths compared (verify build only; ticket N3): see harness_ui.h. The image's interface only
// (image/harness_ui_image.cpp); game.exe's runs draw with the original code.
#include "harness_ui.h"

#include "harness_objects.h"
#include "image/image_side.h"

#include "../../src/hook/log.h"
#include "ei/hook.h"

namespace ei::verify::uicompare {

void Install() {
    if (ei::Standalone()) image::uicompare::Install();
}

void Frame() {
    if (ei::Standalone()) image::uicompare::Frame();
}

bool Step(const std::string& verb, const std::string& argument, std::string* error) {
    if (verb != "uipath" && verb != "uimask" && verb != "uidiff" && verb != "uidiffcheck" && verb != "uimirror" && verb != "uifault")
        return false;
    if (!ei::Standalone()) {
        Log("harness: %s: skipped in a run of the original code", verb.c_str());
        return true;
    }
    return image::uicompare::Step(verb, argument, error);
}

void Report(FILE* f) {
    if (ei::Standalone()) image::uicompare::Report(f);
}

std::string Failure() { return ei::Standalone() ? image::uicompare::Failure() : std::string(); }

} // namespace ei::verify::uicompare
