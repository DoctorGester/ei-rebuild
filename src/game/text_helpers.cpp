// Small text helpers of the engine base (text_helpers.h; A-E1).
#include "text_helpers.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstring>
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
    return word;
}
EI_REPLACE_METHOD(0x00445D30, TextTokenizer::Next);

const char* TextTokenizer::Start() {
    position = 0;
    return Next();
}
EI_REPLACE_METHOD(0x00445D20, TextTokenizer::Start);

int __fastcall RemoveCarriageReturns(mfc::CString* text) { return text->Remove('\r'); }
EI_REPLACE(0x004461E0, RemoveCarriageReturns);

namespace {
const unsigned char* Bytes(const mfc::CString& text) {
    return reinterpret_cast<const unsigned char*>(static_cast<const char*>(text));
}
} // namespace

mfc::CString* __fastcall FindListedName(mfc::CString* out, const mfc::CStringArray* names, mfc::CString name) {
    for (int i = 0; i < names->size; ++i) {
        if (libcrt::Mbsicmp(Bytes(names->data[i]), Bytes(name)) == 0) {
            new (out) mfc::CString(names->data[i]);
            return out;
        }
    }
    new (out) mfc::CString(name);
    return out;
}
EI_REPLACE(0x00447F50, FindListedName);
