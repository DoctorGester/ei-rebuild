// The game's CArray<T> instantiations (mfc_array.h); our code calls them by name.
// Instantiations with the same element policy share one body; the
// groups were found by comparing the instructions of every instantiation our code forwards to
// (addresses masked), the element sizes and helpers read from each.
#include "mfc_array.h"
#include "mfc_slots.h"

namespace mfc::impl {

// The script's string arrays' Serialize (game.exe's CArray<CString>): the count (loading: SetSize(count, -1)),
// then each string as MFC's operator<< / operator>> of a CString stores it (here so the vectors build, which links
// no registration file, has it for StringArrayClass).
template <>
void Strings::Serialize(CArchive* ar) {
    if (!ar->IsLoading()) {
        ar->WriteCount(static_cast<unsigned long>(size));
        for (int i = 0; i < size; ++i) StoreText(*ar, CString(data[i].c_str(), static_cast<int>(data[i].size())));
    } else {
        SetSize(static_cast<int>(ar->ReadCount()), -1);
        for (int i = 0; i < size; ++i) {
            CString text;
            LoadText(*ar, text);
            data[i].assign(static_cast<const char*>(text), static_cast<std::size_t>(text.GetLength()));
        }
    }
}

} // namespace mfc::impl
