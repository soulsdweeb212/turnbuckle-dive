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
# Python 3 writes the C of the game's variables (tools/game_vars.py) and lists the developer program's files.
find_package(Python3 COMPONENTS Interpreter)
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
elseif(NOT EXISTS ${TD_DECOMP}/port/game_vars.tsv)
    # port/game_vars.tsv says where each game variable goes on PC (docs/GAME_CODE.md, "The game's variables").
    message(STATUS "Game code: not built (${TD_DECOMP} has no port/game_vars.tsv, the map of the game's variables: "
                   "update the decompilation's port-groundwork branch)")
elseif(NOT Python3_FOUND)
    message(STATUS "Game code: not built (no Python 3, which writes the C of the game's variables)")
else()
    set(TD_GAME_CODE ON)
    message(STATUS "Game code: compiled from ${TD_DECOMP}")
endif()

# Our port code, linked into the program and the tests. Besides the ROM check, it holds what the game's C calls in the
# port: port.h's functions (src/port.c, the handle table in src/slots.c, port_rom_read in src/rom.c, and the game's heap
# on PC in src/game_heap.c). All of it is built even without game code, so CI compiles it with both of its compilers.
add_library(td_core STATIC
    src/rom.c
    src/sha1.c
    src/port.c
    src/slots.c
    src/game_heap.c)
target_include_directories(td_core PUBLIC src)
td_strict(td_core)
# The handle table's lock (src/slots.c): a pthread mutex, which on Linux and macOS needs the system's thread library
# (Threads::Threads adds it where it is separate, and nothing where it is not, as on Windows).
find_package(Threads REQUIRED)
target_link_libraries(td_core PUBLIC Threads::Threads)
# Visual Studio's compiler (cl, not clang-cl): src/slots.c relies on volatile loads and stores being ordered as its
# /volatile:ms option says, which is the default only on x86 and x64 (the file explains why).
set_source_files_properties(src/slots.c PROPERTIES COMPILE_OPTIONS "$<$<C_COMPILER_ID:MSVC>:/volatile:ms>")
# The decompilation's folder also holds the user's ROM, as baserom.z64, because the decompilation's own build needs it
# there. TD_DECOMP_ROM lets the program and the tests find it there without being given a path (rom_find in src/rom.c).
target_compile_definitions(td_core PRIVATE TD_DECOMP_ROM="${TD_DECOMP}/baserom.z64")

if(TD_GAME_CODE)
    # The game's variables (docs/GAME_CODE.md, "The game's variables"). tools/game_vars.py turns the decompilation's map
    # of them, port/game_vars.tsv, into C in the build folder: game_vars.c defines every variable the game files use,
    # game_vars_link.txt holds the linker options that give some functions their old names as well, and
    # game_functions.c lists the functions that the game's address tables hold (td_game_functions, below). CMake runs it
    # now, while it configures, and again whenever the map or the tool changes. td_game_vars: that C and
    # src/game_vars.c, which fills the variables from the ROM.
    set(TD_VARS_C ${CMAKE_BINARY_DIR}/game_vars.c)
    set(TD_VARS_LINK ${CMAKE_BINARY_DIR}/game_vars_link.txt)
    set(TD_FUNCTIONS_C ${CMAKE_BINARY_DIR}/game_functions.c)
    execute_process(
        COMMAND ${Python3_EXECUTABLE} -B ${PROJECT_SOURCE_DIR}/tools/game_vars.py ${TD_DECOMP}/port/game_vars.tsv
                ${TD_VARS_C} ${TD_VARS_LINK} ${TD_FUNCTIONS_C}
        RESULT_VARIABLE td_vars_result
        ERROR_VARIABLE td_vars_error)
    if(NOT td_vars_result EQUAL 0)
        message(FATAL_ERROR "tools/game_vars.py could not write the game's variables:\n${td_vars_error}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        ${TD_DECOMP}/port/game_vars.tsv ${PROJECT_SOURCE_DIR}/tools/game_vars.py)
    file(STRINGS ${TD_VARS_LINK} TD_VARS_LINK_OPTIONS ENCODING UTF-8)
    add_library(td_game_vars STATIC ${TD_VARS_C} src/game_vars.c)
    target_include_directories(td_game_vars PUBLIC src)
    target_link_libraries(td_game_vars PUBLIC td_core)
    td_strict(td_game_vars)

    # The game files this build uses so far, compiled in place from the decompilation (docs/GAME_CODE.md says what each
    # one does): the game's ROM loader, then the functions that turn a model, an effect sprite or a picture into display
    # lists, which the viewer draws, and the game's two heap functions. INTERFACE: whatever links td_game also links
    # td_game_vars and td_core, which have the variables and the port functions these files use, but the game files
    # themselves do not see our headers: port.h declares those functions for them.
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
        ${TD_DECOMP}/src/code_80090000/func_80096D8C.c
        ${TD_DECOMP}/src/main/allocateHeap.c
        ${TD_DECOMP}/src/main/releaseHeapBlock.c)
    td_game_code(td_game)
    target_link_libraries(td_game INTERFACE td_game_vars)

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

    # Every game file of the developer program (the program that starts straight into a match), compiled with the
    # game's settings but not linked yet: td_game_all. The decompilation's tools/port_filelist.py works out the list
    # (its docs/port.md says how) and writes two text files: the list, and the paths of every file it read to make it;
    # CMake configures again when one of those files changes, so the list follows the decompilation. A game file that
    # stops compiling on the PC stops a full build. The tool needs the data files that the decompilation's build
    # extracts from the ROM on its first run; without them, or when the tool finds the decompilation's list incomplete,
    # configure says why and leaves td_game_all out (docs/GAME_CODE.md).
    set(TD_MATCH_LIST ${CMAKE_BINARY_DIR}/game_match_files.txt)
    set(TD_MATCH_DEPENDS ${CMAKE_BINARY_DIR}/game_match_depends.txt)
    execute_process(
        COMMAND ${Python3_EXECUTABLE} -B ${TD_DECOMP}/tools/port_filelist.py --set match
                --output ${TD_MATCH_LIST} --depends ${TD_MATCH_DEPENDS}
        WORKING_DIRECTORY ${TD_DECOMP}
        RESULT_VARIABLE td_match_result
        OUTPUT_QUIET
        ERROR_VARIABLE td_match_error)
    if(NOT td_match_result EQUAL 0)
        # The build goes on without td_game_all, and says so as a warning. CMake keeps watching what the tool reads,
        # so that the next build configures again once the cause is fixed (the decompilation's build run, or the
        # game file mended): the files the last complete list depended on, the tool and build.json, and the
        # decompilation's folders, whose dates change when build.py creates its build/ and asm/ folders.
        string(STRIP "${td_match_error}" td_match_error)
        message(WARNING "Game code: td_game_all not built, because tools/port_filelist.py could not list the files "
                        "(docs/GAME_CODE.md says what to do):\n${td_match_error}")
        set(td_match_watch ${TD_DECOMP} ${TD_DECOMP}/build.json ${TD_DECOMP}/tools/port_filelist.py)
        foreach(dir build asm asm/data)
            if(IS_DIRECTORY ${TD_DECOMP}/${dir})
                list(APPEND td_match_watch ${TD_DECOMP}/${dir})
            endif()
        endforeach()
        if(EXISTS ${TD_MATCH_DEPENDS})
            file(STRINGS ${TD_MATCH_DEPENDS} td_match_depends ENCODING UTF-8)
            foreach(path IN LISTS td_match_depends)
                if(EXISTS ${path})
                    list(APPEND td_match_watch ${path})
                endif()
            endforeach()
        endif()
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${td_match_watch})
    else()
        file(STRINGS ${TD_MATCH_LIST} td_match_files ENCODING UTF-8)
        file(STRINGS ${TD_MATCH_DEPENDS} td_match_depends ENCODING UTF-8)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${td_match_depends})
        list(TRANSFORM td_match_files PREPEND ${TD_DECOMP}/)
        list(LENGTH td_match_files td_match_count)
        add_library(td_game_all OBJECT ${td_match_files})
        td_game_code(td_game_all)
        message(STATUS "Game code: td_game_all compiles the developer program's ${td_match_count} files")

        # The functions that the game's address tables hold, by N64 address (game_functions.c, from tools/game_vars.py).
        # It names game functions, so only programs that link every game file use it.
        add_library(td_game_functions STATIC ${TD_FUNCTIONS_C})
        target_link_libraries(td_game_functions PUBLIC td_game_vars)
        td_strict(td_game_functions)

        # The SDK functions the developer program's files call and the port does not provide yet (src/game_sdk.c).
        add_library(td_game_sdk STATIC src/game_sdk.c)
        target_link_libraries(td_game_sdk PUBLIC td_core)
        td_strict(td_game_sdk)
    endif()
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

    # The handle table behind port.h's PTR_TO_SLOT and SLOT_TO_PTR (tests/test_slots.c says what it checks). The three
    # other tests run the same program in a way that must stop it: each passes only when the output holds the table's
    # message, which shows that a handle never given out, one beyond the table's end, and one handle too many, really
    # stop the program.
    add_executable(test_slots tests/test_slots.c)
    target_link_libraries(test_slots PRIVATE td_core)
    td_strict(test_slots)
    add_test(NAME slots COMMAND test_slots)
    add_test(NAME slots_never_issued COMMAND test_slots --never-issued)
    set_tests_properties(slots_never_issued PROPERTIES
        PASS_REGULAR_EXPRESSION "Turnbuckle Dive stopped: SLOT_TO_PTR: handle 0x3039 was never issued")
    add_test(NAME slots_out_of_range COMMAND test_slots --out-of-range)
    set_tests_properties(slots_out_of_range PROPERTIES
        PASS_REGULAR_EXPRESSION "Turnbuckle Dive stopped: SLOT_TO_PTR: handle 0x80123456 was never issued")
    add_test(NAME slots_full COMMAND test_slots --full)
    set_tests_properties(slots_full PROPERTIES
        PASS_REGULAR_EXPRESSION "Turnbuckle Dive stopped: PTR_TO_SLOT: the handle table is full: all 0x100000 handles")

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

        if(TARGET td_game_all)
            # Every game file of the developer program linked with the game's variables and the SDK stand-ins, and a
            # check that the variables start as on the N64 (tests/game_vars/main.c says what it checks). The link
            # itself is the first check: it fails on any name a game file uses and the port lacks. Linux keeps the
            # C library's math functions (sqrtf, which nw2Sqrtf calls) in a library of their own, libm. The linker
            # options give some functions their old names as well (game_vars_link.txt). Like rom_loader, it needs the
            # ROM and is skipped without one.
            add_executable(game_vars_check tests/game_vars/main.c $<TARGET_OBJECTS:td_game_all>)
            target_link_libraries(game_vars_check PRIVATE td_game_functions td_game_sdk $<$<PLATFORM_ID:Linux>:m>)
            target_link_options(game_vars_check PRIVATE ${TD_VARS_LINK_OPTIONS})
            td_strict(game_vars_check)
            add_test(NAME game_vars COMMAND game_vars_check)
            set_tests_properties(game_vars PROPERTIES SKIP_RETURN_CODE 77)
            # The same check with one value left in the N64's byte order: it must fail, which shows that the
            # comparison really looks at the values.
            add_test(NAME game_vars_detects_change COMMAND game_vars_check --unswap)
            set_tests_properties(game_vars_detects_change PROPERTIES
                SKIP_RETURN_CODE 77
                PASS_REGULAR_EXPRESSION "FAIL: main\\.data: .* is 0x[0-9A-F]+ on PC, 0x[0-9A-F]+ in the ROM")
        endif()
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
