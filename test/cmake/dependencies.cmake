include(FetchContent)

# =============================================================================
# cxxopts
# =============================================================================

message(STATUS "Installing cxxopts")

FetchContent_Declare(
    cxxopts
    GIT_REPOSITORY https://github.com/jarro2783/cxxopts.git
    GIT_TAG        v3.3.1
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(cxxopts)

target_include_directories(${PROJECT_NAME} PRIVATE ${cxxopts_SOURCE_DIR}/include)

# =============================================================================
# nlohmann::json
# =============================================================================

message(STATUS "Installing nlohmann::json")

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.12.0
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(nlohmann_json)

target_include_directories(${PROJECT_NAME} PRIVATE ${nlohmann_json_SOURCE_DIR}/include)

# =============================================================================
# miniaudio
# =============================================================================

message(STATUS "Installing miniaudio")

FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        0.11.22
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(miniaudio)

target_include_directories(${PROJECT_NAME} PRIVATE ${miniaudio_SOURCE_DIR})

# =============================================================================
# spdlog
# =============================================================================

message(STATUS "Installing spdlog")

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.15.3
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(spdlog)

target_include_directories(${PROJECT_NAME} PRIVATE ${spdlog_SOURCE_DIR}/include)
