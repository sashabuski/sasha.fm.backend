
cmake_minimum_required(VERSION 3.20)

project(sasha_fm_broadcaster LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(sasha_fm_broadcaster WIN32
    main.cpp

    src/audio/wasapi_capture.cpp
    src/audio/process_capture.cpp

    src/sources/source_manager.cpp

    src/processing/audio_processor.cpp

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
)
