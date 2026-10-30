// The help / message window's page (HelpWindow::ShowPage): everything the
// window shows is rebuilt from the texts for page page (see ui.h).
// Rebirth's WideScreen patches it (a context hook: the pulsing frames of some tutorial messages follow the
// battle screen's panels to the screen's edges), reproduced here. Rebirth-patched callees: the surface's
// text, the sprites' Place, SetRectangle and SetFrame, the regions' SetRect. See docs/notes/items_spells.md.
#include "ui.h"

#include "text_manager.h"
#include "object_tables.h"      // FindCommandByKey
#include "ui_widescreen.h"

#include "ei/rebirth.h"
#include "lib/gameapi.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include "game_singletons.h"
#include "world.h"
#include "lib/str.h"
#include "lib/text_case.h"
#include "lib/win_user.h"

#include <algorithm>
#include <cstdlib>
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
constexpr float kTexel = 0.00390625f;

constexpr char kHelpAction = ';';                  // the help key's action (g_keyActions)
constexpr COLORREF kTitleColour = 0x82F5FF;
constexpr COLORREF kTextColour = 0xBBF9FF;
constexpr int kStripHeight = 55;                   // the title's and the buttons' strips
constexpr int kRowHeight = 16;
constexpr int kPictureSize = 64;
constexpr int kMaxPictures = 16;                   // the picture texture's cells
constexpr int kWrapFormat = DT_WORDBREAK | DT_EXPANDTABS;

// The body's texts: their left edge without a picture and beside one, and the room the original left
// on the right (the width the texts wrap to is the page's less both).
constexpr int kTextLeft = 15;
constexpr int kPictureTextLeft = 90;
constexpr int kTextRightMargin = 30;

// A "#text" entry of the page: the text's key, its picture's name (empty: none) and the text read
// for the key.
struct PageEntry {
    std::string key;
    std::string picture;
    std::string text;
};

void SetTint(UiMesh* sprite, float value) { sprite->tint[0] = sprite->tint[1] = sprite->tint[2] = value; }

void SetPlain(UiMesh* sprite, std::uint32_t colour) {
    sprite->texture = nullptr;
    sprite->flags |= FigureMeshFlag::Flat;
    sprite->flatColour = colour;
}

UiSurface* NewSurface(Interface3D* ui, int left, int top, int right, int bottom) {
    UiSurface* surface = new UiSurface(ui, left, top, right, bottom, SurfaceBlend::Keyed, true);
    return surface;
}

void SetRectangle(UiMesh* sprite, const RECT& rect, int u0, int v0, int u1, int v1) {
    sprite->SetRectangle(3.0f, 100.0f, static_cast<float>(rect.left), static_cast<float>(rect.top),
                         static_cast<float>(rect.right), static_cast<float>(rect.bottom), u0, v0, u1, v1);
}

// Rebirth's WideScreen (addon's handler, a context hook over the first AddSprite of a
// "#rectangle" line): on a message window (message), the page "<text>:<page>" of the
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

int TutorialFrameShift(const HelpWindow* window) {
    if (!widescreen::Active() || !window->message) return 0;
    const std::string page = std::string(window->text) + ':' + std::to_string(window->page);
    if (Listed(page, kLeftPanelPages, std::size(kLeftPanelPages))) return -widescreen::EdgeShift();
    if (Listed(page, kRightPanelPages, std::size(kRightPanelPages))) return widescreen::EdgeShift();
    return 0;
}

} // namespace

void HelpWindow::ShowPage() {
    pulsing.clear();
    pictures.clear();
    ClearSurfaces();
    ClearRegions();
    ClearSceneObjects();

    TextManager* texts = g_clientWorld.texts;
    std::vector<std::string> words;
    if (!texts->Open(std::string(kTutorialSection) + text)) {
        ui->SetPendingValue(0);
        return;
    }
    // The page's "#screen" line; the line after it is the page's title key.
    for (int i = page + 1; i != 0; --i) {
        if (!texts->FindLine(kScreenLine)) {
            ui->SetPendingValue(0);
            return;
        }
        texts->NextLine();
    }
    texts->NextLine();

    // The page's rectangle. "#position" is looked for from here on (not only in this page), and
    // the reading goes on from here afterwards.
    RECT pageArea{200, 50, 600, 560};
    bool rowsGiven = false;
    const char* resume = texts->cursor;
    if (texts->FindLine(kPositionLine)) {
        words = texts->Split(true);
        if (words.size() > 3) {
            pageArea.left = str::ToInt(words[1]);
            pageArea.top = str::ToInt(words[2]);
            pageArea.right = str::ToInt(words[3]);
            int rows = 1;
            if (words.size() > 4) {
                rows = str::ToInt(words[4]);
                rowsGiven = true;
            }
            if (pageArea.right - pageArea.left < 300) pageArea.right = pageArea.left + 300;
            pageArea.bottom = rows * kRowHeight + pageArea.top + 2 * kStripHeight;
            if (pageArea.bottom - pageArea.top > 600) pageArea.bottom = pageArea.top + 600;
        }
    }
    texts->cursor = resume;

    // Scene objects 0 the background, 1 the frame, 2-4 the previous, OK and next buttons.
    UiMesh* back = AddBlankSprite();
    SetPlain(back, message ? 0xFF282313 : 0xFF211B11);
    UiMesh* frame = AddBlankSprite();
    if (message) frame->textureOffsetV = 0.02734375f;
    UiMesh* previous = AddBlankSprite();
    if (page <= 0) previous->visible = false;
    SetTint(previous, 0.6f);
    UiMesh* ok = AddBlankSprite();
    if (page < static_cast<int>(lines.size()) - 1) SetTint(ok, 0.6f);
    UiMesh* next = AddBlankSprite();
    if (page >= static_cast<int>(lines.size()) - 1) next->visible = false;

    // Surface 0: the page's title and the topic's with the page's number.
    std::string titleText;
    const int width = pageArea.right - pageArea.left;
    UiSurface* header = NewSurface(ui, pageArea.left, pageArea.top, pageArea.right, pageArea.top + kStripHeight);
    AddSurface(header);
    header->Clear();
    RECT line{0, 8, width, 30};
    header->DrawTextIn(lines[page], &line, 2, kTitleColour, DT_CENTER, 1);
    line = RECT{0, 30, width, 50};
    titleText = str::Format(kTitleFormat, title, page + 1, static_cast<int>(lines.size()));
    header->DrawTextIn(titleText, &line, 1, kTitleColour, DT_CENTER, 1);

    // The page's lines up to the next "#screen": the pulsing frames and the texts.
    std::vector<PageEntry> entries;
    while (texts->FindHeader()) {
        words = texts->Split(true);
        if (text::CompareIgnoringCase(words[0], kScreenLine) == 0) break;
        if (text::CompareIgnoringCase(words[0], kRectangleLine) == 0 && words.size() > 4) {
            const int top = str::ToInt(words[2]);
            const int bottom = str::ToInt(words[4]);
            const int shift = TutorialFrameShift(this);
            const int left = str::ToInt(words[1]) + shift;
            const int right = str::ToInt(words[3]) + shift;
            UiMesh* mark = AddBlankSprite();
            mark->SetFrame(3.0f, 100.0f, static_cast<float>(left), static_cast<float>(top), static_cast<float>(right),
                           static_cast<float>(bottom), 5.0f, 0, 0, 256, 256);
            SetPlain(mark, 0x82FF0000);
            pulsing.push_back(mark);
        } else if (text::CompareIgnoringCase(words[0], kTextLine) == 0) {
            std::string picture;
            if (words.size() > 1) {
                picture = words[1];
            }
            // The next line's first word is the text's key.
            words = texts->Split(true);
            if (words.size() > 0) {
                const PageEntry entry{words[0], picture, {}};
                entries.push_back(entry);
            }
        }
    }

    // The texts, and the block they make on the last pass: its widest line, from the texts' left edge.
    // The block is centred in the page (the original left it 15 units from the page's left, with 30
    // units and the lines' ragged ends on the right); the texts wrap to the original's width, so the
    // lines break and the page is as high as before. A text has its picture's place on the last pass
    // when the first pass (none with a row count) made fewer than all the pictures' cells.
    int pictured = 0;
    for (PageEntry& entry : entries) {
        if (texts->Open(std::string(kTutorSection) + entry.key.data())) entry.text = texts->ReadToHash();
        str::TrimRight(entry.text, kLineEnds);
        if (entry.picture.size() != 0) ++pictured;
    }
    const int madePictures = rowsGiven ? 0 : (std::min)(pictured, kMaxPictures);
    int block = 0;
    for (const PageEntry& entry : entries) {
        const int left = entry.picture.size() != 0 && madePictures < kMaxPictures ? kPictureTextLeft : kTextLeft;
        for (std::string wrapped : ui->WrapText(entry.text, width - kTextRightMargin - left, 1)) {
            str::TrimRight(wrapped, ' ');
            block = (std::max)(block, left - kTextLeft + static_cast<int>(ui->TextExtent(wrapped, 1).cx));
        }
    }
    const int shift = (std::max)(0, (width - block) / 2 - kTextLeft);

    // Surface 1: the texts, one under the other. Without a row count the first pass makes the
    // pictures and measures; its surface is dropped and the page's bottom follows the height.
    int pictureCount = 0;
    for (int pass = rowsGiven ? 1 : 0; pass < 2; ++pass) {
        UiSurface* body = NewSurface(ui, pageArea.left, pageArea.top + kStripHeight, pageArea.right, pageArea.bottom - kStripHeight);
        AddSurface(body);
        RECT row{kTextLeft + shift, 0, width - kTextRightMargin + shift, kRowHeight};
        int y = 0;
        body->Clear();
        for (const PageEntry& entry : entries) {
            bool withPicture = false;
            if (entry.picture.size() != 0 && pictureCount < kMaxPictures) {
                if (pass == 0) {
                    // The picture's cell in referenced object 1, four to a column.
                    const int cellX = pictureCount / 4 * kPictureSize;
                    const int cellY = pictureCount % 4 * kPictureSize;
                    {
                        std::string name = std::string(kPicturePrefix) + entry.picture.data();
                        CopyImage(objects[1], name, kPictureSize, kPictureSize, kPictureSize, kPictureSize, cellX,
                                  cellY);
                    }
                    UiMesh* picture = AddBlankSprite();
                    picture->SetRectangle(3.0f, 100.0f, 0.0f, 0.0f, 64.0f, 64.0f, 0, 0, kPictureSize, kPictureSize);
                    picture->textureOffsetU = cellX * kTexel;
                    picture->textureOffsetV = (cellY + kPictureSize) * kTexel;
                    picture->Place(static_cast<float>(pageArea.left + 47 + shift),
                                        static_cast<float>(y + pageArea.top + 95), 3.0f);
                    picture->texture = objects[1];
                    pictures.push_back(picture);
                    ++pictureCount;
                }
                row.left = kPictureTextLeft + shift;
                withPicture = true;
            } else {
                row.left = kTextLeft + shift;
            }
            row.top = y;
            row.bottom = y + kRowHeight;
            int height = body->DrawWrappedText(entry.text, &row, 100, 1, kTextColour, kWrapFormat, 1) * kRowHeight;
            // A text with a picture is at least as high as the picture's place.
            if (withPicture && height <= 80) height = 80;
            y += height;
        }
        if (pass == 0) {
            RemoveSurfaceAt(1);
            pageArea.bottom = y + pageArea.top + 2 * kStripHeight;
        }
    }

    SetRectangle(back, pageArea, 0, 0, 256, 256);
    frame->SetFrame(3.0f, 100.0f, static_cast<float>(pageArea.left - 5), static_cast<float>(pageArea.top - 5),
                    static_cast<float>(pageArea.right + 5), static_cast<float>(pageArea.bottom + 5), 5.0f, 2, 0xF5, 0xB4, 0xF0);

    // The three buttons along the bottom, each with its region.
    RECT button{0, pageArea.bottom - 30, 50, pageArea.bottom - 10};
    const int gap = (width - 150) / 6;
    gameapi::OffsetRect(&button, gap + pageArea.left, 0);
    const int step = 2 * gap - button.left + button.right;
    AddRegion()->SetRectFrom(button);
    SetRectangle(previous, button, 2, 0x33, 0x34, 0x47);
    gameapi::OffsetRect(&button, step, 0);
    UiRegion* okRegion = AddRegion();
    okRegion->SetRectFrom(button);
    okRegion->SetTip(0x7594);
    SetRectangle(ok, button, 2, 0x5D, 0x34, 0x71);
    gameapi::OffsetRect(&button, step, 0);
    AddRegion()->SetRectFrom(button);
    SetRectangle(next, button, 2, 0x48, 0x34, 0x5C);

    // Surface 2: the neighbouring pages' titles under the arrows and the help key's name.
    UiSurface* footer = NewSurface(ui, pageArea.left, pageArea.bottom - kStripHeight, pageArea.right, pageArea.bottom);
    AddSurface(footer);
    footer->Clear();
    if (page > 0) {
        RECT label{0, 5, width / 3, 25};
        footer->DrawTextIn(lines[page - 1], &label, 0, kTitleColour, DT_CENTER, 1);
        regions[0]->SetTip(0x7595);
    }
    if (const char* key = static_cast<const char*>(FindCommandByKey(kHelpAction))) {
        std::string hint = texts->GetString(kHelpKeyText);
        hint += kSpace;
        hint += key;
        RECT label{width / 3, 5, 2 * width / 3, 25};
        footer->DrawTextIn(hint, &label, 0, kTitleColour, DT_CENTER, 1);
    }
    if (page < static_cast<int>(lines.size()) - 1) {
        RECT label{2 * width / 3, 5, width, 25};
        footer->DrawTextIn(lines[page + 1], &label, 0, kTitleColour, DT_CENTER, 1);
        regions[2]->SetTip(0x7596);
    }

    entries.clear();
}
