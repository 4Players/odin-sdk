cmake_minimum_required(VERSION 3.28)

project(
    odin_samples
    VERSION 2.0.0
    DESCRIPTION "Sample clients for ODIN Voice written in C/C++"
    HOMEPAGE_URL "https://www.4players.io"
    LANGUAGES C CXX
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Default to debug build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH ON)

include("./cmake/dependencies.cmake")

set(ARCH ${CMAKE_SYSTEM_PROCESSOR})
if(ARCH STREQUAL "x86_64" OR ARCH STREQUAL "amd64" OR ARCH STREQUAL "AMD64")
    set(ARCH "x86_64")
elseif(ARCH STREQUAL "x86" OR ARCH STREQUAL "i386" OR ARCH STREQUAL "i686")
    set(ARCH "x86")
elseif(ARCH STREQUAL "aarch64" OR ARCH STREQUAL "arm64" OR ARCH STREQUAL "arm64e" OR ARCH STREQUAL "ARM64")
    set(ARCH "aarch64")
else()
    message(FATAL_ERROR "Unknown processor; ${ARCH}")
endif()

if(WIN32)
    set(ODIN_SDK_DIR ${CMAKE_SOURCE_DIR}/../bin/windows-${ARCH})
elseif(APPLE)
    set(ODIN_SDK_DIR ${CMAKE_SOURCE_DIR}/../bin/macos-${ARCH})
elseif(UNIX)
    set(ODIN_SDK_DIR ${CMAKE_SOURCE_DIR}/../bin/linux-${ARCH})
else()
    message(FATAL_ERROR "Unknown platform; only Windows, macOS and Linux are supported")
endif()

add_library(odin SHARED IMPORTED)
set_target_properties(odin PROPERTIES
    IMPORTED_LOCATION ${ODIN_SDK_DIR}/${CMAKE_SHARED_LIBRARY_PREFIX}odin${CMAKE_SHARED_LIBRARY_SUFFIX}
    IMPORTED_IMPLIB ${ODIN_SDK_DIR}/odin${CMAKE_STATIC_LIBRARY_SUFFIX}
    INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_SOURCE_DIR}/../include
)
add_library(odin_crypto SHARED IMPORTED)
set_target_properties(odin_crypto PROPERTIES
    IMPORTED_LOCATION ${ODIN_SDK_DIR}/${CMAKE_SHARED_LIBRARY_PREFIX}odin_crypto${CMAKE_SHARED_LIBRARY_SUFFIX}
    IMPORTED_IMPLIB ${ODIN_SDK_DIR}/odin_crypto${CMAKE_STATIC_LIBRARY_SUFFIX}
    INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_SOURCE_DIR}/../include
)

# Helper to declare a sample executable; additional libraries to link (e.g.
# odin_crypto) can be passed after the source file
function(add_sample name source)
    add_executable(${name} ${source})
    if(WIN32)
        target_compile_options(${name} PRIVATE /utf-8)
    endif()
    target_compile_definitions(${name} PRIVATE PROJECT_NAME="${name}" PROJECT_DESCRIPTION="${PROJECT_DESCRIPTION}")
    target_link_libraries(${name} PRIVATE sample_deps odin ${ARGN})
    if(APPLE)
        add_custom_command(TARGET ${name} POST_BUILD COMMAND ${CMAKE_INSTALL_NAME_TOOL} -rpath ${ODIN_SDK_DIR} "@executable_path" $<TARGET_FILE:${name}>)
    endif()
    foreach(lib odin ${ARGN})
        add_custom_command(TARGET ${name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${lib}>
                $<TARGET_FILE_DIR:${name}>
        )
    endforeach()
endfunction()

add_sample(odin_client src/main.cpp odin_crypto)
add_sample(odin_sockets src/sockets.cpp)
add_sample(odin_positions src/positions.cpp)
