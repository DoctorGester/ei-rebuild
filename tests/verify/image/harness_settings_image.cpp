// The run's settings file in the image (image_side.h; harness_settings.cpp, harness_startup.cpp).
#include "image_side.h"

#include "game/settings.h"

namespace ei::verify::image {

const char* SettingsFileName() { return ::settings::kFileName; }
std::string SettingsText(const std::vector<SettingsLine>& lines) {
    ::settings::Document document;
    for (const SettingsLine& line : lines) {
        if (line.name.empty()) document.Comment(line.section, line.value);
        else document.Set(line.section, line.name, line.value);
    }
    return document.Text();
}
void PinSettings(const char* file, SettingsMissing missing, bool readOnly, SettingsPrepare prepare) {
    ::settings::Pin(file, missing, readOnly, prepare);
}
void DefaultSettings(const char* file, int width, int height) {
    ::settings::SetDesktopSize(width, height);
    ::settings::Pin(file, nullptr, false);
}
void AddSettingsReadFilter(SettingsReadFilter filter) { ::settings::AddReadFilter(filter); }

} // namespace ei::verify::image
