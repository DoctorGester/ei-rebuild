#pragma once

// The help / message window.

#include "game/ui/ui.h"

// The help / message window (Interface3D::ShowMessage and OpenHelp). Its pages come from the texts:
// "tutorial <text>" holds the title key and a "#screen" line per page, and "tutor <key>" each key's text.
class HelpWindow : public UiContainer {
public:
    HelpWindow(bool newMessage, const std::string& newText, int newPage)
        : message(newMessage), text(newText), page(newPage) {}
    void DrawSprites(SceneRenderer* pass) override;
    // The pulsing frames, pictures, surfaces, regions and sprites dropped.
    void CloseScreen() override;
    // Without the text or without pages the interface's pending value is set to 0 and nothing else happens.
    void CreateScreen() override;
    // The pulse: v = (cos(5 t) + 1) / 2 gives alpha 255 * v and the colour 80 * (1 - v).
    void Update() override;
    void OnEvent(UiEvent& event) override;
    // The previous page, OK (Finish) or the next page.
    void LeftDown(UiEvent& event);
    // Enter the next page (Finish on the last), Esc or the help key Finish.
    void KeyDown(UiEvent& event);
    // Rebirth-patched (WideScreen). Builds the page from its "#screen" line: the rectangle
    // (200, 50)-(600, 560) or "#position", one pulsing frame per "#rectangle" line, one entry per "#text"
    // line. Details: docs/notes/ui.md, "Behaviour moved out of header comments".
    void ShowPage();
    // The page built again: its pulsing frames follow the battle screen's panels to the window's new edges.
    void LayoutChanged() override;
    // The surfaces' paints: the page's and the topic's titles; the texts; the neighbouring pages' titles and the
    // help key's name.
    void PaintHeader(UiSurface& surface) const;
    void PaintBody(UiSurface& surface) const;
    void PaintFooter(UiSurface& surface) const;

    bool message = false;                // a message window (ShowMessage), not help
    std::string text;                   // the message, or the help topic
    std::string title;
    std::vector<std::string> lines;      // the pages' texts
    int page = 0;                        // the page shown (the topic's or message's saved page)
    std::vector<UiMesh*> pulsing;      // sprites whose plain colour pulses (Update)
    std::vector<UiMesh*> pictures;     // the entries' picture sprites
    UiRegion* previousButton = nullptr;   // the page's buttons (ShowPage; null while none is built)
    UiRegion* okButton = nullptr;
    UiRegion* nextButton = nullptr;
    float pulseTime = 0.0f;
    // What ShowPage keeps for the paints: the page's width (layout), the texts with their first rows, and the help
    // key's hint ("<show_tutorial_key> <key>"; empty without a key).
    struct HelpText {
        std::string text;
        Rect row;
    };
    int pageWidth = 0;
    std::vector<HelpText> bodyTexts;
    std::string keyHint;

private:
    // The page is kept for the topic (or the message).
    void Finish();
};
