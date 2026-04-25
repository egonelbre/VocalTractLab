// ****************************************************************************
// This file is part of VocalTractLab.
// Copyright (C) 2025, Peter Birkholz, Dresden, Germany
// www.vocaltractlab.de
// author: Peter Birkholz
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.
//
// ****************************************************************************

#ifndef ___DATA__
#define ___DATA__

#include <wx/wx.h>
#include <string>
#include <wx/fileconf.h>
#include <wx/tokenzr.h>

#include "Backend/dsp/IirFilter.h"
#include "Backend/glottis/LfPulse.h"
#include "Backend/analysis/PoleZeroPlan.h"
#include "Backend/dsp/Signal.h"
#include "Backend/acoustics/TdsModel.h"
#include "Backend/acoustics/TlModel.h"
#include "Backend/anatomy/VocalTract.h"
#include "Backend/acoustics/Tube.h"
#include "Backend/core/TimeFunction.h"
#include "Backend/glottis/Glottis.h"
#include "Backend/glottis/GeometricGlottis2019.h"
#include "Backend/glottis/GeometricGlottis2025.h"
#include "Backend/glottis/TriangularGlottis.h"
#include "Backend/synthesis/Synthesizer.h"

#include "Backend/analysis/F0EstimatorYin.h"
#include "Backend/analysis/VoiceQualityEstimator.h"

#include "Backend/acoustics/TubeSequence.h"
#include "Backend/synthesis/StaticPhone.h"
#include "Backend/synthesis/GesturalScore.h"
#include "Backend/phonetics/SegmentSequence.h"
#include "Backend/anatomy/AnatomyParams.h"

#include "Graph.h"
#include "ColorScale.h"
#include "FormantOptimizationDialog.h"


// ****************************************************************************
// Declare some custom event types.
// The updateRequestEvent is meant to be used by child widgets to request their
// parent to update some region or other child widgets.
// A command event of this type is posted as follows:
//
//   wxCommandEvent event(updateRequestEvent);
//   event.SetInt(REFRESH_PICTURES | REFRESH_PICTURES_AND_CONTROLS);
//   wxPostEvent(receiverWindow, event);
//
// The receiver window must have the following in his event table:
//
//  EVT_COMMAND(wxID_ANY, updateRequestEvent, OnUpdateRequest)
// 
// The function OnUpdateRequest(...) takes one wxCommandEvent parameter.
// ****************************************************************************

extern const wxEventType updateRequestEvent;
extern const int REFRESH_PICTURES;
extern const int UPDATE_PICTURES;
extern const int REFRESH_PICTURES_AND_CONTROLS;
extern const int UPDATE_PICTURES_AND_CONTROLS;
extern const int UPDATE_VOCAL_TRACT;


// ****************************************************************************
/// Singleton class containing the data and common methods for the frontend 
/// classes.
// ****************************************************************************

class Data
{
  // **************************************************************************
  // Public data.
  // **************************************************************************

public:
  static const int TRACK_DURATION_S = 60;                    // Length of the tracks in s
  static const int MAX_F0_FRAMES = TRACK_DURATION_S*1000;    // max. one frame for each ms
  // Left margin of the gestural score picture
  static const int LEFT_SCORE_MARGIN = 120;
  static const double MIN_ADVISED_VOWEL_AREA_CM2;

  enum ProgramPage
  {
    SIGNAL_PAGE,
    VOCAL_TRACT_PAGE,
    TDS_PAGE,
    GESTURAL_SCORE_PAGE,
    NUM_PAGES    
  };

  enum Quantity
  {
    QUANTITY_PRESSURE,
    QUANTITY_FLOW,
    QUANTITY_AREA,
    QUANTITY_VELOCITY,
    NUM_QUANTITIES
  };

  enum SynthesisType
  {
    SYNTHESIS_PHONE,
    SYNTHESIS_GESMOD,
    NUM_SYNTHESIS_TYPES
  };

  enum AmplitudeType 
  { 
    NORMAL_AMP, 
    LOG_AMP, 
    QUAD_AMP 
  };

  enum TrackType
  {
    MAIN_TRACK,
    EGG_TRACK,
    EXTRA_TRACK,
    NUM_TRACKS
  };

  enum GlottisModel
  {
    GEOMETRIC_GLOTTIS_2025,
    GEOMETRIC_GLOTTIS_2019,
    TRIANGULAR_GLOTTIS,
    NUM_GLOTTIS_MODELS
  };

  enum PhoneticParams
  {
    TONGUE_HEIGHT,
    TONGUE_FRONTNESS,
    LIP_ROUNDING,
    VELUM_POSITION,
    LABIAL_CONSTRICTION_DEGREE,
    ALVEOLAR_CONSTRICTION_DEGREE,
    VELAR_CONSTRICTION_DEGREE,
    NUM_PHONETIC_PARAMS
  };

  // Types for the simple spectrum picture

  enum UserSpectrumType 
  {
    NORMAL_SPECTRUM = 0,
    CEPSTRUM,
    AVERAGE_SPECTRUM,   // Long-term average spectrum
    TEST_SPECTRUM,
    NUM_SPECTRUM_TYPES
  };

  int currentPage;    ///< The current program page

  wxFileConfig *config;

  // ****************************************************************
  // Vocal tract data.
  // ****************************************************************

  VocalTract *vocalTract;
  TlModel *tlModel;
  PoleZeroPlan *poleZeroPlan;
  AnatomyParams *anatomyParams;

  double transitionPos;    // 0 <= x <= 1

  // Spectra in the spectrum picture
  ComplexSignal *primarySpectrum;
  ComplexSignal *poleZeroSpectrum;

  double noiseFilterCutoffFreq;

  wxString speakerFileName;
  wxString svgFileName;
  wxString spectrumFileName;
  wxString emaFileName;
  wxString tractGlottisParamsFileName;
  wxString backgroundImageFileName;

  // All phonetic parameter values are between 0.0 and 1.0.
  double phoneticParamValue[NUM_PHONETIC_PARAMS];
  static const wxString phoneticParamName[NUM_PHONETIC_PARAMS];

  FormantOptimizationDialog *formantOptimizationDialog;

  // ****************************************************************
  // TDS variables
  // ****************************************************************

  LfPulse lfPulse;
  bool showPulseDerivative;

  TdsModel *tdsModel;

  SynthesisType synthesisType{ SYNTHESIS_PHONE };
  int nextTdsSampleIndex{ 0 };

  Quantity quantity;
  bool showMainPath;
  bool showSidePath;
  bool showSubglottalSystem;
  bool showAnimation;
  bool normalizeAmplitude;      // After the synthesis
  int synthesisSpeed_percent;
  wxString videoFramesFolder;
  wxString tractContoursFolder;
  wxString equationSetsFolder;
  Graph *tdsPressureTimeGraph;
  Graph *tdsFlowTimeGraph;
  Graph *tdsAreaTimeGraph;
  Graph* tdsVelocityTimeGraph;

  // List with glottis models
  Glottis *glottis[NUM_GLOTTIS_MODELS];
  bool saveGlottisSignals;
  wxString glottisSignalsFileName;

  // User settings before the start of a TDS synthesis
  double userF0_Hz;
  double userPressure_dPa;

  // Specialized tube sequences
  StaticPhone *staticPhone;
  GesturalScore *gesturalScore;
  Synthesizer *synthesizer;

  // The color scale
  static const int NUM_TDS_SCALE_COLORS = 256;
  wxColor tdsScaleColor[NUM_TDS_SCALE_COLORS];

  // ****************************************************************
  // Oscillogram variables
  // ****************************************************************

  Signal16 *track[NUM_TRACKS];
  bool showTrack[NUM_TRACKS];
  int selectionMark_pt[2];
  int mark_pt;

  int centerPos_pt;     ///< Sampling index in the middle of the oscillograms
  int oscillogramVisTimeRange_pt;
  double oscillogramAmpZoom;
  bool setGCI;          ///< Edit glottal closure instants instead of the standard marks

  bool showGci;         ///< Show glottal closure instants ?
  bool showF0;          ///< Show F0 track ?
  bool showFormants;    ///< Show formant tracks ?
  bool showVoiceQuality;  ///< Show voice quality signal ?

  // Analysis objects.

  F0EstimatorYin *f0EstimatorYin;
  VoiceQualityEstimator *voiceQualityEstimator;

  // Samples for the F0 track.

  double f0TimeStep_s;
  vector<double> f0Signal[NUM_TRACKS];

  // Samples for the voice quality track

  double voiceQualityTimeStep_s;
  vector<double> voiceQualitySignal[NUM_TRACKS];

  // Data for the user spectrum calculation

  int spectrumWindowLength_pt;
  int userSpectrumType;                 // one of UserSpectrumType
  double averageSpectrumTimeStep_ms;
  ComplexSignal *userSpectrum;
  ComplexSignal *tdsSpectrum;

  // ****************************************************************
  // Spectrogram variables
  // ****************************************************************

  int spectrogramVisTimeRange_pt;
  bool showSpectrogramText;
  // is either MAIN_TRACK, EGG_TRACK, EXTRA_TRACK, or NUM_TRACKS (=none)
  int selectedSpectrogram;    

  static const int NUM_SCALE_COLORS = 1024;
  wxColor colorScale[NUM_SCALE_COLORS];

  // ****************************************************************
  // The window currently used for short time transforms
  // ****************************************************************

  Signal *currWindow;
  int currWindowLength_pt;
  int currWindowShape;

  /// Path to the executable file
  wxString programPath;

  // ****************************************************************
  // Gestural score variables.
  // ****************************************************************

  SegmentSequence *segmentSequence;
  Graph *gsTimeAxisGraph;
  wxString gesturalScoreFileName;
  wxString segmentSequenceFileName;
  double gesturalScoreMark_s;
  double gesturalScoreRefMark_s;
  int selectedGestureType;
  int selectedGestureIndex;
  int selectedSegmentIndex;
  

  // **************************************************************************
  // Public functions.
  // **************************************************************************

public:
  static Data *getInstance();
  void init(const wxString &arg0);
  void readConfig();
  void writeConfig();

  bool isValidSelection();
  static int selectTrack(wxWindow *parent, const wxString &message, int defaultSelection = MAIN_TRACK);
  int synthesizeVowelFormantLf(LfPulse &lfPulse, int startPos, bool isLongVowel);
  int synthesizeVowelLf(TlModel *tlModel, LfPulse &lfPulse, int startPos, bool isLongVowel);

  void calcUserSpectrum();
  bool calcRadiatedNoiseSpectrum(double noiseSourcePos_cm, double noiseFilterCutoffFreq,
    int spectrumLength, ComplexSignal *spectrum);

  bool exportVocalTractVideoFrames(const wxString &folderName);
  bool exportTractGlottisParamCurves(const wxString& fileName);
  void calcTongueRootData();
  
  void optimizeFormantsVowel(wxWindow *updateParent, VocalTract *tract, 
    double targetF1, double targetF2, double targetF3, 
    double maxParamChange_cm, double minAdvisedArea_cm2, bool paramFixed[]);

  void optimizeFormantsConsonant(wxWindow *updateParent, VocalTract *tract, 
    const wxString &contextVowel, double targetF1, double targetF2, double targetF3, 
    double maxParamChange_cm, double minArea_cm2, double releaseArea_cm2, bool paramFixed[]);

  bool getReleaseShape(VocalTract *vt, double *consonantParams, double *vowelParams,
    double *releaseParams, double &releasePos, double releaseArea_cm2);

  void createMinVocalTractArea(wxWindow *updateParent, VocalTract *tract, double minAdvisedArea_cm2,
    double skipRegionStart_cm = 0.0, double skipRegionEnd_cm = 0.0);
  double getFormantError(double currentF1, double currentF2, double currentF3, 
    double targetF1, double targetF2, double targetF3);
  bool getVowelFormants(VocalTract *tract, double &F1_Hz, double &F2_Hz, double &F3_Hz, double &minArea_cm2);
  bool getConsonantFormants(VocalTract *tract, const wxString &contextVowel, double releaseArea_cm2,
	  double &F1_Hz, double &F2_Hz, double &F3_Hz);
  double getMinArea_cm2(VocalTract *tract, double startPos_cm, double endPos_cm);
  double getMinAreaOutsideConstriction_cm2(VocalTract *tract, double constrictionStartPos_cm, double constrictionEndPos_cm);

  TubeSequence *getSelectedTubeSequence();
  Gesture *getSelectedGesture();
  Glottis *getSelectedGlottis();
  int getSelectedGlottisIndex();
  void selectGlottis(int index);

  void updateTlModelGeometry(VocalTract *tract);
  void updateModelsFromGesturalScore();
  void getTubeSectionQuantity(TdsModel *model, int sectionIndex, double &leftValue, double &rightValue);
  void phoneticParamsToVocalTract();
  void normalizeAudioAmplitude(int trackIndex);
  void normalizeAudioAmplitude(vector<double> &audio);
  bool saveWavFile(const wxString& fileName, vector<double>& audio);

  bool loadSpeaker(const wxString &fileName);
  bool saveSpeaker(const wxString &fileName);

  void estimateF0(wxWindow *parent, int trackIndex = -1);
  void estimateVoiceQuality(wxWindow *parent, int trackIndex = -1);

  void calcTriangularGlottisF0Params();

  // **************************************************************************
  // Functions for experiments in specific studies.
  // **************************************************************************

  void experiment1WallParameters(wxWindow* updateParent);
  void experiment1CreateVCV(wxString folderName, wxString itemName, 
    wxString vowel, wxString consonant, double K_cgs, double B_cgs, 
    double M_cgs, double f0_Hz, double abduction_mm);

  void experiment2NewGlottisModel(wxWindow* updateParent);
  void experiment2CreateSentence(wxString folderName, int sentenceIndex,
      double lowerDisplacement_cm, double upperDisplacement_cm,
      double phaseLag_rad, double shapeParam);

  void test1();
  void test2();

  // **************************************************************************
  // Private data.       
  // **************************************************************************

private:
  static Data* instance;
  // Access the selected glottis only using selectGlottis(...) and 
  // getSelectedGlottis(...)
  int selectedGlottis{ TRIANGULAR_GLOTTIS };

  // **************************************************************************
  // Private functions.
  // **************************************************************************

private:
  Data();
};

#endif

// ****************************************************************************

