# LibUltraShip (the lib/libultraship git submodule, pinned to one commit), the libraries it needs that are not system
# packages, and f3d.o2r, the archive of its shaders.

set(TD_LUS_DIR ${PROJECT_SOURCE_DIR}/lib/libultraship)
if(NOT EXISTS ${TD_LUS_DIR}/CMakeLists.txt)
    message(FATAL_ERROR "lib/libultraship is empty. Clone with --recursive, or run: git submodule update --init")
endif()

include(FetchContent)
find_package(Git REQUIRED)

# Runs git in a folder and stops the configure if it fails. The arguments are passed one by one, so paths with spaces
# are safe.
function(td_git dir)
    execute_process(COMMAND ${GIT_EXECUTABLE} ${ARGN} WORKING_DIRECTORY ${dir} RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        list(JOIN ARGN " " command)
        message(FATAL_ERROR "git ${command} failed in ${dir}")
    endif()
endfunction()

# Fixes for LibUltraShip bugs that are not fixed upstream yet, applied to the submodule's files (git status leaves
# them out: see .gitmodules). Each patch says why it exists. The files a patch changes are configure dependencies, so
# reverting one makes the next build configure again and re-apply the patch. Before moving the submodule to another
# commit, undo them with: git -C lib/libultraship checkout -- .
file(GLOB TD_LUS_PATCHES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/cmake/patches/libultraship/*.patch)
foreach(patch ${TD_LUS_PATCHES})
    file(STRINGS ${patch} patched_files REGEX "^\\+\\+\\+ b/")
    list(TRANSFORM patched_files REPLACE "^\\+\\+\\+ b/" "${TD_LUS_DIR}/")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${patch} ${patched_files})
    execute_process(COMMAND ${GIT_EXECUTABLE} apply --reverse --check ${patch}
        WORKING_DIRECTORY ${TD_LUS_DIR}
        RESULT_VARIABLE patch_missing
        OUTPUT_QUIET ERROR_QUIET)
    if(patch_missing)
        execute_process(COMMAND ${GIT_EXECUTABLE} apply ${patch}
            WORKING_DIRECTORY ${TD_LUS_DIR}
            RESULT_VARIABLE patch_result)
        if(NOT patch_result EQUAL 0)
            message(FATAL_ERROR "Could not apply ${patch} to lib/libultraship. If an older version of it is applied, "
                                "undo it with: git -C lib/libultraship checkout -- .")
        endif()
        message(STATUS "Patched LibUltraShip: ${patch}")
    endif()
endforeach()

# SDL3. Ubuntu 24.04 has no SDL3 package, so build the release LibUltraShip's own CI uses (the tarball is signed by
# SDL's maintainer; the hash below is of that signed file). OVERRIDE_FIND_PACKAGE makes LibUltraShip's
# find_package(SDL3) use this copy. Linked statically, so the programs need no SDL library next to them.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
        URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.12/SDL3-3.4.12.tar.gz
        URL_HASH SHA256=f07b958a9ac5020fb7a44cadb957f658b2149c3c8abb4f63145fac9303249db7
        SYSTEM
        OVERRIDE_FIND_PACKAGE)
    FetchContent_MakeAvailable(SDL3)
endif()

# ImGui. LibUltraShip clones its whole repository (135 MB of history) once per build folder. Instead, fetch only the
# pinned tag, once, into build/_sources where every preset can share it, and apply LibUltraShip's ImGui patch there
# ourselves: FetchContent applies no patches to a source folder given through FETCHCONTENT_SOURCE_DIR_IMGUI. The
# folder's .td-ready file records the tag and the patch's hash, and the folder is fetched again when either changes.
set(TD_IMGUI_TAG v1.91.9b-docking)
set(TD_IMGUI_PATCH ${TD_LUS_DIR}/cmake/dependencies/patches/imgui-fixes-and-config.patch)
set(TD_IMGUI_DIR ${PROJECT_SOURCE_DIR}/build/_sources/imgui-${TD_IMGUI_TAG})
file(READ ${TD_LUS_DIR}/cmake/dependencies/common.cmake TD_LUS_DEPENDENCIES)
string(FIND "${TD_LUS_DEPENDENCIES}" "GIT_TAG ${TD_IMGUI_TAG}" TD_IMGUI_PINNED)
if(TD_IMGUI_PINNED EQUAL -1 OR NOT EXISTS ${TD_IMGUI_PATCH})
    message(FATAL_ERROR "LibUltraShip no longer pins ImGui ${TD_IMGUI_TAG} with ${TD_IMGUI_PATCH}: update "
                        "${CMAKE_CURRENT_LIST_FILE} to match its cmake/dependencies/common.cmake")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${TD_IMGUI_PATCH})
file(SHA256 ${TD_IMGUI_PATCH} TD_IMGUI_PATCH_HASH)
set(TD_IMGUI_STAMP "${TD_IMGUI_TAG} ${TD_IMGUI_PATCH_HASH}")
set(TD_IMGUI_READY "")
if(EXISTS ${TD_IMGUI_DIR}/.td-ready)
    file(READ ${TD_IMGUI_DIR}/.td-ready TD_IMGUI_READY)
endif()
if(NOT "${TD_IMGUI_READY}" STREQUAL "${TD_IMGUI_STAMP}")
    message(STATUS "Fetching ImGui ${TD_IMGUI_TAG}")
    file(REMOVE_RECURSE ${TD_IMGUI_DIR})
    file(MAKE_DIRECTORY ${TD_IMGUI_DIR})
    td_git(${TD_IMGUI_DIR} init --quiet)
    td_git(${TD_IMGUI_DIR} fetch --quiet --depth 1 https://github.com/ocornut/imgui.git refs/tags/${TD_IMGUI_TAG})
    td_git(${TD_IMGUI_DIR} checkout --quiet FETCH_HEAD)
    td_git(${TD_IMGUI_DIR} apply ${TD_IMGUI_PATCH})
    file(WRITE ${TD_IMGUI_DIR}/.td-ready "${TD_IMGUI_STAMP}")
endif()
set(FETCHCONTENT_SOURCE_DIR_IMGUI ${TD_IMGUI_DIR} CACHE PATH "Shared ImGui source (see cmake/libultraship.cmake)" FORCE)

# LibUltraShip declares GBI_UCODE with option(), which only holds ON/OFF, so its default F3DEX_GBI_2 becomes OFF (a
# useless -DOFF). Revenge uses F3DEX2, so set it before LibUltraShip reads it.
set(GBI_UCODE F3DEX_GBI_2 CACHE STRING "GBI microcode LibUltraShip is built for" FORCE)

# SYSTEM: warnings inside LibUltraShip's headers are LibUltraShip's business, not part of our strict build.
add_subdirectory(${TD_LUS_DIR} ${CMAKE_BINARY_DIR}/libultraship SYSTEM)

# f3d.o2r holds LibUltraShip's shaders (no game data), zipped with their paths kept, e.g.
# shaders/opengl/default.shader.glsl, and placed next to the programs.
set(TD_F3D_O2R ${CMAKE_BINARY_DIR}/f3d.o2r)
file(GLOB_RECURSE TD_F3D_SHADERS RELATIVE ${TD_LUS_DIR}/src/fast CONFIGURE_DEPENDS ${TD_LUS_DIR}/src/fast/shaders/*)
list(TRANSFORM TD_F3D_SHADERS PREPEND ${TD_LUS_DIR}/src/fast/ OUTPUT_VARIABLE TD_F3D_SHADER_PATHS)
add_custom_command(OUTPUT ${TD_F3D_O2R}
    COMMAND ${CMAKE_COMMAND} -E tar cf ${TD_F3D_O2R} --format=zip -- ${TD_F3D_SHADERS}
    WORKING_DIRECTORY ${TD_LUS_DIR}/src/fast
    DEPENDS ${TD_F3D_SHADER_PATHS}
    COMMENT "Packing LibUltraShip's shaders into f3d.o2r"
    VERBATIM)
add_custom_target(f3d_o2r DEPENDS ${TD_F3D_O2R})
