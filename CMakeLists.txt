cmake_minimum_required(VERSION 3.25)

project(turnbuckle-dive
    VERSION 0.0.1
    DESCRIPTION "PC port of WCW/nWo Revenge built on LibUltraShip"
    LANGUAGES C CXX)

option(TD_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(TD_BUILD_HELLO "Build hello, the LibUltraShip window test (needs the lib/libultraship submodule)" ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# On Windows, LibUltraShip and the libraries vcpkg builds for it use the static C runtime, and MSVC refuses to link
# code built for different runtimes, so every target here uses it too.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# MSVC is also set for clang-cl, which reports itself as Clang but takes MSVC-style options.
if(TD_SANITIZE AND CMAKE_C_COMPILER_ID MATCHES "GNU|Clang" AND NOT MSVC)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# Our own port code is compiled strictly: C17 and C++20 without compiler extensions, with warnings on. Third-party
# code (LibUltraShip, SDL) keeps its own settings, and the game C from the decompilation (added later) gets its own,
# because it was written for KMC GCC 2.7.2.
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

# Shared port code, linked into the program and the tests.
add_library(td_core STATIC
    src/rom.c
    src/sha1.c)
target_include_directories(td_core PUBLIC src)
td_strict(td_core)

add_executable(turnbuckle-dive src/main.c)
target_link_libraries(turnbuckle-dive PRIVATE td_core)
td_strict(turnbuckle-dive)
# TD_SOURCE_DIR lets a developer build find the decompilation's ROM next to this repo without extra arguments.
target_compile_definitions(turnbuckle-dive PRIVATE
    TD_VERSION="${PROJECT_VERSION}"
    TD_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

include(CTest)
if(BUILD_TESTING)
    add_executable(test_sha1 tests/test_sha1.c)
    target_link_libraries(test_sha1 PRIVATE td_core)
    td_strict(test_sha1)
    add_test(NAME sha1 COMMAND test_sha1)

    # The program itself, started with a ROM that does not exist: it must start and say so. (The tests never use a
    # ROM, because CI machines must never have one.)
    add_test(NAME rom_missing COMMAND turnbuckle-dive ${CMAKE_BINARY_DIR}/no-rom-here.z64)
    set_tests_properties(rom_missing PROPERTIES PASS_REGULAR_EXPRESSION "no-rom-here\\.z64: file not found")
endif()

if(TD_BUILD_HELLO)
    # Visual Studio's 32-bit compiler would only fail after vcpkg has spent minutes building 64-bit libraries, so stop
    # at once if it is the one in use.
    if(MSVC AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "This is Visual Studio's 32-bit compiler. Delete the build folder, open the "
                            "\"x64 Native Tools Command Prompt for VS 2022\" and configure again.")
    endif()
    include(cmake/libultraship.cmake)

    add_executable(hello hello/main.cpp)
    target_link_libraries(hello PRIVATE libultraship)
    # LibUltraShip keeps its GBI choice private; code that builds display lists must use the same one. Its headers
    # include <windows.h> on Windows, whose min and max macros would break std::min and std::max.
    target_compile_definitions(hello PRIVATE F3DEX_GBI_2 $<$<PLATFORM_ID:Windows>:NOMINMAX>)
    td_strict(hello)
    add_dependencies(hello f3d_o2r)
endif()
