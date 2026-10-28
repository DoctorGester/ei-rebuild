// Small text helpers of the engine base (text_helpers.h).
#include "text_helpers.h"

#include "lib/crt.h"
#include "lib/str.h"

#include <cstring>
#include <memory>
#include <new>
#include <string>

const char* TextTokenizer::Next() {
    if (position == static_cast<int>(source->size())) position = -1;
    if (position == -1) return nullptr;
    int found = str::Find(*source, separator, position);
    int next;
    if (found == -1) {
        word = str::Mid(*source, position);
        next = source->size();
    } else {
        word = str::Mid(*source, position, found - position);
        next = static_cast<int>(std::strlen(separator)) + found;
    }
    position = next;
    if (next == -1) return nullptr;
    return word.c_str();
}

const char* TextTokenizer::Start() {
    position = 0;
    return Next();
}

int RemoveCarriageReturns(std::string* text) { return str::Remove(*text, '\r'); }

std::string* FindListedName(std::string* out, const std::vector<std::string>* names, const std::string& name) {
    for (const std::string& listed : *names) {
        if (gamecrt::mbsicmp(listed.c_str(), name.c_str()) == 0) {
            *out = listed.c_str();
            return out;
        }
    }
    *out = name;
    return out;
}
