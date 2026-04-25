cmake_minimum_required(VERSION 3.25)

project(VocalTractLab2
  VERSION 2.4
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Math optimizations -----------------------------------------------------
# -fno-math-errno tells the compiler the math library functions (sqrt, pow,
# log, ...) don't set errno, so it can treat them as pure: hoist them out of
# loops, CSE redundant calls, drop them when the result is unused. Safe in
# practice — almost no real code reads errno after a math call. Worth ~1-3%
# on math-heavy code and unlocks better autovectorization.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  add_compile_options($<$<NOT:$<CONFIG:Debug>>:-fno-math-errno>)
endif()

# --- Link-time optimization -------------------------------------------------
# Lets the linker inline across translation units, so e.g. tiny accessors
# defined in a .cpp file (Point2D::normalize) get inlined at the hot call
# sites in Surface.cpp / VocalTract.cpp without us having to hand-move them
# to a header. Enabled for everything except Debug builds.
include(CheckIPOSupported)
check_ipo_supported(RESULT VTL_IPO_SUPPORTED OUTPUT VTL_IPO_ERROR)
if(VTL_IPO_SUPPORTED AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(VTL_ENABLE_IPO TRUE)
  message(STATUS "Link-time optimization: enabled")
else()
  set(VTL_ENABLE_IPO FALSE)
  if(NOT VTL_IPO_SUPPORTED)
    message(STATUS "Link-time optimization: not supported by toolchain (${VTL_IPO_ERROR})")
  else()
    message(STATUS "Link-time optimization: disabled for Debug builds")
  endif()
endif()

# --- Dependencies -----------------------------------------------------------

find_package(wxWidgets REQUIRED COMPONENTS core base gl adv)
include(${wxWidgets_USE_FILE})

if(NOT WIN32)
  find_package(OpenAL CONFIG REQUIRED)
endif()

# --- Sources ----------------------------------------------------------------

set(VTL_SOURCES
  sources/vtl/acoustics/TdsModel.cpp
  sources/vtl/acoustics/TlModel.cpp
  sources/vtl/acoustics/Tube.cpp
  sources/vtl/analysis/F0EstimatorYin.cpp
  sources/vtl/analysis/PoleZeroPlan.cpp
  sources/vtl/analysis/VoiceQualityEstimator.cpp
  sources/vtl/anatomy/AnatomyParams.cpp
  sources/vtl/anatomy/Splines.cpp
  sources/vtl/anatomy/Surface.cpp
  sources/vtl/anatomy/VocalTract.cpp
  sources/vtl/api/VocalTractLabApi.cpp
  sources/vtl/core/Constants.cpp
  sources/vtl/core/Geometry.cpp
  sources/vtl/core/Matrix2x2.cpp
  sources/vtl/core/TimeFunction.cpp
  sources/vtl/dsp/Dsp.cpp
  sources/vtl/dsp/IirFilter.cpp
  sources/vtl/dsp/Signal.cpp
  sources/vtl/glottis/GeometricGlottis2019.cpp
  sources/vtl/glottis/GeometricGlottis2025.cpp
  sources/vtl/glottis/Glottis.cpp
  sources/vtl/glottis/LfPulse.cpp
  sources/vtl/glottis/TriangularGlottis.cpp
  sources/vtl/io/SoundLib.cpp
  sources/vtl/io/XmlHelper.cpp
  sources/vtl/io/XmlNode.cpp
  sources/vtl/phonetics/Sampa.cpp
  sources/vtl/phonetics/SegmentSequence.cpp
  sources/vtl/synthesis/GesturalScore.cpp
  sources/vtl/synthesis/StaticPhone.cpp
  sources/vtl/synthesis/Synthesizer.cpp
)

set(GUI_SOURCES
  sources/gui/app/Application.cpp
  sources/gui/app/Data.cpp
  sources/gui/app/MainWindow.cpp
  sources/gui/app/SynthesisThread.cpp
  sources/gui/dialogs/AnalysisResultsDialog.cpp
  sources/gui/dialogs/AnalysisSettingsDialog.cpp
  sources/gui/dialogs/AnatomyParamsDialog.cpp
  sources/gui/dialogs/AnnotationDialog.cpp
  sources/gui/dialogs/EmaConfigDialog.cpp
  sources/gui/dialogs/FdsOptionsDialog.cpp
  sources/gui/dialogs/FormantOptimizationDialog.cpp
  sources/gui/dialogs/GlottisDialog.cpp
  sources/gui/dialogs/GlottisPanel.cpp
  sources/gui/dialogs/LfPulseDialog.cpp
  sources/gui/dialogs/PhoneticParamsDialog.cpp
  sources/gui/dialogs/PoleZeroDialog.cpp
  sources/gui/dialogs/SpectrumOptionsDialog.cpp
  sources/gui/dialogs/TdsOptionsDialog.cpp
  sources/gui/dialogs/TransitionDialog.cpp
  sources/gui/dialogs/VocalTractDialog.cpp
  sources/gui/dialogs/VocalTractShapesDialog.cpp
  sources/gui/graphing/ColorScale.cpp
  sources/gui/graphing/Graph.cpp
  sources/gui/graphing/SpectrogramPlot.cpp
  sources/gui/pages/GesturalScorePage.cpp
  sources/gui/pages/SignalPage.cpp
  sources/gui/pages/TdsPage.cpp
  sources/gui/pages/VocalTractPage.cpp
  sources/gui/pictures/AreaFunctionPicture.cpp
  sources/gui/pictures/BasicPicture.cpp
  sources/gui/pictures/CrossSectionPicture.cpp
  sources/gui/pictures/GesturalScorePicture.cpp
  sources/gui/pictures/GlottisPicture.cpp
  sources/gui/pictures/LfPulsePicture.cpp
  sources/gui/pictures/PoleZeroPlot.cpp
  sources/gui/pictures/SignalComparisonPicture.cpp
  sources/gui/pictures/SignalPicture.cpp
  sources/gui/pictures/SimpleSpectrumPicture.cpp
  sources/gui/pictures/SpectrogramPicture.cpp
  sources/gui/pictures/SpectrumPicture.cpp
  sources/gui/pictures/TdsSpatialSignalPicture.cpp
  sources/gui/pictures/TdsTimeSignalPicture.cpp
  sources/gui/pictures/TdsTubePicture.cpp
  sources/gui/pictures/TimeAxisPicture.cpp
  sources/gui/pictures/VocalTractPicture.cpp
  sources/gui/util/SilentMessageBox.cpp
)

set(ALL_SOURCES ${VTL_SOURCES} ${GUI_SOURCES})

if(WIN32)
  list(APPEND ALL_SOURCES sources/gui/windows/VocalTractLab2.rc)
endif()

# --- Target -----------------------------------------------------------------

add_executable(VocalTractLab2 WIN32 MACOSX_BUNDLE ${ALL_SOURCES})

set_target_properties(VocalTractLab2 PROPERTIES
  INTERPROCEDURAL_OPTIMIZATION ${VTL_ENABLE_IPO}
)

target_include_directories(VocalTractLab2 PRIVATE
  sources
  sources/vtl
  sources/gui
)
if(WIN32)
  # resource.h is included by sources/gui/windows/VocalTractLab2.rc.
  target_include_directories(VocalTractLab2 PRIVATE sources/gui/windows)
endif()

target_link_libraries(VocalTractLab2 PRIVATE ${wxWidgets_LIBRARIES})

if(NOT WIN32)
  target_compile_definitions(VocalTractLab2 PRIVATE HAVE_OPENAL)
  target_link_libraries(VocalTractLab2 PRIVATE OpenAL::OpenAL)
endif()

# --- Runtime data staging ---------------------------------------------------
#
# Data::init resolves runtime files (speakers, config.ini, example01.*, the
# vowel outline GIFs) at programPath — wxStandardPaths::GetExecutablePath() on
# Linux/Windows, GetResourcesDir() on macOS. The batch tools additionally look
# for an Examples/ subdirectory next to the binary. Stage the data/ tree into
# the right place at build time so the binary is runnable in place.

# Files to land flat next to the executable (relative to data/).
set(RUNTIME_DATA_FILES
  speakers/JD2.speaker
  speakers/M01.speaker
  speakers/W02.speaker
  config.ini
  example01-orig.wav
  example01.ges
  example01.seg
  example01.tube
  Vowel-a-outline.gif
  Vowel-i-outline.gif
  Vowel-u-outline.gif
)

if(APPLE)
  set_target_properties(VocalTractLab2 PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME          "VocalTractLab2"
    MACOSX_BUNDLE_GUI_IDENTIFIER       "de.vocaltractlab.VocalTractLab2"
    MACOSX_BUNDLE_BUNDLE_VERSION       "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_ICON_FILE            "VocalTractLab2.icns"
  )

  set(BUNDLE_RESOURCE_FILES "")
  foreach(REL ${RUNTIME_DATA_FILES})
    list(APPEND BUNDLE_RESOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/data/${REL}")
  endforeach()
  target_sources(VocalTractLab2 PRIVATE ${BUNDLE_RESOURCE_FILES})
  set_source_files_properties(${BUNDLE_RESOURCE_FILES} PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources"
  )

  file(GLOB EXAMPLE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/data/batch/*")
  target_sources(VocalTractLab2 PRIVATE ${EXAMPLE_FILES})
  set_source_files_properties(${EXAMPLE_FILES} PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources/Examples"
  )
else()
  # Linux/Windows: copy data files next to the built binary so it can be
  # launched in place from build/<preset>/.
  foreach(REL ${RUNTIME_DATA_FILES})
    cmake_path(GET REL FILENAME BASENAME)
    add_custom_command(TARGET VocalTractLab2 POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/data/${REL}"
        "$<TARGET_FILE_DIR:VocalTractLab2>/${BASENAME}"
      VERBATIM
    )
  endforeach()
  add_custom_command(TARGET VocalTractLab2 POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
      "${CMAKE_CURRENT_SOURCE_DIR}/data/batch"
      "$<TARGET_FILE_DIR:VocalTractLab2>/Examples"
    VERBATIM
  )
endif()

# --- VocalTractLabApi shared library ----------------------------------------
#
# The vtl sources are compiled once into an OBJECT library and reused by both
# the public shared library and (when enabled) the synthesis_bench executable.
# Static-linking the .o files into the benchmark lets the linker inline across
# the bench/API boundary, which dynamic linking through the .dylib blocks.

add_library(VocalTractLabApiObj OBJECT ${VTL_SOURCES})

set_target_properties(VocalTractLabApiObj PROPERTIES
  POSITION_INDEPENDENT_CODE ON
  INTERPROCEDURAL_OPTIMIZATION ${VTL_ENABLE_IPO}
)

target_include_directories(VocalTractLabApiObj PUBLIC
  sources
  sources/vtl
)

if(WIN32)
  target_compile_definitions(VocalTractLabApiObj PRIVATE
    _USRDLL
    _USE_MATH_DEFINES
    _CRT_SECURE_NO_WARNINGS
  )
  target_link_libraries(VocalTractLabApiObj PUBLIC winmm)
else()
  target_compile_definitions(VocalTractLabApiObj PUBLIC HAVE_OPENAL)
  target_link_libraries(VocalTractLabApiObj PUBLIC OpenAL::OpenAL)
endif()

set(API_SHARED_SOURCES "")
if(WIN32)
  list(APPEND API_SHARED_SOURCES sources/vtl/api/VocalTractLabApi.def)
endif()

add_library(VocalTractLabApi SHARED ${API_SHARED_SOURCES} $<TARGET_OBJECTS:VocalTractLabApiObj>)
target_link_libraries(VocalTractLabApi PRIVATE VocalTractLabApiObj)

set_target_properties(VocalTractLabApi PROPERTIES
  INTERPROCEDURAL_OPTIMIZATION ${VTL_ENABLE_IPO}
)

# --- Benchmarks -------------------------------------------------------------
# Microbenchmarks for the vtl synthesis pipeline. Use them to gauge how close
# the backend is to real-time on a given platform / build configuration:
#
#   cmake --build --preset mac-debug --target synthesis_bench
#   ./build/mac-debug/synthesis_bench --benchmark_min_time=2s

option(VTL_BUILD_BENCHMARKS "Build the synthesis benchmarks" ON)

if(VTL_BUILD_BENCHMARKS)
  find_package(benchmark CONFIG REQUIRED)

  configure_file(
    benchmarks/benchmark_data.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/benchmarks/benchmark_data.h"
    @ONLY
  )

  # Statically link the API object files into the benchmark so the linker
  # can inline the API entry points (vtlSynthesisAddTube, ...) and the
  # downstream call chain into the bench loop bodies. The .dylib boundary
  # blocks LTO from doing this, which matters most for the fast-loop
  # benchmarks (BM_SynthesisAddTube_Block).
  add_executable(synthesis_bench
    benchmarks/synthesis_bench.cpp
    $<TARGET_OBJECTS:VocalTractLabApiObj>
  )
  set_target_properties(synthesis_bench PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION ${VTL_ENABLE_IPO}
  )
  target_link_libraries(synthesis_bench PRIVATE
    VocalTractLabApiObj
    benchmark::benchmark
  )
  target_include_directories(synthesis_bench PRIVATE
    sources/vtl/api
    "${CMAKE_CURRENT_BINARY_DIR}/benchmarks"
  )
endif()
