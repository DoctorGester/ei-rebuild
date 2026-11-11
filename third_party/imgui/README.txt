Dear ImGui (https://github.com/ocornut/imgui), MIT (LICENSE.txt).
Release v1.93.0, its docking branch (tag v1.93.0-docking, commit b916186e66aa3c02b72c6ca17455516512ed8049):
imgui.cpp, imgui_draw.cpp, imgui_tables.cpp, imgui_widgets.cpp, imgui.h, imgui_internal.h, imconfig.h and the three
imstb_*.h, unmodified; no backend and no demo. Built as the imgui library with its warnings off (CMakeLists.txt). Used
by the effect viewer alone (ei-effects, src/tools; the game links none of it): its renderer backend draws on the
renderer's overlay (src/render/render_overlay.cpp) and its input comes from the platform layer's events
(src/tools/imgui_layer.cpp).
