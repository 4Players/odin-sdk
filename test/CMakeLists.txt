cmake_minimum_required(VERSION 3.16)

project(odin_minimal LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(MINIAUDIO_DIR ${PROJECT_SOURCE_DIR}/deps/miniaudio CACHE PATH "miniaudio library")

include(FetchContent)
FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        dbca7a3b44594e0226f887472b31f54aa5f14214 # version 0.10.42
    DOWNLOAD_DIR   ${MINIAUDIO_DIR}
    SOURCE_DIR     ${MINIAUDIO_DIR}
)
FetchContent_MakeAvailable(miniaudio)

if(WIN32)
    set(ODIN_SDK_DIR "${CMAKE_SOURCE_DIR}/../bin/windows-x86_64")
    set(ODIN_SDK_LIB "odin.dll")
endif()

if(APPLE)
    if(${CMAKE_SYSTEM_PROCESSOR} STREQUAL "arm64")
        set(ODIN_SDK_DIR "${CMAKE_SOURCE_DIR}/../bin/macos-aarch64")
    else()
        set(ODIN_SDK_DIR "${CMAKE_SOURCE_DIR}/../bin/macos-x86_64")
    endif()
    set(ODIN_SDK_LIB "libodin.dylib")
endif()

if(UNIX AND NOT APPLE)
    set(ODIN_SDK_DIR "${CMAKE_SOURCE_DIR}/../bin/linux-x86_64")
    set(ODIN_SDK_LIB "libodin.so")
endif()

include_directories("${CMAKE_SOURCE_DIR}/../include" ${MINIAUDIO_DIR})
link_directories(${ODIN_SDK_DIR})

add_executable(${PROJECT_NAME} src/main.c)
target_link_libraries(${PROJECT_NAME} PRIVATE odin)

if(APPLE)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD COMMAND ${CMAKE_INSTALL_NAME_TOOL} -rpath ${ODIN_SDK_DIR} "@executable_path" $<TARGET_FILE:${PROJECT_NAME}>)
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE dl m pthread)
endif()

file(COPY "${ODIN_SDK_DIR}/${ODIN_SDK_LIB}" DESTINATION ${CMAKE_BINARY_DIR})
