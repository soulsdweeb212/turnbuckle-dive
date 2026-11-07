cmake_minimum_required(VERSION 3.24)

project(turnbuckle-dive
    VERSION 0.0.1
    DESCRIPTION "PC port of WCW/nWo Revenge built on LibUltraShip"
    LANGUAGES C)

option(TD_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

# Our own port code is plain C17, compiled strictly. The game C from the decompilation (added later) gets its own
# settings, because it was written for KMC GCC 2.7.2.
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set(TD_WARNINGS -Wall -Wextra -Wpedantic -Werror=implicit-function-declaration)
    if(TD_SANITIZE)
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

# Shared port code, linked into the program and the tests.
add_library(td_core STATIC
    src/rom.c
    src/sha1.c)
target_include_directories(td_core PUBLIC src)
target_compile_options(td_core PRIVATE ${TD_WARNINGS})

add_executable(turnbuckle-dive src/main.c)
target_link_libraries(turnbuckle-dive PRIVATE td_core)
target_compile_options(turnbuckle-dive PRIVATE ${TD_WARNINGS})
# TD_SOURCE_DIR lets a developer build find the decompilation's ROM next to this repo without extra arguments.
target_compile_definitions(turnbuckle-dive PRIVATE
    TD_VERSION="${PROJECT_VERSION}"
    TD_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

include(CTest)
if(BUILD_TESTING)
    add_executable(test_sha1 tests/test_sha1.c)
    target_link_libraries(test_sha1 PRIVATE td_core)
    target_compile_options(test_sha1 PRIVATE ${TD_WARNINGS})
    add_test(NAME sha1 COMMAND test_sha1)
endif()
