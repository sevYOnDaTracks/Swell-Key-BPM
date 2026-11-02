cmake_minimum_required(VERSION 3.22)

project(SwellKey VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows: link the C++ runtime statically so nothing depends on the
# Visual C++ Redistributable installed on the user's machine
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# Detection engine test bench: analyse files, or score the engine on generated beats
juce_add_console_app(SwellKeyBench PRODUCT_NAME "SwellKeyBench")
target_sources(SwellKeyBench PRIVATE tools/Bench.cpp)
target_compile_definitions(SwellKeyBench PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1)
target_link_libraries(SwellKeyBench
    PRIVATE
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
