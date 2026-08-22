cmake_minimum_required(VERSION 3.25)
project(PulseFX LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PULSEFX_BUILD_TESTS "Build PulseFX DSP tests" ON)
option(PULSEFX_BUILD_APO_BRIDGE "Build the portable Windows APO-to-DSP bridge" ON)
option(PULSEFX_BUILD_WASAPI_RELAY "Build the Windows virtual-endpoint relay" OFF)
option(PULSEFX_ENABLE_MYSOFA "Build AES69/SOFA measured-HRTF loading through libmysofa" OFF)

include(FetchContent)

FetchContent_Declare(
  signalsmith_stretch
  GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
  GIT_TAG 57b93f4e9206a089a45387eaa39bdc9f310d3308
)
FetchContent_MakeAvailable(signalsmith_stretch)

add_library(pulsefx_dsp STATIC
    engine/src/Biquad.cpp
    engine/src/Equalizer.cpp
    engine/src/HeadphoneCorrection.cpp
    engine/src/BassEnhancer.cpp
    engine/src/VirtualBassEnhancer.cpp
    engine/src/AdaptiveSignature.cpp
    engine/src/FidelityEnhancer.cpp
    engine/src/ClarityEnhancer.cpp
    engine/src/Dynamics.cpp
    engine/src/PitchShifter.cpp
    engine/src/SpatialSurround.cpp
    engine/src/SpatialProfileTuning.cpp
    engine/src/MultichannelBinaural.cpp
    engine/src/Ambience.cpp
    engine/src/StereoEnhancer.cpp
    engine/src/Limiter.cpp
    engine/src/TruePeakDetector.cpp
    engine/src/Processor.cpp
)

target_include_directories(pulsefx_dsp PUBLIC engine/include)
target_compile_definitions(pulsefx_dsp PRIVATE _USE_MATH_DEFINES)
target_link_libraries(pulsefx_dsp PRIVATE signalsmith-stretch)

add_library(pulsefx_host_protocol STATIC
    windows/host/HostProtocol.cpp
)
target_include_directories(pulsefx_host_protocol PUBLIC windows/host)

if(PULSEFX_ENABLE_MYSOFA)
  find_package(mysofa CONFIG REQUIRED)
  add_library(pulsefx_sofa STATIC engine/optional/SofaHrtfLoader.cpp)
  target_include_directories(pulsefx_sofa PUBLIC engine/include)
  target_link_libraries(pulsefx_sofa PUBLIC pulsefx_dsp)
  if(TARGET mysofa::mysofa-static)
    target_link_libraries(pulsefx_sofa PRIVATE mysofa::mysofa-static)
  elseif(TARGET mysofa::mysofa-shared)
    target_link_libraries(pulsefx_sofa PRIVATE mysofa::mysofa-shared)
  else()
    message(FATAL_ERROR "libmysofa package found but no supported CMake target was exported")
  endif()
endif()

if(PULSEFX_BUILD_APO_BRIDGE)
  add_library(pulsefx_apo_bridge STATIC windows/apo/PulseFxApoBridge.cpp)
  target_include_directories(pulsefx_apo_bridge PUBLIC windows/apo)
  target_link_libraries(pulsefx_apo_bridge PUBLIC pulsefx_dsp)
endif()

if(PULSEFX_BUILD_WASAPI_RELAY)
  if(NOT WIN32)
    message(FATAL_ERROR "PULSEFX_BUILD_WASAPI_RELAY requires Windows")
  endif()
  if(NOT PULSEFX_BUILD_APO_BRIDGE)
    message(FATAL_ERROR "PULSEFX_BUILD_WASAPI_RELAY requires PULSEFX_BUILD_APO_BRIDGE=ON")
  endif()

  add_library(pulsefx_wasapi_relay STATIC windows/relay/WasapiRelayV2.cpp)
  target_include_directories(pulsefx_wasapi_relay PUBLIC windows/relay)
  target_link_libraries(pulsefx_wasapi_relay PUBLIC pulsefx_apo_bridge PRIVATE ole32 avrt uuid ksuser)
  target_compile_definitions(pulsefx_wasapi_relay PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

  add_executable(pulsefx_audio_host
    windows/host/PulseFxHost.cpp
    windows/host/HeadphoneProfileCommand.cpp
    windows/host/AudioDeviceCatalog.cpp
    windows/host/AudioSessionMixer.cpp
  )
  target_include_directories(pulsefx_audio_host PRIVATE windows/host windows/relay windows/apo)
  target_link_libraries(pulsefx_audio_host PRIVATE pulsefx_wasapi_relay pulsefx_host_protocol ole32 uuid)
  target_compile_definitions(pulsefx_audio_host PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

  add_executable(pulsefx_device_setup windows/driver/PulseFxDeviceSetup.cpp)
  target_link_libraries(pulsefx_device_setup PRIVATE setupapi cfgmgr32)
  target_compile_definitions(pulsefx_device_setup PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
endif()

if(MSVC)
  target_compile_options(pulsefx_dsp PRIVATE /W4 /permissive-)
  target_compile_options(pulsefx_host_protocol PRIVATE /W4 /permissive-)
  if(TARGET pulsefx_wasapi_relay)
    target_compile_options(pulsefx_wasapi_relay PRIVATE /W4 /permissive-)
  endif()
  if(TARGET pulsefx_audio_host)
    target_compile_options(pulsefx_audio_host PRIVATE /W4 /permissive-)
  endif()
  if(TARGET pulsefx_device_setup)
    target_compile_options(pulsefx_device_setup PRIVATE /W4 /permissive-)
  endif()
else()
  target_compile_options(pulsefx_dsp PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
  target_compile_options(pulsefx_host_protocol PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
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

  add_executable(pulsefx_parity_tests engine/tests/parity_tests.cpp)
  target_link_libraries(pulsefx_parity_tests PRIVATE pulsefx_dsp)
  if(MSVC)
    target_compile_options(pulsefx_parity_tests PRIVATE /W4 /permissive-)
  else()
    target_compile_options(pulsefx_parity_tests PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME pulsefx_parity_tests COMMAND pulsefx_parity_tests)

  add_executable(pulsefx_spatial_profile_tests engine/tests/spatial_profile_tests.cpp)
  target_link_libraries(pulsefx_spatial_profile_tests PRIVATE pulsefx_dsp)
  if(MSVC)
    target_compile_options(pulsefx_spatial_profile_tests PRIVATE /W4 /permissive-)
  else()
    target_compile_options(pulsefx_spatial_profile_tests PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME pulsefx_spatial_profile_tests COMMAND pulsefx_spatial_profile_tests)

  add_executable(pulsefx_virtual_bass_tests engine/tests/virtual_bass_tests.cpp)
  target_link_libraries(pulsefx_virtual_bass_tests PRIVATE pulsefx_dsp)
  if(MSVC)
    target_compile_options(pulsefx_virtual_bass_tests PRIVATE /W4 /permissive-)
  else()
    target_compile_options(pulsefx_virtual_bass_tests PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME pulsefx_virtual_bass_tests COMMAND pulsefx_virtual_bass_tests)

  add_executable(pulsefx_adaptive_signature_tests engine/tests/adaptive_signature_tests.cpp)
  target_link_libraries(pulsefx_adaptive_signature_tests PRIVATE pulsefx_dsp)
  if(MSVC)
    target_compile_options(pulsefx_adaptive_signature_tests PRIVATE /W4 /permissive-)
  else()
    target_compile_options(pulsefx_adaptive_signature_tests PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME pulsefx_adaptive_signature_tests COMMAND pulsefx_adaptive_signature_tests)

  add_executable(pulsefx_perceptual_enhancement_tests engine/tests/perceptual_enhancement_tests.cpp)
  target_link_libraries(pulsefx_perceptual_enhancement_tests PRIVATE pulsefx_dsp)
  if(MSVC)
    target_compile_options(pulsefx_perceptual_enhancement_tests PRIVATE /W4 /permissive-)
  else()
    target_compile_options(pulsefx_perceptual_enhancement_tests PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME pulsefx_perceptual_enhancement_tests COMMAND pulsefx_perceptual_enhancement_tests)

  add_executable(pulsefx_host_protocol_tests windows/host/host_protocol_tests.cpp)
  target_link_libraries(pulsefx_host_protocol_tests PRIVATE pulsefx_host_protocol)
  if(MSVC)
    target_compile_options(pulsefx_host_protocol_tests PRIVATE /W4 /permissive-)
  else()
    target_compile_options(pulsefx_host_protocol_tests PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  add_test(NAME pulsefx_host_protocol_tests COMMAND pulsefx_host_protocol_tests)

  if(PULSEFX_BUILD_APO_BRIDGE)
    add_executable(pulsefx_apo_bridge_tests windows/apo/bridge_tests.cpp)
    target_link_libraries(pulsefx_apo_bridge_tests PRIVATE pulsefx_apo_bridge)
    add_test(NAME pulsefx_apo_bridge_tests COMMAND pulsefx_apo_bridge_tests)

    add_executable(
      pulsefx_headphone_profile_tests
      windows/host/HeadphoneProfileCommand.cpp
      windows/host/headphone_profile_tests.cpp
    )
    target_include_directories(pulsefx_headphone_profile_tests PRIVATE windows/host windows/apo)
    target_link_libraries(pulsefx_headphone_profile_tests PRIVATE pulsefx_apo_bridge)
    add_test(NAME pulsefx_headphone_profile_tests COMMAND pulsefx_headphone_profile_tests)
  endif()

  if(PULSEFX_BUILD_WASAPI_RELAY)
    add_executable(pulsefx_wasapi_relay_tests windows/relay/relay_smoke_tests.cpp)
    target_link_libraries(pulsefx_wasapi_relay_tests PRIVATE pulsefx_wasapi_relay)
    add_test(NAME pulsefx_wasapi_relay_tests COMMAND pulsefx_wasapi_relay_tests)
  endif()
endif()
