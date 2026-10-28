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

int __fastcall RemoveCarriageReturns(mfc::CString* text) { return text->Remove('\r'); }

mfc::CString* __fastcall FindListedName(mfc::CString* out, const mfc::CStringArray* names, mfc::CString name) {
    for (int i = 0; i < names->size; ++i) {
        if (gamecrt::mbsicmp(names->data[i].GetString(), name.GetString()) == 0) {
            std::construct_at(out, names->data[i]);
            return out;
        }
    }
    std::construct_at(out, name);
    return out;
}
