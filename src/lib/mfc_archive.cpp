// MFC 4.2's CArchive buffer and string operators, registered at their library addresses
// (mfc_archive.h).
#include "mfc_archive.h"

#include "ei/hook.h"
#include "gameapi.h"

#include <cstring>
#include <cwchar>

namespace mfc::impl {

namespace {

mfc::CFileVirtuals* Virtuals(CFile* file) { return reinterpret_cast<mfc::CFileVirtuals*>(file); }

void** Start(CArchive* ar) { return reinterpret_cast<void**>(&ar->bufferStart); }
void** Max(CArchive* ar) { return reinterpret_cast<void**>(&ar->bufferMax); }

unsigned Available(const CArchive* ar) { return static_cast<unsigned>(ar->bufferMax - ar->bufferCur); }

} // namespace

// What the buffer holds first; then whole buffers straight from the file into the caller's memory;
// then the buffer refilled (at least a buffer's worth, or the rest when larger) for the remainder.
unsigned CArchive::Read(void* buffer, unsigned count) {
    if (count == 0) return 0;
    auto* to = static_cast<unsigned char*>(buffer);
    unsigned left = count;
    unsigned part = Available(this) < left ? Available(this) : left;
    std::memcpy(to, bufferCur, part);
    bufferCur += part;
    to += part;
    left -= part;
    if (left != 0) {
        const unsigned whole = left - left % bufferSize;
        unsigned want = whole;
        unsigned got = 0;
        unsigned read;
        do {
            read = Virtuals(file)->Read(to, want);
            to += read;
            got += read;
            want -= read;
        } while (read > 0 && want > 0);
        left -= got;
        if (got == whole) {
            if (!directBuffer) {
                want = left > bufferSize ? left : bufferSize;
                unsigned char* at = bufferStart;
                unsigned have = 0;
                do {
                    read = Virtuals(file)->Read(at, want);
                    at += read;
                    want -= read;
                    have += read;
                } while (read > 0 && want > 0 && have < left);
                bufferCur = bufferStart;
                bufferMax = bufferStart + have;
            } else {
                Virtuals(file)->GetBufferPtr(bufferRead, bufferSize, Start(this), Max(this));
                bufferCur = bufferStart;
            }
            part = Available(this) < left ? Available(this) : left;
            std::memcpy(to, bufferCur, part);
            bufferCur += part;
            left -= part;
        }
    }
    return count - left;
}

// Into the buffer while it has room; then the buffer flushed, whole buffers written straight to the
// file, and the rest into the (new) buffer.
void CArchive::Write(const void* buffer, unsigned count) {
    if (count == 0) return;
    auto* from = static_cast<const unsigned char*>(buffer);
    unsigned left = count;
    const unsigned part = Available(this) < left ? Available(this) : left;
    std::memcpy(bufferCur, from, part);
    bufferCur += part;
    from += part;
    left -= part;
    if (left == 0) return;
    Flush();
    const unsigned whole = left - left % bufferSize;
    Virtuals(file)->Write(from, whole);
    from += whole;
    left -= whole;
    if (directBuffer) {
        Virtuals(file)->GetBufferPtr(bufferWrite, bufferSize, Start(this), Max(this));
        bufferCur = bufferStart;
    }
    std::memcpy(bufferCur, from, left);
    bufferCur += left;
}

// Loading: the file goes back over what the buffer has not handed out. Storing: the buffer is written
// (or committed to the file's own buffer, and a new one taken).
void CArchive::Flush() {
    if (IsLoading()) {
        if (bufferMax != bufferCur) Virtuals(file)->Seek(static_cast<long>(bufferCur - bufferMax), FILE_CURRENT);
        bufferCur = bufferMax;
        return;
    }
    if (!directBuffer) {
        if (bufferCur != bufferStart)
            Virtuals(file)->Write(bufferStart, static_cast<unsigned>(bufferCur - bufferStart));
    } else {
        if (bufferCur != bufferStart)
            Virtuals(file)->GetBufferPtr(bufferCommit, static_cast<unsigned>(bufferCur - bufferStart), nullptr, nullptr);
        Virtuals(file)->GetBufferPtr(bufferWrite, bufferSize, Start(this), Max(this));
    }
    bufferCur = bufferStart;
}

void CArchive::FillBuffer(unsigned needed) {
    const unsigned unused = Available(this);
    const unsigned total = needed + unused;
    if (!directBuffer) {
        if (bufferCur > bufferStart) {
            if (static_cast<int>(unused) > 0) {
                std::memmove(bufferStart, bufferCur, unused);
                bufferCur = bufferStart;
                bufferMax = bufferStart + unused;
            }
            unsigned have = unused;
            unsigned want = bufferSize - unused;
            unsigned char* at = bufferStart + unused;
            unsigned read;
            do {
                read = Virtuals(file)->Read(at, want);
                have += read;
                at += read;
                want -= read;
            } while (read > 0 && want > 0 && have < needed);
            bufferCur = bufferStart;
            bufferMax = bufferStart + have;
        }
    } else {
        if (unused != 0) Virtuals(file)->Seek(-static_cast<long>(unused), FILE_CURRENT);
        Virtuals(file)->GetBufferPtr(bufferRead, bufferSize, Start(this), Max(this));
        bufferCur = bufferStart;
    }
    if (Available(this) < total) ThrowArchiveException(CArchiveException::endOfFile, nullptr);
}

CArchive* CArchive::StoreByte(unsigned char value) {
    if (bufferCur + sizeof(value) > bufferMax) Flush();
    std::memcpy(bufferCur, &value, sizeof(value));
    bufferCur += sizeof(value);
    return this;
}

CArchive* CArchive::StoreWord(unsigned short value) {
    if (bufferCur + sizeof(value) > bufferMax) Flush();
    std::memcpy(bufferCur, &value, sizeof(value));
    bufferCur += sizeof(value);
    return this;
}

CArchive* CArchive::StoreDword(unsigned long value) {
    if (bufferCur + sizeof(value) > bufferMax) Flush();
    std::memcpy(bufferCur, &value, sizeof(value));
    bufferCur += sizeof(value);
    return this;
}

CArchive* CArchive::LoadByte(unsigned char& value) {
    if (bufferCur + sizeof(value) > bufferMax) FillBuffer(static_cast<unsigned>(sizeof(value) - Available(this)));
    std::memcpy(&value, bufferCur, sizeof(value));
    bufferCur += sizeof(value);
    return this;
}

CArchive* CArchive::LoadWord(unsigned short& value) {
    if (bufferCur + sizeof(value) > bufferMax) FillBuffer(static_cast<unsigned>(sizeof(value) - Available(this)));
    std::memcpy(&value, bufferCur, sizeof(value));
    bufferCur += sizeof(value);
    return this;
}

CArchive* CArchive::LoadDword(unsigned long& value) {
    if (bufferCur + sizeof(value) > bufferMax) FillBuffer(static_cast<unsigned>(sizeof(value) - Available(this)));
    std::memcpy(&value, bufferCur, sizeof(value));
    bufferCur += sizeof(value);
    return this;
}

void CArchive::WriteCount(unsigned long count) {
    if (count < 0xFFFF) {
        StoreWord(static_cast<unsigned short>(count));
    } else {
        StoreWord(0xFFFF);
        StoreDword(count);
    }
}

unsigned long CArchive::ReadCount() {
    unsigned short word;
    LoadWord(word);
    if (word != 0xFFFF) return word;
    unsigned long dword;
    LoadDword(dword);
    return dword;
}

unsigned __stdcall ReadStringLength(CArchive& ar) {
    unsigned char byte;
    ar.LoadByte(byte);
    if (byte < 0xFF) return byte;
    unsigned short word;
    ar.LoadWord(word);
    if (word == 0xFFFE) return 0xFFFFFFFFu;
    if (word == 0xFFFF) {
        unsigned long dword;
        ar.LoadDword(dword);
        return dword;
    }
    return word;
}

CArchive& __stdcall StoreText(CArchive& ar, const CString& text) {
    const int length = text.GetLength();
    if (length < 0xFF) {
        ar.StoreByte(static_cast<unsigned char>(length));
    } else if (length < 0xFFFE) {
        ar.StoreByte(0xFF);
        ar.StoreWord(static_cast<unsigned short>(length));
    } else {
        ar.StoreByte(0xFF);
        ar.StoreWord(0xFFFF);
        ar.StoreDword(static_cast<unsigned long>(length));
    }
    ar.Write(static_cast<const char*>(text), static_cast<unsigned>(text.GetLength()));
    return ar;
}

// A wide string is read as its bytes, terminated, then converted into a fresh buffer.
CArchive& __stdcall LoadText(CArchive& ar, CString& text) {
    int wide = 0;
    int length = static_cast<int>(ReadStringLength(ar));
    if (length == -1) {
        wide = 1;
        length = static_cast<int>(ReadStringLength(ar));
    }
    const int bytes = length * (wide + 1);
    if (length == 0) {
        text.GetBufferSetLength(0);
        return ar;
    }
    text.GetBufferSetLength(bytes + wide);
    char* data = const_cast<char*>(static_cast<const char*>(text));
    if (ar.Read(data, static_cast<unsigned>(bytes)) != static_cast<unsigned>(bytes))
        ThrowArchiveException(CArchiveException::endOfFile, nullptr);
    if (wide) {
        auto* characters = reinterpret_cast<wchar_t*>(data);
        characters[length] = L'\0';
        CStringData* old = text.GetData();
        text.Init();
        AssignWide(&text, nullptr, characters);
        CString::FreeData(old);
    }
    return ar;
}

CString* __fastcall AssignWide(CString* self, void*, const wchar_t* text) {
    const int length = text ? static_cast<int>(std::wcslen(text)) : 0;
    self->AllocBeforeWrite(length * 2);
    WideToMultiByte(const_cast<char*>(static_cast<const char*>(*self)), text, static_cast<unsigned>(length * 2 + 1));
    self->ReleaseBuffer(-1);
    return self;
}

int __cdecl WideToMultiByte(char* to, const wchar_t* from, unsigned count) {
    if (count == 0 && to) return 0;
    const int written = gameapi::WideCharToMultiByte(CP_ACP, 0, from, -1, to, static_cast<int>(count), nullptr, nullptr);
    if (written > 0) to[written - 1] = '\0';
    return written;
}

EI_REPLACE_METHOD(0x00708832, CArchive::Read);
EI_REPLACE_METHOD(0x00708941, CArchive::Write);
EI_REPLACE_METHOD(0x007089D1, CArchive::Flush);
EI_REPLACE_METHOD(0x00708A4D, CArchive::FillBuffer);
EI_REPLACE_METHOD(0x00708B0F, CArchive::WriteCount);
EI_REPLACE_METHOD(0x00708B3D, CArchive::ReadCount);
EI_REPLACE_METHOD(0x006E6214, CArchive::StoreByte);
EI_REPLACE_METHOD(0x006E6237, CArchive::StoreWord);
EI_REPLACE_METHOD(0x006E625E, CArchive::StoreDword);
EI_REPLACE_METHOD(0x006E6284, CArchive::LoadByte);
EI_REPLACE_METHOD(0x006E62B3, CArchive::LoadWord);
EI_REPLACE_METHOD(0x006E62E6, CArchive::LoadDword);
EI_REPLACE(0x007084C0, ReadStringLength);
EI_REPLACE(0x00708447, StoreText);
EI_REPLACE(0x00708517, LoadText);
EI_REPLACE(0x00700B20, AssignWide);
EI_REPLACE(0x00700F63, WideToMultiByte);

} // namespace mfc::impl
