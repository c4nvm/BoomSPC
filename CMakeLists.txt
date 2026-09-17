cmake_minimum_required(VERSION 3.24)
project(BoomSPC VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(SDL2 REQUIRED)
find_package(ZLIB REQUIRED)

# ---------------------------------------------------------------------------
# Dear ImGui (fetched at configure time, SDL2 + SDL_Renderer backends)
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.9-docking
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer2.cpp
)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL2::SDL2)

# ---------------------------------------------------------------------------
# snes_spc 0.9.0 (blargg) - accurate SPC700 + S-DSP core, LGPL 2.1
# ---------------------------------------------------------------------------
add_library(snes_spc STATIC
    third_party/snes_spc/SNES_SPC.cpp
    third_party/snes_spc/SNES_SPC_misc.cpp
    third_party/snes_spc/SNES_SPC_state.cpp
    third_party/snes_spc/SPC_DSP.cpp
    third_party/snes_spc/SPC_Filter.cpp
)
target_include_directories(snes_spc PUBLIC third_party/snes_spc)
# The core is hand-tuned C++03; keep our warning flags off it.
target_compile_options(snes_spc PRIVATE -w)

# ---------------------------------------------------------------------------
# boomspc
# ---------------------------------------------------------------------------
add_executable(boomspc
    src/main.cpp
    src/spc_file.cpp
    src/engine.cpp
    src/snes.cpp
    src/cpu65816.cpp
    src/snsf.cpp
    src/brr.cpp
    src/wav.cpp
    src/tracker.cpp
    src/spc700.cpp
    src/driver/nspc.cpp
    src/driver/nspc_driver.cpp
    src/driver/follin.cpp
    src/driver/capcom.cpp
    src/driver/rare.cpp
    src/driver/stream_edit.cpp
    src/driver/akao.cpp
    src/driver/wario.cpp
    src/driver/seq.cpp
    src/ui/ui.cpp
    src/ui/player_panel.cpp
    src/ui/voices_panel.cpp
    src/ui/dsp_panel.cpp
    src/ui/memory_panel.cpp
    src/ui/sequencer_panel.cpp
    src/ui/pianoroll.cpp src/ui/arrangement.cpp
    src/ui/effects_panel.cpp
    src/ui/event_panel.cpp
    src/ui/instruments_panel.cpp
    src/ui/samples_panel.cpp
    src/ui/disasm_panel.cpp
    src/ui/theme.cpp
    src/ui/actions.cpp
    src/ui/fonts.cpp
    src/ui/file_dialog.cpp
    src/ui/about.cpp
    src/project.cpp
)
target_include_directories(boomspc PRIVATE src third_party/stb)
if(WIN32)
    target_link_libraries(boomspc PRIVATE SDL2::SDL2main)
endif()
target_link_libraries(boomspc PRIVATE imgui snes_spc SDL2::SDL2 ZLIB::ZLIB)

target_compile_options(boomspc PRIVATE -Wall -Wextra)
target_compile_definitions(boomspc PRIVATE BOOMSPC_VERSION="${PROJECT_VERSION}")
# The logo and other assets live next to the executable; copied on every
# build so an asset edit lands without relinking.
add_custom_target(boomspc_assets ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:boomspc>/assets
    COMMENT "Copying assets")
add_dependencies(boomspc_assets boomspc)

# ---------------------------------------------------------------------------
# spc2wav: headless render tool (no SDL)
# ---------------------------------------------------------------------------
add_executable(spc2wav tools/spc2wav.cpp src/spc_file.cpp)
target_include_directories(spc2wav PRIVATE src)
target_link_libraries(spc2wav PRIVATE snes_spc)
target_compile_options(spc2wav PRIVATE -Wall -Wextra)

add_executable(spcdump tools/spcdump.cpp src/spc_file.cpp src/driver/nspc.cpp src/driver/nspc_driver.cpp src/driver/follin.cpp src/driver/akao.cpp src/driver/rare.cpp src/driver/capcom.cpp src/driver/wario.cpp src/driver/seq.cpp src/driver/stream_edit.cpp src/spc700.cpp)
target_include_directories(spcdump PRIVATE src)
target_link_libraries(spcdump PRIVATE snes_spc)
target_compile_options(spcdump PRIVATE -Wall -Wextra)

# ---------------------------------------------------------------------------
# parsecheck: verifies a driver's parse against the emulator's real playback
add_executable(parsecheck tools/parsecheck.cpp src/spc_file.cpp src/driver/nspc.cpp src/driver/nspc_driver.cpp src/driver/follin.cpp src/driver/akao.cpp src/driver/rare.cpp src/driver/capcom.cpp src/driver/wario.cpp src/driver/seq.cpp src/driver/stream_edit.cpp)
target_include_directories(parsecheck PRIVATE src)
target_link_libraries(parsecheck PRIVATE snes_spc)

# ---------------------------------------------------------------------------
# edittest: headless checks of the tracker edit primitives
add_executable(edittest tools/edittest.cpp src/project.cpp src/tracker.cpp src/driver/nspc.cpp src/driver/nspc_driver.cpp src/driver/follin.cpp src/driver/akao.cpp src/driver/rare.cpp src/driver/capcom.cpp src/driver/wario.cpp src/driver/seq.cpp src/driver/stream_edit.cpp src/brr.cpp src/engine.cpp src/snes.cpp src/cpu65816.cpp src/snsf.cpp src/spc_file.cpp src/wav.cpp)
target_include_directories(edittest PRIVATE src)
target_link_libraries(edittest PRIVATE snes_spc SDL2::SDL2 ZLIB::ZLIB)

# ramtrace: prints watched RAM bytes over time (driver reverse-engineering aid)
add_executable(ramtrace tools/ramtrace.cpp src/spc_file.cpp)
target_include_directories(ramtrace PRIVATE src)
target_link_libraries(ramtrace PRIVATE snes_spc)

# MinGW: carry the C++ runtime inside the executables (only SDL2.dll and zlib remain).
if(MINGW)
    foreach(t boomspc spc2wav spcdump parsecheck edittest ramtrace)
        target_link_options(${t} PRIVATE -static-libgcc -static-libstdc++ -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive,-Bdynamic)
    endforeach()
endif()
