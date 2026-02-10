# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2025-2026 Kernel Forge LLC

cmake_minimum_required(VERSION 3.10)
project(ota_workbench C CXX)

include(CheckIncludeFile)
include(CheckCXXSourceCompiles)

# Vendored dependencies (pinned snapshots in external/):
# - ImGui
# - ImGuiFileDialog
# - civetweb
# - inih
# Exact upstream commits/tags are tracked in external/DEPENDENCIES.md.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(POLICY CMP0072)
    cmake_policy(SET CMP0072 NEW)
endif()
set(OpenGL_GL_PREFERENCE GLVND)

option(OTA_WORKBENCH_STATIC_RUNTIME
    "Link libstdc++ and libgcc statically when available"
    OFF
)
option(OTA_WORKBENCH_WARNINGS
    "Enable project warning flags"
    ON
)
option(OTA_WORKBENCH_WERROR
    "Treat project warnings as errors"
    OFF
)
option(OTA_WORKBENCH_BUILD_TESTS
    "Build headless unit tests"
    OFF
)

function(ota_workbench_apply_warnings target_name)
    if(OTA_WORKBENCH_WARNINGS)
        if(MSVC)
            target_compile_options(${target_name} PRIVATE /W4)
        else()
            target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
        endif()
    endif()

    if(OTA_WORKBENCH_WERROR)
        if(MSVC)
            target_compile_options(${target_name} PRIVATE /WX)
        else()
            target_compile_options(${target_name} PRIVATE -Werror)
        endif()
    endif()
endfunction()

if(OTA_WORKBENCH_STATIC_RUNTIME)
    set(_OTA_WORKBENCH_REQUIRED_FLAGS_SAVE "${CMAKE_REQUIRED_FLAGS}")
    set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -static-libstdc++ -static-libgcc")
    check_cxx_source_compiles("int main(void) { return 0; }"
        OTA_WORKBENCH_HAVE_STATIC_RUNTIME
    )
    set(CMAKE_REQUIRED_FLAGS "${_OTA_WORKBENCH_REQUIRED_FLAGS_SAVE}")
    unset(_OTA_WORKBENCH_REQUIRED_FLAGS_SAVE)

    if(OTA_WORKBENCH_HAVE_STATIC_RUNTIME)
        add_link_options(-static-libstdc++ -static-libgcc)
    else()
        message(STATUS
            "Static libstdc++/libgcc not available; using dynamic runtime linking."
        )
    endif()
endif()

# ImGui sources
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/external/imgui)

set(IGFD_DIR ${CMAKE_SOURCE_DIR}/external/ImGuiFileDialog)

set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp
    # Backends
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    ${IGFD_DIR}/ImGuiFileDialog.cpp
)

add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${IGFD_DIR}
)

# Find GLFW and OpenGL
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW3 REQUIRED glfw3)
# cJSON is a system dependency (intentionally not vendored). Some distros
# do not ship a cjson.pc file, so pkg-config is optional here.
pkg_check_modules(CJSON_PC QUIET cjson)
if(CJSON_PC_FOUND)
    set(CJSON_INCLUDE_DIRS ${CJSON_PC_INCLUDE_DIRS})
    set(CJSON_LIBRARIES ${CJSON_PC_LIBRARIES})
else()
    find_path(CJSON_INCLUDE_DIR cjson/cJSON.h)
    find_library(CJSON_LIBRARY cjson)
    if(CJSON_INCLUDE_DIR AND CJSON_LIBRARY)
        set(CJSON_INCLUDE_DIRS ${CJSON_INCLUDE_DIR})
        set(CJSON_LIBRARIES ${CJSON_LIBRARY})
    else()
        message(FATAL_ERROR "Could not find cJSON")
    endif()
endif()

find_package(OpenGL REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(X11 REQUIRED)

set(CMAKE_REQUIRED_INCLUDES ${OPENSSL_INCLUDE_DIR} ${OPENSSL_INCLUDE_DIRS})
check_include_file(openssl/engine.h CIVETWEB_HAVE_OPENSSL_ENGINE_H)
unset(CMAKE_REQUIRED_INCLUDES)

add_library(civetweb STATIC
    external/civetweb/src/civetweb.c
)
target_include_directories(civetweb PUBLIC
    ${CMAKE_SOURCE_DIR}/external/civetweb/include
)
target_compile_definitions(civetweb
    PRIVATE
        NO_CGI
        NO_SSL_DL
        USE_WEBSOCKET
        USE_SSL
        $<$<BOOL:${CIVETWEB_HAVE_OPENSSL_ENGINE_H}>:HAVE_OPENSSL_ENGINE_H>
)
target_link_libraries(civetweb PUBLIC
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
)

add_library(inih_vendor STATIC
    external/inih/ini.c
)
target_include_directories(inih_vendor PUBLIC
    ${CMAKE_SOURCE_DIR}/external/inih
)

add_library(ota_workbench_core STATIC
    src/app_config.cpp
    src/crypto.cpp
    src/log.cpp
    src/manifest.cpp
    src/release_store.cpp
)

target_include_directories(ota_workbench_core
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src
    PRIVATE
        ${CMAKE_SOURCE_DIR}/external/inih
        ${CJSON_INCLUDE_DIRS}
)

target_link_libraries(ota_workbench_core
    PUBLIC
        inih_vendor
        civetweb
        ${CJSON_LIBRARIES}
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads
)

if(CMAKE_DL_LIBS)
    target_link_libraries(ota_workbench_core PUBLIC ${CMAKE_DL_LIBS})
endif()

ota_workbench_apply_warnings(ota_workbench_core)

add_executable(ota-workbench
    src/main.cpp
    src/ui/panels.cpp
)

target_include_directories(ota-workbench PRIVATE
    ${GLFW3_INCLUDE_DIRS}
    ${CJSON_INCLUDE_DIRS}
)

target_link_libraries(ota-workbench PRIVATE
    ota_workbench_core
    imgui
    ${GLFW3_LIBRARIES}
    OpenGL::GL
    X11::X11
)

ota_workbench_apply_warnings(ota-workbench)

if(OTA_WORKBENCH_BUILD_TESTS)
    enable_testing()

    add_executable(ota-workbench-tests
        tests/test_main.cpp
    )
    target_link_libraries(ota-workbench-tests PRIVATE
        ota_workbench_core
    )
    ota_workbench_apply_warnings(ota-workbench-tests)

    add_test(NAME ota-workbench-tests COMMAND ota-workbench-tests)
endif()
