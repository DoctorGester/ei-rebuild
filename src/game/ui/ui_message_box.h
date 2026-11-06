#pragma once

// The message box and the difficulty question.

#include "game/ui/ui.h"
#include "game/ui/ui_edit_field.h"

// The buttons of a message box.
enum class MessageButtons : std::uint32_t {
    Ok = 1,
    Cancel = 2,         // the cancel button alone (region 1; region 0 stays empty)
    OkCancel = 3,
    OkCancelEdit = 7,   // OK and cancel under an edit field instead of the text (its initial text is the text key itself)
};

// Which question a message box asks: the high half of what it leaves pending, so that the window below tells its
// boxes apart (each window numbers its own, from First).
enum class MessageQuestion : std::uint32_t {
    First = 0x10000,
    Second = 0x20000,
    Third = 0x30000,
    Fourth = 0x40000,
    Fifth = 0x50000,
    Disconnect = 0x990000,   // the multiplayer client lost its server
    Exit = 0xFFFF0000,       // the end of the game: OK plays the exit movie and the game quits
};

// What was pressed: the region's number plus one.
enum class MessageAnswer : std::uint32_t { Ok = 1, Cancel = 2 };

// What a box of `question` leaves pending (Interface3D::SetPendingValue) for the window below: its MessageResult.
constexpr std::uint32_t BoxResult(MessageQuestion question, MessageAnswer answer) {
    return static_cast<std::uint32_t>(question) | static_cast<std::uint32_t>(answer);
}

// The answers the battle and briefing screens take from their boxes.
inline constexpr std::uint32_t kLeaveZoneYes = BoxResult(MessageQuestion::First, MessageAnswer::Ok);     // "leave_zone"

inline constexpr std::uint32_t kGameOverLoad = BoxResult(MessageQuestion::Second, MessageAnswer::Ok);    // "game_over"

inline constexpr std::uint32_t kGameOverQuit = BoxResult(MessageQuestion::Second, MessageAnswer::Cancel);

// The message box.
class MessageWindow : public UiContainer {
public:
    // A missing text gives its key. The text is centred and word-wrapped.
    MessageWindow(std::string_view titleKey, const char* textKey, MessageQuestion question, MessageButtons buttons);
    void CreateScreen() override;
    void OnEvent(UiEvent& event) override;
    // OK: for the exit question the exit movie and the game quits; otherwise the edit field's text into editText.
    // OK or cancel then leaves BoxResult(question, Ok / Cancel) pending.
    void LeftDown(UiEvent& event);
    // Unless a child used the key, Enter answers as OK and Esc as cancel.
    void KeyDown(UiEvent& event);
    // Into the caller's buffer.
    void CopyEditText();
    // Also the master request box's and the difficulty question's.
    void CloseScreen() override;

    std::string title;
    std::string text;
    MessageQuestion question = {};   // Exit: the text in font 1, else 2
    MessageButtons buttons = {};
    std::vector<UiLabel> labels;     // the surface's paint: the title and the text (CreateScreen measures and places them)
    UiEditField* edit = nullptr;    // the edit field (OkCancelEdit)
    char* editText = nullptr;        // the edit field's first text; OK copies the typed one here
    UiRegion* okButton = nullptr;    // null in a cancel-only box
    UiRegion* cancelButton = nullptr;   // null in an OK-only box
};

// The new game's difficulty question (the main menu's new game button): a message box ("option_difficulty", OK and
// cancel) with two check boxes. Its button handler sets easy from the boxes and copies it to
// options::g_options.difficulty on OK.
class DifficultyBox : public MessageWindow {
public:
    DifficultyBox() : MessageWindow("option_difficulty", "", MessageQuestion::Second, MessageButtons::OkCancel) {}
    void OnEvent(UiEvent& event) override;
    // No container handler. OK (easy to options::g_options.difficulty), cancel, or a check box checked.
    void LeftDown(UiEvent& event);
    // Unless a child used the key, Enter answers as OK and Esc as cancel.
    void KeyDown(UiEvent& event);
    // The texture shift of a checked box (0xBDB80000).
    static constexpr float kCheckedOffset = -0.08984375f;

    // Hard checked first.
    void CreateScreen() override;

    int easy = 0;                    // 1 easy, 0 hard (the default)
    UiMesh* easyBox = nullptr;       // the check boxes' sprites and regions
    UiMesh* hardBox = nullptr;
    UiRegion* easyArea = nullptr;
    UiRegion* hardArea = nullptr;
};
