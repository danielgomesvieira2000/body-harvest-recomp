cmake_minimum_required(VERSION 3.20)

# The deployment target has to be set before project(): it is what the compiler
# check itself is run with, and RT64's Metal shader build reads it too. 15.0 is
# what the macOS build was validated against; override it with -DBH_MACOS_TARGET
# or by setting CMAKE_OSX_DEPLOYMENT_TARGET directly.
if(CMAKE_HOST_APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "Minimum macOS version")
endif()

project(BodyHarvestRecomp
    VERSION 0.1.0
    DESCRIPTION "Native PC port of Body Harvest via static recompilation"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
#  Phase gating
#
#  This project is built in phases (see docs/PLAN.md). Each option below is
#  turned on by the phase that earns it, so a partially finished tree always
#  configures and builds.
# ---------------------------------------------------------------------------
option(BH_WITH_RUNTIME    "Link ultramodern + librecomp + RT64 (phase 03)" OFF)
option(BH_WITH_RECOMPILED "Compile generated code in RecompiledFuncs/ (phase 02)" OFF)
option(BH_BUILD_RECOMPILER "Also build the N64Recomp tool from lib/ (phase 02)" OFF)
option(BH_WITH_FRONTEND   "Link RecompFrontend for menus and input (phase 06)" OFF)

# ---------------------------------------------------------------------------
#  Compiler policy
#
#  N64Recomp's output is validated against Clang and MSVC. Modern GCC's
#  optimizer has produced miscompiles of recompiled code, so warn loudly
#  rather than silently building something that fails at runtime only.
# ---------------------------------------------------------------------------
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(WARNING
        "Building with GCC. N64Recomp output is known to be sensitive to modern "
        "GCC optimizations; Clang or MSVC is strongly recommended.")
endif()

if(MSVC)
    add_compile_options(/utf-8)
endif()

# ---------------------------------------------------------------------------
#  Dependencies
# ---------------------------------------------------------------------------
set(BH_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib")

# RT64's Metal backend and recompui's Apple support file are Objective-C++.
# CMake only compiles those if the languages are enabled, and enabling them in
# a subdirectory is too late for the ones added below.
if(APPLE)
    enable_language(OBJC OBJCXX)
endif()

# SDL2. On Windows it comes from the copy RT64 vendors under
# contrib/mupen64plus-win32-deps; everywhere else it is a system package, and
# this project needs it in its own scope because src/callbacks.cpp includes
# SDL.h and creates the window. RT64 and RecompFrontend each run their own
# find_package in their own directories.
if(NOT WIN32)
    find_package(SDL2 REQUIRED)
endif()

if(BH_WITH_RUNTIME)
    if(NOT EXISTS "${BH_LIB_DIR}/RT64/CMakeLists.txt")
        message(FATAL_ERROR
            "Submodules are missing. Run: git submodule update --init --recursive")
    endif()

    set(RT64_STATIC ON CACHE BOOL "" FORCE)

    # On Linux this one option decides a type, not a preference. plume's
    # RenderWindow is an X11 { Display*, Window } pair by default, but
    # ultramodern's Linux WindowHandle is an SDL_Window* -- the two do not
    # meet, and the port has nothing to hand the renderer. Turning it on makes
    # plume's RenderWindow an SDL_Window* as well, which also takes X11 out of
    # the build entirely so it works under Wayland.
    #
    # It is deliberately not set on macOS: there plume and ultramodern already
    # agree on { window, view }, and RT64 ignores this option off Linux anyway.
    #
    # The two definitions go with it, and they have to be set *here*, at
    # directory scope, before anything is added -- not on a target. RT64
    # defines them for its own directory when the option is on, and that
    # reaches neither this project's target nor RecompFrontend's. Every
    # translation unit that includes plume_render_interface_types.h has to
    # agree about what RenderWindow is: recompui assigns an SDL_Window* to
    # Application::Core::window (rt64_render_context.cpp), which does not
    # compile against the X11 definition, and a target that compiled against
    # the wrong one would be an ABI mismatch at the call that passes the
    # handle across rather than an error.
    if(UNIX AND NOT APPLE)
        set(RT64_SDL_WINDOW_VULKAN ON CACHE BOOL "" FORCE)
        add_compile_definitions(PLUME_SDL_VULKAN_ENABLED RT64_SDL_WINDOW_VULKAN)
    endif()

    add_subdirectory("${BH_LIB_DIR}/RT64" EXCLUDE_FROM_ALL)
    add_subdirectory("${BH_LIB_DIR}/N64ModernRuntime" EXCLUDE_FROM_ALL)
endif()

# RecompFrontend supplies the pieces every N64: Recompiled port needs and none
# of which are game-specific: recompinput for controller mapping, rebindable
# keys and per-device profiles, and recompui for the config and mod menus, built
# on RmlUi and drawn through RT64. It takes the paths to the two libraries it
# sits on top of, which this project already has, so it is added after them.
if(BH_WITH_FRONTEND)
    if(NOT BH_WITH_RUNTIME)
        message(FATAL_ERROR
            "BH_WITH_FRONTEND needs BH_WITH_RUNTIME: the frontend draws "
            "through RT64 and reads the runtime's config.")
    endif()
    if(NOT EXISTS "${BH_LIB_DIR}/RecompFrontend/recompui/lib/RmlUi/CMakeLists.txt")
        message(FATAL_ERROR
            "RecompFrontend's own submodules are missing. Run: "
            "git submodule update --init --recursive")
    endif()

    # On Windows the frontend expects SDL2 at ${sdl2_SOURCE_DIR}, which is what
    # FetchContent would have set had it fetched SDL2 itself. This project takes
    # SDL2 from the copy RT64 already vendors -- two SDL2s in one process is not
    # a thing that ends well -- so the variable is pointed at that instead.
    # Off Windows the frontend already falls back to find_package(SDL2), so the
    # variable must stay unset there or it points at a Windows import library.
    if(WIN32)
        set(sdl2_SOURCE_DIR
            "${BH_LIB_DIR}/RT64/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3")

        # SDL 2.26's SDL_endian.h defines _m_prefetch as a workaround for an old
        # Clang bug, guarded only by __PRFCHWINTRIN_H. Current Clang has
        # _m_prefetch as a builtin, so that definition is now an error rather
        # than a fix. RT64 already defines the guard for its own targets; the
        # frontend is a separate directory scope and does not inherit it, so it
        # is set here for everything added below.
        #
        # This is a fact about the vendored SDL 2.26, not about the compiler, so
        # it must not be defined off Windows: there it would hide Clang's own
        # prfchwintrin.h from every translation unit that wants _m_prefetch.
        add_compile_definitions(__PRFCHWINTRIN_H)
    endif()

    # recompui compiles its own HLSL through RT64's build_vertex_shader and
    # build_pixel_shader. Those functions are global, but the variables they
    # expand -- the DXC binary and its option lists -- are set with plain set()
    # inside RT64's own directory, so a sibling directory calling them gets an
    # empty compiler and a command line that begins with the .hlsl file. The
    # shader step then "succeeds" while producing nothing, and the failure
    # surfaces one step later as an empty file. Re-declaring them here is what
    # makes the functions usable from outside RT64.
    #
    # The compiler itself is a different binary on each platform, and the two
    # Unix ones need their own libdxcompiler on the library path to run at all.
    # RT64 vendors all of them under contrib/dxc; this mirrors the selection
    # RT64 makes for itself in lib/RT64/CMakeLists.txt.
    set(BH_RT64_DIR "${BH_LIB_DIR}/RT64")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
        set(BH_DXC_ARCH arm64)
    else()
        set(BH_DXC_ARCH x64)
    endif()
    if(WIN32)
        set(DXC "${BH_RT64_DIR}/src/contrib/dxc/bin/x64/dxc.exe")
    elseif(APPLE)
        set(DXC "${CMAKE_COMMAND}" -E env
            "DYLD_LIBRARY_PATH=${BH_RT64_DIR}/src/contrib/dxc/lib/${BH_DXC_ARCH}"
            "${BH_RT64_DIR}/src/contrib/dxc/bin/${BH_DXC_ARCH}/dxc-macos")
    else()
        set(DXC "${CMAKE_COMMAND}" -E env
            "LD_LIBRARY_PATH=${BH_RT64_DIR}/src/contrib/dxc/lib/${BH_DXC_ARCH}"
            "${BH_RT64_DIR}/src/contrib/dxc/bin/${BH_DXC_ARCH}/dxc-linux")
    endif()
    set(DXC_COMMON_OPTS "-I${BH_RT64_DIR}/src")
    set(DXC_DXIL_OPTS "-Wno-ignored-attributes")
    set(DXC_SPV_OPTS "-spirv" "-fspv-target-env=vulkan1.0" "-fvk-use-dx-layout")
    set(DXC_PS_OPTS "${DXC_COMMON_OPTS}" "-E" "PSMain" "-T ps_6_3")
    set(DXC_VS_OPTS "${DXC_COMMON_OPTS}" "-E" "VSMain" "-T vs_6_3" "-fvk-invert-y")

    set(RECOMP_FRONTEND_N64MODERNRUNTIME_PATH "${BH_LIB_DIR}/N64ModernRuntime"
        CACHE PATH "" FORCE)
    set(RECOMP_FRONTEND_RT64_PATH "${BH_LIB_DIR}/RT64" CACHE PATH "" FORCE)
    add_subdirectory("${BH_LIB_DIR}/RecompFrontend" EXCLUDE_FROM_ALL)
endif()

# The recompiler tool lives inside N64ModernRuntime as its own submodule. Using
# that copy rather than a separately pinned one keeps the generated code and the
# runtime that consumes it on the same revision of the shared headers.
if(BH_BUILD_RECOMPILER AND NOT TARGET N64Recomp)
    add_subdirectory("${BH_LIB_DIR}/N64ModernRuntime/N64Recomp" EXCLUDE_FROM_ALL)
endif()

# ---------------------------------------------------------------------------
#  Recompiled game code (generated from the user's own ROM; never committed)
# ---------------------------------------------------------------------------
if(BH_WITH_RECOMPILED)
    file(GLOB BH_RECOMPILED_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/RecompiledFuncs/*.c")

    if(NOT BH_RECOMPILED_SOURCES)
        message(FATAL_ERROR
            "BH_WITH_RECOMPILED is ON but RecompiledFuncs/ is empty.\n"
            "Generate it first: wsl -d Ubuntu -e bash tools/regenerate.sh")
    endif()

    # The recompiled RSP audio microcode, produced by RSPRecomp. It is C++
    # because it includes librecomp's rsp headers, and it is listed explicitly
    # rather than globbed so it cannot be swept into the C sources.
    set(BH_RSP_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/RecompiledFuncs/aspMain_rsp.cpp")
    if(EXISTS "${BH_RSP_SOURCE}")
        list(APPEND BH_RECOMPILED_SOURCES "${BH_RSP_SOURCE}")

        # librecomp emulates the RSP's vector unit with SSSE3 and SSE4.1
        # intrinsics -- _mm_shuffle_epi8 and _mm_blendv_epi8 have no portable
        # fallback there. Neither is enabled by default, so this one file needs
        # them explicitly. Both predate every x86-64 CPU capable of running RT64.
        #
        # On arm64 -- Apple Silicon -- there is no such flag and asking for one
        # is a hard error. librecomp reaches the same intrinsics through the
        # sse2neon header bundled with the runtime instead; the include path for
        # it is added to the target below.
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|i.86)$")
            if(MSVC)
                set_source_files_properties("${BH_RSP_SOURCE}" PROPERTIES
                    COMPILE_OPTIONS "/clang:-mssse3;/clang:-msse4.1")
            else()
                set_source_files_properties("${BH_RSP_SOURCE}" PROPERTIES
                    COMPILE_OPTIONS "-mssse3;-msse4.1")
            endif()
        endif()
    endif()

    add_library(bh_recompiled STATIC ${BH_RECOMPILED_SOURCES})
    target_include_directories(bh_recompiled PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        # recomp.h and the librecomp headers the generated code includes, plus
        # RecompiledFuncs itself for funcs.h and reimplemented_decls.h.
        "${BH_LIB_DIR}/N64ModernRuntime/N64Recomp/include"
        "${BH_LIB_DIR}/N64ModernRuntime/librecomp/include"
        # The recompiled RSP microcode reaches ultramodern's headers through
        # librecomp/rsp.hpp, so this target needs them too.
        "${BH_LIB_DIR}/N64ModernRuntime/ultramodern/include"
        # sse2neon, which is how librecomp's RSP vector unit compiles on arm64.
        # Harmless on x86: nothing includes it there.
        "${BH_LIB_DIR}/N64ModernRuntime/thirdparty/sse2neon"
        "${CMAKE_CURRENT_SOURCE_DIR}/RecompiledFuncs")

    # Recompiled code is machine-generated: its warnings are noise, and it must
    # never be built with strict aliasing or signed-overflow assumptions.
    if(NOT MSVC)
        target_compile_options(bh_recompiled PRIVATE
            -fno-strict-aliasing -fwrapv -w)
    else()
        target_compile_options(bh_recompiled PRIVATE /w)
    endif()
endif()

# ---------------------------------------------------------------------------
#  The port itself
# ---------------------------------------------------------------------------
set(BH_SOURCES src/main.cpp src/rom.cpp src/crash_handler.cpp)

# The harness only makes sense with both the runtime and the generated code.
# Each phase adds its sources here (docs/PLAN.md).
if(BH_WITH_RUNTIME AND BH_WITH_RECOMPILED)
    list(APPEND BH_SOURCES src/callbacks.cpp src/renderer.cpp src/overlays.cpp
                           src/libultra_stubs.cpp src/testdrive.cpp src/audiodiag.cpp
                           src/resample.cpp src/thread_sampler.cpp src/spin_yield.cpp
                           src/inspector.cpp src/mods.cpp)
    if(BH_WITH_FRONTEND)
        list(APPEND BH_SOURCES src/frontend.cpp)
    endif()
    file(GLOB BH_PATCH_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/patches/*.cpp")
    list(APPEND BH_SOURCES ${BH_PATCH_SOURCES})
endif()

# The Windows resource compiler (llvm-rc, alongside clang-cl) is opt-in
# per generator, unlike C/CXX -- it has to be enabled explicitly, and only
# where a resource compiler exists at all.
if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/app.rc")
    enable_language(RC)
    list(APPEND BH_SOURCES src/app.rc)
endif()

add_executable(body-harvest-recomp ${BH_SOURCES})

# On macOS the product is an application bundle, not a bare executable: it is
# what the player double-clicks, it is what carries the assets, and it is what
# can be signed. Everything staged below goes into Contents/Resources instead of
# the directory beside the binary because of it.
if(APPLE)
    set_target_properties(body-harvest-recomp PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Body Harvest Recompiled"
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.danielgomesvieira2000.bodyharvestrecomp"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
endif()

# Where the frontend's assets are staged, and where the port looks for them at
# run time. These have to agree; see executable_directory() in src/main.cpp.
if(APPLE)
    set(BH_ASSET_DIR "$<TARGET_BUNDLE_CONTENT_DIR:body-harvest-recomp>/Resources/assets")
else()
    set(BH_ASSET_DIR "$<TARGET_FILE_DIR:body-harvest-recomp>/assets")
endif()

target_include_directories(body-harvest-recomp PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

# dbghelp resolves the crash handler's addresses to function names. The crash
# handler is in every build (phase 00 included), so this is not runtime-gated.
if(WIN32)
    target_link_libraries(body-harvest-recomp PRIVATE dbghelp)
endif()

# The version, from the one place it is declared. It was previously written out
# by hand in three places -- project(), the runtime's Version struct and the
# --version banner -- and the banner had been left at 0.4.0 for five releases.
target_compile_definitions(body-harvest-recomp PRIVATE
    BH_VERSION="${PROJECT_VERSION}"
    BH_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    BH_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    BH_VERSION_PATCH=${PROJECT_VERSION_PATCH})

# src/callbacks.cpp creates the window and reads the event queue, so the port
# links SDL2 itself rather than relying on it arriving through RT64.
if(NOT WIN32)
    if(TARGET SDL2::SDL2)
        target_link_libraries(body-harvest-recomp PRIVATE SDL2::SDL2)
    else()
        target_include_directories(body-harvest-recomp PRIVATE ${SDL2_INCLUDE_DIRS})
        target_link_libraries(body-harvest-recomp PRIVATE ${SDL2_LIBRARIES})
    endif()
endif()

if(BH_WITH_RUNTIME)
    target_link_libraries(body-harvest-recomp PRIVATE rt64 ultramodern librecomp)
    if(WIN32)
        # LNK4099: a library has debug info pointing at a .pdb that is not
        # here. RmlUi links the prebuilt freetype under
        # recompui/lib/freetype-windows-binaries, whose objects name
        # "C:/freetype/objs/x64/Release Static/freetype.pdb" -- the path it had
        # on whoever's machine built it. lld-link reports that once per object
        # and then *exits 1*, so the executable is produced and the build is
        # still called a failure, which fails a release script for a warning
        # about symbols nobody has. Suppressed by number rather than globally so
        # a real link warning still stops the build.
        target_link_options(body-harvest-recomp PRIVATE /ignore:4099)
    endif()
    target_compile_definitions(body-harvest-recomp PRIVATE BH_WITH_RUNTIME=1)

    # RT64 declares its definitions, include paths and link paths with the
    # directory-scoped add_compile_definitions/include_directories/
    # link_directories, none of which reach a target declared outside its
    # CMakeLists. Since src/renderer.cpp includes RT64 headers directly, all
    # three have to be repeated here. hlsl++ will not parse without HLSL_CPU,
    # and the Windows headers need NOMINMAX.
    target_compile_definitions(body-harvest-recomp PRIVATE
        HLSL_CPU
        FFX_GCC
        IMGUI_IMPL_VULKAN_NO_PROTOTYPES)

    if(WIN32)
        target_compile_definitions(body-harvest-recomp PRIVATE
            NOMINMAX
            __PRFCHWINTRIN_H)
    endif()

    target_include_directories(body-harvest-recomp PRIVATE
        # RT64's extended GBI header.
        "${BH_LIB_DIR}/RT64/include"
        "${BH_LIB_DIR}/RT64/src"
        "${BH_LIB_DIR}/RT64/src/contrib"
        "${BH_LIB_DIR}/RT64/src/contrib/plume"
        "${BH_LIB_DIR}/RT64/src/contrib/imgui"
        "${BH_LIB_DIR}/RT64/src/contrib/hlslpp/include"
        "${BH_LIB_DIR}/RT64/src/contrib/mupen64plus-core/src/api"
        "${BH_LIB_DIR}/RT64/src/contrib/nativefiledialog-extended/src/include"
        # The vendored SDL2 is the Windows one only; elsewhere SDL2 arrives
        # through the imported target or SDL2_INCLUDE_DIRS above.
        $<$<BOOL:${WIN32}>:${BH_LIB_DIR}/RT64/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3/include>
        "${BH_LIB_DIR}/N64ModernRuntime/N64Recomp/include"
        "${BH_LIB_DIR}/N64ModernRuntime/librecomp/include"
        "${BH_LIB_DIR}/N64ModernRuntime/ultramodern/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/RecompiledFuncs")

    if(WIN32)
        target_include_directories(body-harvest-recomp PRIVATE
            "${BH_LIB_DIR}/RT64/src/contrib/dxc/inc")

        # RT64 vendors SDL2 for Windows and points at it with link_directories(),
        # which is directory-scoped: it applies to targets declared inside RT64's
        # own CMakeLists but not to ours. The library *names* propagate to us
        # through target_link_libraries, so the link asks for SDL2.lib without
        # knowing where it lives. Supply the search path rather than patching the
        # submodule.
        set(BH_SDL2_DIR
            "${BH_LIB_DIR}/RT64/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3")
        target_link_directories(body-harvest-recomp PRIVATE "${BH_SDL2_DIR}/lib/x64")

        # Runtime DLLs have to sit beside the executable or it will not start at
        # all -- Windows reports only 0xC0000135 with no clue which one is
        # missing. SDL2 is linked directly; dxcompiler and dxil are loaded by
        # RT64 to compile shaders. RT64 does copy the latter two, but into its
        # own binary directory rather than ours.
        add_custom_command(TARGET body-harvest-recomp POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${BH_SDL2_DIR}/lib/x64/SDL2.dll"
                    "${BH_LIB_DIR}/RT64/src/contrib/dxc/bin/x64/dxcompiler.dll"
                    "${BH_LIB_DIR}/RT64/src/contrib/dxc/bin/x64/dxil.dll"
                    "$<TARGET_FILE_DIR:body-harvest-recomp>"
            COMMENT "Copying SDL2, dxcompiler and dxil next to the executable")
    endif()
endif()

# The frontend's own assets. recompui reads them from <executable>/assets, and
# ships none itself: the port supplies the stylesheet and the fonts.
#
# Lato and Noto Emoji are copied out of the RmlUi submodule rather than
# committed. They are SIL Open Font License faces this repository already has,
# and a second copy of a binary is worth avoiding.
#
# PromptFont -- the controller and keyboard glyphs recompui draws input prompts
# with -- is committed under assets/, because it is not vendored anywhere in
# this tree and the alternative was a build that reaches the network to
# succeed. See assets/promptfont/README.md. recompui asks for it by the
# relative path "promptfont/promptfont.ttf", so it keeps its subdirectory.
#
# assets/icons holds the eleven SVGs recompui asks for by name. Without them the
# buttons that use them are still there and still work -- they are laid out,
# focusable and clickable -- and draw nothing at all, which is how their absence
# was noticed. They are drawn for this project rather than taken from another
# port, whose assets are GPL-3.0 while this repository is MIT.
if(BH_WITH_FRONTEND)
    target_compile_definitions(body-harvest-recomp PRIVATE BH_WITH_FRONTEND=1)
    target_link_libraries(body-harvest-recomp PRIVATE recompui recompinput)
    target_include_directories(body-harvest-recomp PRIVATE
        "${BH_LIB_DIR}/RecompFrontend/recompui/include"
        "${BH_LIB_DIR}/RecompFrontend/recompinput/include")

    set(BH_RMLUI_ASSETS
        "${BH_LIB_DIR}/RecompFrontend/recompui/lib/RmlUi/Samples/assets")
    add_custom_command(TARGET body-harvest-recomp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${BH_ASSET_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/recomp.rcss"
                "${BH_RMLUI_ASSETS}/LatoLatin-Regular.ttf"
                "${BH_RMLUI_ASSETS}/LatoLatin-Bold.ttf"
                "${BH_RMLUI_ASSETS}/LatoLatin-Italic.ttf"
                "${BH_RMLUI_ASSETS}/NotoEmoji-Regular.ttf"
                "${BH_ASSET_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons"
                "${BH_ASSET_DIR}/icons"
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "${BH_ASSET_DIR}/promptfont"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/promptfont/promptfont.ttf"
                "${CMAKE_CURRENT_SOURCE_DIR}/assets/promptfont/LICENSE.txt"
                "${BH_ASSET_DIR}/promptfont"
        COMMENT "Staging the frontend's stylesheet and fonts")
endif()

if(BH_WITH_RECOMPILED)
    # The archive-group flags exist to break the cycle between the recompiled
    # code and librecomp, so they are only meaningful when both are in the link.
    # They are also ld/lld-ELF syntax: lld-link ignores them with a warning, so
    # they must not be emitted for an MSVC-style link at all.
    if(BH_WITH_RUNTIME AND UNIX AND NOT APPLE)
        target_link_libraries(body-harvest-recomp PRIVATE
            -Wl,--start-group bh_recompiled librecomp -Wl,--end-group)
    else()
        target_link_libraries(body-harvest-recomp PRIVATE bh_recompiled)
    endif()
    target_compile_definitions(body-harvest-recomp PRIVATE BH_WITH_RECOMPILED=1)
endif()

message(STATUS "body-harvest-recomp configuration:")
message(STATUS "  compiler          ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  runtime + RT64    ${BH_WITH_RUNTIME}")
message(STATUS "  recompiled code   ${BH_WITH_RECOMPILED}")
message(STATUS "  frontend UI       ${BH_WITH_FRONTEND}")
message(STATUS "  recompiler tool   ${BH_BUILD_RECOMPILER}")


