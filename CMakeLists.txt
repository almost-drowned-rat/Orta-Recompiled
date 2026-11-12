cmake_minimum_required(VERSION 3.20)

# Panzer Dragoon Orta - static recompilation.
# After sp00nznet/xboxrecomp templates/new-game/CMakeLists.txt and
# refs/defjam_recomp/CMakeLists.txt. Build only via tools/build.ps1.

project(orta_recomp C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX)
if(MINGW)
    add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00)
endif()

# Toolkit: option-c, pinned worktree tools\xboxrecomp_orta (tools/paths.ps1; build.ps1 passes it explicitly).
set(XBOXRECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../tools/xboxrecomp_orta" CACHE PATH
    "Path to the xboxrecomp toolkit root directory")

# Generated C lives outside the repo (never committed): tools/lift.ps1 output.
set(RECOMP_GEN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../private/orta/gen/pal" CACHE PATH
    "Directory holding the generated C (recomp_*.c, recomp_types.h, ...)")

# Record every indirect-branch target at runtime (icall_targets.dump) for the
# seeds feedback loop. Must be set before add_subdirectory (icall_feedback.c).
# Remove once function detection has converged (hot-path store).
add_compile_definitions(RECOMP_ICALL_FEEDBACK)

# Vulkan renderer (toolkit nv2a_vk, M2): glslang + SDL3 from tools/deps.ps1
# (CMAKE_PREFIX_PATH). OFF = the toolkit's CPU executor path, nothing drawn.
option(ORTA_VULKAN "Render through the toolkit's Vulkan back end (nv2a_vk)" ON)

# Custom soundtracks: opt-in per port, OFF by default. Only for the titles listed in the toolkit's
# docs/porting_guide.md ("Custom soundtracks - opt-in per port"); every other port leaves it off.
# Reserved switch: the toolkit module (dashboard music store -> soundtracks/ folder) is not implemented yet.
option(ORTA_CUSTOM_SOUNDTRACKS "Dashboard custom soundtracks (only titles on the porting-guide list)" OFF)
set(XBOXRECOMP_VULKAN ${ORTA_VULKAN} CACHE BOOL "" FORCE)
# Vulkan only: no D3D11 libraries in the toolkit build (xbox_d3d8, xbox_nv2a,
# the D3D8 movie player) -- CLAUDE.md, M2 stage 5.
if(ORTA_VULKAN)
    set(XBOXRECOMP_D3D11 OFF CACHE BOOL "" FORCE)
else()
    set(XBOXRECOMP_D3D11 ON CACHE BOOL "" FORCE)
endif()

add_subdirectory(${XBOXRECOMP_DIR} ${CMAKE_BINARY_DIR}/xboxrecomp)

# CONFIGURE_DEPENDS: a re-lift that adds/removes chunks is picked up.
file(GLOB RECOMP_GEN_SOURCES CONFIGURE_DEPENDS "${RECOMP_GEN_DIR}/*.c")
if(NOT RECOMP_GEN_SOURCES)
    message(FATAL_ERROR "No generated C in ${RECOMP_GEN_DIR} - run tools/lift.ps1 first")
endif()

add_executable(${PROJECT_NAME}
    src/main.c
    src/recomp_manual.c
    src/input.c
    src/settings.c
    src/vk_check.c
    ${RECOMP_GEN_SOURCES}
)
# The host layer (src/platform/host.h): Windows, or Linux (v1.1.0 Linux port, AppImage).
if(WIN32)
    target_sources(${PROJECT_NAME} PRIVATE src/platform/host_win32.c src/platform/apu_diag_win32.c)
else()
    target_sources(${PROJECT_NAME} PRIVATE src/platform/host_linux.c)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${CMAKE_DL_LIBS} pthread)
endif()
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME ORTA)
# The port's icon (assets/README.md): the exe's icon in Explorer as a resource; the window / taskbar icon as the BMP's
# bytes in a generated header (main.c -> xbox_set_window_icon). Model: THPS 2X CMakeLists.txt.
if(WIN32)
    enable_language(RC)
    target_sources(${PROJECT_NAME} PRIVATE assets/orta.rc)
endif()
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon_64.bmp" ORTA_ICON_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," ORTA_ICON_HEX "${ORTA_ICON_HEX}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/port_icon.h"
     "/* generated from assets/icon_64.bmp (CMakeLists.txt) */\nstatic const unsigned char g_port_icon_bmp[] = {${ORTA_ICON_HEX}};\n")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon_64.bmp")
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
# Only the game goes into the portable install layout, not the toolkit's tests.
if(ORTA_OUTPUT_DIR)
    set_target_properties(${PROJECT_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${ORTA_OUTPUT_DIR}")
endif()

# The toolkit libraries the game uses, named one by one instead of the
# umbrella target `xboxrecomp`: that one also pulls in xbox_d3d8 (D3D8 ->
# D3D11 for titles without their own D3D) and xbox_nv2a (an older D3D11
# PGRAPH path). Panzer Dragoon Orta's D3D is lifted code driving the NV2A pushbuffer, so
# with nv2a_vk neither is needed -- Vulkan only (CLAUDE.md).
target_link_libraries(${PROJECT_NAME} PRIVATE
    xbox_kernel xbox_dsound xbox_input xbox_usb xbox_apu xbox_video)
if(ORTA_VULKAN)
    target_link_libraries(${PROJECT_NAME} PRIVATE xbox_nv2a_vk)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ORTA_VULKAN=1)
    # The optional boot animation media\boot.mpg (toolkit src/bootvideo, pl_mpeg; RECOMP OPTIONS > BOOT ANIMATION).
    if(TARGET xbox_bootvideo)
        target_link_libraries(${PROJECT_NAME} PRIVATE xbox_bootvideo)
        target_compile_definitions(${PROJECT_NAME} PRIVATE ORTA_BOOTVIDEO=1)
    endif()
    # The first-start installer (toolkit src/install, M5; model THPS 2X): game/ holds the extracted disc or one
    # disc image, an image is extracted, checked and deleted. Its SDL3 progress window comes with the Vulkan build.
    if(TARGET xbox_install_ui)
        target_link_libraries(${PROJECT_NAME} PRIVATE xbox_install_ui)
        target_compile_definitions(${PROJECT_NAME} PRIVATE ORTA_INSTALLER=1)
    endif()
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE xboxrecomp)
endif()
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${RECOMP_GEN_DIR}
    ${XBOXRECOMP_DIR}/include
)

# System libraries the toolkit's Windows runtime still needs. No D3D: the
# renderer is Vulkan (M2 stage 5 removed d3d11/dxgi/dxguid).
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        $<IF:$<BOOL:${MINGW}>,xinput1_4,xinput>
        winmm
        dbghelp   # crash handler: host RIP -> generated sub_XXXXXXXX
        shell32   # CommandLineToArgvW: the command line as UTF-16
    )
else()
    # Linux: dladdr names the generated function in a crash report when the symbols are exported
    target_link_options(${PROJECT_NAME} PRIVATE -rdynamic)
endif()

if(MINGW)
    # CodeView + .pdb (llvm-mingw/lld) so dbghelp in the crash handler can
    # name the generated function; DWARF is invisible to dbghelp.
    target_compile_options(${PROJECT_NAME} PRIVATE -g -gcodeview)
    # -static: libc++ (glslang) and winpthread linked in, so the portable
    # folder needs no extra DLLs (docs/toolkit.md, toolchain decision).
    target_link_options(${PROJECT_NAME} PRIVATE -Wl,--pdb= -Wl,--large-address-aware -static)
endif()
# The release launcher "Panzer Dragoon Orta Recomp.exe": the toolkit's launcher with Orta's
# names, version and icon - no game code. Built into the build folder (not the layout); tools/release.ps1 appends
# the payload to it. Model: THPS 2X.
if(TARGET xbox_launcher)   # Windows: the release exe; Linux: usr/bin/panzer-dragoon-orta-recomp in the AppImage
    # setup_win32.c (Windows only): the setup window in another port's launcher look (maintainer 2026-11-05) and the
    # toolkit's progress UI (game_setup_ui.h) in the same look - its six functions replace game_setup_ui.c's at link
    # time. Linux (maintainer 2026-11-11): launcher/setup_ui_sdl.c, the same look drawn with SDL (also Android's).
    # src/vk_check.c: the Vulkan 1.3 check with the apology box (ORTA.exe uses the same file).
    add_executable(orta_launcher launcher/main.c src/vk_check.c)
    target_include_directories(orta_launcher PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    if(WIN32)
        target_sources(orta_launcher PRIVATE launcher/setup_win32.c assets/orta.rc)
        set_target_properties(orta_launcher PROPERTIES WIN32_EXECUTABLE ON)
        target_link_libraries(orta_launcher PRIVATE comdlg32 shell32 ole32 gdi32 user32)
    else()
        target_sources(orta_launcher PRIVATE launcher/setup_ui_sdl.c)
    endif()
    if(WIN32)
        set_target_properties(orta_launcher PROPERTIES OUTPUT_NAME "Panzer Dragoon Orta Recomp")
    else()
        set_target_properties(orta_launcher PROPERTIES OUTPUT_NAME "panzer-dragoon-orta-recomp")
    endif()
    set_target_properties(orta_launcher PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/launcher")
    target_include_directories(orta_launcher PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
    target_link_libraries(orta_launcher PRIVATE xbox_launcher)
    if(MINGW)
        target_link_options(orta_launcher PRIVATE -static)
    endif()
endif()
if(MSVC)
    target_link_options(${PROJECT_NAME} PRIVATE /LARGEADDRESSAWARE
        $<$<CONFIG:Release>:/DEBUG> $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
    target_compile_options(${PROJECT_NAME} PRIVATE /bigobj $<$<CONFIG:Release>:/Zi>)
endif()
