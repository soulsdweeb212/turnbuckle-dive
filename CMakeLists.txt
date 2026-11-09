cmake_minimum_required(VERSION 3.25)

project(turnbuckle-dive
    VERSION 0.0.1
    DESCRIPTION "PC port of WCW/nWo Revenge built on LibUltraShip"
    LANGUAGES C CXX)

option(TD_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(TD_BUILD_HELLO
    "Build the programs that open a LibUltraShip window: hello and, with game code, the viewer (needs lib/libultraship)"
    ON)
# The game's own C comes from the decompilation and is compiled in place from its folder; this repository keeps no copy
# of it (docs/GAME_CODE.md). By default the decompilation is the folder "source" next to this repository's folder.
set(TD_DECOMP_DIR "${PROJECT_SOURCE_DIR}/../source" CACHE PATH
    "Folder of the WCW/nWo Revenge decompilation, whose game C is compiled in place")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# On Windows, LibUltraShip and the libraries vcpkg builds for it use the static C runtime, and MSVC refuses to link
# code built for different runtimes, so every target here uses it too.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# MSVC is also set for clang-cl, which reports itself as Clang but takes MSVC-style options.
# -fno-sanitize-recover=undefined: undefined behaviour stops the program at its first report, as a memory error does.
# By default UndefinedBehaviorSanitizer prints its report and lets the program run on, and end with exit code 0.
if(TD_SANITIZE AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND NOT MSVC)
    add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# Our own port code is compiled strictly: C17 and C++20 without compiler extensions, with warnings on. Third-party
# code (LibUltraShip, SDL) keeps its own settings, and the game C from the decompilation gets its own (td_game_code,
# below), because it was written for KMC GCC 2.7.2.
function(td_strict target)
    set_target_properties(${target} PROPERTIES
        C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
        CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    if(MSVC)
        # clang-cl reads -Wall as -Weverything, so it gets /W4 like cl. Calling an undeclared function is an error.
        # _CRT_SECURE_NO_WARNINGS: MSVC warns about standard C functions such as fopen and getenv and suggests its
        # own fopen_s family instead, which other systems do not have.
        target_compile_options(${target} PRIVATE /W4)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
        if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
            target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:C>:-Werror=implicit-function-declaration>)
        else()
            target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:C>:/we4013>)
        endif()
    elseif(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic
            $<$<COMPILE_LANGUAGE:C>:-Werror=implicit-function-declaration>)
    endif()
endfunction()

# The settings for the game's C. It was written for KMC GCC 2.7.2 on the N64; these settings make GCC and Clang read it
# the same way for a 64-bit PC, and stop the build on code that would cut a 64-bit pointer down to 32 bits. The build
# type's own options still apply, so a Debug build keeps its debug information (-g) and no optimisation, and the game's
# C can be stepped through in a debugger. Every game file gets its settings from this one function.
function(td_game_code target)
    # C17 with GNU extensions (-std=gnu17): the game's C uses GCC extensions, and old-style "()" declarations, which the
    # newer C23 rejects.
    set_target_properties(${target} PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS ON)
    # The game's headers: common.h, and port.h, which common.h includes at its end.
    target_include_directories(${target} PRIVATE ${TD_DECOMP}/include)
    target_compile_definitions(${target} PRIVATE
        # Selects the PC version wherever a game file or port.h has one (#ifdef TARGET_PC).
        TARGET_PC
        # Selects the C versions of the functions that the N64 build takes from hand-written assembly.
        NON_MATCHING
        # Selects, where a file has one, a version of the code without the undefined behaviour that the original keeps
        # in order to match the ROM.
        AVOID_UB
        # The display-list format the game uses (F3DEX2), the one LibUltraShip is built for too.
        F3DEX_GBI_2)
    target_compile_options(${target} PRIVATE
        # The game reads the same memory through pointers of different types; this stops the compiler from assuming
        # that such reads and writes never touch the same bytes.
        -fno-strict-aliasing
        # A signed integer that overflows wraps around, as on the N64's CPU, instead of being undefined behaviour.
        -fwrapv
        # Plain char is signed, as on the N64 (on ARM Linux it would be unsigned).
        -fsigned-char
        # No fused multiply-add: the N64 rounds a*b before it adds c, and rounding once instead of twice can change a
        # float result.
        -ffp-contract=off
        # The game has its own sinf and cosf, which port.h renames n64_sinf and n64_cosf so that they give the N64's
        # results; as a second guard, the compiler must not compute sinf or cosf itself.
        -fno-builtin-sinf -fno-builtin-cosf
        # A call to a function that has no declaration: the compiler would take its result as a 32-bit int, which cuts
        # a returned 64-bit pointer in half.
        -Werror=implicit-function-declaration
        # A pointer stored in an integer, or an integer in a pointer, without a cast.
        -Werror=int-conversion
        # A pointer cast to a smaller integer, or such an integer cast to a pointer: on the N64 both are 32 bits, on a
        # 64-bit PC the pointer would lose its upper half.
        -Werror=pointer-to-int-cast -Werror=int-to-pointer-cast)
endfunction()

# Game code is built when the decompilation is there, with the game's PC code, and the compiler is GCC or Clang, whose
# options and extensions the game's C needs. Otherwise the build leaves it out and turnbuckle-dive only checks the ROM:
# on CI, which has no copy of the decompilation, and with MSVC, which CMake also reports for clang-cl. Configure prints
# which case it is.
# TD_DECOMP is TD_DECOMP_DIR written without "..", which is how debuggers and editors show the game's files.
cmake_path(SET TD_DECOMP NORMALIZE "${TD_DECOMP_DIR}")
set(TD_GAME_CODE OFF)
if(MSVC)
    message(STATUS "Game code: not built (the game's C needs GCC or Clang, and this compiler takes MSVC's options)")
elseif(NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    message(STATUS
        "Game code: not built (the game's C needs GCC or Clang, and this compiler is ${CMAKE_C_COMPILER_ID})")
elseif(NOT EXISTS ${TD_DECOMP}/include/common.h)
    message(STATUS "Game code: not built (no decompilation in ${TD_DECOMP}; set TD_DECOMP_DIR to its folder)")
elseif(NOT EXISTS ${TD_DECOMP}/include/port.h)
    # include/port.h starts the game's PC code (the decompilation's docs/port.md). A branch of the decompilation
    # without it has none of that code, and its game files would stop the build here with errors.
    message(STATUS "Game code: not built (${TD_DECOMP} has no include/port.h, the start of the game's PC code: "
                   "check out the decompilation's port-groundwork branch)")
else()
    set(TD_GAME_CODE ON)
    message(STATUS "Game code: compiled from ${TD_DECOMP}")
endif()

# Our port code, linked into the program and the tests. Besides the ROM check, it holds what the game's C calls in the
# port: port.h's functions (src/port.c, and port_rom_read in src/rom.c) and stand-ins for game parts that are not
# compiled yet (src/game_heap.c, src/game_data.c). All of it is built even without game code, so CI compiles it with
# both of its compilers; the stand-ins are only linked into a program that uses game code.
add_library(td_core STATIC
    src/rom.c
    src/sha1.c
    src/port.c
    src/game_heap.c
    src/game_data.c)
target_include_directories(td_core PUBLIC src)
td_strict(td_core)
# The decompilation's folder also holds the user's ROM, as baserom.z64, because the decompilation's own build needs it
# there. TD_DECOMP_ROM lets the program and the tests find it there without being given a path (rom_find in src/rom.c).
target_compile_definitions(td_core PRIVATE TD_DECOMP_ROM="${TD_DECOMP}/baserom.z64")

if(TD_GAME_CODE)
    # The game files this build uses so far, compiled in place from the decompilation (docs/GAME_CODE.md says what each
    # one does): the game's ROM loader, then the functions that turn a model, an effect sprite or a picture into display
    # lists, which the viewer draws. INTERFACE: whatever links td_game also links td_core, which has the port functions
    # these files call, but the game files themselves do not see our headers: port.h declares those functions for them.
    add_library(td_game STATIC
        ${TD_DECOMP}/src/main/initializeAssetSizeTable.c
        ${TD_DECOMP}/src/main/lookupResourceDescriptor.c
        ${TD_DECOMP}/src/main/func_80003684.c
        ${TD_DECOMP}/src/main/func_800037F4.c
        ${TD_DECOMP}/src/main/readRomSynchronousChunks.c
        ${TD_DECOMP}/src/main/func_8000486C.c
        ${TD_DECOMP}/src/main/buildVertexDisplayList.c
        ${TD_DECOMP}/src/main/buildObjectDisplayListTail.c
        ${TD_DECOMP}/src/main/buildCiTextureDisplayList.c
        ${TD_DECOMP}/src/main/func_80004E08.c
        ${TD_DECOMP}/src/main/convertRgba16ToGrayscale.c
        ${TD_DECOMP}/src/main/func_8000670C.c
        ${TD_DECOMP}/src/main/func_80007920.c
        ${TD_DECOMP}/src/main/buildCi4TextureDisplayList.c
        ${TD_DECOMP}/src/code_80090000/func_80096A6C.c
        ${TD_DECOMP}/src/code_80090000/func_80096D8C.c)
    td_game_code(td_game)
    target_link_libraries(td_game INTERFACE td_core)

    # Compiled with the game's settings but never linked: it stops the build if src/game.h and the decompilation's
    # port.h ever declare what they share differently (src/game_check.c explains how). td_game depends on it, so the
    # check also runs when only one program is built, as VS Code does before it starts the debugger.
    add_library(td_game_check OBJECT src/game_check.c)
    td_game_code(td_game_check)
    target_include_directories(td_game_check PRIVATE src)
    add_dependencies(td_game td_game_check)

    # Our code that calls the game's loader, used by the program and the rom_loader test. WHOLE_ARCHIVE: every game
    # file in td_game goes into each program that uses game code, even a file that nothing calls yet, so the link names
    # every function or variable that a game file needs and the build does not have yet. From a plain static library
    # the linker would take only the files that are called, and leave out what the others are missing.
    add_library(td_resources STATIC src/resources.c)
    target_link_libraries(td_resources PUBLIC "$<LINK_LIBRARY:WHOLE_ARCHIVE,td_game>")
    td_strict(td_resources)
endif()

add_executable(turnbuckle-dive src/main.c)
target_link_libraries(turnbuckle-dive PRIVATE td_core)
td_strict(turnbuckle-dive)
target_compile_definitions(turnbuckle-dive PRIVATE TD_VERSION="${PROJECT_VERSION}")
if(TD_GAME_CODE)
    # With game code, the program runs the game's ROM loader after the ROM check (see src/main.c).
    target_link_libraries(turnbuckle-dive PRIVATE td_resources)
    target_compile_definitions(turnbuckle-dive PRIVATE TD_GAME_CODE)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(test_sha1 tests/test_sha1.c)
    target_link_libraries(test_sha1 PRIVATE td_core)
    td_strict(test_sha1)
    add_test(NAME sha1 COMMAND test_sha1)

    # The program itself, started with a ROM that does not exist: it must start and say so. It needs no ROM, so it
    # runs on CI too, which must never have one.
    add_test(NAME rom_missing COMMAND turnbuckle-dive ${CMAKE_BINARY_DIR}/no-rom-here.z64)
    set_tests_properties(rom_missing PROPERTIES PASS_REGULAR_EXPRESSION "no-rom-here\\.z64: file not found")

    if(TD_GAME_CODE)
        # The game's ROM loader on the user's ROM (tests/rom_loader/main.c says what it checks). It needs the ROM, so
        # it is built only with game code, and CTest reports it as skipped when it finds no ROM (exit code 77).
        add_executable(rom_loader tests/rom_loader/main.c)
        target_link_libraries(rom_loader PRIVATE td_resources)
        td_strict(rom_loader)
        add_test(NAME rom_loader COMMAND rom_loader)
        set_tests_properties(rom_loader PROPERTIES SKIP_RETURN_CODE 77)
        # The same test with one byte of resource 0x123 changed after it is loaded: it must fail on the digest, which
        # shows that rom_loader's comparison really looks at the bytes of the resources.
        add_test(NAME rom_loader_detects_change COMMAND rom_loader --flip 0x123)
        set_tests_properties(rom_loader_detects_change PROPERTIES
            SKIP_RETURN_CODE 77
            PASS_REGULAR_EXPRESSION "FAIL: the digest of the 7657 resources of .* is [0-9a-f]+, expected")
    endif()
endif()

if(TD_BUILD_HELLO)
    # Visual Studio's 32-bit compiler would only fail after vcpkg has spent minutes building 64-bit libraries, so stop
    # at once if it is the one in use.
    if(MSVC AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "This is Visual Studio's 32-bit compiler. Delete the build folder, open the "
                            "\"x64 Native Tools Command Prompt for VS 2022\" and configure again.")
    endif()
    include(cmake/libultraship.cmake)

    # What the programs that open a LibUltraShip window share: its start-up (src/context.cpp), drawing and checking
    # the game's picture (src/frame.cpp), and the debug menu (src/debug.cpp). PUBLIC: what links td_window also gets
    # LibUltraShip and these settings. LibUltraShip keeps its GBI choice private; code that builds display lists must
    # use the same one. Its headers include <windows.h> on Windows, whose min and max macros would break std::min and
    # std::max. f3d_o2r: the programs open f3d.o2r, LibUltraShip's shaders, when they start.
    add_library(td_window STATIC src/context.cpp src/frame.cpp src/debug.cpp)
    target_link_libraries(td_window PUBLIC libultraship)
    target_include_directories(td_window PUBLIC src)
    target_compile_definitions(td_window PUBLIC F3DEX_GBI_2 $<$<PLATFORM_ID:Windows>:NOMINMAX>)
    td_strict(td_window)
    add_dependencies(td_window f3d_o2r)

    add_executable(hello hello/main.cpp)
    target_link_libraries(hello PRIVATE td_window)
    td_strict(hello)

    if(TD_GAME_CODE)
        # The asset viewer (docs/VIEWER.md): it draws a model, an effect sprite and a picture from the ROM with the
        # game's own functions in td_game, so it needs game code as well as LibUltraShip, and CI, which has no game
        # code, never builds it.
        # viewer/viewer_game.c includes the game's headers, so it gets the game's settings, like src/game_check.c,
        # plus the warnings that the port's own code is held to (-Wpedantic is left out: the game's headers use GCC
        # extensions on purpose). The viewer's other files are port code with the port's strict settings.
        # td_resources brings the game's loader and every file in td_game.
        add_library(td_viewer_game OBJECT viewer/viewer_game.c)
        td_game_code(td_viewer_game)
        target_compile_options(td_viewer_game PRIVATE -Wall -Wextra)
        add_executable(viewer viewer/main.cpp viewer/viewer_rom.c $<TARGET_OBJECTS:td_viewer_game>)
        target_link_libraries(viewer PRIVATE td_window td_resources)
        td_strict(viewer)

        if(BUILD_TESTING)
            find_package(Python3 COMPONENTS Interpreter)
            if(Python3_FOUND)
                # The viewer's check (docs/VIEWER.md) in two tests. viewer_reference has tools/viewer_ref.py compute
                # from the user's ROM, without the game's code, what the head, the effect sprite and the picture must
                # look like, pixel by pixel, into the build folder (it holds data decoded from the game, so it is never
                # committed). viewer_check then draws them with the game's code, reads each back and compares. FIXTURES:
                # CTest runs viewer_reference first, also when only viewer_check is asked for. Both need the ROM, so
                # CTest reports them as skipped when there is none (exit code 77). The viewer opens a window; it runs in
                # the build folder, where it writes its log and settings.
                add_test(NAME viewer_reference
                    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/viewer_ref.py
                            --or-rom ${TD_DECOMP}/baserom.z64 --out ${CMAKE_BINARY_DIR}/viewer_ref.json)
                set_tests_properties(viewer_reference PROPERTIES FIXTURES_SETUP viewer_reference SKIP_RETURN_CODE 77)
                add_test(NAME viewer_check COMMAND viewer --check --reference ${CMAKE_BINARY_DIR}/viewer_ref.json
                    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
                set_tests_properties(viewer_check PROPERTIES FIXTURES_REQUIRED viewer_reference SKIP_RETURN_CODE 77)
                if(TD_SANITIZE)
                    # In the asan preset the window's GPU driver and SDL's sound thread keep a few blocks to the end
                    # that the program cannot free (hello shows the same ones), and LeakSanitizer would report them
                    # and fail the test. So this test runs without LeakSanitizer; AddressSanitizer's memory errors
                    # and UndefinedBehaviorSanitizer still stop the viewer and fail it. docs/VIEWER.md says how to
                    # run the check with LeakSanitizer and read its report.
                    set_tests_properties(viewer_check PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
                endif()
            else()
                message(STATUS "viewer_check: not added (no Python 3, which computes its reference, was found)")
            endif()
        endif()
    endif()
endif()

# Every test in this file fails when its output holds a report of AddressSanitizer, LeakSanitizer or
# UndefinedBehaviorSanitizer (the asan preset), whatever its exit code: CTest ignores the exit code of a test that
# passes on its output, such as rom_missing, so such a test would otherwise pass with the report in it. These lines
# come last, after every add_test.
if(BUILD_TESTING)
    get_directory_property(TD_TESTS TESTS)
    set_tests_properties(${TD_TESTS} PROPERTIES
        FAIL_REGULAR_EXPRESSION "AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer|runtime error:")
endif()
