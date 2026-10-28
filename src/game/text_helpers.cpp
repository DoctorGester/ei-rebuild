// Small text helpers of the engine base (text_helpers.h).
#include "text_helpers.h"

#include "lib/crt.h"

#include <cstring>
#include <memory>
#include <new>

const char* TextTokenizer::Next() {
    if (position == source->GetLength()) position = -1;
    if (position == -1) return nullptr;
    int found = source->Find(separator, position);
    int next;
    if (found == -1) {
        word = source->Mid(position);
        next = source->GetLength();
    } else {
        word = source->Mid(position, found - position);
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

int RemoveCarriageReturns(mfc::CString* text) { return text->Remove('\r'); }

mfc::CString* FindListedName(mfc::CString* out, const std::vector<std::string>* names, mfc::CString name) {
    for (const std::string& listed : *names) {
        if (gamecrt::mbsicmp(listed.c_str(), name.GetString()) == 0) {
            std::construct_at(out, listed.c_str());
            return out;
        }
    }
    std::construct_at(out, name);
    return out;
}
