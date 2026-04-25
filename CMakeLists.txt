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

set(BACKEND_SOURCES
  ${GUI_DIR}/Sources/Backend/AnatomyParams.cpp
  ${GUI_DIR}/Sources/Backend/Constants.cpp
  ${GUI_DIR}/Sources/Backend/Dsp.cpp
  ${GUI_DIR}/Sources/Backend/F0EstimatorYin.cpp
  ${GUI_DIR}/Sources/Backend/GeometricGlottis2019.cpp
  ${GUI_DIR}/Sources/Backend/GeometricGlottis2025.cpp
  ${GUI_DIR}/Sources/Backend/Geometry.cpp
  ${GUI_DIR}/Sources/Backend/GesturalScore.cpp
  ${GUI_DIR}/Sources/Backend/Glottis.cpp
  ${GUI_DIR}/Sources/Backend/IirFilter.cpp
  ${GUI_DIR}/Sources/Backend/LfPulse.cpp
  ${GUI_DIR}/Sources/Backend/Matrix2x2.cpp
  ${GUI_DIR}/Sources/Backend/PoleZeroPlan.cpp
  ${GUI_DIR}/Sources/Backend/Sampa.cpp
  ${GUI_DIR}/Sources/Backend/SegmentSequence.cpp
  ${GUI_DIR}/Sources/Backend/Signal.cpp
  ${GUI_DIR}/Sources/Backend/SoundLib.cpp
  ${GUI_DIR}/Sources/Backend/Splines.cpp
  ${GUI_DIR}/Sources/Backend/StaticPhone.cpp
  ${GUI_DIR}/Sources/Backend/Surface.cpp
  ${GUI_DIR}/Sources/Backend/Synthesizer.cpp
  ${GUI_DIR}/Sources/Backend/TdsModel.cpp
  ${GUI_DIR}/Sources/Backend/TimeFunction.cpp
  ${GUI_DIR}/Sources/Backend/TlModel.cpp
  ${GUI_DIR}/Sources/Backend/TriangularGlottis.cpp
  ${GUI_DIR}/Sources/Backend/Tube.cpp
  ${GUI_DIR}/Sources/Backend/VocalTract.cpp
  ${GUI_DIR}/Sources/Backend/VocalTractLabApi.cpp
  ${GUI_DIR}/Sources/Backend/VoiceQualityEstimator.cpp
  ${GUI_DIR}/Sources/Backend/XmlHelper.cpp
  ${GUI_DIR}/Sources/Backend/XmlNode.cpp
)

set(FRONTEND_SOURCES
  ${GUI_DIR}/Sources/Frontend/AnalysisResultsDialog.cpp
  ${GUI_DIR}/Sources/Frontend/AnalysisSettingsDialog.cpp
  ${GUI_DIR}/Sources/Frontend/AnatomyParamsDialog.cpp
  ${GUI_DIR}/Sources/Frontend/AnnotationDialog.cpp
  ${GUI_DIR}/Sources/Frontend/Application.cpp
  ${GUI_DIR}/Sources/Frontend/AreaFunctionPicture.cpp
  ${GUI_DIR}/Sources/Frontend/BasicPicture.cpp
  ${GUI_DIR}/Sources/Frontend/ColorScale.cpp
  ${GUI_DIR}/Sources/Frontend/CrossSectionPicture.cpp
  ${GUI_DIR}/Sources/Frontend/Data.cpp
  ${GUI_DIR}/Sources/Frontend/EmaConfigDialog.cpp
  ${GUI_DIR}/Sources/Frontend/FdsOptionsDialog.cpp
  ${GUI_DIR}/Sources/Frontend/FormantOptimizationDialog.cpp
  ${GUI_DIR}/Sources/Frontend/GesturalScorePage.cpp
  ${GUI_DIR}/Sources/Frontend/GesturalScorePicture.cpp
  ${GUI_DIR}/Sources/Frontend/GlottisDialog.cpp
  ${GUI_DIR}/Sources/Frontend/GlottisPanel.cpp
  ${GUI_DIR}/Sources/Frontend/GlottisPicture.cpp
  ${GUI_DIR}/Sources/Frontend/Graph.cpp
  ${GUI_DIR}/Sources/Frontend/LfPulseDialog.cpp
  ${GUI_DIR}/Sources/Frontend/LfPulsePicture.cpp
  ${GUI_DIR}/Sources/Frontend/MainWindow.cpp
  ${GUI_DIR}/Sources/Frontend/PhoneticParamsDialog.cpp
  ${GUI_DIR}/Sources/Frontend/PoleZeroDialog.cpp
  ${GUI_DIR}/Sources/Frontend/PoleZeroPlot.cpp
  ${GUI_DIR}/Sources/Frontend/SignalComparisonPicture.cpp
  ${GUI_DIR}/Sources/Frontend/SignalPage.cpp
  ${GUI_DIR}/Sources/Frontend/SignalPicture.cpp
  ${GUI_DIR}/Sources/Frontend/SilentMessageBox.cpp
  ${GUI_DIR}/Sources/Frontend/SimpleSpectrumPicture.cpp
  ${GUI_DIR}/Sources/Frontend/SpectrogramPicture.cpp
  ${GUI_DIR}/Sources/Frontend/SpectrogramPlot.cpp
  ${GUI_DIR}/Sources/Frontend/SpectrumOptionsDialog.cpp
  ${GUI_DIR}/Sources/Frontend/SpectrumPicture.cpp
  ${GUI_DIR}/Sources/Frontend/SynthesisThread.cpp
  ${GUI_DIR}/Sources/Frontend/TdsOptionsDialog.cpp
  ${GUI_DIR}/Sources/Frontend/TdsPage.cpp
  ${GUI_DIR}/Sources/Frontend/TdsSpatialSignalPicture.cpp
  ${GUI_DIR}/Sources/Frontend/TdsTimeSignalPicture.cpp
  ${GUI_DIR}/Sources/Frontend/TdsTubePicture.cpp
  ${GUI_DIR}/Sources/Frontend/TimeAxisPicture.cpp
  ${GUI_DIR}/Sources/Frontend/TransitionDialog.cpp
  ${GUI_DIR}/Sources/Frontend/VocalTractDialog.cpp
  ${GUI_DIR}/Sources/Frontend/VocalTractPage.cpp
  ${GUI_DIR}/Sources/Frontend/VocalTractPicture.cpp
  ${GUI_DIR}/Sources/Frontend/VocalTractShapesDialog.cpp
)

set(ALL_SOURCES ${BACKEND_SOURCES} ${FRONTEND_SOURCES})

if(WIN32)
  list(APPEND ALL_SOURCES ${GUI_DIR}/VocalTractLab2.rc)
endif()

# --- Target -----------------------------------------------------------------

add_executable(VocalTractLab2 WIN32 MACOSX_BUNDLE ${ALL_SOURCES})

target_include_directories(VocalTractLab2 PRIVATE
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
