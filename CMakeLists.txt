
cmake_minimum_required(VERSION 3.20)

project(sasha_fm_broadcaster LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# LiveKit SDK
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(LiveKitSDK)

livekit_sdk_setup(
    VERSION "${LIVEKIT_SDK_VERSION}"
    SDK_DIR "${CMAKE_BINARY_DIR}/_deps/livekit-sdk"
    GITHUB_TOKEN "$ENV{GITHUB_TOKEN}"
)

find_package(LiveKit CONFIG REQUIRED)

# Opus
find_package(Opus CONFIG REQUIRED
    PATHS "C:/Users/sasha/source/repos/vcpkg/installed/x64-windows/share/opus"
    NO_DEFAULT_PATH
)

find_package(nlohmann_json CONFIG REQUIRED)

add_executable(sasha_fm_broadcaster WIN32
    main.cpp

    src/audio/wasapi_capture.cpp
    src/audio/process_capture.cpp
        src/audio/audio_analyser.cpp
    src/sources/source_manager.cpp

    src/processing/audio_processor.cpp
    src/processing/opus_encoder.cpp

    src/streaming/broadcaster.cpp
)

target_include_directories(sasha_fm_broadcaster PRIVATE
    include
    src
)

target_link_libraries(sasha_fm_broadcaster PRIVATE
    ole32
    uuid
    propsys
    user32
    gdi32
    comctl32
    Mmdevapi
    winhttp

    Opus::opus
    LiveKit::livekit
    nlohmann_json::nlohmann_json
)
# Copy LiveKit runtime DLLs beside the executable
set(LIVEKIT_DLL_DIR
    "${CMAKE_BINARY_DIR}/_deps/livekit-sdk/livekit-sdk-windows-x64-1.13.1/bin"
)

add_custom_command(TARGET sasha_fm_broadcaster POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${LIVEKIT_DLL_DIR}/livekit.dll"
        "$<TARGET_FILE_DIR:sasha_fm_broadcaster>"

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${LIVEKIT_DLL_DIR}/livekit_ffi.dll"
        "$<TARGET_FILE_DIR:sasha_fm_broadcaster>"
)
add_custom_command(TARGET sasha_fm_broadcaster POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<IF:$<CONFIG:Debug>,C:/Users/sasha/source/repos/vcpkg/installed/x64-windows/debug/bin/opus.dll,C:/Users/sasha/source/repos/vcpkg/installed/x64-windows/bin/opus.dll>"
        "$<TARGET_FILE_DIR:sasha_fm_broadcaster>"
)