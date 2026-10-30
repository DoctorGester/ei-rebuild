# Compile the native game and platform independently while the renderer is being ported.
foreach(off SDL_SHARED SDL_TEST_LIBRARY SDL_TESTS SDL_EXAMPLES SDL_INSTALL SDL_UNINSTALL SDL_GPU SDL_RENDER
            SDL_CAMERA SDL_JOYSTICK SDL_HAPTIC SDL_HIDAPI SDL_POWER SDL_SENSOR SDL_DIALOG SDL_OPENGL
            SDL_OPENGLES SDL_VULKAN SDL_OFFSCREEN SDL_DISKAUDIO SDL_DUMMYAUDIO SDL_DUMMYVIDEO)
    set(${off} OFF CACHE BOOL "" FORCE)
endforeach()
set(SDL_STATIC ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/SDL EXCLUDE_FROM_ALL SYSTEM)
file(GLOB EI_PLATFORM_SOURCES CONFIGURE_DEPENDS src/platform/*.cpp src/platform/posix/*.cpp
     src/platform/sdl/*.cpp src/platform/bink/*.cpp)
add_library(ei_platform STATIC ${EI_PLATFORM_SOURCES})
target_include_directories(ei_platform PRIVATE include src third_party)
target_link_libraries(ei_platform PUBLIC SDL3::SDL3-static)

enable_testing()
add_executable(ei_port_check tests/port/port_check.cpp src/lib/text_case.cpp src/game/settings_document.cpp
               tests/vectors/replay_settings.cpp)
target_include_directories(ei_port_check PRIVATE include src)
target_link_libraries(ei_port_check PRIVATE ei_platform)
add_test(NAME port_platform COMMAND ei_port_check)
set_tests_properties(port_platform PROPERTIES TIMEOUT 60)
