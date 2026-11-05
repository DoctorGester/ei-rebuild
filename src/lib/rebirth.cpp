// The values Rebirth's patches leave to the player (rebirth.h).
#include "lib/rebirth.h"

#include "lib/text_case.h"

#include <string>

namespace ei::rebirth {

namespace {

// Function statics: the harness queues its specs before this file's static constructors may have run.
std::string& SecondMaster() {
    static std::string address;
    return address;
}
std::string& Queued() {
    static std::string specs;
    return specs;
}

} // namespace

const std::string& PluginSecondMaster() { return SecondMaster(); }
void SetPluginSecondMaster(std::string_view address) { SecondMaster() = address; }

bool ApplySpec(const char* spec) {
    bool ok = true;
    const char* p = spec;
    while (*p) {
        while (*p == ',' || *p == ' ' || *p == ';' || *p == '\t') ++p;
        if (!*p) break;
        std::string token;
        while (*p && *p != ',' && *p != ' ' && *p != ';' && *p != '\t') token += *p++;
        if (text::CompareIgnoringCase(token.c_str(), "SecondMaster=", 13) == 0)
            SetPluginSecondMaster(token.c_str() + 13);
        else
            ok = false;
    }
    return ok;
}

void QueueSpec(const char* spec) {
    Queued() += spec;
    Queued() += ';';
}

void Reset() {
    SecondMaster().clear();
}

void ApplyQueued() { ApplySpec(Queued().c_str()); }

} // namespace ei::rebirth
