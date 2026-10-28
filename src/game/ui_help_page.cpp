// The help / message window's page (CHelpWindow::ShowPage): everything the
// window shows is rebuilt from the texts for page page_ (see ui.h).
// Rebirth's WideScreen patches it (a context hook: the pulsing frames of some tutorial messages follow the
// battle screen's panels to the screen's edges), reproduced here. Rebirth-patched callees: the surface's
// text, the sprites' Place, SetRectangle and SetFrame, the regions' SetRect. See docs/notes/items_spells.md.
#include "ui.h"
#include "game_strings.h"

#include "item_db_text.h"
#include "object_tables.h"      // FindCommandByKey
#include "text_reader.h"
#include "ui_gdi.h"
#include "ui_widescreen.h"

#include "ei/rebirth.h"
#include "lib/gameapi.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include "game_data.h"
#include "game_singletons.h"
#include "lib/str.h"
#include "lib/text_case.h"

#include <cstdlib>
#include <list>
#include <vector>

namespace {

constexpr const char* kTutorialSection = "tutorial ";
constexpr const char* kScreenLine = "#screen";
constexpr const char* kPositionLine = "#position";
constexpr const char* kRectangleLine = "#rectangle";
constexpr const char* kTextLine = "#text";
constexpr const char* kTitleFormat = "%s (%d/%d)";
constexpr const char* kPicturePrefix = "tutorial_";
constexpr const char* kTutorSection = "tutor ";
constexpr const char* kLineEnds = "\n\r";
constexpr const char* kHelpKeyText = "show_tutorial_key";
constexpr const char* kSpace = " ";
// One texel of the 256-pixel picture texture.
float Texel() { return 0.00390625f /*0073EEB8*/; }

constexpr char kHelpAction = ';';                  // the help key's action (KeyActions)
constexpr std::uint32_t kPlainColour = 0x100;      // CUiMesh::flags_: drawn in flatColour_
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
    std::string key;
    std::string picture;
};

bool Same(std::string_view word, std::string_view text) {
    return text::CompareIgnoringCase(word,
                           text) == 0;
}

void SetTint(CUiMesh* sprite, float value) { sprite->tint_[0] = sprite->tint_[1] = sprite->tint_[2] = value; }

void SetPlain(CUiMesh* sprite, std::uint32_t colour) {
    sprite->texture_ = nullptr;
    sprite->flags_ |= kPlainColour;
    sprite->flatColour_ = colour;
}

CUiSurface* NewSurface(CInterface3D* ui, int left, int top, int right, int bottom) {
    return new CUiSurface(ui, left, top, right, bottom, kSurfaceFormat, 1);
}

void SetRectangle(CUiMesh* sprite, const RECT& rect, int u0, int v0, int u1, int v1) {
    sprite->SetRectangle(3.0f, 100.0f, static_cast<float>(rect.left), static_cast<float>(rect.top),
                         static_cast<float>(rect.right), static_cast<float>(rect.bottom), u0, v0, u1, v1);
}

void SetRegion(CUiRegion* region, const RECT& rect) { region->SetRect(rect.left, rect.top, rect.right, rect.bottom); }

// Rebirth's WideScreen (addon's handler, a context hook over the first AddSprite of a
// "#rectangle" line): on a message window (message_), the page "<text_>:<page_>" of the
// zone tutorials whose frames point at the battle screen's left panels moves them left by the
// layout's extra units, of those pointing at its right panels right (addon's two string sets, built
// once from its literals; the frame's left and right, the original's locals, change; top and bottom do not).
constexpr const char* kLeftPanelPages[] = {"zt9:0", "zt12:0", "zt12:1", "zt13:0", "zt14:0", "zt14:2", "zt17:0", "zt20:0", "zt22:0"};
constexpr const char* kRightPanelPages[] = {"zt3:2", "zt4:0", "zt4:1", "zt7:0", "zt10:0", "zt10:1", "zt18:0"};

bool Listed(const std::string& page, const char* const* names, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i)
        if (page == names[i]) return true;
    return false;
}

int TutorialFrameShift(const CHelpWindow* window) {
    if (!widescreen::Active() || !window->message_) return 0;
    const std::string page = std::string(window->text_.c_str()) + ':' + std::to_string(window->page_);
    if (Listed(page, kLeftPanelPages, std::size(kLeftPanelPages))) return -widescreen::EdgeShift();
    if (Listed(page, kRightPanelPages, std::size(kRightPanelPages))) return widescreen::EdgeShift();
    return 0;
}

} // namespace

void CHelpWindow::ShowPage() {
    pulsing_.clear();
    pictures_.clear();
    ClearSurfaces();
    ClearRegions();
    ClearSceneObjects();

    CTextManager* texts = TheTextManager();
    std::vector<std::string> words;
    BOOL found;
    {
        std::string section = std::string(kTutorialSection) + text_;
        found = texts->Open(section.c_str());
    }
    if (!found) {
        interface_->SetPendingValue(0);
        return;
    }
    // The page's "#screen" line; the line after it is the page's title key.
    for (int i = page_ + 1; i != 0; --i) {
        if (!texts->FindLine(kScreenLine)) {
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
    if (texts->FindLine(kPositionLine)) {
        texts->Split(&words, 1);
        if (words.size() > 3) {
            page.left = std::atoi(words[1].c_str());
            page.top = std::atoi(words[2].c_str());
            page.right = std::atoi(words[3].c_str());
            int rows = 1;
            if (words.size() > 4) {
                rows = std::atoi(words[4].c_str());
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
    if (message_) frame->textureOffsetV_ = 0.02734375f;
    CUiMesh* previous = AddSprite(nullptr);
    if (page_ <= 0) previous->visible_ = 0;
    SetTint(previous, 0.6f);
    CUiMesh* ok = AddSprite(nullptr);
    if (page_ < static_cast<int>(lines_.size()) - 1) SetTint(ok, 0.6f);
    CUiMesh* next = AddSprite(nullptr);
    if (page_ >= static_cast<int>(lines_.size()) - 1) next->visible_ = 0;

    // Surface 0: the page's title and the topic's with the page's number.
    std::string text;
    const int width = page.right - page.left;
    CUiSurface* header = NewSurface(interface_, page.left, page.top, page.right, page.top + kStripHeight);
    AddSurface(header);
    UiSurfaceGetDC(header);
    header->Clear();
    RECT line{0, 8, width, 30};
    header->DrawTextIn(lines_[page_], &line, 2, kTitleColour, DT_CENTER, 1);
    line = RECT{0, 30, width, 50};
    text = str::Format(kTitleFormat, title_.c_str(), page_ + 1, static_cast<int>(lines_.size()));
    header->DrawTextIn(text.c_str(), &line, 1, kTitleColour, DT_CENTER, 1);
    UiSurfaceReleaseDC(header);

    // The page's lines up to the next "#screen": the pulsing frames and the texts.
    std::list<PageEntry> entries;
    while (texts->FindLine(nullptr)) {
        texts->Split(&words, 1);
        if (Same(words[0], kScreenLine)) break;
        if (Same(words[0], kRectangleLine) && words.size() > 4) {
            const int top = std::atoi(words[2].c_str());
            const int bottom = std::atoi(words[4].c_str());
            const int shift = TutorialFrameShift(this);
            const int left = std::atoi(words[1].c_str()) + shift;
            const int right = std::atoi(words[3].c_str()) + shift;
            CUiMesh* mark = AddSprite(nullptr);
            mark->SetFrame(3.0f, 100.0f, static_cast<float>(left), static_cast<float>(top), static_cast<float>(right),
                           static_cast<float>(bottom), 5.0f, 0, 0, 256, 256);
            SetPlain(mark, 0x82FF0000);
            pulsing_.push_back(mark);
        } else if (Same(words[0], kTextLine)) {
            std::string picture;
            if (words.size() > 1) {
                const char* name = words[1].c_str();
                picture.assign(name, name + std::strlen(name));
            }
            // The next line's first word is the text's key.
            texts->Split(&words, 1);
            if (words.size() > 0) {
                const char* key = words[0].c_str();
                const PageEntry entry{std::string(key), picture};
                entries.push_back(entry);
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
        UiSurfaceGetDC(body);
        body->Clear();
        for (const PageEntry& entry : entries) {
            bool withPicture = false;
            if (entry.picture.size() != 0 && pictures < kMaxPictures) {
                if (pass == 0) {
                    // The picture's cell in referenced object 1, four to a column.
                    const int cellX = pictures / 4 * kPictureSize;
                    const int cellY = pictures % 4 * kPictureSize;
                    {
                        std::string name = std::string(kPicturePrefix) + entry.picture.data();
                        CopyImage(objects_[1], name.c_str(), kPictureSize, kPictureSize, kPictureSize, kPictureSize, cellX,
                                  cellY, 0, 0);
                    }
                    CUiMesh* picture = AddSprite(nullptr);
                    picture->SetRectangle(3.0f, 100.0f, 0.0f, 0.0f, 64.0f, 64.0f, 0, 0, kPictureSize, kPictureSize);
                    picture->textureOffsetU_ = static_cast<float>(cellX) * Texel();
                    picture->textureOffsetV_ = static_cast<float>(cellY + kPictureSize) * Texel();
                    picture->PlaceBody(static_cast<float>(page.left + 47),
                                        static_cast<float>(y + page.top + 95), 3.0f);
                    picture->texture_ = objects_[1];
                    pictures_.push_back(picture);
                    ++pictures;
                }
                row.left = 90;
                withPicture = true;
            } else {
                row.left = 15;
            }
            row.top = y;
            row.bottom = y + kRowHeight;
            text.clear();
            BOOL open;
            {
                std::string section = std::string(kTutorSection) + entry.key.data();
                open = texts->Open(section.c_str());
            }
            if (open) texts->ReadToHash(&text, 1);
            str::TrimRight(text, kLineEnds);
            int height = body->DrawWrappedText(text.c_str(), &row, 100, 1, kTextColour, kWrapFormat, 1) * kRowHeight;
            // A text with a picture is at least as high as the picture's place.
            if (withPicture && height <= 80) height = 80;
            y += height;
        }
        UiSurfaceReleaseDC(body);
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
    UiSurfaceGetDC(footer);
    footer->Clear();
    if (page_ > 0) {
        RECT label{0, 5, width / 3, 25};
        footer->DrawTextIn(lines_[page_ - 1], &label, 0, kTitleColour, DT_CENTER, 1);
        regions_[0]->SetTip(0x7595, 1, 0);
    }
    if (const char* key = static_cast<const char*>(FindCommandByKey(kHelpAction))) {
        std::string hint;
        texts->GetString(kHelpKeyText, &hint);
        hint += kSpace;
        hint += key;
        RECT label{width / 3, 5, 2 * width / 3, 25};
        footer->DrawTextIn(hint.c_str(), &label, 0, kTitleColour, DT_CENTER, 1);
    }
    if (page_ < static_cast<int>(lines_.size()) - 1) {
        RECT label{2 * width / 3, 5, width, 25};
        footer->DrawTextIn(lines_[page_ + 1], &label, 0, kTitleColour, DT_CENTER, 1);
        regions_[2]->SetTip(0x7596, 1, 0);
    }
    UiSurfaceReleaseDC(footer);

    entries.clear();
}
