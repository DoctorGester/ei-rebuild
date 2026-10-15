// The help / message window's page (CHelpWindow::ShowPage, 00659420; A-H26): everything the
// window shows is rebuilt from the texts for page page_ (see ui.h).
// Rebirth's WideScreen patches it (a context hook at +574: the pulsing frames of some tutorial
// messages follow the battle screen's panels to the screen's edges), which this reproduces, so it
// installs in both launches; our callers keep the forwarder in ui_help_window.cpp (rule 9). Patched callees through their
// addresses: the surface's text 005FDE20, the sprites' Place 005E6FA0, SetRectangle 005E70E0 and
// SetFrame 005E7D60, the regions' SetRect 005FC9E0. See docs/notes/items_spells.md ("A-H26").
#include "ui.h"
#include "game_strings.h"

#include "item_db_text.h"
#include "object_tables.h"      // FindCommandByKey
#include "text_reader.h"
#include "ui_gdi.h"
#include "ui_widescreen.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "originals_library.h"  // GameAtoi
#include "originals_ui.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace {

constexpr std::uintptr_t kTutorialSection = std::uintptr_t(0x00794A78);   // "tutorial "
constexpr std::uintptr_t kScreenLine = std::uintptr_t(0x00794A70);        // "#screen"
constexpr std::uintptr_t kPositionLine = std::uintptr_t(0x007941E0);      // "#position"
constexpr std::uintptr_t kRectangleLine = std::uintptr_t(0x00794ACC);     // "#rectangle"
constexpr std::uintptr_t kTextLine = std::uintptr_t(0x00794AC4);          // "#text"
constexpr std::uintptr_t kTitleFormat = std::uintptr_t(0x00794AD8);       // "%s (%d/%d)"
constexpr std::uintptr_t kPicturePrefix = std::uintptr_t(0x00794AB8);     // "tutorial_"
constexpr std::uintptr_t kTutorSection = std::uintptr_t(0x00794A68);      // "tutor "
constexpr std::uintptr_t kLineEnds = std::uintptr_t(0x00794AB4);          // "\n\r"
constexpr std::uintptr_t kHelpKeyText = std::uintptr_t(0x00794AA0);       // "show_tutorial_key"
constexpr std::uintptr_t kSpace = std::uintptr_t(0x0078DA40);             // " "
// One texel of the 256-pixel picture texture.
float Texel() { return *reinterpret_cast<const float*>(std::uintptr_t(0x0073EEB8)); }

constexpr char kHelpAction = ';';                  // the help key's action (KeyActions)
constexpr std::uint32_t kPlainColour = 0x100;      // CUiMesh::flags_: drawn in plainColour_
constexpr COLORREF kTitleColour = 0x82F5FF;
constexpr COLORREF kTextColour = 0xBBF9FF;
constexpr int kStripHeight = 55;                   // the title's and the buttons' strips
constexpr int kRowHeight = 16;
constexpr int kPictureSize = 64;
constexpr int kMaxPictures = 16;                   // the picture texture's cells
constexpr int kSurfaceFormat = 5;
constexpr int kWrapFormat = DT_WORDBREAK | DT_EXPANDTABS;

// A "#text" entry of the page: the text's key and its picture's name (empty: none). The original
// keeps an STLport list of pair<string, string>.
struct PageEntry {
    stlp::string key;
    stlp::string picture;
};

bool Same(const char* word, std::uintptr_t text) {
    return gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(word),
                            reinterpret_cast<const unsigned char*>(game::Text(text))) == 0;
}

void SetTint(CUiMesh* sprite, float value) { sprite->tint_[0] = sprite->tint_[1] = sprite->tint_[2] = value; }

void SetPlain(CUiMesh* sprite, std::uint32_t colour) {
    sprite->texture_ = nullptr;
    sprite->flags_ |= kPlainColour;
    sprite->plainColour_ = colour;
}

CUiSurface* NewSurface(CInterface3D* ui, int left, int top, int right, int bottom) {
    void* memory = gamecrt::operator_new(sizeof(CUiSurface));
    return memory ? static_cast<CUiSurface*>(memory)->Construct(ui, left, top, right, bottom, kSurfaceFormat, 1) : nullptr;
}

void SetRectangle(CUiMesh* sprite, const RECT& rect, int u0, int v0, int u1, int v1) {
    sprite->SetRectangle(3.0f, 100.0f, static_cast<float>(rect.left), static_cast<float>(rect.top),
                         static_cast<float>(rect.right), static_cast<float>(rect.bottom), u0, v0, u1, v1);
}

void SetRegion(CUiRegion* region, const RECT& rect) { region->CallSetRect(rect.left, rect.top, rect.right, rect.bottom); }

// Rebirth's WideScreen (addon's handler 10023702, a context hook over the first AddSprite of a
// "#rectangle" line at 00659994): on a message window (message_), the page "<text_>:<page_>" of the
// zone tutorials whose frames point at the battle screen's left panels moves them left by the
// layout's extra units, of those pointing at its right panels right (addon's two string sets, built
// once from its literals 100AF5F0..100AF668; the frame's left and right, the original's locals
// [ESP+0x64] / [ESP+0x58], change; top and bottom do not).
constexpr const char* kLeftPanelPages[] = {"zt9:0", "zt12:0", "zt12:1", "zt13:0", "zt14:0", "zt14:2", "zt17:0", "zt20:0", "zt22:0"};
constexpr const char* kRightPanelPages[] = {"zt3:2", "zt4:0", "zt4:1", "zt7:0", "zt10:0", "zt10:1", "zt18:0"};

bool Listed(const std::string& page, const char* const* names, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i)
        if (page == names[i]) return true;
    return false;
}

int TutorialFrameShift(const CHelpWindow* window) {
    if (!widescreen::Active() || !window->message_) return 0;
    const std::string page = std::string(static_cast<const char*>(window->text_)) + ':' + std::to_string(window->page_);
    if (Listed(page, kLeftPanelPages, std::size(kLeftPanelPages))) return -widescreen::EdgeShift();
    if (Listed(page, kRightPanelPages, std::size(kRightPanelPages))) return widescreen::EdgeShift();
    return 0;
}

} // namespace

void CHelpWindow::ShowPageVanilla() {
    pulsing_.ClearWith([](CUiMesh**) {});
    pictures_.ClearWith([](CUiMesh**) {});
    ClearSurfaces();
    ClearRegions();
    ClearSceneObjects();

    CTextManager* texts = GameTexts();
    console::CStringArray words;
    BOOL found;
    {
        mfc::CString section = mfc::CString(game::Text(kTutorialSection)) + text_;
        found = TextOpen(texts, nullptr, section);
    }
    if (!found) {
        interface_->SetPendingValue(0);
        return;
    }
    // The page's "#screen" line; the line after it is the page's title key.
    for (int i = page_ + 1; i != 0; --i) {
        if (!TextFindLine(texts, nullptr, game::Text(kScreenLine))) {
            interface_->SetPendingValue(0);
            return;
        }
        TextNextLine(texts);
    }
    TextNextLine(texts);

    // The page's rectangle. "#position" is looked for from here on (not only in this page), and
    // the reading goes on from here afterwards.
    RECT page{200, 50, 600, 560};
    bool rowsGiven = false;
    const char* resume = texts->cursor_;
    if (TextFindLine(texts, nullptr, game::Text(kPositionLine))) {
        TextSplit(texts, nullptr, &words, 1);
        if (words.GetSize() > 3) {
            page.left = GameAtoi(words[1]);
            page.top = GameAtoi(words[2]);
            page.right = GameAtoi(words[3]);
            int rows = 1;
            if (words.GetSize() > 4) {
                rows = GameAtoi(words[4]);
                rowsGiven = true;
            }
            if (page.right - page.left < 300) page.right = page.left + 300;
            page.bottom = rows * kRowHeight + page.top + 2 * kStripHeight;
            if (page.bottom - page.top > 600) page.bottom = page.top + 600;
        }
    }
    texts->cursor_ = resume;

    // Scene objects 0 the background, 1 the frame, 2-4 the previous, OK and next buttons.
    CUiMesh* back = AddSprite(nullptr);
    SetPlain(back, message_ ? 0xFF282313 : 0xFF211B11);
    CUiMesh* frame = AddSprite(nullptr);
    if (message_) frame->vOffset_ = 0.02734375f;
    CUiMesh* previous = AddSprite(nullptr);
    if (page_ <= 0) previous->visible_ = 0;
    SetTint(previous, 0.6f);
    CUiMesh* ok = AddSprite(nullptr);
    if (page_ < lines_.size - 1) SetTint(ok, 0.6f);
    CUiMesh* next = AddSprite(nullptr);
    if (page_ >= lines_.size - 1) next->visible_ = 0;

    // Surface 0: the page's title and the topic's with the page's number.
    mfc::CString text;
    const int width = page.right - page.left;
    CUiSurface* header = NewSurface(interface_, page.left, page.top, page.right, page.top + kStripHeight);
    AddSurface(header);
    SurfaceGetDC(header);
    header->Clear();
    RECT line{0, 8, width, 30};
    SurfaceDrawText(header, nullptr, lines_.data[page_], &line, 2, kTitleColour, DT_CENTER, 1);
    line = RECT{0, 30, width, 50};
    text.Format(game::Text(kTitleFormat), static_cast<const char*>(title_), page_ + 1, lines_.size);
    SurfaceDrawText(header, nullptr, text, &line, 1, kTitleColour, DT_CENTER, 1);
    SurfaceReleaseDC(header);

    // The page's lines up to the next "#screen": the pulsing frames and the texts.
    stlp::list<PageEntry> entries;
    entries.Construct();
    while (TextFindLine(texts, nullptr, nullptr)) {
        TextSplit(texts, nullptr, &words, 1);
        if (Same(words[0], kScreenLine)) break;
        if (Same(words[0], kRectangleLine) && words.GetSize() > 4) {
            const int top = GameAtoi(words[2]);
            const int bottom = GameAtoi(words[4]);
            const int shift = TutorialFrameShift(this);
            const int left = GameAtoi(words[1]) + shift;
            const int right = GameAtoi(words[3]) + shift;
            CUiMesh* mark = AddSprite(nullptr);
            mark->SetFrame(3.0f, 100.0f, static_cast<float>(left), static_cast<float>(top), static_cast<float>(right),
                           static_cast<float>(bottom), 5.0f, 0, 0, 256, 256);
            SetPlain(mark, 0x82FF0000);
            pulsing_.PushBack(mark);
        } else if (Same(words[0], kTextLine)) {
            stlp::string picture;
            stlp::string::Reserve(picture, 8);
            if (words.GetSize() > 1) {
                const char* name = words[1];
                picture.Assign(name, name + std::strlen(name));
            }
            // The next line's first word is the text's key.
            TextSplit(texts, nullptr, &words, 1);
            if (words.GetSize() > 0) {
                const char* key = words[0];
                const PageEntry entry{stlp::string(key), picture};
                entries.PushBack(entry);
            }
        }
    }

    // Surface 1: the texts, one under the other. Without a row count the first pass makes the
    // pictures and measures; its surface is dropped and the page's bottom follows the height.
    int pictures = 0;
    for (int pass = rowsGiven ? 1 : 0; pass < 2; ++pass) {
        CUiSurface* body = NewSurface(interface_, page.left, page.top + kStripHeight, page.right, page.bottom - kStripHeight);
        AddSurface(body);
        RECT row{15, 0, width - 30, kRowHeight};
        int y = 0;
        SurfaceGetDC(body);
        body->Clear();
        for (auto* node = entries.First(); node != entries.End(); node = node->next) {
            const PageEntry& entry = node->data;
            bool withPicture = false;
            if (entry.picture.size() != 0 && pictures < kMaxPictures) {
                if (pass == 0) {
                    // The picture's cell in referenced object 1, four to a column.
                    const int cellX = pictures / 4 * kPictureSize;
                    const int cellY = pictures % 4 * kPictureSize;
                    {
                        mfc::CString name = mfc::CString(game::Text(kPicturePrefix)) + entry.picture.data();
                        CopyImage(objects_.data[1], name, kPictureSize, kPictureSize, kPictureSize, kPictureSize, cellX,
                                  cellY, 0, 0);
                    }
                    CUiMesh* picture = AddSprite(nullptr);
                    picture->SetRectangle(3.0f, 100.0f, 0.0f, 0.0f, 64.0f, 64.0f, 0, 0, kPictureSize, kPictureSize);
                    picture->uOffset_ = static_cast<float>(cellX) * Texel();
                    picture->vOffset_ = static_cast<float>(cellY + kPictureSize) * Texel();
                    SpritePlaceOriginal(picture, nullptr, static_cast<float>(page.left + 47),
                                        static_cast<float>(y + page.top + 95), 3.0f);
                    picture->texture_ = objects_.data[1];
                    pictures_.PushBack(picture);
                    ++pictures;
                }
                row.left = 90;
                withPicture = true;
            } else {
                row.left = 15;
            }
            row.top = y;
            row.bottom = y + kRowHeight;
            text.Empty();
            BOOL open;
            {
                mfc::CString section = mfc::CString(game::Text(kTutorSection)) + entry.key.data();
                open = TextOpen(texts, nullptr, section);
            }
            if (open) TextReadToHash(texts, nullptr, &text, 1);
            text.TrimRight(game::Text(kLineEnds));
            int height = body->DrawWrappedText(text, &row, 100, 1, kTextColour, kWrapFormat, 1) * kRowHeight;
            // A text with a picture is at least as high as the picture's place.
            if (withPicture && height <= 80) height = 80;
            y += height;
        }
        SurfaceReleaseDC(body);
        if (pass == 0) {
            RemoveSurfaceAt(1);
            page.bottom = y + page.top + 2 * kStripHeight;
        }
    }

    SetRectangle(back, page, 0, 0, 256, 256);
    frame->SetFrame(3.0f, 100.0f, static_cast<float>(page.left - 5), static_cast<float>(page.top - 5),
                    static_cast<float>(page.right + 5), static_cast<float>(page.bottom + 5), 5.0f, 2, 0xF5, 0xB4, 0xF0);

    // The three buttons along the bottom, each with its region.
    RECT button{0, page.bottom - 30, 50, page.bottom - 10};
    const int gap = (width - 150) / 6;
    gameapi::OffsetRect(&button, gap + page.left, 0);
    const int step = 2 * gap - button.left + button.right;
    SetRegion(AddRegion(), button);
    SetRectangle(previous, button, 2, 0x33, 0x34, 0x47);
    gameapi::OffsetRect(&button, step, 0);
    CUiRegion* okRegion = AddRegion();
    SetRegion(okRegion, button);
    okRegion->SetTip(0x7594, 1, 0);
    SetRectangle(ok, button, 2, 0x5D, 0x34, 0x71);
    gameapi::OffsetRect(&button, step, 0);
    SetRegion(AddRegion(), button);
    SetRectangle(next, button, 2, 0x48, 0x34, 0x5C);

    // Surface 2: the neighbouring pages' titles under the arrows and the help key's name.
    CUiSurface* footer = NewSurface(interface_, page.left, page.bottom - kStripHeight, page.right, page.bottom);
    AddSurface(footer);
    SurfaceGetDC(footer);
    footer->Clear();
    if (page_ > 0) {
        RECT label{0, 5, width / 3, 25};
        SurfaceDrawText(footer, nullptr, lines_.data[page_ - 1], &label, 0, kTitleColour, DT_CENTER, 1);
        regions_.data[0]->SetTip(0x7595, 1, 0);
    }
    if (const char* key = static_cast<const char*>(FindCommandByKey(kHelpAction))) {
        mfc::CString hint;
        texts->GetString(game::Text(kHelpKeyText), &hint);
        hint += game::Text(kSpace);
        hint += key;
        RECT label{width / 3, 5, 2 * width / 3, 25};
        SurfaceDrawText(footer, nullptr, hint, &label, 0, kTitleColour, DT_CENTER, 1);
    }
    if (page_ < lines_.size - 1) {
        RECT label{2 * width / 3, 5, width, 25};
        SurfaceDrawText(footer, nullptr, lines_.data[page_ + 1], &label, 0, kTitleColour, DT_CENTER, 1);
        regions_.data[2]->SetTip(0x7596, 1, 0);
    }
    SurfaceReleaseDC(footer);

    entries.ClearWith([](PageEntry* entry) { entry->~PageEntry(); });
    stlp::deallocate(entries.head, sizeof(stlp::list<PageEntry>::Node));
}
EI_REPLACE_METHOD(0x00659420, CHelpWindow::ShowPageVanilla);
EI_REBIRTH_IMPLEMENTS(0x00659420, ei::rebirth::Patch::WideScreen);
