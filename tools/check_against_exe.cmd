@echo off
rem The checks that compare our sources and a built ei_hook.dll with the original game.exe (tools/exe_path.py finds it:
rem run\vanilla or run\rebirth, or EI_EXE). The build reads no game.exe (C25c); run these by hand after build.cmd when a
rem change touches what they cover (named data, cells, literals, static initialisers, imports, short functions, x87).
rem usage: tools\check_against_exe.cmd [release|verify]   (the build folder whose DLL and map are checked)
setlocal
set CFG=%~1
if "%CFG%"=="" set CFG=release
set OUT=%~dp0..\build\%CFG%
rem check_fp_fold undecorates names with the toolset's undname (build.cmd's VS root).
set VSROOT=E:\bin\vs\buildtools
call "%VSROOT%\VC\Auxiliary\Build\vcvars32.bat" >nul || exit /b 1
python "%~dp0exe_path.py" >nul || exit /b 1
rem Double constants folded from a product the original computes at 24 bits (SV7d1a).
python "%~dp0check_fp_fold.py" --dll "%OUT%\ei_hook.dll" || exit /b 1
rem Literals standing for game.exe's .rdata constants (/*0073B86C*/ tags) against its bytes (S18e).
python "%~dp0rdata_literals.py" --check || exit /b 1
rem Named globals in place of game.exe's data (/*0074111C*/ {...} initialisers) against its bytes, and nothing
rem else reaching their ranges, whose bytes the image's copy leaves zero (config/named_data.csv, C18).
python "%~dp0named_data.py" --check || exit /b 1
rem Functions under five bytes (EI_REPLACE_SHORT): each port's kind against game.exe's bytes and our compiled code.
python "%~dp0check_short_functions.py" --map "%OUT%\ei_hook.map" || exit /b 1
rem The standalone image's static initialisers (src/standalone/static_init.cpp) against game.exe's.
python "%~dp0check_static_init.py" || exit /b 1
rem The image's pointer and code cells (src/standalone/image_cells.inc, image_code_cells.inc) against game.exe's data.
python "%~dp0gen_image_cells.py" --check || exit /b 1
rem The image's import table (src/standalone/image_imports.inc) and gameapi.h against game.exe's imports.
python "%~dp0gen_iat.py" --check || exit /b 1
echo check_against_exe: 0 problems
