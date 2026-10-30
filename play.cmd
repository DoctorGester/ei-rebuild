@echo off
rem Builds the product with every option on and starts it to play.
rem usage: play.cmd [rebirth|vanilla] [x64]
rem   rebirth (default)  the Rebirth install's data (EI_GAME_REBIRTH, default F:\games\Evil Islands (Rebirth)) with the Rebirth profile
rem                      (config\rebirth_profile.ini: widescreen, draw radii, console and script additions,
rem                      the bug fixes)
rem   vanilla            the plain GOG install's data (EI_GAME_VANILLA, default F:\games\EvilIslands) with the plain profile (the bug fixes)
rem                      and widescreen
rem   The profile is the data folder's (src/standalone/startup.cpp); a patch named in ei_settings.ini's [patches]
rem   (name = true or false) wins over it in either mode.
rem   x64                the 64-bit build (build.cmd play-x64, into build\play-x64) in place of the 32-bit one, in its own
rem                      folder run\play-x64 (its own ei_settings.ini and log), on the same data and the same profile; the
rem                      combination is free (play.cmd vanilla x64). No ei_launch (it is 32-bit): the exe is started
rem                      here with EI_GAME_ROOT set to the data folder, as ei_launch does (docs/notes/standalone.md "The
rem                      x64 build").
rem The release build (build\play, apart from the builds the suites use) is copied to run\play, so a
rem game left open never locks a build, and drawn with the smooth camera. The settings are
rem run\play\ei_settings.ini, next to the exe; when it is missing the first start writes it with the defaults
rem (docs/notes/standalone.md "Settings file": borderless full screen at the desktop's size, every graphics option
rem at its best, the starter's WideScreen on; the registry is never read). Delete it to start from them again.
rem The renderer's switches are in that file's [renderer] section:
rem   Msaa = 8             the world's multisampling: 1 (off), 2, 4 or 8 (at most what the device gives;
rem                        docs/notes/renderer.md section 11)
rem   Anisotropy = 16      the world's models' anisotropic sampling: 1 (off) .. 16
rem   PerfOverlay = true   the performance overlay: FPS, frame and GPU times, draws (section 15; false by default)
rem   VSync = false        run unlocked, tearing where the system allows it (section 15a; true by default)
rem A harness game open does not end it (ST1b: harness windows have their own title).
setlocal
set MODE=rebirth
set ARCH=
for %%a in (%*) do (
    if /i "%%~a"=="x64" (set ARCH=x64) else if /i "%%~a"=="vanilla" (set MODE=vanilla) else if /i "%%~a"=="rebirth" (set MODE=rebirth) else (
        echo play: unknown argument %%a ^(usage: play.cmd [rebirth^|vanilla] [x64]^)
        exit /b 2
    )
)
set _MSPDBSRV_ENDPOINT_=play
set CFG=play
set PLAY=%~dp0run\play
set FILES=ei_launch.exe ei_launch.pdb ei-rebuild.exe ei-rebuild.pdb third_party_notices.txt
if defined ARCH (
    set CFG=play-x64
    set PLAY=%~dp0run\play-x64
    set FILES=ei-rebuild.exe ei-rebuild.pdb third_party_notices.txt
)
echo play: building the release (build\%CFG%)...
call "%~dp0build.cmd" %CFG% > "%~dp0build\%CFG%_build.log" 2>&1
if errorlevel 1 (
    echo play: the build failed, see build\%CFG%_build.log
    pause
    exit /b 1
)
if not exist "%PLAY%" mkdir "%PLAY%"
for %%f in (%FILES%) do (
    copy /y "%~dp0build\%CFG%\%%f" "%PLAY%\%%f" >nul 2>&1 || echo play: %%f in use, playing the copy already there
)
rem The Rebirth patches follow the data folder's profile; ei_settings.ini's [patches] overrides any of them
rem (docs/notes/rebirth.md).
if not defined EI_GAME_REBIRTH set "EI_GAME_REBIRTH=F:\games\Evil Islands (Rebirth)"
if not defined EI_GAME_VANILLA set "EI_GAME_VANILLA=F:\games\EvilIslands"
set "GAME=%EI_GAME_REBIRTH%"
if /i "%MODE%"=="vanilla" set "GAME=%EI_GAME_VANILLA%"
cd /d "%PLAY%"
if defined ARCH (
    rem The data folder as the 32-bit path's ei_launch names it; the exe's settings and log are run\play-x64's.
    set "EI_GAME_ROOT=%GAME%"
    start "" /wait "%PLAY%\ei-rebuild.exe"
    exit /b
)
rem No --exe: ei_launch starts the ei-rebuild.exe next to it, so the settings file is run\play's too.
"%PLAY%\ei_launch.exe" --game "%GAME%" --standalone
