cmake_minimum_required(VERSION 3.22)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

option(ACUSTRA_BUILD_UNIVERSAL "Build arm64 and x86_64 slices on macOS" ON)

# Establish the deployment target and architectures before project(), so
# CMake's compiler probes use the same settings as the final plug-in.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING
            "Minimum supported macOS version")
    endif()

    if(ACUSTRA_BUILD_UNIVERSAL AND NOT DEFINED CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING
            "macOS architectures")
    endif()
endif()

# JUCE's macOS AU and Standalone wrappers compile C/Objective-C translation
# units, so non-Xcode generators require both languages here.
project(Acustra VERSION 1.1.3 LANGUAGES C CXX)
include(cmake/Distribution.cmake)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ACUSTRA_BUILD_PLUGIN "Build the JUCE plug-in and standalone app" ON)
option(ACUSTRA_BUILD_TOOLS "Build offline Acustra tools" ON)
option(ACUSTRA_BUILD_PLUGIN_REFERENCE_HOST
    "Build the optional JUCE VST3 reference renderer/editor" OFF)
option(ACUSTRA_REQUIRE_PYTHON_TESTS
    "Fail to configure unless every Python self-test can be registered (CI sets it)" OFF)
set(ACUSTRA_JUCE_PATH "" CACHE PATH
    "Optional path to a local JUCE 8.0.14 checkout (avoids downloading JUCE)")

include(CTest)

file(GLOB ACUSTRA_BANK_PARTS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Source/DSP/SampleBank/BankData-*.cpp")

add_library(AcustraDSP STATIC
    Source/DSP/AcustraEngine.cpp
    Source/DSP/AcustraEngine.h
    Source/DSP/CaptureVoicingData.h
    Source/DSP/MicrophoneBalanceData.h
    Source/DSP/PluckPolarisationData.h
    Source/DSP/ReleasePolicyData.h
    Source/DSP/SustainPolicyData.h
    Source/DSP/PiezoBlampTable.h
    Source/DSP/AcustraPerformer.cpp
    Source/DSP/AcustraPerformer.h)

# The Reason Rack Extension compiles this engine and player itself, as C++17
# (the Jukebox toolchain forces -std=c++17) with -Wall -Wextra -Werror. Build
# them that way here too, so a C++20-only construct or a new warning fails in
# this repository rather than only in the Rack Extension's.
if(NOT MSVC)
    add_library(AcustraDSPCxx17 OBJECT
        Source/DSP/AcustraEngine.cpp
        Source/DSP/AcustraPerformer.cpp)
    target_include_directories(AcustraDSPCxx17
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
    set_target_properties(AcustraDSPCxx17 PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # -Wshadow on top of the Rack Extension's flags: a local that hides
        # a namespace constant or an outer variable is how a formula quietly
        # reads the wrong value.
        target_compile_options(AcustraDSPCxx17 PRIVATE
            -Wall -Wextra -Wshadow -Werror)
    endif()
endif()

add_library(AcustraReferenceBank STATIC EXCLUDE_FROM_ALL
    Source/DSP/SampleBank/EmbeddedGuitarBank.cpp
    Source/DSP/SampleBank/EmbeddedGuitarBank.h
    Source/DSP/SampleBank/GeneratedBankData.h
    Source/DSP/SampleBank/BankData.cpp
    ${ACUSTRA_BANK_PARTS})

target_include_directories(AcustraDSP
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source")
target_compile_features(AcustraDSP PUBLIC cxx_std_20)
set_target_properties(AcustraDSP PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(AcustraReferenceBank
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source")
target_compile_features(AcustraReferenceBank PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(AcustraDSP PRIVATE /W4 /permissive-)
    target_compile_options(AcustraReferenceBank PRIVATE /W4 /permissive-)
else()
    target_compile_options(AcustraDSP PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(AcustraReferenceBank PRIVATE
        -Wall -Wextra -Wpedantic)
endif()

if(ACUSTRA_BUILD_PLUGIN)
    if(ACUSTRA_JUCE_PATH)
        if(NOT EXISTS "${ACUSTRA_JUCE_PATH}/CMakeLists.txt")
            message(FATAL_ERROR
                "ACUSTRA_JUCE_PATH does not contain a JUCE CMakeLists.txt: ${ACUSTRA_JUCE_PATH}")
        endif()
        add_subdirectory("${ACUSTRA_JUCE_PATH}"
                         "${CMAKE_CURRENT_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)
    else()
        include(FetchContent)
        FetchContent_Declare(
            JUCE
            # JUCE 8.0.14, pinned to its immutable release commit and archive
            # checksum so automated builds cannot follow a moved tag.
            URL https://github.com/juce-framework/JUCE/archive/2cdfca8feb300fb424002ba2c2751569e5bacb64.tar.gz
            URL_HASH SHA256=814019716960a16f8f850a1593eeb56e2f21910aff2d673f7d3513b4ebaf251d)
        FetchContent_MakeAvailable(JUCE)
    endif()

    if(APPLE)
        set(ACUSTRA_PLUGIN_FORMATS VST3 AU Standalone)
    else()
        set(ACUSTRA_PLUGIN_FORMATS VST3 Standalone)
    endif()

    juce_add_plugin(Acustra
        VERSION ${PROJECT_VERSION}
        BUILD_VERSION ${ACUSTRA_BUILD_NUMBER}
        COMPANY_NAME "Protocodus"
        COMPANY_WEBSITE "https://protocodus.cz"
        COMPANY_EMAIL "protocodus@proton.me"
        COMPANY_COPYRIGHT "Copyright (c) 2026 Acustra contributors"
        BUNDLE_ID "cz.protocodus.acustra"
        PLUGIN_MANUFACTURER_CODE Acst
        PLUGIN_CODE Acu1
        PRODUCT_NAME "Acustra"
        DESCRIPTION "Acustra acoustic guitar instrument"
        FORMATS ${ACUSTRA_PLUGIN_FORMATS}
        VST3_CATEGORIES Instrument Synth
        AU_MAIN_TYPE kAudioUnitType_MusicDevice
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        AU_SANDBOX_SAFE TRUE
        COPY_PLUGIN_AFTER_BUILD FALSE)

    instrument_distribution_identity(Acustra ${ACUSTRA_PLUGIN_FORMATS})

    juce_add_binary_data(AcustraUIAssets
        NAMESPACE AcustraUIAssets
        HEADER_NAME AcustraUIAssets.h
        SOURCES Assets/UI/cedar-background.png Assets/UI/walnut-satin.png)

    juce_generate_juce_header(Acustra)

    target_sources(Acustra PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h)

    target_link_libraries(Acustra
        PRIVATE
            AcustraDSP
            AcustraUIAssets
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    target_compile_definitions(Acustra
        PUBLIC
            JUCE_WEB_BROWSER=0
            JUCE_USE_CURL=0
            JUCE_VST3_CAN_REPLACE_VST2=0)

    if(ACUSTRA_BUILD_PLUGIN_REFERENCE_HOST)
        juce_add_console_app(AcustraPluginReferenceHost
            PRODUCT_NAME "Acustra Plugin Reference Host")
        target_sources(AcustraPluginReferenceHost
            PRIVATE Tools/PluginReferenceHost.cpp)
        target_compile_definitions(AcustraPluginReferenceHost PRIVATE
            JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
            JUCE_MODAL_LOOPS_PERMITTED=1)
        target_link_libraries(AcustraPluginReferenceHost PRIVATE
            juce::juce_audio_processors juce::juce_audio_formats
            juce::juce_recommended_config_flags)
        if(BUILD_TESTING)
            add_test(NAME Acustra.PluginReferenceHost
                COMMAND AcustraPluginReferenceHost --self-test)
        endif()
    endif()
elseif(ACUSTRA_BUILD_PLUGIN_REFERENCE_HOST)
    message(FATAL_ERROR
        "ACUSTRA_BUILD_PLUGIN_REFERENCE_HOST requires ACUSTRA_BUILD_PLUGIN=ON")
endif()

# The demonstrations are rendered by the same JUCE-free engine used
# by the plug-in. Build the renderer in CI so its source cannot silently rot.
if(ACUSTRA_BUILD_TOOLS)
    add_executable(AcustraRenderDemos Tools/RenderDemos.cpp)
    target_link_libraries(AcustraRenderDemos PRIVATE AcustraDSP)
    target_compile_features(AcustraRenderDemos PRIVATE cxx_std_20)

    add_executable(AcustraRenderRealismSongs Tools/RenderRealismSongs.cpp)
    target_link_libraries(AcustraRenderRealismSongs PRIVATE AcustraDSP)
    target_compile_features(AcustraRenderRealismSongs PRIVATE cxx_std_20)

    add_executable(AcustraPhysicalFitRenderer Tools/PhysicalFitRenderer.cpp)
    target_link_libraries(AcustraPhysicalFitRenderer
        PRIVATE AcustraDSP AcustraReferenceBank)
    target_compile_features(AcustraPhysicalFitRenderer PRIVATE cxx_std_20)

    # PhysicalFitRenderer's model half for corpora outside the embedded bank;
    # driven by Tools/BenchmarkOpenCorpora.py.
    add_executable(AcustraExternalCorpusRenderer Tools/ExternalCorpusRenderer.cpp)
    target_link_libraries(AcustraExternalCorpusRenderer PRIVATE AcustraDSP)
    target_compile_features(AcustraExternalCorpusRenderer PRIVATE cxx_std_20)

    add_executable(AcustraBridgeProbe Tools/BridgeProbe.cpp)
    target_link_libraries(AcustraBridgeProbe PRIVATE AcustraDSP)
    target_compile_features(AcustraBridgeProbe PRIVATE cxx_std_20)

    # Read-only modal/string traces with all reciprocal bridge ports retained.
    add_executable(AcustraTraceRinging Tools/TraceRinging.cpp)
    target_link_libraries(AcustraTraceRinging PRIVATE AcustraDSP)
    target_compile_features(AcustraTraceRinging PRIVATE cxx_std_20)

    add_executable(AcustraPerformanceRenderer Tools/PerformanceRenderer.cpp)
    target_link_libraries(AcustraPerformanceRenderer PRIVATE AcustraDSP)

    # Identical short scores for frozen before/after listening comparisons.
    add_executable(AcustraRenderAudibleRealism Tools/RenderAudibleRealism.cpp)
    add_executable(AcustraRenderNaturalPerformance Tools/RenderNaturalPerformance.cpp)
    target_link_libraries(AcustraRenderNaturalPerformance PRIVATE AcustraDSP)
    target_link_libraries(AcustraRenderAudibleRealism PRIVATE AcustraDSP)
    target_compile_features(AcustraRenderAudibleRealism PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(AcustraRenderAudibleRealism PRIVATE /W4 /permissive-)
    else()
        target_compile_options(AcustraRenderAudibleRealism PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    # Expressive repertoire, solo and ensemble: one engine per guitar, the
    # performances written by Tools/PerformRepertoire.py.
    find_package(Threads REQUIRED)
    add_executable(AcustraRenderRepertoire Tools/RenderRepertoire.cpp)
    target_link_libraries(AcustraRenderRepertoire PRIVATE AcustraDSP Threads::Threads)
    target_compile_features(AcustraRenderRepertoire PRIVATE cxx_std_20)

    if(MSVC)
        target_compile_options(AcustraRenderDemos PRIVATE /W4 /permissive-)
        target_compile_options(AcustraRenderRepertoire PRIVATE /W4 /permissive-)
        target_compile_options(AcustraPhysicalFitRenderer
            PRIVATE /W4 /permissive-)
        target_compile_options(AcustraExternalCorpusRenderer
            PRIVATE /W4 /permissive-)
        target_compile_options(AcustraBridgeProbe PRIVATE /W4 /permissive-)
        target_compile_options(AcustraTraceRinging PRIVATE /W4 /permissive-)
    else()
        target_compile_options(AcustraRenderDemos PRIVATE
            -Wall -Wextra -Wpedantic)
        target_compile_options(AcustraRenderRepertoire PRIVATE
            -Wall -Wextra -Wpedantic)
        target_compile_options(AcustraPhysicalFitRenderer PRIVATE
            -Wall -Wextra -Wpedantic)
        target_compile_options(AcustraExternalCorpusRenderer PRIVATE
            -Wall -Wextra -Wpedantic)
        target_compile_options(AcustraBridgeProbe PRIVATE
            -Wall -Wextra -Wpedantic)
        target_compile_options(AcustraTraceRinging PRIVATE
            -Wall -Wextra -Wpedantic)
    endif()
endif()

if(BUILD_TESTING)
    # ctest raises the soft stack limit (to 64 MB on macOS), which hid test
    # suites that overflowed the 8 MB a shell gives a program run directly or
    # under a debugger (audit F16). The C++ suites therefore run at a shell's
    # default stack under ctest too, so a test that parks a 1.7 MB engine on
    # the stack of every nested helper fails here rather than only by hand.
    # Windows links its own stack size (CI passes /STACK).
    function(acustra_add_suite name target)
        if(WIN32)
            add_test(NAME ${name} COMMAND ${target} ${ARGN})
        else()
            add_test(NAME ${name}
                     COMMAND /bin/sh -c "ulimit -s 8176 2>/dev/null; exec \"$0\" \"$@\""
                             "$<TARGET_FILE:${target}>" ${ARGN})
        endif()
    endfunction()

    add_executable(AcustraSampleBankTests Tests/SampleBankTests.cpp)
    target_link_libraries(AcustraSampleBankTests PRIVATE AcustraReferenceBank)
    target_compile_features(AcustraSampleBankTests PRIVATE cxx_std_20)

    if(MSVC)
        target_compile_options(AcustraSampleBankTests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(AcustraSampleBankTests PRIVATE
            -Wall -Wextra -Wpedantic)
    endif()

    acustra_add_suite(Acustra.SampleBank AcustraSampleBankTests)
    set_tests_properties(Acustra.SampleBank PROPERTIES TIMEOUT 90)

    add_executable(AcustraEngineTests Tests/AcustraEngineTests.cpp)
    target_link_libraries(AcustraEngineTests PRIVATE AcustraDSP)
    target_compile_features(AcustraEngineTests PRIVATE cxx_std_20)

    if(MSVC)
        target_compile_options(AcustraEngineTests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(AcustraEngineTests PRIVATE
            -Wall -Wextra -Wpedantic)
    endif()

    # CI's Linux and Windows runners take about 1.8 times as long as its
    # macOS runner (on 2026-09-25 the engine suite ran 268 s against 149 s
    # and the release suite timed out at 90 s against 50 s), so these two
    # long suites keep that much headroom over the slowest runner.
    acustra_add_suite(Acustra.Engine AcustraEngineTests)
    set_tests_properties(Acustra.Engine PROPERTIES TIMEOUT 600)

    add_executable(AcustraReleaseJoinTests Tests/ReleaseJoinTests.cpp)
    target_link_libraries(AcustraReleaseJoinTests PRIVATE AcustraDSP)
    target_compile_features(AcustraReleaseJoinTests PRIVATE cxx_std_20)
    acustra_add_suite(Acustra.ReleaseJoin AcustraReleaseJoinTests)
    set_tests_properties(Acustra.ReleaseJoin PROPERTIES TIMEOUT 120)

    add_executable(AcustraStrumDirectionTests Tests/StrumDirectionTests.cpp)
    target_link_libraries(AcustraStrumDirectionTests PRIVATE AcustraDSP)
    target_compile_features(AcustraStrumDirectionTests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(AcustraStrumDirectionTests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(AcustraStrumDirectionTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    acustra_add_suite(Acustra.StrumDirection AcustraStrumDirectionTests)
    set_tests_properties(Acustra.StrumDirection PROPERTIES TIMEOUT 120)

    add_executable(AcustraHandAllocatorTests Tests/HandAllocatorTests.cpp)
    target_link_libraries(AcustraHandAllocatorTests PRIVATE AcustraDSP)
    target_compile_features(AcustraHandAllocatorTests PRIVATE cxx_std_20)
    acustra_add_suite(Acustra.HandAllocator AcustraHandAllocatorTests)
    set_tests_properties(Acustra.HandAllocator PROPERTIES TIMEOUT 120)

    add_executable(AcustraPerformerTests Tests/PerformerTests.cpp)
    target_link_libraries(AcustraPerformerTests PRIVATE AcustraDSP)
    target_compile_features(AcustraPerformerTests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(AcustraPerformerTests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(AcustraPerformerTests PRIVATE
            -Wall -Wextra -Wpedantic)
    endif()
    acustra_add_suite(Acustra.Performer AcustraPerformerTests)
    set_tests_properties(Acustra.Performer PROPERTIES TIMEOUT 600)

    # A separate ABI: opt-in fixed storage for 4x at 192 kHz. Keeping this
    # definition PUBLIC makes every consumer use the matching engine layout.
    # Compile the library and its heap-only regression suite as SDK C++17.
    add_library(AcustraDSPExtendedCxx17 STATIC
        Source/DSP/AcustraEngine.cpp
        Source/DSP/AcustraPerformer.cpp)
    target_include_directories(AcustraDSPExtendedCxx17
        PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source")
    target_compile_definitions(AcustraDSPExtendedCxx17
        PUBLIC ACUSTRA_EXTENDED_SAMPLE_RATES=1)
    set_target_properties(AcustraDSPExtendedCxx17 PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(AcustraDSPExtendedCxx17 PRIVATE /W4 /permissive-)
    else()
        target_compile_options(AcustraDSPExtendedCxx17 PRIVATE
            -Wall -Wextra -Wshadow -Werror)
    endif()
    add_executable(AcustraExtendedSampleRateTests Tests/ExtendedSampleRateTests.cpp)
    target_link_libraries(AcustraExtendedSampleRateTests PRIVATE AcustraDSPExtendedCxx17)
    set_target_properties(AcustraExtendedSampleRateTests PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    acustra_add_suite(Acustra.ExtendedSampleRates AcustraExtendedSampleRateTests)
    set_tests_properties(Acustra.ExtendedSampleRates PROPERTIES TIMEOUT 120)

    add_executable(AcustraNormalLayoutParity Tests/SampleRateLayoutParity.cpp)
    target_link_libraries(AcustraNormalLayoutParity PRIVATE AcustraDSP)
    add_executable(AcustraExtendedLayoutParity Tests/SampleRateLayoutParity.cpp)
    target_link_libraries(AcustraExtendedLayoutParity PRIVATE AcustraDSPExtendedCxx17)
    set_target_properties(AcustraExtendedLayoutParity PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    add_test(NAME Acustra.SampleRateLayoutParity
        COMMAND "${CMAKE_COMMAND}"
            "-Dnormal=$<TARGET_FILE:AcustraNormalLayoutParity>"
            "-Dextended=$<TARGET_FILE:AcustraExtendedLayoutParity>"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/sample-rate-layout-parity"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/Tests/CheckSampleRateLayoutParity.cmake")
    set_tests_properties(Acustra.SampleRateLayoutParity PROPERTIES TIMEOUT 120)

    add_executable(AcustraReleaseTests Tests/ReleaseTests.cpp)
    target_link_libraries(AcustraReleaseTests PRIVATE AcustraDSP)
    acustra_add_suite(Acustra.Release AcustraReleaseTests)
    set_tests_properties(Acustra.Release PROPERTIES TIMEOUT 240)

    add_executable(AcustraContactTravelTests Tests/ContactTravelTests.cpp)
    target_link_libraries(AcustraContactTravelTests PRIVATE AcustraDSP)
    target_compile_features(AcustraContactTravelTests PRIVATE cxx_std_20)
    acustra_add_suite(Acustra.ContactTravel AcustraContactTravelTests)
    set_tests_properties(Acustra.ContactTravel PROPERTIES TIMEOUT 90)

    add_executable(AcustraOutputBusTests Tests/OutputBusTests.cpp)
    target_link_libraries(AcustraOutputBusTests PRIVATE AcustraDSP)
    target_compile_features(AcustraOutputBusTests PRIVATE cxx_std_17)
    if(NOT MSVC)
        target_compile_options(AcustraOutputBusTests PRIVATE
            -Wall -Wextra -Wpedantic)
    endif()
    acustra_add_suite(Acustra.OutputBuses AcustraOutputBusTests)
    set_tests_properties(Acustra.OutputBuses PROPERTIES TIMEOUT 600)

    add_executable(AcustraCaptureTests Tests/CaptureTests.cpp)
    target_link_libraries(AcustraCaptureTests PRIVATE AcustraDSP)
    acustra_add_suite(Acustra.Capture AcustraCaptureTests)
    set_tests_properties(Acustra.Capture PROPERTIES TIMEOUT 300)

    add_executable(AcustraCaptureVoicingTests Tests/CaptureVoicingTests.cpp)
    target_link_libraries(AcustraCaptureVoicingTests PRIVATE AcustraDSP)
    set_target_properties(AcustraCaptureVoicingTests PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    acustra_add_suite(Acustra.CaptureVoicing AcustraCaptureVoicingTests)

    add_executable(AcustraMicrophoneBalanceTests Tests/MicrophoneBalanceTests.cpp)
    target_link_libraries(AcustraMicrophoneBalanceTests PRIVATE AcustraDSP)
    set_target_properties(AcustraMicrophoneBalanceTests PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    acustra_add_suite(Acustra.MicrophoneBalance AcustraMicrophoneBalanceTests)
    set_tests_properties(Acustra.MicrophoneBalance PROPERTIES TIMEOUT 50)

    # The piezo chain against Tools/PiezoReference.py's circuit simulation,
    # through the fixtures it wrote (Tests/Fixtures).
    add_executable(AcustraPiezoCircuitTests Tests/PiezoCircuitTests.cpp)
    target_link_libraries(AcustraPiezoCircuitTests PRIVATE AcustraDSP)
    target_compile_features(AcustraPiezoCircuitTests PRIVATE cxx_std_17)
    target_compile_definitions(AcustraPiezoCircuitTests PRIVATE
        ACUSTRA_PIEZO_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/Tests/Fixtures")
    if(NOT MSVC)
        target_compile_options(AcustraPiezoCircuitTests PRIVATE
            -Wall -Wextra -Wpedantic)
    endif()
    acustra_add_suite(Acustra.PiezoCircuit AcustraPiezoCircuitTests)
    set_tests_properties(Acustra.PiezoCircuit PROPERTIES TIMEOUT 300)

    add_executable(AcustraGuitarModelTests Tests/GuitarModelTests.cpp)
    target_link_libraries(AcustraGuitarModelTests PRIVATE AcustraDSP)
    acustra_add_suite(Acustra.GuitarModels AcustraGuitarModelTests)

    add_executable(AcustraPlayerBodyLoadingTests Tests/PlayerBodyLoadingTests.cpp)
    target_link_libraries(AcustraPlayerBodyLoadingTests PRIVATE AcustraDSP)
    set_target_properties(AcustraPlayerBodyLoadingTests PROPERTIES
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    acustra_add_suite(Acustra.PlayerBodyLoading AcustraPlayerBodyLoadingTests)
    set_tests_properties(Acustra.GuitarModels PROPERTIES TIMEOUT 180)

    add_executable(AcustraBodyShapeTests Tests/BodyShapeTests.cpp)
    target_link_libraries(AcustraBodyShapeTests PRIVATE AcustraDSP)
    target_compile_features(AcustraBodyShapeTests PRIVATE cxx_std_20)
    acustra_add_suite(Acustra.BodyShape AcustraBodyShapeTests)
    set_tests_properties(Acustra.BodyShape PROPERTIES TIMEOUT 90)

    # Every construction a player can choose, crossed (audit F28).
    add_executable(AcustraConstructionMatrixTests Tests/ConstructionMatrixTests.cpp)
    target_link_libraries(AcustraConstructionMatrixTests PRIVATE AcustraDSP)
    target_compile_features(AcustraConstructionMatrixTests PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(AcustraConstructionMatrixTests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(AcustraConstructionMatrixTests PRIVATE
            -Wall -Wextra -Wpedantic)
    endif()
    acustra_add_suite(Acustra.ConstructionMatrix AcustraConstructionMatrixTests)
    set_tests_properties(Acustra.ConstructionMatrix PROPERTIES TIMEOUT 300)

    find_package(Threads REQUIRED)
    add_executable(AcustraConstructionLoudnessTests Tests/ConstructionLoudnessTests.cpp)
    target_link_libraries(AcustraConstructionLoudnessTests PRIVATE AcustraDSP Threads::Threads)
    if(NOT MSVC)
        target_compile_options(AcustraConstructionLoudnessTests PRIVATE
            -Wall -Wextra -Wpedantic)
    endif()
    acustra_add_suite(Acustra.ConstructionLoudness AcustraConstructionLoudnessTests)
    set_tests_properties(Acustra.ConstructionLoudness PROPERTIES TIMEOUT 600)

    if(ACUSTRA_BUILD_TOOLS)
        add_executable(AcustraRecuerdosArticulationTests
            Tests/RecuerdosArticulationTests.cpp)
        target_link_libraries(AcustraRecuerdosArticulationTests PRIVATE AcustraDSP)
        target_compile_features(AcustraRecuerdosArticulationTests PRIVATE cxx_std_20)
        acustra_add_suite(Acustra.RecuerdosArticulation AcustraRecuerdosArticulationTests)
        set_tests_properties(Acustra.RecuerdosArticulation PROPERTIES TIMEOUT 60)

        add_test(NAME Acustra.RenderDemos
                 COMMAND AcustraRenderDemos --smoke
                         "${CMAKE_CURRENT_BINARY_DIR}/demo-smoke")
        set_tests_properties(Acustra.RenderDemos PROPERTIES TIMEOUT 60)
        add_test(NAME Acustra.RenderRepertoire
                 COMMAND AcustraRenderRepertoire --smoke)
        set_tests_properties(Acustra.RenderRepertoire PROPERTIES TIMEOUT 60)
        add_test(NAME Acustra.DemoCount
                 COMMAND "${CMAKE_COMMAND}"
                         "-DRENDERER=$<TARGET_FILE:AcustraRenderDemos>"
                         "-DWORKFLOW=${CMAKE_CURRENT_SOURCE_DIR}/.github/workflows/ci.yml"
                         -P "${CMAKE_CURRENT_SOURCE_DIR}/Tests/CheckDemoCount.cmake")
        set_tests_properties(Acustra.DemoCount PROPERTIES TIMEOUT 30)

        add_test(NAME Acustra.PhysicalFitRenderer
                 COMMAND AcustraPhysicalFitRenderer --smoke
                         "${CMAKE_CURRENT_BINARY_DIR}/physical-fit-smoke"
                         1 1 1 0 .03
                         1 1 1 1 1 1 0
                         1 1 .0061 -.030 1.3 .001 1000 .02 0 80 .0008
                         0 2 0 .0001162109375 .035 .002334375
                         0 0 1 4000 8000 .0207 0 0)
        set_tests_properties(Acustra.PhysicalFitRenderer PROPERTIES TIMEOUT 120)

        add_test(NAME Acustra.BridgeProbe
                 COMMAND AcustraBridgeProbe
                         "${CMAKE_CURRENT_BINARY_DIR}/bridge-probe-smoke.f32"
                         on 0.5 auditorium 40)
        set_tests_properties(Acustra.BridgeProbe PROPERTIES TIMEOUT 30)

        add_test(NAME Acustra.TraceRinging
                 COMMAND AcustraTraceRinging --self-test)
        set_tests_properties(Acustra.TraceRinging PROPERTIES TIMEOUT 60)

        # ExtractExcitation.py's self-test fails only on a synthesised note
        # whose excitation is known exactly, so what this pins is the
        # estimator, not the engine; the probe renders beside it are reported,
        # not gated (the engine's own released shape departs from the ideal
        # comb, which the tool's docstring records). Registered only where the
        # interpreter and the two libraries it imports are present, so a build
        # without them still configures.
        find_package(Python3 COMPONENTS Interpreter QUIET)
        if(Python3_Interpreter_FOUND)
            add_test(NAME Acustra.RepertoireTempo
                     COMMAND "${Python3_EXECUTABLE}"
                             "${CMAKE_CURRENT_SOURCE_DIR}/Tests/RepertoireTempoTests.py")
            set_tests_properties(Acustra.RepertoireTempo PROPERTIES TIMEOUT 60)
            add_test(NAME Acustra.GaussianApertureData
                     COMMAND "${Python3_EXECUTABLE}"
                             "${CMAKE_CURRENT_SOURCE_DIR}/Tools/GenerateGaussianAperture.py"
                             --check)
            set_tests_properties(Acustra.GaussianApertureData PROPERTIES TIMEOUT 30)
            add_test(NAME Acustra.CalibrationRenderer
                     COMMAND "${Python3_EXECUTABLE}"
                             "${CMAKE_CURRENT_SOURCE_DIR}/Tests/CalibrationRendererTests.py"
                             "$<TARGET_FILE:AcustraPerformanceRenderer>")
            set_tests_properties(Acustra.CalibrationRenderer PROPERTIES TIMEOUT 120)
            execute_process(COMMAND "${Python3_EXECUTABLE}" -c
                                    "import numpy, scipy.io.wavfile"
                            RESULT_VARIABLE ACUSTRA_EXCITATION_PY_DEPS
                            OUTPUT_QUIET ERROR_QUIET)
            if(ACUSTRA_EXCITATION_PY_DEPS EQUAL 0)
                add_test(NAME Acustra.PhysicalScorer
                         COMMAND "${Python3_EXECUTABLE}"
                                 "${CMAKE_CURRENT_SOURCE_DIR}/Tools/FitPhysicalModel.py"
                                 --self-test)
                set_tests_properties(Acustra.PhysicalScorer PROPERTIES TIMEOUT 180)
                # The calibration vector's size, as documented and accepted.
                add_test(NAME Acustra.CalibrationVector
                         COMMAND "${Python3_EXECUTABLE}"
                                 "${CMAKE_CURRENT_SOURCE_DIR}/Tests/CalibrationVectorTests.py"
                                 "$<TARGET_FILE:AcustraPhysicalFitRenderer>"
                                 "$<TARGET_FILE:AcustraExternalCorpusRenderer>")
                set_tests_properties(Acustra.CalibrationVector PROPERTIES TIMEOUT 120)
                # No stage may refit a value a listener chose.
                add_test(NAME Acustra.OptimizerFreeze
                         COMMAND "${Python3_EXECUTABLE}"
                                 "${CMAKE_CURRENT_SOURCE_DIR}/Tests/OptimizerFreezeTests.py")
                set_tests_properties(Acustra.OptimizerFreeze PROPERTIES TIMEOUT 60)
                add_test(NAME Acustra.NaturalPerformancePackaging
                         COMMAND "${Python3_EXECUTABLE}"
                                 "${CMAKE_CURRENT_SOURCE_DIR}/Tests/NaturalPerformancePackagingTests.py")
                set_tests_properties(Acustra.NaturalPerformancePackaging PROPERTIES TIMEOUT 60)
                add_test(NAME Acustra.MaterialLoudness
                         COMMAND "${Python3_EXECUTABLE}"
                                 "${CMAKE_CURRENT_SOURCE_DIR}/Tools/MeasureMaterialLoudness.py"
                                 --self-test)
                set_tests_properties(Acustra.MaterialLoudness PROPERTIES TIMEOUT 60)
                add_test(NAME Acustra.PiezoReference
                         COMMAND "${Python3_EXECUTABLE}"
                                 "${CMAKE_CURRENT_SOURCE_DIR}/Tools/PiezoReference.py"
                                 --self-test)
                set_tests_properties(Acustra.PiezoReference PROPERTIES TIMEOUT 120)
                add_test(NAME Acustra.PiezoCalibration
                         COMMAND "${Python3_EXECUTABLE}"
                                 "${CMAKE_CURRENT_SOURCE_DIR}/Tools/CalibratePiezo.py"
                                 --self-test)
                set_tests_properties(Acustra.PiezoCalibration PROPERTIES TIMEOUT 60)
                add_test(NAME Acustra.ConstructionLoudnessCalibration
                         COMMAND "${Python3_EXECUTABLE}"
                                 "${CMAKE_CURRENT_SOURCE_DIR}/Tools/CalibrateConstructionLoudness.py"
                                 --self-test)
                set_tests_properties(Acustra.ConstructionLoudnessCalibration
                                     PROPERTIES TIMEOUT 60)
                add_test(NAME Acustra.PerformanceBenchmark
                         COMMAND "${Python3_EXECUTABLE}"
                                 "${CMAKE_CURRENT_SOURCE_DIR}/Tools/BenchmarkPerformances.py"
                                 --self-test --renderer "$<TARGET_FILE:AcustraPerformanceRenderer>")
                set_tests_properties(Acustra.PerformanceBenchmark PROPERTIES TIMEOUT 180)
                add_test(NAME Acustra.TechniqueBenchmark
                         COMMAND "${Python3_EXECUTABLE}"
                                 "${CMAKE_CURRENT_SOURCE_DIR}/Tools/BenchmarkTechniqueNotes.py"
                                 --self-test)
                set_tests_properties(Acustra.TechniqueBenchmark PROPERTIES TIMEOUT 180)
                # Recording-only: audits the AG-PT-set hammer-on plateau and
                # never drives the engine, which plays no hammer-on since
                # legato was removed on 2026-09-28.
                add_test(NAME Acustra.HammerOnBenchmark
                         COMMAND "${Python3_EXECUTABLE}"
                                 "${CMAKE_CURRENT_SOURCE_DIR}/Tools/BenchmarkHammerOnNotes.py"
                                 --self-test)
                set_tests_properties(Acustra.HammerOnBenchmark PROPERTIES TIMEOUT 180)
                add_test(NAME Acustra.BodyForcePair
                         COMMAND "${Python3_EXECUTABLE}"
                                 "${CMAKE_CURRENT_SOURCE_DIR}/Tools/AuditBodyForcePair.py"
                                 --self-test)
                set_tests_properties(Acustra.BodyForcePair PROPERTIES TIMEOUT 180)
                add_test(NAME Acustra.GenerateBodyForcePair
                         COMMAND "${Python3_EXECUTABLE}"
                                 "${CMAKE_CURRENT_SOURCE_DIR}/Tools/GenerateBodyForcePair.py"
                                 --self-test)
                set_tests_properties(Acustra.GenerateBodyForcePair PROPERTIES TIMEOUT 180)
                # The steel blend's parallel part (Source/DSP/SteelBodyBlend.h).
                add_test(NAME Acustra.GenerateJointBody
                         COMMAND "${Python3_EXECUTABLE}"
                                 "${CMAKE_CURRENT_SOURCE_DIR}/Tools/GenerateJointBody.py"
                                 --self-test)
                set_tests_properties(Acustra.GenerateJointBody PROPERTIES TIMEOUT 300
                                     ENVIRONMENT "OPENBLAS_NUM_THREADS=1")
                add_test(NAME Acustra.FyldeBridgeReference
                         COMMAND "${Python3_EXECUTABLE}"
                                 "${CMAKE_CURRENT_SOURCE_DIR}/Tools/FyldeBridgeReference.py"
                                 --self-test)
                set_tests_properties(Acustra.FyldeBridgeReference PROPERTIES TIMEOUT 180)
                add_test(NAME Acustra.ExtractExcitation
                         COMMAND "${Python3_EXECUTABLE}"
                                 "${CMAKE_CURRENT_SOURCE_DIR}/Tools/ExtractExcitation.py"
                                 --self-test
                                 --probe "$<TARGET_FILE:AcustraBridgeProbe>"
                                 --work "${CMAKE_CURRENT_BINARY_DIR}/excitation-selftest")
                set_tests_properties(Acustra.ExtractExcitation PROPERTIES TIMEOUT 180)
                # The offline audit and corpus tools' own self-tests: each
                # synthesises what it checks and downloads nothing.
                add_test(NAME Acustra.AuditAttackBands
                         COMMAND "${Python3_EXECUTABLE}"
                                 "${CMAKE_CURRENT_SOURCE_DIR}/Tools/AuditAttackBands.py"
                                 --self-test)
                set_tests_properties(Acustra.AuditAttackBands PROPERTIES TIMEOUT 60)
                add_test(NAME Acustra.AuditDampingCurve
                         COMMAND "${Python3_EXECUTABLE}"
                                 "${CMAKE_CURRENT_SOURCE_DIR}/Tools/AuditDampingCurve.py"
                                 --self-test)
                set_tests_properties(Acustra.AuditDampingCurve PROPERTIES TIMEOUT 60)
                add_test(NAME Acustra.AuditBridgeSpatialMap
                         COMMAND "${Python3_EXECUTABLE}"
                                 "${CMAKE_CURRENT_SOURCE_DIR}/Tools/AuditBridgeSpatialMap.py"
                                 --self-test)
                set_tests_properties(Acustra.AuditBridgeSpatialMap PROPERTIES TIMEOUT 60)
                add_test(NAME Acustra.PrototypePlectrumContact
                         COMMAND "${Python3_EXECUTABLE}"
                                 "${CMAKE_CURRENT_SOURCE_DIR}/Tools/PrototypePlectrumContact.py"
                                 --self-test)
                set_tests_properties(Acustra.PrototypePlectrumContact PROPERTIES TIMEOUT 60)
                add_test(NAME Acustra.FitCaptureVoicing
                         COMMAND "${Python3_EXECUTABLE}"
                                 "${CMAKE_CURRENT_SOURCE_DIR}/Tools/FitCaptureVoicing.py"
                                 --self-test)
                set_tests_properties(Acustra.FitCaptureVoicing PROPERTIES TIMEOUT 60)
                add_test(NAME Acustra.FitModelConvergence
                         COMMAND "${Python3_EXECUTABLE}"
                                 "${CMAKE_CURRENT_SOURCE_DIR}/Tools/FitModelConvergence.py"
                                 --self-test)
                set_tests_properties(Acustra.FitModelConvergence PROPERTIES TIMEOUT 60)
                add_test(NAME Acustra.ValidateCapture
                         COMMAND "${Python3_EXECUTABLE}"
                                 "${CMAKE_CURRENT_SOURCE_DIR}/Tools/ValidateCapture.py"
                                 --self-test)
                set_tests_properties(Acustra.ValidateCapture PROPERTIES TIMEOUT 60)
                # The open-corpus benchmark end to end on a synthetic corpus.
                add_test(NAME Acustra.OpenCorporaBenchmark
                         COMMAND "${Python3_EXECUTABLE}"
                                 "${CMAKE_CURRENT_SOURCE_DIR}/Tools/BenchmarkOpenCorpora.py"
                                 --smoke --renderer "$<TARGET_FILE:AcustraExternalCorpusRenderer>")
                set_tests_properties(Acustra.OpenCorporaBenchmark PROPERTIES TIMEOUT 180)
                add_test(NAME Acustra.AirModeAudit
                         COMMAND "${Python3_EXECUTABLE}"
                                 "${CMAKE_CURRENT_SOURCE_DIR}/Tools/AuditAirMode.py"
                                 --self-test)
                set_tests_properties(Acustra.AirModeAudit PROPERTIES TIMEOUT 120)
                add_test(NAME Acustra.RingingTraceAnalysis
                         COMMAND "${Python3_EXECUTABLE}"
                                 "${CMAKE_CURRENT_SOURCE_DIR}/Tools/AnalyzeRingingTrace.py"
                                 --self-test)
                set_tests_properties(Acustra.RingingTraceAnalysis PROPERTIES TIMEOUT 60)
                # Every tool with a self-test is one of the above.
                add_test(NAME Acustra.SelfTestRegistry
                         COMMAND "${Python3_EXECUTABLE}"
                                 "${CMAKE_CURRENT_SOURCE_DIR}/Tests/SelfTestRegistryTests.py")
                set_tests_properties(Acustra.SelfTestRegistry PROPERTIES TIMEOUT 30)
                if(NOT WIN32)
                    add_test(NAME Acustra.PythonTestsReported
                             COMMAND "${CMAKE_COMMAND}"
                                     "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}"
                                     "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/python-tests-reported"
                                     "-DPYTHON=${Python3_EXECUTABLE}"
                                     -P "${CMAKE_CURRENT_SOURCE_DIR}/Tests/CheckPythonTestsReported.cmake")
                    set_tests_properties(Acustra.PythonTestsReported PROPERTIES TIMEOUT 180)
                endif()
            else()
                string(CONCAT ACUSTRA_PYTHON_TESTS_MISSING
                    "${Python3_EXECUTABLE} cannot import numpy and scipy, so the Python "
                    "self-tests that need them (the physical scorer, calibration, loudness, "
                    "piezo, benchmark and fit-tool checks) are not registered")
            endif()
        else()
            set(ACUSTRA_PYTHON_TESTS_MISSING
                "no Python 3 interpreter was found, so no Python self-test is registered")
        endif()
        if(DEFINED ACUSTRA_PYTHON_TESTS_MISSING)
            string(CONCAT ACUSTRA_PYTHON_TESTS_MISSING
                "Acustra: ${ACUSTRA_PYTHON_TESTS_MISSING}. Pass "
                "-DPython3_EXECUTABLE=<a Python 3 with numpy and scipy> to run them.")
            if(ACUSTRA_REQUIRE_PYTHON_TESTS)
                message(FATAL_ERROR "${ACUSTRA_PYTHON_TESTS_MISSING}")
            else()
                message(WARNING "${ACUSTRA_PYTHON_TESTS_MISSING}")
            endif()
        endif()
    elseif(ACUSTRA_REQUIRE_PYTHON_TESTS)
        message(FATAL_ERROR
            "Acustra: ACUSTRA_REQUIRE_PYTHON_TESTS needs ACUSTRA_BUILD_TOOLS=ON, "
            "whose renderers the Python self-tests drive")
    endif()

    if(ACUSTRA_BUILD_PLUGIN)
        add_executable(AcustraPluginProcessorTests
            Tests/PluginProcessorTests.cpp)
        target_link_libraries(AcustraPluginProcessorTests PRIVATE Acustra)
        target_include_directories(AcustraPluginProcessorTests PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/Source"
            "$<TARGET_PROPERTY:Acustra,JUCE_GENERATED_SOURCES_DIRECTORY>"
            "$<TARGET_PROPERTY:Acustra,INCLUDE_DIRECTORIES>")
        target_compile_features(AcustraPluginProcessorTests PRIVATE cxx_std_20)

        if(MSVC)
            target_compile_options(AcustraPluginProcessorTests PRIVATE
                /W4 /permissive-)
        else()
            target_compile_options(AcustraPluginProcessorTests PRIVATE
                -Wall -Wextra -Wpedantic)
        endif()

        acustra_add_suite(Acustra.PluginProcessor
                 AcustraPluginProcessorTests)
        set_tests_properties(Acustra.PluginProcessor PROPERTIES TIMEOUT 90)

        add_executable(AcustraPluginTempoReleaseTests
            Tests/PluginTempoReleaseTests.cpp)
        target_link_libraries(AcustraPluginTempoReleaseTests PRIVATE Acustra)
        target_include_directories(AcustraPluginTempoReleaseTests PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/Source"
            "$<TARGET_PROPERTY:Acustra,JUCE_GENERATED_SOURCES_DIRECTORY>"
            "$<TARGET_PROPERTY:Acustra,INCLUDE_DIRECTORIES>")
        target_compile_features(AcustraPluginTempoReleaseTests PRIVATE cxx_std_20)
        if(MSVC)
            target_compile_options(AcustraPluginTempoReleaseTests PRIVATE /W4 /permissive-)
        else()
            target_compile_options(AcustraPluginTempoReleaseTests PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        acustra_add_suite(Acustra.PluginTempoRelease AcustraPluginTempoReleaseTests)
        set_tests_properties(Acustra.PluginTempoRelease PROPERTIES TIMEOUT 120)
    endif()
endif()

# Recording-guided realism regression 1.
if(BUILD_TESTING)
    add_executable(AcustraVelocityRealismTests Tests/VelocityRealismTests.cpp)
    target_link_libraries(AcustraVelocityRealismTests PRIVATE AcustraDSP)
    set_target_properties(AcustraVelocityRealismTests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_test(NAME Acustra.VelocityRealism COMMAND AcustraVelocityRealismTests)
    set_tests_properties(Acustra.VelocityRealism PROPERTIES TIMEOUT 60)
endif()

# Recording-guided realism regression 2.
if(BUILD_TESTING)
    add_executable(AcustraContactReleaseTests Tests/ContactReleaseTests.cpp)
    target_link_libraries(AcustraContactReleaseTests PRIVATE AcustraDSP)
    set_target_properties(AcustraContactReleaseTests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_test(NAME Acustra.ContactRelease COMMAND AcustraContactReleaseTests)
    set_tests_properties(Acustra.ContactRelease PROPERTIES TIMEOUT 60)

    add_executable(AcustraStringPitchRealismTests Tests/StringPitchRealismTests.cpp)
    target_link_libraries(AcustraStringPitchRealismTests PRIVATE AcustraDSP)
    set_target_properties(AcustraStringPitchRealismTests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_test(NAME Acustra.StringPitchRealism COMMAND AcustraStringPitchRealismTests)
    set_tests_properties(Acustra.StringPitchRealism PROPERTIES TIMEOUT 60)
endif()

# Recording-guided realism regression 3.
if(BUILD_TESTING)
    add_executable(AcustraStringDecayRealismTests Tests/StringDecayRealismTests.cpp)
    target_link_libraries(AcustraStringDecayRealismTests PRIVATE AcustraDSP)
    set_target_properties(AcustraStringDecayRealismTests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_test(NAME Acustra.StringDecayRealism COMMAND AcustraStringDecayRealismTests)
    set_tests_properties(Acustra.StringDecayRealism PROPERTIES TIMEOUT 60)
endif()

# Recording-guided realism regression 4.
if(BUILD_TESTING)
    add_executable(AcustraBodyRealismTests Tests/BodyRealismTests.cpp)
    target_link_libraries(AcustraBodyRealismTests PRIVATE AcustraDSP)
    set_target_properties(AcustraBodyRealismTests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_test(NAME Acustra.BodyRealism COMMAND AcustraBodyRealismTests)
    set_tests_properties(Acustra.BodyRealism PROPERTIES TIMEOUT 60)
endif()

# Recording-guided realism regression 5.
if(BUILD_TESTING)
    add_executable(AcustraVoicingRealismTests Tests/VoicingRealismTests.cpp)
    target_link_libraries(AcustraVoicingRealismTests PRIVATE AcustraDSP)
    set_target_properties(AcustraVoicingRealismTests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_test(NAME Acustra.VoicingRealism COMMAND AcustraVoicingRealismTests)
    set_tests_properties(Acustra.VoicingRealism PROPERTIES TIMEOUT 60)
endif()

# Recording-guided realism regression 6.
if(BUILD_TESTING)
    add_executable(AcustraSympathyRealismTests Tests/SympathyRealismTests.cpp)
    target_link_libraries(AcustraSympathyRealismTests PRIVATE AcustraDSP)
    set_target_properties(AcustraSympathyRealismTests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_test(NAME Acustra.SympathyRealism COMMAND AcustraSympathyRealismTests)
    set_tests_properties(Acustra.SympathyRealism PROPERTIES TIMEOUT 60)
endif()

# Recording-guided realism regression 7.
if(BUILD_TESTING)
    add_executable(AcustraRepeatedPluckRealismTests Tests/RepeatedPluckRealismTests.cpp)
    target_link_libraries(AcustraRepeatedPluckRealismTests PRIVATE AcustraDSP)
    set_target_properties(AcustraRepeatedPluckRealismTests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_test(NAME Acustra.RepeatedPluckRealism COMMAND AcustraRepeatedPluckRealismTests)
    set_tests_properties(Acustra.RepeatedPluckRealism PROPERTIES TIMEOUT 60)
endif()

# Correlated picking gestures and controller/scheduling boundaries.
if(BUILD_TESTING)
    add_executable(AcustraPickingHandTests Tests/PickingHandTests.cpp)
    target_link_libraries(AcustraPickingHandTests PRIVATE AcustraDSP)
    set_target_properties(AcustraPickingHandTests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_test(NAME Acustra.PickingHand COMMAND AcustraPickingHandTests)
    set_tests_properties(Acustra.PickingHand PROPERTIES TIMEOUT 120)
    add_executable(AcustraPickingHandBoundaryTests Tests/PickingHandBoundaryTests.cpp)
    target_link_libraries(AcustraPickingHandBoundaryTests PRIVATE AcustraDSP)
    set_target_properties(AcustraPickingHandBoundaryTests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_test(NAME Acustra.PickingHandBoundary COMMAND AcustraPickingHandBoundaryTests)
    set_tests_properties(Acustra.PickingHandBoundary PROPERTIES TIMEOUT 120)
endif()

# Recording-guided realism regression 8.
if(BUILD_TESTING)
    add_executable(AcustraStrummingRealismTests Tests/StrummingRealismTests.cpp)
    target_link_libraries(AcustraStrummingRealismTests PRIVATE AcustraDSP)
    set_target_properties(AcustraStrummingRealismTests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_test(NAME Acustra.StrummingRealism COMMAND AcustraStrummingRealismTests)
    set_tests_properties(Acustra.StrummingRealism PROPERTIES TIMEOUT 60)
endif()

# Recording-guided realism regression 9.
if(BUILD_TESTING)
    add_executable(AcustraLegatoRealismTests Tests/LegatoRealismTests.cpp)
    target_link_libraries(AcustraLegatoRealismTests PRIVATE AcustraDSP)
    set_target_properties(AcustraLegatoRealismTests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_test(NAME Acustra.LegatoRealism COMMAND AcustraLegatoRealismTests)
    set_tests_properties(Acustra.LegatoRealism PROPERTIES TIMEOUT 60)
endif()

# Recording-guided realism regression 10.
if(BUILD_TESTING)
    add_executable(AcustraReleaseRealismTests Tests/ReleaseRealismTests.cpp)
    target_link_libraries(AcustraReleaseRealismTests PRIVATE AcustraDSP)
    set_target_properties(AcustraReleaseRealismTests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    add_test(NAME Acustra.ReleaseRealism COMMAND AcustraReleaseRealismTests)
    set_tests_properties(Acustra.ReleaseRealism PROPERTIES TIMEOUT 60)
endif()

# Live tuning changes must preserve both waves and filter histories.
if(BUILD_TESTING)
    add_executable(AcustraRetuneContinuityTests Tests/RetuneContinuityTests.cpp)
    target_link_libraries(AcustraRetuneContinuityTests PRIVATE AcustraDSP)
    set_target_properties(AcustraRetuneContinuityTests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    acustra_add_suite(Acustra.RetuneContinuity AcustraRetuneContinuityTests)
endif()

# Exact-input memoization must preserve pitch, contact geometry and transitions.
if(BUILD_TESTING)
    add_executable(AcustraPitchGeometryCacheTests Tests/PitchGeometryCacheTests.cpp)
    target_link_libraries(AcustraPitchGeometryCacheTests PRIVATE AcustraDSP)
    set_target_properties(AcustraPitchGeometryCacheTests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    acustra_add_suite(Acustra.PitchGeometryCache AcustraPitchGeometryCacheTests)
endif()

# Native construction-strum measurement and string-release energy checks.
if(ACUSTRA_BUILD_TOOLS)
    add_executable(AcustraConstructionStrumLevels Tools/ConstructionStrumLevels.cpp)
    target_link_libraries(AcustraConstructionStrumLevels PRIVATE AcustraDSP)
endif()
if(BUILD_TESTING)
    add_executable(AcustraPluckPolarisationTests Tests/PluckPolarisationTests.cpp)
    target_link_libraries(AcustraPluckPolarisationTests PRIVATE AcustraDSP)
    acustra_add_suite(Acustra.PluckPolarisation AcustraPluckPolarisationTests)
    add_executable(AcustraReleasePolicyTests Tests/ReleasePolicyTests.cpp)
    target_link_libraries(AcustraReleasePolicyTests PRIVATE AcustraDSP)
    acustra_add_suite(Acustra.ReleasePolicy AcustraReleasePolicyTests)
    add_executable(AcustraSustainPolicyTests Tests/SustainPolicyTests.cpp)
    target_link_libraries(AcustraSustainPolicyTests PRIVATE AcustraDSP)
    set_target_properties(AcustraSustainPolicyTests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    acustra_add_suite(Acustra.SustainPolicy AcustraSustainPolicyTests)
    set_tests_properties(Acustra.PluckPolarisation PROPERTIES TIMEOUT 50)
endif()
