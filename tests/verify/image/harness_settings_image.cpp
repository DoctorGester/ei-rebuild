// The run's settings file in the image (image_side.h; harness_settings.cpp, harness_startup.cpp).
#include "image_side.h"

#include "game/settings_file.h"

namespace ei::verify::image {

namespace {

settings_file::Document Document(const std::vector<SettingsValue>& entries) {
    settings_file::Document document;
    for (const SettingsValue& entry : entries) {
        if (entry.value) document.Set(entry.key, entry.name.c_str(), entry.type, entry.data);
        else document.AddKey(entry.key);
    }
    return document;
}

} // namespace

const char* SettingsFileName() { return settings_file::kFileName; }
std::string SettingsText(const std::vector<SettingsValue>& document) { return Document(document).Text(); }
void PinSettings(const char* file, SettingsMissing missing, bool readOnly) { settings_file::Pin(file, missing, readOnly); }
void ImportSettings(const std::vector<SettingsValue>& document, const char* file, SettingsMissing missing) {
    static settings_file::Document registry;
    static settings_file::DocumentSource source(registry);
    registry = Document(document);
    settings_file::SetImportSource(&source);
    settings_file::Pin(file, missing, false);
}
void AddSettingsReadFilter(SettingsReadFilter filter) { settings_file::AddReadFilter(filter); }

} // namespace ei::verify::image
