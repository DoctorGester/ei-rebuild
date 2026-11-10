stb (https://github.com/nothings/stb), public domain or MIT (licence text at the end of each header).
All at commit 2c980bb59875b0d32144a71867fbdebb2f77cd20, unmodified:
  stb_truetype.h v1.26     the native interface text path (src/render/render_text, docs/notes/ui-native.md)
  stb_image.h v2.30        the maps' PNGs read (src/game/map/map_folder.cpp; docs/notes/mods.md, "Maps as folders"),
                           and the reference the checks hold lib/png.h's reader to
  stb_image_write.h v1.16  the checks' PNGs (tests/unit/texture_png.cpp)
  stb_dxt.h v1.12          PNG textures of a DXT format compressed again at load (src/game/models/texture_png.cpp;
                           docs/notes/mods.md, "Textures as PNG")
stb_image_impl.c is ours: the three image headers' implementations (PNG only; stb_image_write's deflate is miniz's),
built as the stb_image library with their warnings off (CMakeLists.txt).
