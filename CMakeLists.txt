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

set(GUI_DIR official/GUI/Developer)
set(API_DIR official/API/Developer)

set(BACKEND_SOURCES
  ${GUI_DIR}/Sources/Backend/acoustics/TdsModel.cpp
  ${GUI_DIR}/Sources/Backend/acoustics/TlModel.cpp
  ${GUI_DIR}/Sources/Backend/acoustics/Tube.cpp
  ${GUI_DIR}/Sources/Backend/analysis/F0EstimatorYin.cpp
  ${GUI_DIR}/Sources/Backend/analysis/PoleZeroPlan.cpp
  ${GUI_DIR}/Sources/Backend/analysis/VoiceQualityEstimator.cpp
  ${GUI_DIR}/Sources/Backend/anatomy/AnatomyParams.cpp
  ${GUI_DIR}/Sources/Backend/anatomy/Splines.cpp
  ${GUI_DIR}/Sources/Backend/anatomy/Surface.cpp
  ${GUI_DIR}/Sources/Backend/anatomy/VocalTract.cpp
  ${GUI_DIR}/Sources/Backend/api/VocalTractLabApi.cpp
  ${GUI_DIR}/Sources/Backend/core/Constants.cpp
  ${GUI_DIR}/Sources/Backend/core/Geometry.cpp
  ${GUI_DIR}/Sources/Backend/core/Matrix2x2.cpp
  ${GUI_DIR}/Sources/Backend/core/TimeFunction.cpp
  ${GUI_DIR}/Sources/Backend/dsp/Dsp.cpp
  ${GUI_DIR}/Sources/Backend/dsp/IirFilter.cpp
  ${GUI_DIR}/Sources/Backend/dsp/Signal.cpp
  ${GUI_DIR}/Sources/Backend/glottis/GeometricGlottis2019.cpp
  ${GUI_DIR}/Sources/Backend/glottis/GeometricGlottis2025.cpp
  ${GUI_DIR}/Sources/Backend/glottis/Glottis.cpp
  ${GUI_DIR}/Sources/Backend/glottis/LfPulse.cpp
  ${GUI_DIR}/Sources/Backend/glottis/TriangularGlottis.cpp
  ${GUI_DIR}/Sources/Backend/io/SoundLib.cpp
  ${GUI_DIR}/Sources/Backend/io/XmlHelper.cpp
  ${GUI_DIR}/Sources/Backend/io/XmlNode.cpp
  ${GUI_DIR}/Sources/Backend/phonetics/Sampa.cpp
  ${GUI_DIR}/Sources/Backend/phonetics/SegmentSequence.cpp
  ${GUI_DIR}/Sources/Backend/synthesis/GesturalScore.cpp
  ${GUI_DIR}/Sources/Backend/synthesis/StaticPhone.cpp
  ${GUI_DIR}/Sources/Backend/synthesis/Synthesizer.cpp
)

set(FRONTEND_SOURCES
  ${GUI_DIR}/Sources/Frontend/app/Application.cpp
  ${GUI_DIR}/Sources/Frontend/app/Data.cpp
  ${GUI_DIR}/Sources/Frontend/app/MainWindow.cpp
  ${GUI_DIR}/Sources/Frontend/app/SynthesisThread.cpp
  ${GUI_DIR}/Sources/Frontend/dialogs/AnalysisResultsDialog.cpp
  ${GUI_DIR}/Sources/Frontend/dialogs/AnalysisSettingsDialog.cpp
  ${GUI_DIR}/Sources/Frontend/dialogs/AnatomyParamsDialog.cpp
  ${GUI_DIR}/Sources/Frontend/dialogs/AnnotationDialog.cpp
  ${GUI_DIR}/Sources/Frontend/dialogs/EmaConfigDialog.cpp
  ${GUI_DIR}/Sources/Frontend/dialogs/FdsOptionsDialog.cpp
  ${GUI_DIR}/Sources/Frontend/dialogs/FormantOptimizationDialog.cpp
  ${GUI_DIR}/Sources/Frontend/dialogs/GlottisDialog.cpp
  ${GUI_DIR}/Sources/Frontend/dialogs/GlottisPanel.cpp
  ${GUI_DIR}/Sources/Frontend/dialogs/LfPulseDialog.cpp
  ${GUI_DIR}/Sources/Frontend/dialogs/PhoneticParamsDialog.cpp
  ${GUI_DIR}/Sources/Frontend/dialogs/PoleZeroDialog.cpp
  ${GUI_DIR}/Sources/Frontend/dialogs/SpectrumOptionsDialog.cpp
  ${GUI_DIR}/Sources/Frontend/dialogs/TdsOptionsDialog.cpp
  ${GUI_DIR}/Sources/Frontend/dialogs/TransitionDialog.cpp
  ${GUI_DIR}/Sources/Frontend/dialogs/VocalTractDialog.cpp
  ${GUI_DIR}/Sources/Frontend/dialogs/VocalTractShapesDialog.cpp
  ${GUI_DIR}/Sources/Frontend/graphing/ColorScale.cpp
  ${GUI_DIR}/Sources/Frontend/graphing/Graph.cpp
  ${GUI_DIR}/Sources/Frontend/graphing/SpectrogramPlot.cpp
  ${GUI_DIR}/Sources/Frontend/pages/GesturalScorePage.cpp
  ${GUI_DIR}/Sources/Frontend/pages/SignalPage.cpp
  ${GUI_DIR}/Sources/Frontend/pages/TdsPage.cpp
  ${GUI_DIR}/Sources/Frontend/pages/VocalTractPage.cpp
  ${GUI_DIR}/Sources/Frontend/pictures/AreaFunctionPicture.cpp
  ${GUI_DIR}/Sources/Frontend/pictures/BasicPicture.cpp
  ${GUI_DIR}/Sources/Frontend/pictures/CrossSectionPicture.cpp
  ${GUI_DIR}/Sources/Frontend/pictures/GesturalScorePicture.cpp
  ${GUI_DIR}/Sources/Frontend/pictures/GlottisPicture.cpp
  ${GUI_DIR}/Sources/Frontend/pictures/LfPulsePicture.cpp
  ${GUI_DIR}/Sources/Frontend/pictures/PoleZeroPlot.cpp
  ${GUI_DIR}/Sources/Frontend/pictures/SignalComparisonPicture.cpp
  ${GUI_DIR}/Sources/Frontend/pictures/SignalPicture.cpp
  ${GUI_DIR}/Sources/Frontend/pictures/SimpleSpectrumPicture.cpp
  ${GUI_DIR}/Sources/Frontend/pictures/SpectrogramPicture.cpp
  ${GUI_DIR}/Sources/Frontend/pictures/SpectrumPicture.cpp
  ${GUI_DIR}/Sources/Frontend/pictures/TdsSpatialSignalPicture.cpp
  ${GUI_DIR}/Sources/Frontend/pictures/TdsTimeSignalPicture.cpp
  ${GUI_DIR}/Sources/Frontend/pictures/TdsTubePicture.cpp
  ${GUI_DIR}/Sources/Frontend/pictures/TimeAxisPicture.cpp
  ${GUI_DIR}/Sources/Frontend/pictures/VocalTractPicture.cpp
  ${GUI_DIR}/Sources/Frontend/util/SilentMessageBox.cpp
)

set(ALL_SOURCES ${BACKEND_SOURCES} ${FRONTEND_SOURCES})

if(WIN32)
  list(APPEND ALL_SOURCES ${GUI_DIR}/VocalTractLab2.rc)
endif()

# --- Target -----------------------------------------------------------------

add_executable(VocalTractLab2 WIN32 MACOSX_BUNDLE ${ALL_SOURCES})

target_include_directories(VocalTractLab2 PRIVATE
  ${GUI_DIR}/Sources
  ${GUI_DIR}/Sources/Backend
  ${GUI_DIR}/Sources/Frontend
  ${GUI_DIR}
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

set(API_SOURCES ${BACKEND_SOURCES})
if(WIN32)
  list(APPEND API_SOURCES ${API_DIR}/VocalTractLabApi.def)
endif()

add_library(VocalTractLabApi SHARED ${API_SOURCES})

target_include_directories(VocalTractLabApi PRIVATE
  ${GUI_DIR}/Sources
  ${GUI_DIR}/Sources/Backend
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
