stb (https://github.com/nothings/stb), public domain or MIT (licence text at the end of each header).
All at commit 2c980bb59875b0d32144a71867fbdebb2f77cd20, unmodified:
  stb_truetype.h v1.26     the native interface text path (src/render/render_text, docs/notes/ui-native.md)
  stb_image.h v2.30        PNG textures read (src/game/models/texture_png.cpp; docs/notes/mods.md, "Textures as PNG")
  stb_image_write.h v1.16  PNG textures written (ei-convert; the same)
  stb_dxt.h v1.12          PNG textures of a DXT format compressed again at load (the same)
stb_image_impl.c is ours: the three image headers' implementations (PNG only; stb_image_write's deflate is miniz's),
built as the stb_image library with their warnings off (CMakeLists.txt).
