cmake_minimum_required(VERSION 3.16)
project(czx LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CZX_TARGET_DEVICE "Force Cardputer Zero Linux device backend" OFF)
set(CZX_BACKEND "AUTO" CACHE STRING "Platform backend: AUTO, DESKTOP_X11, DESKTOP_SDL2, DEVICE_LINUX")
set_property(CACHE CZX_BACKEND PROPERTY STRINGS AUTO DESKTOP_X11 DESKTOP_SDL2 DEVICE_LINUX)
option(CZX_BUILD_DEVICE_LIB_CHECK "Compile device platform sources on Linux hosts for verification" ON)
option(CZX_BUILD_TESTS "Build unit tests" ON)

if(CZX_TARGET_DEVICE)
    set(CZX_SELECTED_BACKEND "DEVICE_LINUX")
elseif(CZX_BACKEND STREQUAL "AUTO")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|arm)")
        set(CZX_SELECTED_BACKEND "DEVICE_LINUX")
    else()
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(SDL2 QUIET sdl2)
        endif()
        if(SDL2_FOUND)
            set(CZX_SELECTED_BACKEND "DESKTOP_SDL2")
        else()
            set(CZX_SELECTED_BACKEND "DESKTOP_X11")
        endif()
    endif()
else()
    set(CZX_SELECTED_BACKEND "${CZX_BACKEND}")
endif()

message(STATUS "czx backend: ${CZX_SELECTED_BACKEND}")

find_package(PkgConfig QUIET)
find_package(Threads REQUIRED)

add_library(czx_warnings INTERFACE)
target_compile_options(czx_warnings INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    $<$<COMPILE_LANGUAGE:CXX>:-Wconversion>
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-sign-conversion>
)

# Patch upstream g800/basic.c runProg() uninitialized pointer order into build dir, leaving third_party/g800 untouched.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/third_party/g800/basic.c" CZX_G800_BASIC_ORIG)
string(REGEX REPLACE "skipBlank\\(&p\\);[\r\n\t ]+p = buf;" "p = buf;\n\tskipBlank(&p);" CZX_G800_BASIC_PATCHED "${CZX_G800_BASIC_ORIG}")
if(CZX_G800_BASIC_PATCHED STREQUAL CZX_G800_BASIC_ORIG)
    message(FATAL_ERROR "Failed to apply runProg() patch to third_party/g800/basic.c")
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/g800_basic.c" "${CZX_G800_BASIC_PATCHED}")

# Built-in SHARP PC-G850/G815/E200 core (2-Clause BSD, SDL-free C bridge)
add_library(czx_g800_upstream STATIC
    cmake/g800_czx/g800_wrap.c
    third_party/g800/z80.c
    third_party/g800/z80disasm.c
    third_party/g800/conf.c
    third_party/g800/io.c
    third_party/g800/iocs.c
    third_party/g800/sio.c
    third_party/g800/sound.c
    third_party/g800/util.c
    third_party/g800/menu.c
    third_party/g800/monitor.c
    "${CMAKE_CURRENT_BINARY_DIR}/g800_basic.c"
    third_party/g800/init.c
)
target_include_directories(czx_g800_upstream PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/g800_czx
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/g800
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_definitions(czx_g800_upstream PRIVATE Z80_LITTLEENDIAN=1)
target_compile_options(czx_g800_upstream PRIVATE
    -include ${CMAKE_CURRENT_SOURCE_DIR}/cmake/g800_czx/depend.h
    -fsigned-char
    -fno-strict-aliasing
    -w
    -Wno-incompatible-pointer-types
    -Wno-implicit-function-declaration
)

# Core-agnostic runtime library (scaler, HUD, menu, browser, input router, cores, app)
add_library(czx_runtime STATIC
    src/video/scaler.cpp
    src/video/font6x8.cpp
    src/video/font_jp_data.cpp
    src/video/hud.cpp
    src/video/menu.cpp
    src/video/file_browser.cpp
    src/input/tca8418_keymap.cpp
    src/input/input_router.cpp
    src/core/core_registry.cpp
    src/core/d88_image.cpp
    src/core/g800_core.cpp
    src/core/libretro_keys.cpp
    src/core/libretro_core.cpp
    src/core/test_core.cpp
    src/app/emulator_app.cpp
)
target_include_directories(czx_runtime PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/g800_czx
)
target_link_libraries(czx_runtime PUBLIC czx_warnings Threads::Threads ${CMAKE_DL_LIBS} PRIVATE czx_g800_upstream m)

# Device Linux platform library (DRM + fbdev + evdev + dynamic ALSA)
if(UNIX AND NOT APPLE)
    if(CZX_BUILD_DEVICE_LIB_CHECK OR CZX_SELECTED_BACKEND STREQUAL "DEVICE_LINUX")
        add_library(czx_device_platform STATIC
            src/platform/linux_drm.cpp
            src/platform/linux_fbdev.cpp
            src/platform/linux_evdev.cpp
            src/platform/linux_alsa.cpp
            src/platform/linux_audio.cpp
            src/platform/device_factory.cpp
        )
        target_include_directories(czx_device_platform PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/src
        )
        target_link_libraries(czx_device_platform PUBLIC czx_runtime ${CMAKE_DL_LIBS})
    endif()
endif()

# Selected platform backend library
if(CZX_SELECTED_BACKEND STREQUAL "DEVICE_LINUX")
    add_library(czx_platform INTERFACE)
    target_link_libraries(czx_platform INTERFACE czx_device_platform)
    target_compile_definitions(czx_platform INTERFACE CZX_BACKEND_DEVICE_LINUX=1)
elseif(CZX_SELECTED_BACKEND STREQUAL "DESKTOP_SDL2")
    if(NOT SDL2_FOUND AND PkgConfig_FOUND)
        pkg_check_modules(SDL2 REQUIRED sdl2)
    endif()
    add_library(czx_platform STATIC
        src/platform/desktop_sdl2.cpp
        src/platform/linux_alsa.cpp
        src/platform/linux_audio.cpp
        src/platform/desktop_sdl2_factory.cpp
    )
    target_include_directories(czx_platform PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${SDL2_INCLUDE_DIRS}
    )
    target_link_libraries(czx_platform PUBLIC czx_runtime ${SDL2_LIBRARIES} ${CMAKE_DL_LIBS})
    target_compile_definitions(czx_platform PUBLIC CZX_BACKEND_DESKTOP_SDL2=1)
elseif(CZX_SELECTED_BACKEND STREQUAL "DESKTOP_X11")
    if(NOT PkgConfig_FOUND)
        message(FATAL_ERROR "PkgConfig is required for DESKTOP_X11 backend")
    endif()
    pkg_check_modules(X11 REQUIRED x11)
    add_library(czx_platform STATIC
        src/platform/desktop_x11.cpp
        src/platform/linux_alsa.cpp
        src/platform/linux_audio.cpp
        src/platform/desktop_x11_factory.cpp
    )
    target_include_directories(czx_platform PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${X11_INCLUDE_DIRS}
    )
    target_link_libraries(czx_platform PUBLIC czx_runtime ${X11_LIBRARIES} ${CMAKE_DL_LIBS})
    target_compile_definitions(czx_platform PUBLIC CZX_BACKEND_DESKTOP_X11=1)
else()
    message(FATAL_ERROR "Unknown CZX_BACKEND: ${CZX_SELECTED_BACKEND}")
endif()

add_executable(czx src/main.cpp)
target_link_libraries(czx PRIVATE czx_runtime czx_platform)

if(CZX_BUILD_TESTS)
    enable_testing()

    add_executable(test_scaler tests/test_scaler.cpp)
    target_link_libraries(test_scaler PRIVATE czx_runtime)
    add_test(NAME test_scaler COMMAND test_scaler)

    add_executable(test_input_router tests/test_input_router.cpp)
    target_link_libraries(test_input_router PRIVATE czx_runtime)
    add_test(NAME test_input_router COMMAND test_input_router)

    add_executable(test_libretro_keys tests/test_libretro_keys.cpp)
    target_link_libraries(test_libretro_keys PRIVATE czx_runtime)
    add_test(NAME test_libretro_keys COMMAND test_libretro_keys)

    add_executable(test_text_font tests/test_text_font.cpp)
    target_link_libraries(test_text_font PRIVATE czx_runtime)
    add_test(NAME test_text_font COMMAND test_text_font)

    add_executable(test_d88 tests/test_d88.cpp)
    target_link_libraries(test_d88 PRIVATE czx_runtime)
    add_test(NAME test_d88 COMMAND test_d88)
endif()
