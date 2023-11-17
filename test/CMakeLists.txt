cmake_minimum_required(VERSION 3.16)

project(odin_minimal LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(ARGPARSE_DIR ${PROJECT_SOURCE_DIR}/deps/argparse CACHE PATH "argparse library")
set(MINIAUDIO_DIR ${PROJECT_SOURCE_DIR}/deps/miniaudio CACHE PATH "miniaudio library")

include(FetchContent)
FetchContent_Declare(
    argparse
    GIT_REPOSITORY https://github.com/cofyc/argparse.git
    GIT_TAG        15091715cc379bc647d5de6345185a288a35f999 # version 1.1.0
    DOWNLOAD_DIR   ${ARGPARSE_DIR}
    SOURCE_DIR     ${ARGPARSE_DIR}
)
FetchContent_MakeAvailable(argparse)

include(FetchContent)
FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        4a5b74bef029b3592c54b6048650ee5f972c1a48 # version 0.11.21
    DOWNLOAD_DIR   ${MINIAUDIO_DIR}
    SOURCE_DIR     ${MINIAUDIO_DIR}
)
FetchContent_MakeAvailable(miniaudio)

set(ARCH ${CMAKE_SYSTEM_PROCESSOR})
if(ARCH STREQUAL "x86_64" OR ARCH STREQUAL "amd64" OR ARCH STREQUAL "AMD64")
    set(ARCH "x86_64")
elseif(ARCH STREQUAL "x86" OR ARCH STREQUAL "i386" OR ARCH STREQUAL "i686")
    set(ARCH "x86")
elseif(ARCH STREQUAL "aarch64" OR ARCH STREQUAL "arm64" OR ARCH STREQUAL "arm64e")
    set(ARCH "aarch64")
else()
    message(FATAL_ERROR "Unknown processor; ${ARCH}")
endif()

if(WIN32)
    set(ODIN_SDK_DIR "${CMAKE_SOURCE_DIR}/../bin/windows-${ARCH}")
    set(ODIN_SDK_LIB "odin.dll")
elseif(APPLE)
    set(ODIN_SDK_DIR "${CMAKE_SOURCE_DIR}/../bin/macos-${ARCH}")
    set(ODIN_SDK_LIB "libodin.dylib")
elseif(UNIX AND NOT APPLE)
    set(ODIN_SDK_DIR "${CMAKE_SOURCE_DIR}/../bin/linux-${ARCH}")
    set(ODIN_SDK_LIB "libodin.so")
else()
    message(FATAL_ERROR "Unknown platform; only Windows, macOS and Linux are supported")
endif()

include_directories("${CMAKE_SOURCE_DIR}/../include" ${ARGPARSE_DIR} ${MINIAUDIO_DIR})
link_directories(${ODIN_SDK_DIR})

add_executable(${PROJECT_NAME} src/main.c)
target_link_libraries(${PROJECT_NAME} PRIVATE odin argparse)

if(APPLE)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD COMMAND ${CMAKE_INSTALL_NAME_TOOL} -rpath ${ODIN_SDK_DIR} "@executable_path" $<TARGET_FILE:${PROJECT_NAME}>)
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE dl m pthread)
endif()

file(COPY "${ODIN_SDK_DIR}/${ODIN_SDK_LIB}" DESTINATION ${CMAKE_BINARY_DIR})
