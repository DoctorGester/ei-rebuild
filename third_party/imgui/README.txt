Dear ImGui (https://github.com/ocornut/imgui), MIT (LICENSE.txt).
Release v1.93.0 (tag v1.93.0, commit c1aaac65e896a45f81f47a4d68f9959e0f86c859): imgui.cpp, imgui_draw.cpp,
imgui_tables.cpp, imgui_widgets.cpp, imgui.h, imgui_internal.h, imconfig.h and the three imstb_*.h, unmodified; no
backend and no demo. Built as the imgui library with its warnings off (CMakeLists.txt). Used by the tools built into
ei-rebuild (src/tools: the effect viewer, ei-rebuild --effects): their renderer backend draws on the renderer's overlay
(src/render/render_overlay.cpp) and their input comes from the platform layer's events (src/tools/imgui_layer.cpp).
