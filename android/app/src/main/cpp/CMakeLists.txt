cmake_minimum_required(VERSION 3.22.1)
project(czx_android LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(CZX_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)

set(CZX_BACKEND "ANDROID" CACHE STRING "" FORCE)
set(CZX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CZX_BUILD_DEVICE_LIB_CHECK OFF CACHE BOOL "" FORCE)

add_subdirectory("${CZX_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/czx_root")

add_library(czx_android SHARED
    android_platform.cpp
    jni_bridge.cpp
    "${CZX_ROOT}/src/platform/gles2_presenter.cpp"
)

target_include_directories(czx_android PRIVATE
    "${CZX_ROOT}/include"
    "${CZX_ROOT}/src"
    "${CMAKE_CURRENT_LIST_DIR}"
)

target_compile_definitions(czx_android PRIVATE
    CZX_BACKEND_ANDROID=1
)

target_link_libraries(czx_android PRIVATE
    czx_runtime
    android
    EGL
    GLESv2
    aaudio
    log
)
