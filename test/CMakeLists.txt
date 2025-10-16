cmake_minimum_required(VERSION 3.28)

project(
    odin_client
    VERSION 2.0.0
    DESCRIPTION "Versatile CLI test client for ODIN Voice written in C/C++"
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

add_executable(${PROJECT_NAME})

if(WIN32)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)
endif()

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

target_sources(${PROJECT_NAME} PRIVATE src/main.cpp)
target_compile_definitions(${PROJECT_NAME} PRIVATE PROJECT_NAME="${PROJECT_NAME}" PROJECT_DESCRIPTION="${PROJECT_DESCRIPTION}")
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/../include)
target_link_directories(${PROJECT_NAME} PRIVATE ${ODIN_SDK_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE odin odin_crypto)

if(APPLE)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD COMMAND ${CMAKE_INSTALL_NAME_TOOL} -rpath ${ODIN_SDK_DIR} "@executable_path" $<TARGET_FILE:${PROJECT_NAME}>)
endif()

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${ODIN_SDK_DIR}/${CMAKE_SHARED_LIBRARY_PREFIX}odin${CMAKE_SHARED_LIBRARY_SUFFIX}
        $<TARGET_FILE_DIR:${PROJECT_NAME}>
)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${ODIN_SDK_DIR}/${CMAKE_SHARED_LIBRARY_PREFIX}odin_crypto${CMAKE_SHARED_LIBRARY_SUFFIX}
        $<TARGET_FILE_DIR:${PROJECT_NAME}>
)
