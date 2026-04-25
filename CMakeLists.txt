cmake_minimum_required(VERSION 3.25)

project(VocalTractLab2
  VERSION 2.4
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
  list(APPEND ALL_SOURCES official/GUI/Developer/VocalTractLab2.rc)
endif()

# --- Target -----------------------------------------------------------------

add_executable(VocalTractLab2 WIN32 MACOSX_BUNDLE ${ALL_SOURCES})

target_include_directories(VocalTractLab2 PRIVATE
  sources
  sources/vtl
  sources/gui
  official/GUI/Developer
)

target_link_libraries(VocalTractLab2 PRIVATE ${wxWidgets_LIBRARIES})

if(NOT WIN32)
  target_compile_definitions(VocalTractLab2 PRIVATE HAVE_OPENAL)
  target_link_libraries(VocalTractLab2 PRIVATE OpenAL::OpenAL)
endif()

if(APPLE)
  set_target_properties(VocalTractLab2 PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME          "VocalTractLab2"
    MACOSX_BUNDLE_GUI_IDENTIFIER       "de.vocaltractlab.VocalTractLab2"
    MACOSX_BUNDLE_BUNDLE_VERSION       "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_ICON_FILE            "VocalTractLab2.icns"
  )

  # Bundle speaker/config/example files into Contents/Resources/.
  # Data::init uses wxStandardPaths::GetResourcesDir() on Apple, so the
  # app resolves these at the same relative location the Windows build
  # expects them next to the .exe.
  set(GUI_RESOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/official/GUI")
  set(BUNDLE_RESOURCE_FILES
    "${GUI_RESOURCE_DIR}/JD2.speaker"
    "${GUI_RESOURCE_DIR}/M01.speaker"
    "${GUI_RESOURCE_DIR}/W02.speaker"
    "${GUI_RESOURCE_DIR}/config.ini"
    "${GUI_RESOURCE_DIR}/example01-orig.wav"
    "${GUI_RESOURCE_DIR}/example01.ges"
    "${GUI_RESOURCE_DIR}/example01.seg"
    "${GUI_RESOURCE_DIR}/example01.tube"
    "${GUI_RESOURCE_DIR}/Vowel-a-outline.gif"
    "${GUI_RESOURCE_DIR}/Vowel-i-outline.gif"
    "${GUI_RESOURCE_DIR}/Vowel-u-outline.gif"
  )
  target_sources(VocalTractLab2 PRIVATE ${BUNDLE_RESOURCE_FILES})
  set_source_files_properties(${BUNDLE_RESOURCE_FILES} PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources"
  )

  # Mirror the GUI/Examples directory into Contents/Resources/Examples.
  file(GLOB EXAMPLE_FILES "${GUI_RESOURCE_DIR}/Examples/*")
  target_sources(VocalTractLab2 PRIVATE ${EXAMPLE_FILES})
  set_source_files_properties(${EXAMPLE_FILES} PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources/Examples"
  )
endif()

# --- VocalTractLabApi shared library ----------------------------------------

set(API_SOURCES ${VTL_SOURCES})
if(WIN32)
  list(APPEND API_SOURCES official/API/Developer/VocalTractLabApi.def)
endif()

add_library(VocalTractLabApi SHARED ${API_SOURCES})

target_include_directories(VocalTractLabApi PRIVATE
  sources
  sources/vtl
)

if(WIN32)
  target_compile_definitions(VocalTractLabApi PRIVATE
    _USRDLL
    _USE_MATH_DEFINES
    _CRT_SECURE_NO_WARNINGS
  )
  target_link_libraries(VocalTractLabApi PRIVATE winmm)
else()
  target_compile_definitions(VocalTractLabApi PRIVATE HAVE_OPENAL)
  target_link_libraries(VocalTractLabApi PRIVATE OpenAL::OpenAL)
endif()
