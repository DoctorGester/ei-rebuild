#pragma once

// The titles window (CreditsScreen: the main menu's titles, and CreateScreen id 8): the movie, the lines of
// outro.res's credits.scr and their scrolling texts. Members in ui_credits.cpp, the scrolling surface's in
// ui_scrolling_surface.cpp. See docs/notes/ui.md ("Main menu, dialogs, escape menu, credits").

#include "game/app/movies.h"
#include "engine/data/resource.h"
#include "game/ui/ui.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class SceneRenderer;
struct Rect;

namespace render::text {
struct RichLayout;
} // namespace render::text

// A line of the titles' script:
struct TitleLine {
    bool scroll = false;        // the first word is "#scroll"
    float speed = 0.0f;         // the second word (atof): the scroll speed, or the seconds shown
    std::string text;          // the fourth word (the header)
    std::string style;         // the third word (the text file in the archive)
};

// The titles' scrolling text surface.
class UiScrollingSurface : public UiSurface {
public:
    // A surface of format 5 over the layout rectangle (keyed on white); no text, the offsets and the speed 0
    // (startTime is left as it was).
    UiScrollingSurface(const Rect* rect);
    ~UiScrollingSurface() override;   // out of line: the laid out text's type is complete there
    // Loads the text file `name` of `archive` into the surface (LoadRtf); whether it was found.
    bool Load(MappedResource* archive, std::string_view name);
    // Reads the RTF block and lays it out at the surface's width (render_rich_text.h), the offset 0.
    void LoadRtf(const void* data, std::uint32_t size);
    // While the speed is not 0 the offset moves by the game time passed times the speed (truncated) and FindEnd
    // runs; then, shown, the text is drawn into the frame (not in the background colour, the key), its top
    // `offset` twips above the surface's.
    void Blit() override;
    // Starts the text at `speed` (0: still), from below the surface when `fromBelow` (the offset minus the surface's height in
    // twips); the end offset 0, the start time the game time.
    void Start(float speed, bool fromBelow);
    // The first time the whole text fits above 1000 twips below the surface the offset is kept; once the offset has
    // moved 1000 twips plus the height past it, the speed and the kept offset become 0 (RichEdit's EM_FORMATRANGE
    // end in the original).
    void FindEnd();

    float startTime = 0.0f;
    int offsetX = 0;            // twips
    int offset = 0;             // twips scrolled
    float speed = 0.0f;         // 0 once the text has scrolled through
    int endOffset = 0;          // offset when the end of the text came into view (0: not yet)
    std::unique_ptr<render::text::RichLayout> text;    // the loaded text laid out (null before LoadRtf)
};

// The titles window (main menu region 4).
class CreditsScreen : public UiContainer {
public:
    CreditsScreen(bool fromGame, const char* name);
    void OnEvent(UiEvent& event) override;
    void KeyDown(UiEvent& event);
    // Ends the titles: back to where they were opened from.
    void Close();
    // Opened in a game (fromGame 0) the loading screen steps and the music stops; the cursor hidden, the
    // "<name>fin" movies played, "outro.res" opened and its "credits.scr" parsed
    // into lines (one record per line: "#scroll" flag, speed, two texts); the scrolling text surface and the
    // header surface, both frozen; the display's flag kept in savedDisplay and cleared; the player starts
    // "Movies\<name>.bik".
    void CreateScreen() override;
    // Once the game time passes nextLineAt and the text has stopped: both surfaces frozen, then the next
    // line: its text file loaded and started (a "#scroll" line scrolls at its speed under its header, another
    // stays for its speed in seconds), or past the last line finished. The movie steps (stretched unless the
    // movie flag is 1); when it has ended after the last line: "<name>fout" played, the screen flag cleared
    // and the titles closed as Close() does; otherwise the container's draw.
    void DrawSprites(SceneRenderer* pass) override;
    // The lines freed, the display's flag restored, the screen flag set and the cursor shown.
    void CloseScreen() override;
    // A window above closed: the movie player is closed.
    void MessageResult(std::uint32_t result) override;
    // The header surface's paint: header.
    void PaintHeader(UiSurface& surface) const;

    UiScrollingSurface* textSurface = nullptr;    // the scrolling text
    UiSurface* headerSurface = nullptr;
    std::string header;        // the scrolling line's header text
    MappedResource archive;    // "outro.res"
    std::vector<TitleLine> lines;
    float nextLineAt = 0.0f;           // game time the current line ends (0 from Build and for scrolls)
    std::size_t nextLine = 0;          // the index of the next line (lines.size() at the end)
    std::string name;          // ("Titles")
    BinkVideo player;          // the movie player
    bool fromGame = false;      // opened in a game (pending value 0xCCCCCCCC) or the menu
    bool finished = false;              // the last line has gone (cleared by Build)
    int savedDisplay = 0;           // the renderer's fog colour while the titles run
};
