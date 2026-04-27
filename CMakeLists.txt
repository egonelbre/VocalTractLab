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

# MSVC: project-wide compile definitions so every target compiling the
# vtl/* sources sees a consistent set, instead of relying on per-target
# adds that the wxWidgets GUI target was missing.
#   _HAS_STD_BYTE=0    — disables std::byte. The vtl sources pervasively
#                        `using namespace std;` and also pull in Windows
#                        SDK headers (rpcndr.h, wtypes.h, …) that define
#                        `byte` as unsigned char; both reaching the
#                        unqualified namespace at once produces
#                        `error C2872: 'byte': ambiguous symbol`. The
#                        codebase does not use std::byte itself.
#   _USE_MATH_DEFINES  — enables M_PI / M_E / etc. in <cmath>. Multiple
#                        vtl translation units use M_PI directly.
#   _CRT_SECURE_NO_WARNINGS — silences the deprecation warnings on the
#                        upstream Birkholz code's use of strcpy/sprintf.
if(MSVC)
  add_compile_definitions(
    _HAS_STD_BYTE=0
    _USE_MATH_DEFINES
    _CRT_SECURE_NO_WARNINGS
  )
endif()

# EXPERIMENT: -ffast-math lets the compiler reorder FP ops, fuse FMAs,
# treat negative zero as zero, assume no NaN/Inf, etc. Big potential win
# (vectorization, FMA, hoisting) but breaks bit-exact reproducibility.
# Toggled below to measure cost vs benefit.
option(VTL_ENABLE_FAST_MATH "Enable -ffast-math (changes FP results)" OFF)
if(VTL_ENABLE_FAST_MATH AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  add_compile_options($<$<NOT:$<CONFIG:Debug>>:-ffast-math>)
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

# wxWidgets is only used by the legacy GUI; skip it when building for the
# browser (Emscripten), where the wx GUI cannot run anyway.
if(NOT EMSCRIPTEN)
  find_package(wxWidgets REQUIRED COMPONENTS core base gl adv)
  include(${wxWidgets_USE_FILE})
endif()

if(EMSCRIPTEN)
  # Emscripten ships its own OpenAL implementation backed by Web Audio. We
  # don't go through find_package; instead we just pass `-lopenal` to the
  # link line of any target that needs it.
elseif(NOT WIN32)
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

# wxWidgets-based GUI: native only. The browser build skips this entire
# block because wxWidgets has no Emscripten port.
if(NOT EMSCRIPTEN)
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

  # Re-sign the bundle ad-hoc after Resources/ is fully staged. The
  # linker emits an "adhoc, linker-signed" signature on the Mach-O at
  # link time, which only covers the binary itself — leaving the
  # bundle with `Sealed Resources=none` and breaking `spctl --assess`.
  # Doing a plain `codesign --force --sign -` here re-emits the
  # signature against the populated bundle so the seal covers
  # Resources/. CI can replace this with a Developer ID signature
  # later via `codesign --force --options runtime --sign "..."`.
  add_custom_command(TARGET VocalTractLab2 POST_BUILD
    COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:VocalTractLab2>"
    VERBATIM
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
endif()  # NOT EMSCRIPTEN — end of wxWidgets GUI block

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
elseif(EMSCRIPTEN)
  # Emscripten's OpenAL port is a single linker flag away. Used by
  # io/SoundLib.cpp; the live build streams audio through the AudioWorklet
  # path instead and does not call into OpenAL itself.
  target_compile_definitions(VocalTractLabApiObj PUBLIC HAVE_OPENAL)
  target_link_options(VocalTractLabApiObj PUBLIC "SHELL:-lopenal")
  # The vtl_live target enables -sAUDIO_WORKLET / -sWASM_WORKERS at link
  # time, which switches the wasm-ld in --shared-memory mode. Every object
  # file in the link must be built with the 'atomics' + 'bulk-memory'
  # features for that to be allowed; -pthread enables them at compile time.
  target_compile_options(VocalTractLabApiObj PUBLIC -pthread)
else()
  target_compile_definitions(VocalTractLabApiObj PUBLIC HAVE_OPENAL)
  target_link_libraries(VocalTractLabApiObj PUBLIC OpenAL::OpenAL)
endif()

# The vtl/* code is upstream Birkholz source; on macOS it triggers
# -Wdeprecated-declarations from Apple retiring OpenAL (in io/SoundLib.cpp)
# and from libc retiring sprintf (used across anatomy/glottis/io/synthesis/
# phonetics). Suppress for this target so we aren't carrying divergent
# patches over upstream just to silence Apple's deprecation pressure.
if(APPLE)
  target_compile_options(VocalTractLabApiObj PRIVATE -Wno-deprecated-declarations)
endif()

# The C API shared library is consumed by the MATLAB / Python examples and
# cannot be a meaningful artifact in the browser; skip it on Emscripten.
if(NOT EMSCRIPTEN)
  set(API_SHARED_SOURCES "")
  if(WIN32)
    list(APPEND API_SHARED_SOURCES sources/vtl/api/VocalTractLabApi.def)
  endif()

  add_library(VocalTractLabApi SHARED ${API_SHARED_SOURCES} $<TARGET_OBJECTS:VocalTractLabApiObj>)
  target_link_libraries(VocalTractLabApi PRIVATE VocalTractLabApiObj)

  set_target_properties(VocalTractLabApi PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION ${VTL_ENABLE_IPO}
  )
endif()

# --- Live (ImGui realtime experimentation app) -------------------------------
# Lightweight cross-platform app that drives the synthesizer in realtime,
# draws the vocal tract outline and the radiated spectrum with ImGui, and
# lets the user modulate the fundamental frequency on the fly. Independent
# of the wxWidgets GUI; depends only on glfw, imgui (with glfw+opengl3
# bindings), OpenGL, and openal-soft.

option(VTL_BUILD_LIVE "Build the ImGui realtime live app" ON)

# The live build streams audio through OpenAL, which the project only wires up on
# non-Windows targets (matching SoundLib). Skip the live build on Windows for now.
if(WIN32)
  set(VTL_BUILD_LIVE OFF)
endif()

if(VTL_BUILD_LIVE)
  set(LIVE_SOURCES
    sources/live/main.cpp
    sources/live/AssetPaths.cpp
    sources/live/AudioEngine.cpp
    sources/live/ControlPoints.cpp
    sources/live/ControlsPanel.cpp
    sources/live/LfPulsePanel.cpp
    sources/live/PresetsPanel.cpp
    sources/live/SpectrumPanel.cpp
    sources/live/TractView.cpp
    sources/live/VocalTract2DPanel.cpp
    sources/live/VocalTract3DPanel.cpp
    sources/live/VowelChartPanel.cpp
  )

  # Noto Sans Regular (SIL OFL 1.1). Bundled at build time so the live app
  # can render IPA glyphs (ɪ ɛ ɑ ɔ ʌ ɜ ʊ in the vowel chart) — ImGui's
  # default Proggy Clean only covers basic ASCII, and JetBrains Mono /
  # most popular monospace fonts don't cover the IPA Extensions block.
  # Noto Sans has full 96/96 coverage of U+0250–U+02AF. Pinned to a
  # monthly release tag with sha256 so a tampered or moved release URL
  # fails fast at configure time.
  set(VTL_LIVE_FONT_TAG "noto-monthly-release-2026.04.01")
  set(VTL_LIVE_FONT_FILE "${CMAKE_BINARY_DIR}/NotoSans-Regular.ttf")
  set(VTL_LIVE_FONT_SHA256
      "478c558ea716033cd60c03438f628dfa75694dcf6b5f6d505a2f05fd2b4f3823")
  if(NOT EXISTS "${VTL_LIVE_FONT_FILE}")
    message(STATUS "Downloading Noto Sans Regular (${VTL_LIVE_FONT_TAG})")
    file(DOWNLOAD
      "https://github.com/notofonts/notofonts.github.io/raw/${VTL_LIVE_FONT_TAG}/fonts/NotoSans/hinted/ttf/NotoSans-Regular.ttf"
      "${VTL_LIVE_FONT_FILE}"
      EXPECTED_HASH "SHA256=${VTL_LIVE_FONT_SHA256}"
      TLS_VERIFY ON
      STATUS VTL_LIVE_FONT_DL_STATUS
    )
    list(GET VTL_LIVE_FONT_DL_STATUS 0 VTL_LIVE_FONT_DL_RC)
    if(NOT VTL_LIVE_FONT_DL_RC EQUAL 0)
      message(FATAL_ERROR
        "Failed to download Noto Sans Regular: ${VTL_LIVE_FONT_DL_STATUS}")
    endif()
  endif()

  if(EMSCRIPTEN)
    # ---- WASM/browser build -----------------------------------------------
    # Emscripten ships GLFW, OpenAL, and WebGL2 as built-in ports; we only
    # need to fetch ImGui ourselves. We grab the docking branch directly so
    # the live build keeps the same dock-builder layout it has on native.
    include(FetchContent)
    FetchContent_Declare(imgui_src
      GIT_REPOSITORY https://github.com/ocornut/imgui.git
      GIT_TAG v1.92.7-docking
    )
    FetchContent_MakeAvailable(imgui_src)

    add_library(imgui_static STATIC
      ${imgui_src_SOURCE_DIR}/imgui.cpp
      ${imgui_src_SOURCE_DIR}/imgui_demo.cpp
      ${imgui_src_SOURCE_DIR}/imgui_draw.cpp
      ${imgui_src_SOURCE_DIR}/imgui_tables.cpp
      ${imgui_src_SOURCE_DIR}/imgui_widgets.cpp
      ${imgui_src_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
      ${imgui_src_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    )
    target_include_directories(imgui_static PUBLIC
      ${imgui_src_SOURCE_DIR}
      ${imgui_src_SOURCE_DIR}/backends
    )
    # Emscripten ports flags only matter at link time; passing -sUSE_GLFW=3
    # at compile time just warns. The header (GLFW/glfw3.h) is on the
    # include path unconditionally.
    # Same atomics-feature requirement as VocalTractLabApiObj — see the
    # note on AUDIO_WORKLET there.
    target_compile_options(imgui_static PUBLIC -pthread)

    add_executable(vtl_live ${LIVE_SOURCES} $<TARGET_OBJECTS:VocalTractLabApiObj>)
    target_include_directories(vtl_live PRIVATE
      sources
      sources/vtl
      sources/live
    )
    target_link_libraries(vtl_live PRIVATE
      VocalTractLabApiObj
      imgui_static
    )
    set(VTL_LIVE_SHELL "${CMAKE_CURRENT_SOURCE_DIR}/sources/live/web/shell.html")
    # AudioWorklet runs the synthesizer on its own browser-managed audio
    # thread so a slow UI frame can't starve the speakers. -sAUDIO_WORKLET
    # implies -sWASM_WORKERS, which uses SharedArrayBuffer + Wasm atomics:
    # the page must be served with cross-origin isolation
    # (Cross-Origin-Opener-Policy: same-origin and
    # Cross-Origin-Embedder-Policy: require-corp). emrun adds these
    # automatically; static hosting needs to set them in the server config.
    #
    # -pthread is needed at compile time so each TU is built with the
    # 'atomics' + 'bulk-memory' Wasm features that --shared-memory
    # requires at link time, and so the thread-aware libc++ variant
    # (which WASM_WORKERS pulls in transitively) finds its pthread
    # synchronisation symbols. We do not actually spawn any pthreads —
    # std::thread is #if-guarded out on the WASM build — so no thread
    # pool is reserved at startup.
    # -pthread enables 'atomics' + 'bulk-memory' Wasm features at compile
    # time (each TU must opt in for --shared-memory at link to be allowed).
    # AUDIO_WORKLET / WASM_WORKERS themselves are link-only.
    target_compile_options(vtl_live PRIVATE -pthread)
    # Browser stack traces from a release build show only raw wasm offsets
    # (e.g. "vtl_live.wasm:0x111a9"), which is useless for diagnosing the
    # AudioWorklet path. -g2 keeps function names + file/line info in the
    # wasm without pulling in full DWARF, so worklet stack traces become
    # readable without measurably touching runtime cost. The flag adds
    # ~150 KB to vtl_live.wasm — keep it on by default until we ship.
    option(VTL_LIVE_WASM_DEBUG
           "Build the wasm live target with function names in stack traces" ON)
    if(VTL_LIVE_WASM_DEBUG)
      target_compile_options(vtl_live PRIVATE -g2)
      target_link_options(vtl_live PRIVATE -g2)
      # Names from VocalTractLabApiObj/imgui need the same compile-side
      # debug flag to survive into the link, otherwise frames in the synth
      # / ImGui code show up as raw wasm offsets again.
      target_compile_options(VocalTractLabApiObj PUBLIC -g2)
      target_compile_options(imgui_static PUBLIC -g2)
    endif()
    # ASSERTIONS + STACK_OVERFLOW_CHECK add per-call runtime checks that
    # turn into measurable overhead on the AudioWorklet thread, especially
    # under DevTools instrumentation — Chrome will silently drop the
    # processor after a few missed-deadline quanta. Off by default; flip
    # them on when chasing crashes (and close DevTools while listening).
    option(VTL_LIVE_WASM_ASSERTIONS
           "Add ASSERTIONS + STACK_OVERFLOW_CHECK runtime checks" OFF)
    if(VTL_LIVE_WASM_ASSERTIONS)
      target_link_options(vtl_live PRIVATE
        "SHELL:-sASSERTIONS=1"
        "SHELL:-sSTACK_OVERFLOW_CHECK=2"
      )
    endif()
    # SAFE_HEAP traps every out-of-bounds heap access at the actual access
    # site (slow — easily 3-4× CPU). Off by default; flip it on when
    # chasing memory corruption that ASSERTIONS doesn't pinpoint.
    option(VTL_LIVE_WASM_SAFE_HEAP
           "Add SAFE_HEAP runtime bounds checks to the wasm live target" OFF)
    if(VTL_LIVE_WASM_SAFE_HEAP)
      target_link_options(vtl_live PRIVATE "SHELL:-sSAFE_HEAP=1")
    endif()
    target_link_options(vtl_live PRIVATE
      -pthread
      "SHELL:-sUSE_GLFW=3"
      "SHELL:-sUSE_WEBGL2=1"
      "SHELL:-sFULL_ES3=1"
      "SHELL:-sMIN_WEBGL_VERSION=2"
      "SHELL:-sMAX_WEBGL_VERSION=2"
      "SHELL:-sALLOW_MEMORY_GROWTH=1"
      "SHELL:-sINITIAL_MEMORY=128MB"
      "SHELL:-sSTACK_SIZE=2MB"
      "SHELL:-sEXIT_RUNTIME=0"
      "SHELL:-sAUDIO_WORKLET=1"
      "SHELL:-sWASM_WORKERS=1"
      "SHELL:-sPTHREAD_POOL_SIZE=0"
      "SHELL:-lidbfs.js"
      "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/data/speakers/JD2.speaker@/JD2.speaker"
      "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/data/speakers/M01.speaker@/M01.speaker"
      "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/data/speakers/W02.speaker@/W02.speaker"
      "SHELL:--preload-file ${VTL_LIVE_FONT_FILE}@/NotoSans-Regular.ttf"
      "SHELL:--shell-file ${VTL_LIVE_SHELL}"
    )
    # Trigger relink when the shell template changes.
    set_property(TARGET vtl_live APPEND PROPERTY LINK_DEPENDS "${VTL_LIVE_SHELL}")
    # Emit an .html shell so the build artifact can be opened directly.
    set_target_properties(vtl_live PROPERTIES SUFFIX ".html")
  else()
    # ---- Native build (macOS/Linux) ---------------------------------------
    find_package(glfw3 CONFIG REQUIRED)
    find_package(imgui CONFIG REQUIRED)
    find_package(OpenGL REQUIRED)

    add_executable(vtl_live MACOSX_BUNDLE
      ${LIVE_SOURCES}
      $<TARGET_OBJECTS:VocalTractLabApiObj>
    )
    set_target_properties(vtl_live PROPERTIES
      INTERPROCEDURAL_OPTIMIZATION ${VTL_ENABLE_IPO}
    )
    target_include_directories(vtl_live PRIVATE
      sources
      sources/vtl
      sources/live
    )
    target_link_libraries(vtl_live PRIVATE
      VocalTractLabApiObj
      glfw
      imgui::imgui
      OpenGL::GL
    )
    if(APPLE)
      set_target_properties(vtl_live PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME          "VocalTractLab2 Live"
        MACOSX_BUNDLE_GUI_IDENTIFIER       "de.vocaltractlab.VocalTractLab2.Live"
        MACOSX_BUNDLE_BUNDLE_VERSION       "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
      )
      # main.cpp talks to OpenGL (deprecated since 10.14) and AudioEngine.cpp
      # talks to OpenAL (deprecated since 10.15). Apple ships an opt-out for
      # OpenGL via GL_SILENCE_DEPRECATION; OpenAL has no such macro, so the
      # warning has to be silenced at the compiler-flag level too.
      target_compile_definitions(vtl_live PRIVATE GL_SILENCE_DEPRECATION=1)
      target_compile_options(vtl_live PRIVATE -Wno-deprecated-declarations)
      # vcpkg's imgui::imgui pulls glfw3 transitively (its impl_glfw backend
      # depends on it); we also link `glfw` explicitly because main.cpp uses
      # GLFW directly. ld64 then sees libglfw3.a twice on the command line
      # and warns. The dup is harmless — symbols deduplicate — so silence the
      # warning rather than restructuring the link graph.
      target_link_options(vtl_live PRIVATE LINKER:-no_warn_duplicate_libraries)
      set(LIVE_BUNDLE_RESOURCE_FILES "")
      foreach(REL ${RUNTIME_DATA_FILES})
        list(APPEND LIVE_BUNDLE_RESOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/data/${REL}")
      endforeach()
      list(APPEND LIVE_BUNDLE_RESOURCE_FILES "${VTL_LIVE_FONT_FILE}")
      target_sources(vtl_live PRIVATE ${LIVE_BUNDLE_RESOURCE_FILES})
      set_source_files_properties(${LIVE_BUNDLE_RESOURCE_FILES} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
      )

      # Re-sign the bundle ad-hoc once Resources/ is fully staged. See
      # the matching note on VocalTractLab2 for why the linker's
      # auto-emitted signature alone isn't enough.
      add_custom_command(TARGET vtl_live POST_BUILD
        COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:vtl_live>"
        VERBATIM
      )
    else()
      foreach(REL ${RUNTIME_DATA_FILES})
        cmake_path(GET REL FILENAME BASENAME)
        add_custom_command(TARGET vtl_live POST_BUILD
          COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/data/${REL}"
            "$<TARGET_FILE_DIR:vtl_live>/${BASENAME}"
          VERBATIM
        )
      endforeach()
      add_custom_command(TARGET vtl_live POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${VTL_LIVE_FONT_FILE}"
          "$<TARGET_FILE_DIR:vtl_live>/NotoSans-Regular.ttf"
        VERBATIM
      )
    endif()
  endif()
endif()

# --- Benchmarks -------------------------------------------------------------
# Microbenchmarks for the vtl synthesis pipeline. Use them to gauge how close
# the backend is to real-time on a given platform / build configuration:
#
#   cmake --build --preset mac-debug --target synthesis_bench
#   ./build/mac-debug/synthesis_bench --benchmark_min_time=2s

option(VTL_BUILD_BENCHMARKS "Build the synthesis benchmarks" ON)
if(EMSCRIPTEN)
  set(VTL_BUILD_BENCHMARKS OFF)
endif()

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

# --- Tests ------------------------------------------------------------------
# Hash-based regression tests for the synthesis pipeline. Each subtest
# exercises one of the heavily-optimized code paths (geometry,
# transfer function, end-to-end audio) and compares an FNV-1a hash of the
# raw output bytes against a baked-in baseline. The synthesizer is
# deterministic, so any drift is a real behavior change.
#
#   cmake --build --preset mac-release --target audio_regression_test
#   ctest --test-dir build/mac-release --output-on-failure
#
# To re-baseline after an intentional change, edit
# tests/audio_regression_test.cpp and zero out the expected hashes; the
# test will print actual values in capture mode.

option(VTL_BUILD_TESTS "Build the synthesis regression tests" ON)
if(EMSCRIPTEN)
  set(VTL_BUILD_TESTS OFF)
endif()

if(VTL_BUILD_TESTS)
  enable_testing()

  configure_file(
    tests/test_data.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/tests/test_data.h"
    @ONLY
  )

  add_executable(audio_regression_test
    tests/audio_regression_test.cpp
    $<TARGET_OBJECTS:VocalTractLabApiObj>
  )
  set_target_properties(audio_regression_test PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION ${VTL_ENABLE_IPO}
  )
  target_link_libraries(audio_regression_test PRIVATE VocalTractLabApiObj)
  target_include_directories(audio_regression_test PRIVATE
    sources/vtl/api
    "${CMAKE_CURRENT_BINARY_DIR}/tests"
  )

  add_test(NAME audio_regression COMMAND audio_regression_test)
endif()
