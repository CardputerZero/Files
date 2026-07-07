cmake_minimum_required(VERSION 3.16)

project(M5CardputerZeroFiles VERSION 0.1.1 LANGUAGES C CXX ASM)

option(FILES_USE_SDL "Use LVGL's SDL backend for desktop testing" ON)
option(FILES_CONVERT_ASSETS "Convert image assets during CMake configure" ON)
set(FILES_BIN_NAME "M5CardputerZero-Files" CACHE STRING "Output executable name")

if(APPLE AND NOT FILES_USE_SDL)
    message(FATAL_ERROR "macOS builds only support FILES_USE_SDL=ON.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(FILES_ROOT "${CMAKE_CURRENT_LIST_DIR}")
set(FILES_SRC_DIR "${FILES_ROOT}/src")
set(FILES_DEPS_DIR "${FILES_ROOT}/dependencies")
set(FILES_LVGL_DIR "${FILES_DEPS_DIR}/lvgl")
set(FILES_SPDLOG_DIR "${FILES_DEPS_DIR}/spdlog")
set(FILES_SMOOTH_UI_DIR "${FILES_DEPS_DIR}/smooth_ui_toolkit")
set(FILES_MINIAUDIO_DIR "${FILES_DEPS_DIR}/miniaudio")
set(FILES_BOX2D_DIR "${FILES_DEPS_DIR}/box2d")
set(FILES_SYSTEM_LIBS ${CMAKE_DL_LIBS})

if(UNIX AND NOT APPLE)
    list(APPEND FILES_SYSTEM_LIBS m)
endif()

if(APPLE)
    list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew" "/usr/local")
endif()

if(NOT DEFINED Python3_EXECUTABLE)
    foreach(candidate IN ITEMS
        "${FILES_ROOT}/.venv/bin/python"
        "${FILES_ROOT}/../.venv/bin/python"
        "${FILES_ROOT}/../../.venv/bin/python"
    )
        if(EXISTS "${candidate}")
            execute_process(
                COMMAND "${candidate}" -m pip --version
                RESULT_VARIABLE FILES_PYTHON_PIP_RESULT
                OUTPUT_QUIET
                ERROR_QUIET
            )
            if(FILES_PYTHON_PIP_RESULT EQUAL 0)
                set(Python3_EXECUTABLE "${candidate}")
                break()
            endif()
        endif()
    endforeach()
endif()

foreach(required_dir IN LISTS FILES_LVGL_DIR FILES_SPDLOG_DIR FILES_SMOOTH_UI_DIR FILES_MINIAUDIO_DIR FILES_BOX2D_DIR)
    if(NOT EXISTS "${required_dir}")
        message(FATAL_ERROR "Missing dependency: ${required_dir}. Run ./bootstrap.sh first.")
    endif()
endforeach()

find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig QUIET)

if(FILES_CONVERT_ASSETS)
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${FILES_SRC_DIR}/assets/convert_images.py"
        WORKING_DIRECTORY "${FILES_ROOT}"
        RESULT_VARIABLE FILES_ASSET_RESULT
    )
    if(NOT FILES_ASSET_RESULT EQUAL 0)
        message(FATAL_ERROR "Image asset conversion failed")
    endif()
endif()

set(LV_BUILD_CONF_PATH "${FILES_ROOT}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)

set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)

set(SMOOTH_UI_TOOLKIT_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SMOOTH_UI_TOOLKIT_BUILD_TEST OFF CACHE BOOL "" FORCE)

set(BOX2D_SAMPLES OFF CACHE BOOL "" FORCE)
set(BOX2D_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(BOX2D_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(BOX2D_VALIDATE OFF CACHE BOOL "" FORCE)

add_subdirectory("${FILES_LVGL_DIR}" EXCLUDE_FROM_ALL)
add_subdirectory("${FILES_SPDLOG_DIR}" EXCLUDE_FROM_ALL)
add_subdirectory("${FILES_SMOOTH_UI_DIR}" EXCLUDE_FROM_ALL)
add_subdirectory("${FILES_BOX2D_DIR}" EXCLUDE_FROM_ALL)

function(files_add_sdl2_parent_include target imported_target)
    if(NOT TARGET "${imported_target}")
        return()
    endif()

    get_target_property(sdl2_include_dirs "${imported_target}" INTERFACE_INCLUDE_DIRECTORIES)
    if(NOT sdl2_include_dirs)
        return()
    endif()

    foreach(sdl2_include_dir IN LISTS sdl2_include_dirs)
        if(sdl2_include_dir MATCHES "/SDL2$")
            get_filename_component(sdl2_parent_include_dir "${sdl2_include_dir}" DIRECTORY)
            target_include_directories("${target}" SYSTEM PRIVATE "${sdl2_parent_include_dir}")
        endif()
    endforeach()
endfunction()

target_compile_definitions(lvgl PUBLIC
    FILES_LVGL_USE_SDL=$<IF:$<BOOL:${FILES_USE_SDL}>,1,0>
    FILES_LVGL_FS_POSIX_PATH=""
)

if(FILES_USE_SDL)
    if(PkgConfig_FOUND)
        pkg_check_modules(SDL2 QUIET IMPORTED_TARGET sdl2)
    endif()

    if(SDL2_FOUND)
        target_link_libraries(lvgl PRIVATE PkgConfig::SDL2)
        files_add_sdl2_parent_include(lvgl PkgConfig::SDL2)
        set(FILES_SDL_LIBS PkgConfig::SDL2)
    else()
        find_package(SDL2 CONFIG QUIET)
        if(TARGET SDL2::SDL2)
            target_link_libraries(lvgl PRIVATE SDL2::SDL2)
            files_add_sdl2_parent_include(lvgl SDL2::SDL2)
            set(FILES_SDL_LIBS SDL2::SDL2)
        else()
            find_package(SDL2 REQUIRED)
            target_include_directories(lvgl SYSTEM PRIVATE ${SDL2_INCLUDE_DIRS})
            foreach(sdl2_include_dir IN LISTS SDL2_INCLUDE_DIRS)
                if(sdl2_include_dir MATCHES "/SDL2$")
                    get_filename_component(sdl2_parent_include_dir "${sdl2_include_dir}" DIRECTORY)
                    target_include_directories(lvgl SYSTEM PRIVATE "${sdl2_parent_include_dir}")
                endif()
            endforeach()
            set(FILES_SDL_LIBS ${SDL2_LIBRARIES})
        endif()
    endif()
endif()

file(GLOB FILES_CPP CONFIGURE_DEPENDS
    "${FILES_SRC_DIR}/main.cpp"
    "${FILES_SRC_DIR}/core/*.cpp"
    "${FILES_SRC_DIR}/assets/*.cpp"
    "${FILES_SRC_DIR}/hal/*.cpp"
    "${FILES_SRC_DIR}/input/*.cpp"
    "${FILES_SRC_DIR}/models/*.cpp"
    "${FILES_SRC_DIR}/view_models/*.cpp"
    "${FILES_SRC_DIR}/views/*.cpp"
)
file(GLOB_RECURSE FILES_PREVIEW_CPP CONFIGURE_DEPENDS
    "${FILES_SRC_DIR}/preview/*.cpp"
    "${FILES_SRC_DIR}/preview/*.c"
)
file(GLOB_RECURSE FILES_ASSET_C CONFIGURE_DEPENDS
    "${FILES_SRC_DIR}/assets/res_c/*.c"
)

add_executable(${FILES_BIN_NAME}
    ${FILES_CPP}
    ${FILES_PREVIEW_CPP}
    ${FILES_ASSET_C}
)

target_include_directories(${FILES_BIN_NAME} PRIVATE
    "${FILES_SRC_DIR}"
    "${FILES_SRC_DIR}/assets"
    "${FILES_MINIAUDIO_DIR}"
)

target_compile_definitions(${FILES_BIN_NAME} PRIVATE
    FILES_USE_SDL=$<IF:$<BOOL:${FILES_USE_SDL}>,1,0>
)

target_link_libraries(${FILES_BIN_NAME} PRIVATE
    lvgl::lvgl
    box2d
    smooth_ui_toolkit
    spdlog::spdlog
    Threads::Threads
    ${FILES_SDL_LIBS}
    ${FILES_SYSTEM_LIBS}
)

set_target_properties(${FILES_BIN_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${FILES_ROOT}/dist"
)
