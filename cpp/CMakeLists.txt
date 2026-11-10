cmake_minimum_required(VERSION 3.20)
project(ProtocolHex VERSION 1.0.0 LANGUAGES C CXX)
# PROTOCOL.hex: an Arcade Phantasm game. SDL2 (window, input, sound), OpenGL 3, Dear ImGui (drawing) and stb_image
# (the Cursed logo). Only SDL2 has to be installed; the rest is fetched here (or taken from external/ when it's there).
#
#   Windows: build.bat (fetches SDL2's VC package into external/)    Linux: ./build.sh (needs libsdl2-dev)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
if(EXISTS ${CMAKE_SOURCE_DIR}/external/imgui-1.90.4/imgui.h)
    set(FETCHCONTENT_SOURCE_DIR_IMGUI ${CMAKE_SOURCE_DIR}/external/imgui-1.90.4)
endif()
FetchContent_Declare(imgui URL https://github.com/ocornut/imgui/archive/refs/tags/v1.90.4.tar.gz)
FetchContent_MakeAvailable(imgui)

# stb_image: one single file
set(STB_DIR ${CMAKE_BINARY_DIR}/stb)
if(NOT EXISTS ${STB_DIR}/stb_image.h)
    if(EXISTS ${CMAKE_SOURCE_DIR}/external/stb/stb_image.h)
        file(COPY ${CMAKE_SOURCE_DIR}/external/stb/stb_image.h DESTINATION ${STB_DIR})
    else()
        file(DOWNLOAD https://raw.githubusercontent.com/nothings/stb/master/stb_image.h ${STB_DIR}/stb_image.h STATUS st)
    endif()
endif()

if(WIN32 AND NOT SDL2_DIR AND EXISTS ${CMAKE_SOURCE_DIR}/external/SDL2-2.30.9/cmake)
    set(SDL2_DIR ${CMAKE_SOURCE_DIR}/external/SDL2-2.30.9/cmake)
endif()
find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL2::SDL2 OpenGL::GL)

add_executable(ProtocolHex WIN32 src/main.cpp src/game.cpp src/canvas.cpp src/synth.cpp src/app.rc)
target_include_directories(ProtocolHex PRIVATE ${STB_DIR} src)
target_link_libraries(ProtocolHex PRIVATE imgui)
if(TARGET SDL2::SDL2main)
    target_link_libraries(ProtocolHex PRIVATE SDL2::SDL2main)
endif()
if(MSVC)
    target_compile_options(ProtocolHex PRIVATE /W3 /utf-8)
endif()

# the game finds its assets next to the exe
add_custom_command(TARGET ProtocolHex POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:ProtocolHex>/assets)
if(WIN32)
    add_custom_command(TARGET ProtocolHex POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:ProtocolHex>)
endif()
