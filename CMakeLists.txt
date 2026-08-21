cmake_minimum_required(VERSION 3.25)
project(PulseFX LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PULSEFX_BUILD_TESTS "Build PulseFX DSP tests" ON)

add_library(pulsefx_dsp STATIC
    engine/src/Biquad.cpp
    engine/src/Equalizer.cpp
    engine/src/HeadphoneCorrection.cpp
    engine/src/BassEnhancer.cpp
    engine/src/ClarityEnhancer.cpp
    engine/src/Dynamics.cpp
    engine/src/StereoEnhancer.cpp
    engine/src/Limiter.cpp
    engine/src/TruePeakDetector.cpp
    engine/src/Processor.cpp
)

target_include_directories(pulsefx_dsp PUBLIC engine/include)
target_compile_definitions(pulsefx_dsp PRIVATE _USE_MATH_DEFINES)

if(MSVC)
  target_compile_options(pulsefx_dsp PRIVATE /W4 /permissive-)
else()
  target_compile_options(pulsefx_dsp PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

if(PULSEFX_BUILD_TESTS)
  enable_testing()
  add_executable(pulsefx_dsp_tests engine/tests/dsp_tests.cpp)
  target_link_libraries(pulsefx_dsp_tests PRIVATE pulsefx_dsp)
  if(MSVC)
    target_compile_options(pulsefx_dsp_tests PRIVATE /W4 /permissive-)
  else()
    target_compile_options(pulsefx_dsp_tests PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME pulsefx_dsp_tests COMMAND pulsefx_dsp_tests)
endif()
