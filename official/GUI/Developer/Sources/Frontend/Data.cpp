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

#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/progdlg.h>
#include <wx/choicdlg.h>
#include <wx/clipbrd.h>
#include <wx/busyinfo.h>
#include <iomanip>

#include "Data.h"
#include "GlottisDialog.h"
#include "VocalTractDialog.h"
#include "../Backend/AudioFile.h"
#include "../Backend/Dsp.h"
#include "../Backend/XmlNode.h"
#include "../Backend/SoundLib.h"
#include "../Backend/Synthesizer.h"


// Define a custom event type to be used for command events.

const wxEventType updateRequestEvent = wxNewEventType();
const int REFRESH_PICTURES = 0;
const int UPDATE_PICTURES = 1;
const int REFRESH_PICTURES_AND_CONTROLS = 2;
const int UPDATE_PICTURES_AND_CONTROLS = 3;
// Special message from the vocal tract dialog to the event receiver to signal
// that a vocal tract parameter changed.
const int UPDATE_VOCAL_TRACT = 4;

// For vowels, the minimum area should always be greater than 0.25 cm^2,
// because otherwise the glottis model might fail to oscillate.
const double Data::MIN_ADVISED_VOWEL_AREA_CM2 = 0.25;


// ****************************************************************************
// Static data.
// ****************************************************************************

Data *Data::instance = NULL;

const wxString Data::phoneticParamName[Data::NUM_PHONETIC_PARAMS] =
{
  "Tongue height",
  "Tongue frontness",
  "Lip rounding",
  "Velum position",
  "Bilabial constriction degree",
  "Apico-alveolar const. degree",
  "Dorso-velar const. degree"
};

// ****************************************************************************
/// Returns the one instance of this class.
// ****************************************************************************

Data *Data::getInstance()
{
  if (instance == NULL)
  {
    instance = new Data();
  }
  return instance;
}

// ****************************************************************************
/// Init the data. This function must be called once after the first call of
/// getInstance().
/// \param arg0 The first string parameter passed to this program.
// ****************************************************************************

void Data::init(const wxString &arg0)
{
  int i;

  // ****************************************************************
  // Determine the program path from arg0. The option
  // wxPATH_GET_SEPARATOR makes sure that the path is always
  // terminated with a "\".
  // ****************************************************************

#ifdef __APPLE__
  // Inside an .app bundle, resource files live in Contents/Resources/
  // rather than next to the executable at Contents/MacOS/.
  programPath = wxStandardPaths::Get().GetResourcesDir() + wxFileName::GetPathSeparator();
#else
  wxFileName fileName(arg0);
  programPath = fileName.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
#endif
  wxPrintf("The program path is %s.\n", programPath.c_str());

  currentPage = NUM_PAGES;    // Invalid page

  // ****************************************************************

  vocalTract = new VocalTract();
  vocalTract->calculateAll();

  tlModel = new TlModel();
  poleZeroPlan = new PoleZeroPlan();
  anatomyParams = new AnatomyParams();

  updateTlModelGeometry(vocalTract);

  // Phonetic parameters; The range of all these parameters is
  // between 0 and 1.

  for (i=0; i < NUM_PHONETIC_PARAMS; i++)
  {
    phoneticParamValue[i] = 0.0;
  }
  phoneticParamValue[TONGUE_HEIGHT] = 0.5;
  phoneticParamValue[TONGUE_FRONTNESS] = 0.5;
  phoneticParamValue[LIP_ROUNDING] = 0.3;

  transitionPos = 0.0;    // 0 <= x <= 1

  formantOptimizationDialog = new FormantOptimizationDialog(NULL, vocalTract);

  // ****************************************************************
  // Init the spectra
  // ****************************************************************

  primarySpectrum = new ComplexSignal(0);
  poleZeroSpectrum = new ComplexSignal(0);

  // For the preview of turbulence noise spectra.
  noiseFilterCutoffFreq = 1500;    

  // ****************************************************************
  // Oscillogram variables
  // ****************************************************************

  for (i=0; i < NUM_TRACKS; i++)
  {
    track[i] = new Signal16(TRACK_DURATION_S*AUDIO_SAMPLING_RATE_HZ);
  }

  showTrack[MAIN_TRACK] = true;
  showTrack[EGG_TRACK] = false;
  showTrack[EXTRA_TRACK] = false;

  // No current selection.
  selectionMark_pt[0] = -1;
  selectionMark_pt[1] = -1;

  centerPos_pt = 0;
  oscillogramVisTimeRange_pt = 700;
  oscillogramAmpZoom = 1.0;
  setGCI        = false;

  showGci       = false;
  showF0        = true;
  showFormants  = true;
  showVoiceQuality = true;

  // Spectrogram variables

  spectrogramVisTimeRange_pt = oscillogramVisTimeRange_pt*16;
  mark_pt = 0;
  showSpectrogramText = true;
  selectedSpectrogram = MAIN_TRACK;

  // Init the color scale for the sonagram visualization
  
  int b;
  for (i=0; i < NUM_SCALE_COLORS; i++)
  {
    b = 255 - i*255/NUM_SCALE_COLORS;
    colorScale[i] = wxColor(b, b, b);
  }
    
  // The window currently used for short time transforms

  currWindow = new Signal();
  int currWindowLength_pt = 0;
  int currWindowShape = -1;

  // Some classes for analysis tasks
  
  f0EstimatorYin = new F0EstimatorYin();
  voiceQualityEstimator = new VoiceQualityEstimator();

  f0TimeStep_s = f0EstimatorYin->timeStep_s;
  voiceQualityTimeStep_s = voiceQualityEstimator->timeStep_s;

  // Data for the user spectrum calculation

  spectrumWindowLength_pt = 512;
  userSpectrumType = NORMAL_SPECTRUM;
  averageSpectrumTimeStep_ms = 10.0;
  userSpectrum = new ComplexSignal(512);

  // For the spectra calculated from the impulse responses in the time domain.
  tdsSpectrum = new ComplexSignal(512);

  // ****************************************************************
  // TDS data.
  // ****************************************************************

  showPulseDerivative = false;

  tdsModel = new TdsModel();

  quantity = QUANTITY_PRESSURE;
  showMainPath = true;
  showSidePath = true;
  showSubglottalSystem = true;
  showAnimation = false;
  normalizeAmplitude = true;
  synthesisSpeed_percent = 100;

  // Default folder for saving video frames of the vocal tract, equations sets files, etc.
  videoFramesFolder = wxStandardPaths::Get().GetTempDir();
  tractContoursFolder = wxStandardPaths::Get().GetTempDir();
  equationSetsFolder = wxStandardPaths::Get().GetTempDir();

  tdsPressureTimeGraph = NULL;
  tdsFlowTimeGraph = NULL;
  tdsAreaTimeGraph = NULL;
  tdsVelocityTimeGraph = NULL;

  // Init the list with glottis models

  glottis[GEOMETRIC_GLOTTIS_2025] = new GeometricGlottis2025();
  glottis[GEOMETRIC_GLOTTIS_2019] = new GeometricGlottis2019();
  glottis[TRIANGULAR_GLOTTIS] = new TriangularGlottis();

  saveGlottisSignals = false;
  glottisSignalsFileName = wxStandardPaths::Get().GetTempDir();
  wxChar pathSeparator = wxFileName::GetPathSeparator();
  if (glottisSignalsFileName.EndsWith( &pathSeparator ) == false)
  {
    glottisSignalsFileName+= pathSeparator;
  }
  glottisSignalsFileName+= "glottis-signals.txt";


  userF0_Hz = 120.0;
  userPressure_dPa = 8000.0;

  // Init the different specialized tube sequences.

  staticPhone = new StaticPhone();
  gesturalScore = new GesturalScore(vocalTract, glottis[selectedGlottis]);
  synthesizer = new Synthesizer();

  // ****************************************************************
  // Gestural score variables.
  // ****************************************************************

  segmentSequence = new SegmentSequence();
  gsTimeAxisGraph = new Graph();
  gesturalScoreFileName = "";
  segmentSequenceFileName = "";
  gesturalScoreMark_s = 0.1;
  gesturalScoreRefMark_s = -1.0;
  selectedGestureType = GesturalScore::VOWEL_GESTURE;
  selectedGestureIndex = 0;
  selectedSegmentIndex = 0;

  // ****************************************************************

  ColorScale::getYellowBlueScale(NUM_TDS_SCALE_COLORS, tdsScaleColor);

  // ****************************************************************
  // Load the default speaker file (after everything else was 
  // initialized).
  // ****************************************************************

  speakerFileName = programPath + "JD2.speaker";
  loadSpeaker(speakerFileName);

  // ****************************************************************
  // Create the configuration object and read the configuration.
  // ****************************************************************

  config = new wxFileConfig("VocalTractLab", "Birkholz",  
    programPath + "config.ini", "", wxCONFIG_USE_LOCAL_FILE);
  readConfig();
}


// ****************************************************************************
/// Reads the data from the program configuration into the variables.
// ****************************************************************************

void Data::readConfig()
{
  int i;
  wxString st;
  VocalTract::EmaPoint p;
  
  int numEmaPoints = config->Read("EmaPoints/NumPoints", 0l);

  if (numEmaPoints > 0)
  {
    vocalTract->emaPoints.clear();
    
    for (i=0; i < numEmaPoints; i++)
    {
      st = wxString::Format("EmaPoints/%d/", i);
      p.name = config->Read(st + "Name", "invalid").ToStdString();
      p.emaSurface = (VocalTract::EmaSurface)config->Read(st + "Surface", 0l);
      p.vertexIndex = config->Read( st + "Vertex", 0l);
      
      vocalTract->emaPoints.push_back(p);
    }
  }

}


// ****************************************************************************
/// Writes the program configuration.
// ****************************************************************************

void Data::writeConfig()
{
  int i;
  wxString st;
  int numEmaPoints = (int)vocalTract->emaPoints.size();
  
  config->Write("EmaPoints/NumPoints", numEmaPoints);
  for (i=0; i < numEmaPoints; i++)
  {
    st = wxString::Format("EmaPoints/%d/", i);
    config->Write(st + "Name", wxString(vocalTract->emaPoints[i].name));
    config->Write(st + "Surface", (int)vocalTract->emaPoints[i].emaSurface);
    config->Write(st + "Vertex", vocalTract->emaPoints[i].vertexIndex);
  }  
  
  config->Flush();
}


// ****************************************************************************
// ****************************************************************************

bool Data::isValidSelection()
{
  if ((selectionMark_pt[0] < selectionMark_pt[1]) && 
      (selectionMark_pt[0] != -1) &&
      (selectionMark_pt[1] != -1))
  {
    return true;
  }
  else
  {
    return false;
  }
}


// ****************************************************************************
/// Shows a dialog where the user can select one of the three signal tracks.
/// The selected track is returned, or -1, if the dialog is canceled.
// ****************************************************************************

int Data::selectTrack(wxWindow *parent, const wxString &message, int defaultSelection)
{
  wxArrayString choices;
  choices.Add("Main track");
  choices.Add("EGG track");
  choices.Add("Extra track");

  wxSingleChoiceDialog dialog(parent, message, "Select a track", choices);
  if ((defaultSelection >= 0) && (defaultSelection < NUM_TRACKS))
  {
    dialog.SetSelection(defaultSelection);
  }

  if (dialog.ShowModal() == wxID_OK)
  {
    return dialog.GetSelection();
  }
  else
  {
    return -1;
  }
}

// ****************************************************************************
// Produce a sustained vowel by formant synthesis.
/// Returns the approx. duration of the vowel in milliseconds (plus some extra 
/// time).
// ****************************************************************************

int Data::synthesizeVowelFormantLf(LfPulse &lfPulse, int startPos, bool isLongVowel)
{
  const int NUM_F0_NODES  = 4;
  const int NUM_AMP_NODES = 4;
  const int BUFFER_LENGTH = 2048;
  const int BUFFER_MASK = 2047;
  
  TimeFunction ampTimeFunction;
  TimeFunction f0TimeFunction;
  double duration_ms;
  Signal singlePulse;
  Signal pulseSignal(BUFFER_LENGTH);
  Signal pressureSignal(BUFFER_LENGTH);
  int i, k;
  int nextPulsePos = 10;   // Get the first pulse shape at sample number 10
  int pulseLength;
  double t_s, t_ms;
  double sum;
  double filteredValue;

  // Memorize the pulse params to restore them at the end of the function
  LfPulse origLfPulse = lfPulse;

  // ****************************************************************
  // Init the time functions for F0 and glottal pulse amplitude.
  // ****************************************************************

  if (isLongVowel)
  {
    duration_ms = 600.0;
    double max = lfPulse.F0;

    TimeFunction::Node f0[NUM_F0_NODES] =
    {
      {0.0,   0.9*max},
      {300.0, 1.00*max},
      {450.0, 0.8*max},
      {600.0, 0.7*max}
    };

    TimeFunction::Node amp[NUM_AMP_NODES] =
    {
      {0.0,   0.0},
      {40.0,  500.0},
      {400.0, 450.0},
      {600.0, 0.0}
    };

    ampTimeFunction.setNodes(amp, NUM_AMP_NODES);
    f0TimeFunction.setNodes(f0, NUM_F0_NODES);
  }
  else
  {
    duration_ms = 300.0;
    double max = lfPulse.F0;

    TimeFunction::Node f0[NUM_F0_NODES] =
    {
      {0.0,   0.9*max},
      {125.0, 1.0*max},
      {126.0, 1.0*max},
      {300.0, 0.82*max}
    };
    TimeFunction::Node amp[NUM_AMP_NODES] =
    {
      {0.0,   0.0},
      {20.0,  500.0},
      {200.0, 450.0},
      {300.0, 0.0}
    };

    ampTimeFunction.setNodes(amp, NUM_AMP_NODES);
    f0TimeFunction.setNodes(f0, NUM_F0_NODES);
  }

  
  // The length in samples

  int length = (int)((duration_ms/1000.0)*(double)AUDIO_SAMPLING_RATE_HZ);

  // Init the low-pass filter

  const double CUTOFF_FREQ = 6000.0;
  const double NUM_LOWPASS_POLES = 6;
  IirFilter filter;
  filter.createChebyshev(CUTOFF_FREQ/(double)AUDIO_SAMPLING_RATE_HZ, false, (int)NUM_LOWPASS_POLES);

  // ****************************************************************
  // Calc. the vocal tract impulse response from the pole-zero plan.
  // ****************************************************************

  const int IMPULSE_RESPONSE_EXPONENT = 9;
  const int IMPULSE_RESPONSE_LENGTH = 1 << IMPULSE_RESPONSE_EXPONENT;
  
  Signal impulseResponse(IMPULSE_RESPONSE_LENGTH);
  ComplexSignal poleZeroSpectrum(IMPULSE_RESPONSE_LENGTH);
  ComplexSignal radiationSpectrum(IMPULSE_RESPONSE_LENGTH);
  Signal window(IMPULSE_RESPONSE_LENGTH);

  poleZeroPlan->getPoleZeroSpectrum(&poleZeroSpectrum, IMPULSE_RESPONSE_LENGTH, 8000.0);
  
  if (poleZeroPlan->higherPoleCorrection)
  {
    ComplexSignal hpc(IMPULSE_RESPONSE_LENGTH);
    double effectiveLength_cm = 17.0;
    poleZeroPlan->getHigherPoleCorrection(&hpc, IMPULSE_RESPONSE_LENGTH, effectiveLength_cm);
    poleZeroSpectrum*= hpc;
  }

  tlModel->getSpectrum(TlModel::RADIATION_CHARACTERISTIC, &radiationSpectrum, IMPULSE_RESPONSE_LENGTH, 0);
  poleZeroSpectrum*= radiationSpectrum; 

  // Apply a low-pass filter at 6 kHz.

  int firstHarmonic = 6000.0*IMPULSE_RESPONSE_LENGTH / (double)AUDIO_SAMPLING_RATE_HZ;
  for (i=firstHarmonic; i < IMPULSE_RESPONSE_LENGTH; i++)
  {
    poleZeroSpectrum.re[i] = 0.0;
    poleZeroSpectrum.im[i] = 0.0;
  }

  // Convert the spectrum into the time domain and multiply the
  // impulse response with the right half of a Hamming window.

  complexIFFT(poleZeroSpectrum, IMPULSE_RESPONSE_EXPONENT, true);
  getWindow(window, IMPULSE_RESPONSE_LENGTH, RIGHT_HALF_OF_HAMMING_WINDOW);

  for (i=0; i < IMPULSE_RESPONSE_LENGTH; i++)
  {
    impulseResponse.x[i] = poleZeroSpectrum.re[i]*window.x[i];
  }

  // Reduce amplitude with increasing F0.
  impulseResponse*= 80.0 / lfPulse.F0;

  // ****************************************************************
  // Calc. the speech signal samples.
  // ****************************************************************

  for (i=0; i < length; i++)
  {
    t_s = (double)i / (double)AUDIO_SAMPLING_RATE_HZ;
    t_ms = t_s*1000.0;

    // **************************************************************
    // Is a new glottal pulse starting?
    // **************************************************************

    if (i == nextPulsePos)
    {
      // Get the pulse amplitude and F0.

      lfPulse.AMP = ampTimeFunction.getValue(t_ms);
      lfPulse.F0  = f0TimeFunction.getValue(t_ms);

      // Simulate "flutter".

      lfPulse.F0+= 0.5*(lfPulse.F0/100.0)*(sin(2.0*M_PI*12.7*t_s) + sin(2.0*M_PI*7.1*t_s) + sin(2.0*M_PI*4.7*t_s));

      // Get and set the new glottal pulse.

      pulseLength = (int)((double)AUDIO_SAMPLING_RATE_HZ / lfPulse.F0);
      lfPulse.getPulse(singlePulse, pulseLength, false);
      for (k=0; k < pulseLength; k++)
      {
        pulseSignal.x[(i+k) & BUFFER_MASK] = singlePulse.getValue(k); 
      }

      nextPulsePos+= pulseLength;
    }

    // **************************************************************
    // Do the convolution.
    // **************************************************************

    sum = 0.0;
    for (k=0; k < IMPULSE_RESPONSE_LENGTH; k++)
    {
      sum+= impulseResponse.x[k]*pulseSignal.x[(i-k) & BUFFER_MASK];
    }

    pressureSignal.x[i & BUFFER_MASK] = sum;

    // Set the final sample.

    filteredValue = 4000.0*filter.getOutputSample(pressureSignal.getValue(i));
    track[MAIN_TRACK]->setValue(startPos+i, (short)filteredValue);
  }

  // Restore the pulse params

  lfPulse = origLfPulse;

  return (int)(duration_ms + 50.0);   // 50 ms more
}


// ****************************************************************************
/// Produce a sustained vowel based on the vocal tract shape.
/// Returns the approx. duration of the vowel in milliseconds (plus some extra 
/// time).
// ****************************************************************************

int Data::synthesizeVowelLf(TlModel *tlModel, LfPulse &lfPulse, int startPos, bool isLongVowel)
{
  const int NUM_F0_NODES  = 4;
  const int NUM_AMP_NODES = 4;
  const int BUFFER_LENGTH = 2048;
  const int BUFFER_MASK = 2047;
  
  TimeFunction ampTimeFunction;
  TimeFunction f0TimeFunction;
  double duration_ms;
  Signal singlePulse;
  Signal pulseSignal(BUFFER_LENGTH);
  Signal pressureSignal(BUFFER_LENGTH);
  int i, k;
  int nextPulsePos = 10;   // Get the first pulse shape at sample number 10
  int pulseLength;
  double t_s, t_ms;
  double sum;
  double filteredValue;

  // Memorize the pulse params to restore them at the end of the function
  LfPulse origLfPulse = lfPulse;

  // ****************************************************************
  // Init the time functions for F0 and glottal pulse amplitude.
  // ****************************************************************

  if (isLongVowel)
  {
    duration_ms = 650.0;
    double max = lfPulse.F0;

    TimeFunction::Node f0[NUM_F0_NODES] =
    {
      {0.0,   0.9*max},
      {300.0, 1.00*max},
      {450.0, 0.8*max},
      {600.0, 0.7*max}
    };

    TimeFunction::Node amp[NUM_AMP_NODES] =
    {
      {0.0,   0.0},
      {40.0,  500.0},
      {400.0, 450.0},
      {600.0, 0.0}
    };

    ampTimeFunction.setNodes(amp, NUM_AMP_NODES);
    f0TimeFunction.setNodes(f0, NUM_F0_NODES);
  }
  else
  {
    duration_ms = 350.0;
    double max = lfPulse.F0;

    TimeFunction::Node f0[NUM_F0_NODES] =
    {
      {0.0,   0.9*max},
      {125.0, 1.0*max},
      {126.0, 1.0*max},
      {300.0, 0.82*max}
    };
    TimeFunction::Node amp[NUM_AMP_NODES] =
    {
      {0.0,   0.0},
      {20.0,  500.0},
      {200.0, 450.0},
      {300.0, 0.0}
    };

    ampTimeFunction.setNodes(amp, NUM_AMP_NODES);
    f0TimeFunction.setNodes(f0, NUM_F0_NODES);
  }

  // The length in samples

  int length = (int)((duration_ms/1000.0)*(double)AUDIO_SAMPLING_RATE_HZ);

  // Init the low-pass filter

  const double NUM_LOWPASS_POLES = 6;
  IirFilter filter;
  filter.createChebyshev((double)SYNTHETIC_SPEECH_BANDWIDTH_HZ / (double)AUDIO_SAMPLING_RATE_HZ, false, (int)NUM_LOWPASS_POLES);

  // ****************************************************************
  // Calc. the vocal tract impulse response.
  // ****************************************************************

  const int IMPULSE_RESPONSE_EXPONENT = 9;
  const int IMPULSE_RESPONSE_LENGTH = 1 << IMPULSE_RESPONSE_EXPONENT;
  Signal impulseResponse(IMPULSE_RESPONSE_LENGTH);

  tlModel->getImpulseResponse(&impulseResponse, IMPULSE_RESPONSE_EXPONENT);

  // Reduce amplitude with increasing F0.
  impulseResponse*= 80.0 / lfPulse.F0;

  // ****************************************************************
  // Calc. the speech signal samples.
  // ****************************************************************

  for (i=0; i < length; i++)
  {
    t_s = (double)i / (double)AUDIO_SAMPLING_RATE_HZ;
    t_ms = t_s*1000.0;

    // **************************************************************
    // Is a new glottal pulse starting?
    // **************************************************************

    if (i == nextPulsePos)
    {
      // Get the pulse amplitude and F0.

      lfPulse.AMP = ampTimeFunction.getValue(t_ms);
      lfPulse.F0  = f0TimeFunction.getValue(t_ms);

      // Simulate "flutter".

      lfPulse.F0+= 0.5*(lfPulse.F0/100.0)*(sin(2.0*M_PI*12.7*t_s) + sin(2.0*M_PI*7.1*t_s) + sin(2.0*M_PI*4.7*t_s));

      // Get and set the new glottal pulse.

      pulseLength = (int)((double)AUDIO_SAMPLING_RATE_HZ / lfPulse.F0);
      lfPulse.getPulse(singlePulse, pulseLength, false);
      for (k=0; k < pulseLength; k++)
      {
        pulseSignal.x[(i+k) & BUFFER_MASK] = singlePulse.getValue(k); 
      }

      nextPulsePos+= pulseLength;
    }

    // **************************************************************
    // Do the convolution.
    // **************************************************************

    sum = 0.0;
    for (k=0; k < IMPULSE_RESPONSE_LENGTH; k++)
    {
      sum+= impulseResponse.x[k]*pulseSignal.x[(i-k) & BUFFER_MASK];
    }

    pressureSignal.x[i & BUFFER_MASK] = sum;

    // Set the final sample.

    filteredValue = 2000.0 * filter.getOutputSample(pressureSignal.getValue(i));
    track[MAIN_TRACK]->setValue(startPos+i, (short)filteredValue);
  }

  // Restore the pulse params

  lfPulse = origLfPulse;

  return (int)(duration_ms + 50.0);   // 50 ms more
}


// ****************************************************************************
/// Calculates the user spectrum that is obtained from the signal in the main
/// track and displayed in the simple spectrum picture.
// ****************************************************************************

void Data::calcUserSpectrum()
{
  int i;

  // ****************************************************************
  // An ordinary short-time spectrum
  // ****************************************************************

  if (userSpectrumType == NORMAL_SPECTRUM)
  {
    int e = getFrameLengthExponent(spectrumWindowLength_pt);
    int frameLength = (int)1 << e;
    userSpectrum->reset(frameLength);
    Signal window(spectrumWindowLength_pt);

    getWindow(window, spectrumWindowLength_pt, HAMMING_WINDOW);
    int start = mark_pt - spectrumWindowLength_pt/2;
    for (i=0; i < spectrumWindowLength_pt; i++)
    {
      userSpectrum->re[i] = track[MAIN_TRACK]->getValue(start + i) * window.x[i];      
    }
    // The result of the FFT will be in userSpectrum again.
    realFFT(*userSpectrum, e, true);
  }
  else

  // ****************************************************************
  // The cepstrum.
  // ****************************************************************

  if (userSpectrumType == CEPSTRUM)
  {
    int e = getFrameLengthExponent(spectrumWindowLength_pt);
    int frameLength = (int)1 << e;
    ComplexSignal s(frameLength);
    Signal window(spectrumWindowLength_pt);

    getWindow(window, spectrumWindowLength_pt, HAMMING_WINDOW);
    int start = mark_pt - spectrumWindowLength_pt/2;
    for (i=0; i < spectrumWindowLength_pt; i++)
    {
      s.re[i] = track[MAIN_TRACK]->getValue(start + i) * window.x[i];      
    }
    // The result of the FFT will be in s again.
    realFFT(s, e, true);

    // Take the logarithm of the magnitude of the spectrum.
    for (i=0; i < frameLength; i++)
    {
      s.re[i] = log(s.getMagnitude(i));
      s.im[i] = 0.0;
    }

    // Go into the time domain again.
    realIFFT(s, e, false);

    // Get the final user spectrum.
    userSpectrum->reset(spectrumWindowLength_pt);
    for (i=0; i < spectrumWindowLength_pt; i++)
    {
      userSpectrum->re[i] = s.re[i];
      userSpectrum->im[i] = 0.0;
    }
  }
  else

  // ****************************************************************
  // Long-term average spectrum of several time shifted frames of the 
  // signal. Here we average *power* spectra, because that is done
  // for LTAS calculations.
  // ****************************************************************

  if (userSpectrumType == AVERAGE_SPECTRUM)
  {
    if (isValidSelection() == false)
    {
      wxMessageBox("You must select a region of the main signal for the analysis!", 
        "Invalid selection");
      return;
    }

    int regionLength_pt = selectionMark_pt[1] - selectionMark_pt[0] + 1;
    if (spectrumWindowLength_pt > regionLength_pt)
    {
      wxMessageBox("The length of the selected region must be at least the window length!", 
        "Length of selected region to short");
      return;
    }

    // Avoid division by zero.
    if (averageSpectrumTimeStep_ms < 1.0)
    {
      averageSpectrumTimeStep_ms = 1.0;
    }
    int timeStep_pt = (int)(AUDIO_SAMPLING_RATE_HZ * averageSpectrumTimeStep_ms / 1000.0);

    int numFrames = 1 + (regionLength_pt - spectrumWindowLength_pt) / timeStep_pt;
    int frame;
    int startPos;
    int e = getFrameLengthExponent(spectrumWindowLength_pt);
    int frameLength = (int)1 << e;
    ComplexSignal s(frameLength);
    Signal sumSignal(frameLength);
    
    Signal window(spectrumWindowLength_pt);
    getWindow(window, spectrumWindowLength_pt, HAMMING_WINDOW);

    wxPrintf("Averaging %d spectra... ", numFrames);

    // Add up the power spectra of all overlapping frames.

    for (frame = 0; frame < numFrames; frame++)
    {
      startPos = selectionMark_pt[0] + frame * timeStep_pt;
      s.reset(frameLength);   // Make all signal samples zero.
      for (i = 0; i < spectrumWindowLength_pt; i++)
      {
        s.re[i] = track[MAIN_TRACK]->getValue(startPos + i) * window.x[i];
      }
      // The result of the FFT will be in s again. Do NOT yet normalize here.
      // This must be done after taking the square of the magnitude.
      realFFT(s, e, false);

      for (i = 0; i < frameLength; i++)
      {
        double magnitude = s.getMagnitude(i);
        sumSignal.x[i] += (magnitude * magnitude) / (double)frameLength;
      }
    }

    // Get the final user spectrum.

    userSpectrum->reset(frameLength);
    for (i=0; i < frameLength; i++)
    {
      // Take a square root here to have magnitudes again instead
      // of magnitudes squared, because the equation 20*log10(x)
      // is always used to obtain the dB values in the displays.
      userSpectrum->re[i] = sqrt( sumSignal.x[i] / numFrames );
      
      // Scale the spectrum to be in a good range compared to the
      // other types of spectra.
      userSpectrum->re[i] *= 0.001;
    }

    wxPrintf("Done.\n");
  }
  else

  // ****************************************************************
  // A test signal.
  // ****************************************************************

  if (userSpectrumType == TEST_SPECTRUM)
  {
    userSpectrum->reset(512);

    for (i=0; i < userSpectrum->N; i++)
    {
      userSpectrum->re[i] = track[MAIN_TRACK]->getValue(mark_pt + i);
    }
  }
}


// ****************************************************************************
/// Calculates the spectrum of the radiated noise that is generated by a 
/// lumped dipole noise source in the TL model at the position 
/// noiseSourcePos_cm. The spectrum of the source is that of a 2nd-order 
/// lowpass filter with the given cutoff frequency. Since the diploe sources 
/// in the TL model can only be located at the intersections of two tube sections, 
/// the source at the given position is actually split in two sources at the
/// two nearest tube section ends.
// ****************************************************************************

bool Data::calcRadiatedNoiseSpectrum(double noiseSourcePos_cm, double noiseFilterCutoffFreq,
  int spectrumLength, ComplexSignal *spectrum)
{
  int i;
  Tube *tube = &tlModel->tube;


  // ************************************************************
  // Find the tube section of the noise source.
  // ************************************************************

  Tube::Section *ts = NULL;
  int noiseSourceSection = -1;
  double ratio = 0.0;
  double ratio1 = 1.0;

  for (i = 0; (i < Tube::NUM_PHARYNX_MOUTH_SECTIONS) && (noiseSourceSection == -1); i++)
  {
    ts = &tube->pharynxMouthSections[i];
    if ((noiseSourcePos_cm >= ts->pos_cm) && (noiseSourcePos_cm <= ts->pos_cm + ts->length_cm))
    {
      noiseSourceSection = Tube::FIRST_PHARYNX_SECTION + i;
      double length_cm = ts->length_cm;
      if (length_cm < 0.000001)
      {
        length_cm = 0.000001;
      }
      ratio = (noiseSourcePos_cm - ts->pos_cm) / length_cm;
      ratio1 = 1.0 - ratio;
    }
  }

  // ****************************************************************
  // Error: The noise source position is invalid -> return a
  // constant spectrum.
  // ****************************************************************

  if (noiseSourceSection == -1)
  {
    spectrum->reset(spectrumLength);
    for (i = 0; i < spectrumLength; i++)
    {
      spectrum->setValue(i, 1.0);
    }
    return false;
  }

  // ****************************************************************
  // The noise source position is valid.
  // ****************************************************************

  // Calc. two transfer functions from a noise sources at the
  // beginning and the end of the given tube section.

  ComplexSignal spectrum1(spectrumLength);
  ComplexSignal spectrum2(spectrumLength);

  if (noiseSourceSection < Tube::LAST_MOUTH_SECTION)
  {
    tlModel->getSpectrum(TlModel::PRESSURE_SOURCE_TF,
      &spectrum1, spectrumLength, noiseSourceSection);
    tlModel->getSpectrum(TlModel::PRESSURE_SOURCE_TF,
      &spectrum2, spectrumLength, noiseSourceSection + 1);
  }
  else
  {
    tlModel->getSpectrum(TlModel::PRESSURE_SOURCE_TF,
      &spectrum1, spectrumLength, noiseSourceSection);
    // The section -1 means the end of the last mouth section.
    tlModel->getSpectrum(TlModel::PRESSURE_SOURCE_TF,
      &spectrum2, spectrumLength, -1);
  }

  // Calc. the weighted superposition of the two spectra and
  // apply the noise shaping filter.

  ComplexValue c;
  IirFilter filter;
  double freq;

  const double Q = 1.0 / sqrt(2.0);
  filter.createSecondOrderLowpass(noiseFilterCutoffFreq / (double)AUDIO_SAMPLING_RATE_HZ, Q);

  spectrum->reset(spectrumLength);
  for (i = 0; i < spectrumLength; i++)
  {
    c = ratio1 * spectrum1.getValue(i) + ratio * spectrum2.getValue(i);
    // Noise shaping filter
    freq = (double)AUDIO_SAMPLING_RATE_HZ * i / spectrumLength;
    c *= filter.getFrequencyResponse(freq / (double)AUDIO_SAMPLING_RATE_HZ);
    spectrum->setValue(i, c);
  }

  ComplexSignal radiationSpectrum(spectrumLength);
  tlModel->getSpectrum(TlModel::RADIATION_CHARACTERISTIC, &radiationSpectrum, spectrumLength, 0);
  (*spectrum) *= radiationSpectrum;
  (*spectrum) *= 30000.0;           // Arbitrary scaling

  return true;
}


// ****************************************************************************
/// Exports video frames of the vocal tract from a gestural score at a fixed
/// frame rate into the given folder.
// ****************************************************************************

bool Data::exportVocalTractVideoFrames(const wxString &folderName)
{
  const int VIDEO_FRAME_RATE = 30;
  double duration_s = gesturalScore->getScoreDuration_s();
  int numFrames = (int)(duration_s*VIDEO_FRAME_RATE);
  int i;
  int frameIndex;
  double time_s;
  double oldVocalTractParams[VocalTract::NUM_PARAMS];
  double vocalTractParams[VocalTract::NUM_PARAMS];
  double glottisParams[256];

  // ****************************************************************
  // Keep in mind the current vocal tract state.
  // ****************************************************************

  for (i=0; i < VocalTract::NUM_PARAMS; i++)
  {
    oldVocalTractParams[i] = vocalTract->params[i].x;    
  }

  // ****************************************************************
  // Save one image file for each video frame.
  // ****************************************************************

  for (frameIndex=0; frameIndex < numFrames; frameIndex++)
  {
//    wxPrintf("frame #: %d\n", frameIndex);

    // Calculate the vocal tract shape at the current frame.

    time_s = (double)frameIndex / (double)VIDEO_FRAME_RATE;
    gesturalScore->getParams(time_s, vocalTractParams, glottisParams);

    for (i=0; i < VocalTract::NUM_PARAMS; i++)
    {
      vocalTract->params[i].x = vocalTractParams[i];    
    }
    vocalTract->calculateAll();

    // Update the vocal tract dialog.

    VocalTractDialog *vocalTractDialog = VocalTractDialog::getInstance(NULL);
    if (vocalTractDialog->IsShownOnScreen() == false)
    {
      vocalTractDialog->Show(true);
    }

    // Do the refreh/update twice because of the double buffering.
    // Otherwise, the very first frame may be wrong.
    vocalTractDialog->Refresh();
    vocalTractDialog->Update();
    wxYield();

    vocalTractDialog->Refresh();
    vocalTractDialog->Update();
    wxYield();

    // Save the current vocal tract shape as an image (video frame).
    wxString frameFileName = folderName;
    wxChar pathSeparator = wxFileName::GetPathSeparator();
    if (frameFileName.EndsWith( &pathSeparator ) == false)
    {
      frameFileName+= pathSeparator;
    }
    frameFileName+= "vt" + wxString::Format("%03d", frameIndex) + ".bmp";
    vocalTractDialog->getVocalTractPicture()->saveImageBmp(frameFileName);
  }


  // ****************************************************************
  // Restore the old vocal tract state.
  // ****************************************************************

  for (i=0; i < VocalTract::NUM_PARAMS; i++)
  {
    vocalTract->params[i].x = oldVocalTractParams[i];
  }
  vocalTract->calculateAll();

  VocalTractDialog *vocalTractDialog = VocalTractDialog::getInstance(NULL);
  vocalTractDialog->Refresh();
  vocalTractDialog->Update();

  wxPrintf("Finished exporting %d video frames for a frame rate of %d Hz.\n", 
    numFrames, VIDEO_FRAME_RATE);

  return true;
}


// ****************************************************************************
// Generate a text file that contains a sequence of vocal tract and vocal
// fold control parameters (in steps of 120 samples or 2.5 ms @ 48 kHz)
// from the current gestural score.
// ****************************************************************************

bool Data::exportTractGlottisParamCurves(const wxString& fileName)
{
  int i, k;
  int numGlottisParams = (int)gesturalScore->glottis->controlParams.size();
  double scoreDuration_s = gesturalScore->getScoreDuration_s();
  int numStates = (int)(scoreDuration_s * GesturalScore::CURVE_SAMPLING_RATE);
  
  if (numStates > GesturalScore::MAX_CURVE_SAMPLES)
  {
    numStates = GesturalScore::MAX_CURVE_SAMPLES;
  }

  ofstream file;
  file.open(fileName.ToStdString());
  if (file.is_open() == false)
  {
    printf("Error in exportTractGlottisParamCurves(): The file could not be opened!\n");
    return false;
  }

  // Write some header data into the file.

  file << "# The first two lines (below the comment lines) indicate the name" << endl;
  file << "# of the vocal fold model and the number of states." << endl;
  file << "# The next line contains the names of the parameters." << endl;
  file << "# The following lines contain the values in steps of 120 audio samples" << endl;
  file << "# (corresponding to 2.5 ms for the sampling rate of 48000 Hz)." << endl;
  file << "#" << endl;

  // Write the name of the glottis model.
  file << gesturalScore->glottis->getName() << endl;

  // Write the number of states.
  file << numStates << endl;

  // Write the parameter names.

  int index = 1;
  for (k = 0; k < VocalTract::NUM_PARAMS; k++)
  {
    file << index << ":";
    file << vocalTract->params[k].abbr << " ";
    index++;
  }

  for (k = 0; k < numGlottisParams; k++)
  {
    file << index << ":";
    file << gesturalScore->glottis->controlParams[k].abbr << " ";
    index++;
  }
  file << endl;

  // Important: Calc. the parameter curves from the gestural score.
  gesturalScore->calcCurves();

  // ****************************************************************
  // Write the vocal tract and glottis parameters to the file every
  // 120 samples (=2.5 ms).
  // ****************************************************************

  file << std::fixed << std::setprecision(4);   // Set number of post-decimal digits.

  for (i = 0; i < numStates; i++)
  {
    for (k = 0; k < VocalTract::NUM_PARAMS; k++)
    {
      file << gesturalScore->tractParamCurve[k][i] << " ";
    }

    for (k = 0; k < numGlottisParams; k++)
    {
      file << gesturalScore->glottisParamCurve[k][i] << " ";
    }
    file << endl;
  }

  // Close the file.
  file.close();

  return true;
}


// ****************************************************************************
/// Run through all raw vocal tract shapes (which end with "-raw") and 
/// calculate where the manually set point (TRX, TRY) projects on the horizontal
/// line that will later be used for the automatic calculation of (TRX, TRY)
/// similar to the approach in the Mermelstein vocal tract model.
/// For each raw shape, three values are written in one row of the clipboard:
/// The name of the shape, the hyoid-tongue distance, and the position of TRX 
/// on the horizontal line that crosses the hyoid-tongue tangent.
/// These data can then be copied into an Excel sheet, for example.
// ****************************************************************************

void Data::calcTongueRootData()
{
  const wxString NEW_LINE = "\r\n";
  int i, k;
  int numShapes = (int)vocalTract->shapes.size();
  wxString name;
  wxString clipboardString;
  wxString st;
  Point2D H, T, Q;
  double hyoidTongueDistance_cm;

  // This is the caption string.
  clipboardString = "name hyoid-tongue-center-distance TRX TRY TCX TCY HX_cm HY_cm" + NEW_LINE;

  for (i=0; i < numShapes; i++)
  {
    name = wxString( vocalTract->shapes[i].name );
    if (name.Contains("-raw"))
    {

      // Set the parameters of this shape and re-calculate the VT.
      for (k=0; k < VocalTract::NUM_PARAMS; k++)
      {
        vocalTract->params[k].x = vocalTract->shapes[i].param[k];
      }
      vocalTract->calculateAll();

      vocalTract->getHyoidTongueTangent(H, T);    // To get H!

      Point2D M(vocalTract->params[VocalTract::TCX].x, vocalTract->params[VocalTract::TCY].x);
      hyoidTongueDistance_cm = (H - M).magnitude();

      // Write name, hyoidTongueDistance_cm, etc. into the clipboard.
      st = name + wxString::Format(" %2.2f %2.2f %2.2f %2.2f %2.2f %2.2f %2.2f", 
        hyoidTongueDistance_cm, 
        vocalTract->params[VocalTract::TRX].x,
        vocalTract->params[VocalTract::TRY].x,
        vocalTract->params[VocalTract::TCX].x,
        vocalTract->params[VocalTract::TCY].x,
        H.x, H.y) + NEW_LINE;
      clipboardString+= st;
    }
  }

  // This data objects are held by the clipboard, 
  // so do not delete them in the app.

  if (wxTheClipboard->Open())
  {
    wxTheClipboard->SetData( new wxTextDataObject(clipboardString) );
    wxTheClipboard->Close();
  }
}


// ****************************************************************************
/// Optimize the parameters of the given vocal tract so that the formants
/// match the given formant target values as well as possible.
// ****************************************************************************

void Data::optimizeFormantsVowel(wxWindow *updateParent, VocalTract *tract, 
  double targetF1, double targetF2, double targetF3, 
  double maxParamChange_cm, double minAdvisedArea_cm2, bool paramFixed[])
{
  const int MAX_RUNS = 100;

  double F1, F2, F3;
  double minArea_cm2;
  double changeStep[VocalTract::NUM_PARAMS];
  int stepsTaken[VocalTract::NUM_PARAMS];   // Cummulated steps gone by a parameter
  double currParamValue;
  double bestError;
  double currError;
  double newError;
  double bestParamChange;
  int bestParam;
  int i;
  char st[1024];
  wxCommandEvent event(updateRequestEvent);

  VocalTractDialog *vocalTractDialog = VocalTractDialog::getInstance(NULL);


  // ****************************************************************
  // Make sure that all areas are above the given threshold.
  // ****************************************************************

  getVowelFormants(tract, F1, F2, F3, minArea_cm2);
  if (minArea_cm2 < minAdvisedArea_cm2)
  {
    wxString st = wxString::Format(
      "There are cross-sectional areas below the threshold of %2.2f cm^2.\n"
      "Press OK to let it be corrected before the optimization.", 
      minAdvisedArea_cm2);

    if (wxMessageBox(st, "Areas to small", wxOK | wxCANCEL) == wxOK)
    {
      createMinVocalTractArea(updateParent, tract, minAdvisedArea_cm2);
    }
    else
    {
      return;
    }
  }

  double initialError = getFormantError(F1, F2, F3, targetF1, targetF2, targetF3);
  wxPrintf("\n=== Before vowel optimization ===\n");
  wxPrintf("F1:%d  F2:%d  F3:%d   F1':%d  F2':%d  F3':%d   error:%2.2f percent\n",
    (int)F1, (int)F2, (int)F3, (int)targetF1, (int)targetF2, (int)targetF3, initialError);

  // ****************************************************************
  // Define the steps by which the individual parameters are adjusted
  // incrementally. A step value of 0.0 means that the parameters is
  // not supposed to be adjusted at all.
  // ****************************************************************

  // How many steps are maximally allowed per parameter in one direction ?
  const double STEP_SIZE_CM = 0.05;   // = 1/2 mm
  int maxSteps = (int)((maxParamChange_cm / STEP_SIZE_CM) + 0.5);
  if (maxSteps < 1)
  {
    maxSteps = 1;
  }
  if (maxSteps > 20)
  {
    maxSteps = 20;    // Corresponds to 1.0 cm
  }
  wxPrintf("The vocal tract shape may change by at most %1.1f mm.\n", maxSteps*STEP_SIZE_CM*10.0);

  for (i=0; i < VocalTract::NUM_PARAMS; i++)
  {
    changeStep[i] = 0.0;
    stepsTaken[i] = 0;
  }

  changeStep[VocalTract::HX] = STEP_SIZE_CM / 1.5;
  changeStep[VocalTract::HY] = STEP_SIZE_CM;
  changeStep[VocalTract::JX] = STEP_SIZE_CM;
  changeStep[VocalTract::JA] = 0.05;    // -5 .. 0 deg
  changeStep[VocalTract::LP] = 0.05;    // -1 .. +1
  changeStep[VocalTract::LD] = STEP_SIZE_CM;
  changeStep[VocalTract::VS] = 1.0 / (0.5/STEP_SIZE_CM);   // 0 .. 1 corr. to diff. of 0.5 cm

  changeStep[VocalTract::TCX] = STEP_SIZE_CM;
  changeStep[VocalTract::TCY] = STEP_SIZE_CM;
  changeStep[VocalTract::TTX] = STEP_SIZE_CM;
  changeStep[VocalTract::TTY] = STEP_SIZE_CM;

  changeStep[VocalTract::TBX] = STEP_SIZE_CM;
  changeStep[VocalTract::TBY] = STEP_SIZE_CM;
  changeStep[VocalTract::TRX] = STEP_SIZE_CM;
  changeStep[VocalTract::TRY] = STEP_SIZE_CM;

  changeStep[VocalTract::TS1] = 0.05;    // 0 .. +1
  changeStep[VocalTract::TS2] = 0.05;    // 0 .. +1
  changeStep[VocalTract::TS3] = 0.05;    // -1 .. +1

  // Set the change step to zero for parameters that are supposed to be fixed.

  for (i=0; i < VocalTract::NUM_PARAMS; i++)
  {
    if (paramFixed[i])
    {
      changeStep[i] = 0.0;
    }
  }
  
  // ****************************************************************
  // Init. the progress dialog.
  // ****************************************************************

  wxGenericProgressDialog progressDialog("Please wait", "The formant optimization is running...",
    MAX_RUNS, NULL, wxPD_CAN_ABORT | wxPD_APP_MODAL | wxPD_AUTO_HIDE);

  // ****************************************************************
  // ****************************************************************

  bool paramChanged = false;
  bool doContinue = false;
  int runCounter = 0;

  do
  {
    paramChanged = false;
    getVowelFormants(tract, F1, F2, F3, minArea_cm2);
    currError = getFormantError(F1, F2, F3, targetF1, targetF2, targetF3);

    // **************************************************************
    // Find out the improvement of the error when each parameter is 
    // changed individually by a positive changeStep[i] starting 
    // from the current configuration.
    // **************************************************************
    
    bestError = currError;
    bestParam = -1;
    bestParamChange = 0.0;

    for (i=0; i < VocalTract::NUM_PARAMS; i++)
    {
      currParamValue = tract->params[i].x;

      if (changeStep[i] > 0.0)
      {
        // Apply a POSITIVE change to parameter i.
        
        if (stepsTaken[i] < maxSteps)
        {
          tract->params[i].x = currParamValue + changeStep[i];
          getVowelFormants(tract, F1, F2, F3, minArea_cm2);
          
          // Check that the minimum area stays above the threshold.
          if (minArea_cm2 >= minAdvisedArea_cm2)
          {
            newError = getFormantError(F1, F2, F3, targetF1, targetF2, targetF3);
            if (newError < bestError)
            {
              bestError = newError;
              bestParam = i;
              bestParamChange = changeStep[i];
            }
          }
        }

        // Apply a NEGATIVE change to parameter i.

        if (stepsTaken[i] > -maxSteps)
        {
          tract->params[i].x = currParamValue - changeStep[i];
          getVowelFormants(tract, F1, F2, F3, minArea_cm2);
          
          // Check that the minimum area stays above the threshold.
          if (minArea_cm2 >= minAdvisedArea_cm2)
          {
            newError = getFormantError(F1, F2, F3, targetF1, targetF2, targetF3);
            if (newError < bestError)
            {
              bestError = newError;
              bestParam = i;
              bestParamChange = -changeStep[i];
            }
          }
        }

        // Set the parameter value back to its original value.
        tract->params[i].x = currParamValue;
      }
    }

    // **************************************************************
    // Change the parameter with the best error reduction.
    // **************************************************************

    sprintf(st, "no change");

    if ((bestParam != -1) && (bestError < currError))
    {
      tract->params[bestParam].x+= bestParamChange;
      if (bestParamChange > 0.0)
      {
        stepsTaken[bestParam]++;
        sprintf(st, "%s up", tract->params[bestParam].abbr.c_str());
      }
      else
      {
        stepsTaken[bestParam]--;
        sprintf(st, "%s down", tract->params[bestParam].abbr.c_str());
      }

      paramChanged = true;
    }

    printf("Run %d: %s. Error=%2.2f\n", runCounter, st, bestError);

    // Update the pictures on the parent page.

    if (updateParent != NULL)
    {
      // Calculate the vocal tract area function.
      tract->calculateAll();
      // Set the latest vocal tract geometry for the transmission line model.   
      updateTlModelGeometry(tract);

      event.SetInt(UPDATE_PICTURES_AND_CONTROLS);
      wxPostEvent(updateParent, event);

      if (vocalTractDialog->IsShown())
      {
        vocalTractDialog->Refresh();
        vocalTractDialog->Update();
      }

      wxYield();
    }

    doContinue = progressDialog.Update(runCounter);

    runCounter++;

  } while ((paramChanged) && (doContinue) && (runCounter < MAX_RUNS));

  // Hide the progress dialog.
  progressDialog.Update(MAX_RUNS);

  // ****************************************************************
  // The velo-pharyngeal port must be closed.
  // ****************************************************************

  if (tract->params[VocalTract::VO].x > 0.0)
  {
    tract->params[VocalTract::VO].x = 0.0;
  }

  // ****************************************************************
  // ****************************************************************

  getVowelFormants(tract, F1, F2, F3, minArea_cm2);
  double finalError = getFormantError(F1, F2, F3, targetF1, targetF2, targetF3);
  wxPrintf("=== After optimization ===\n");
  wxPrintf("F1:%d  F2:%d  F3:%d   F1':%d  F2':%d  F3':%d   Error:%2.2f percent\n",
    (int)F1, (int)F2, (int)F3, (int)targetF1, (int)targetF2, (int)targetF3, finalError);
  wxPrintf("The error reduced from %2.2f to %2.2f percent.\n", initialError, finalError);
}


// ****************************************************************************
/// This function optimizes the formant frequencies right after the release of
/// a stop consonant (at vowel onset). The formant error is calculated for a
/// vocal tract shape between the given full consonantal shape and the context 
/// vowel shape at the place where the minimal area is just the given
/// minArea_cm2
// ****************************************************************************

void Data::optimizeFormantsConsonant(wxWindow *updateParent, VocalTract *tract, 
  const wxString &contextVowel, double targetF1, double targetF2, double targetF3, 
  double maxParamChange_cm, double minArea_cm2, double releaseArea_cm2, bool paramFixed[])
{
  const int MAX_RUNS = 100;

  double F1, F2, F3;
  double changeStep[VocalTract::NUM_PARAMS];
  int stepsTaken[VocalTract::NUM_PARAMS];   // Cummulated steps gone by a parameter
  double currParamValue;
  double bestError;
  double currError;
  double newError;
  double bestParamChange;
  int bestParam;
  int i, k;
  char st[1024];
  wxCommandEvent event(updateRequestEvent);

  VocalTractDialog *vocalTractDialog = VocalTractDialog::getInstance(NULL);


  // ****************************************************************
  // Find the start and end positions of the constricted region,
  // where the area is allowed to be smaller than minArea_cm2.
  // ****************************************************************

  double constrictionStartPos_cm = 0.0;
  double constrictionEndPos_cm = 0.0;
  
  k = 0;   // Index of the cross-section with the smallest area
  for (i=1; i < VocalTract::NUM_CENTERLINE_POINTS; i++)
  {
    if (tract->crossSection[i].area < tract->crossSection[k].area)
    {
      k = i;
    }    
  }

  // Find the region that is close to the minimum area within an epsilon range.
  
  const double POS_MARGIN_CM = 1.5;
  const double DELTA_AREA_CM2 = 0.1;   // = 10 mm2
  double constrictionArea_cm2 = tract->crossSection[k].area;

  i = k;
  while ((i > 0) && (tract->crossSection[i].area < constrictionArea_cm2 + DELTA_AREA_CM2))
  {
    i--;
  }
  constrictionStartPos_cm = tract->crossSection[i].pos - POS_MARGIN_CM;

  i = k;
  while ((i < VocalTract::NUM_CENTERLINE_POINTS-1) && (tract->crossSection[i].area < constrictionArea_cm2 + DELTA_AREA_CM2))
  {
    i++;
  }
  constrictionEndPos_cm = tract->crossSection[i].pos + POS_MARGIN_CM;

  // ****************************************************************
  // Make sure that all areas left and right of the constriction are 
  // above the given threshold.
  // ****************************************************************

  double minAreaOutsideConstriction_cm2 = getMinAreaOutsideConstriction_cm2(tract, constrictionStartPos_cm, constrictionEndPos_cm);
  if (minAreaOutsideConstriction_cm2 < minArea_cm2)
  {
    wxString st = wxString::Format(
      "There are cross-sectional areas left and right of the constriction\n"
      "(between %2.1f and %2.1f cm) below the threshold of %2.2f cm^2.\n"
      "Press OK to let it be corrected before the optimization.", 
      constrictionStartPos_cm, constrictionEndPos_cm, minArea_cm2);

    if (wxMessageBox(st, "Areas to small", wxOK | wxCANCEL) == wxOK)
    {
      createMinVocalTractArea(updateParent, tract, minArea_cm2, constrictionStartPos_cm, constrictionEndPos_cm);
    }
    else
    {
      return;
    }
  }


  // ****************************************************************
  // The velo-pharyngeal port must be closed.
  // ****************************************************************

  if (tract->params[VocalTract::VO].x > 0.0)
  {
    tract->params[VocalTract::VO].x = 0.0;
  }

  // ****************************************************************
  // Get the initial error.
  // ****************************************************************

  getConsonantFormants(tract, contextVowel, releaseArea_cm2, F1, F2, F3);
  double initialError = getFormantError(F1, F2, F3, targetF1, targetF2, targetF3);

  wxPrintf("\n=== Before consonant formant optimization ===\n");
  wxPrintf("F1:%d  F2:%d  F3:%d   F1':%d  F2':%d  F3':%d   error:%2.2f percent\n",
    (int)F1, (int)F2, (int)F3, (int)targetF1, (int)targetF2, (int)targetF3, initialError);

  // ****************************************************************
  // Define the steps by which the individual parameters are adjusted
  // incrementally. A step value of 0.0 means that the parameters is
  // not supposed to be adjusted at all.
  // ****************************************************************

  // How many steps are maximally allowed per parameter in one direction ?
  const double STEP_SIZE_CM = 0.05;   // = 1/2 mm
  int maxSteps = (int)((maxParamChange_cm / STEP_SIZE_CM) + 0.5);
  if (maxSteps < 1)
  {
    maxSteps = 1;
  }
  if (maxSteps > 10)
  {
    maxSteps = 10;
  }
  wxPrintf("The vocal tract shape may change by at most %1.1f mm.\n", maxSteps*STEP_SIZE_CM*10.0);

  for (i=0; i < VocalTract::NUM_PARAMS; i++)
  {
    changeStep[i] = 0.0;
    stepsTaken[i] = 0;
  }

  changeStep[VocalTract::HX] = STEP_SIZE_CM / 1.5;
  changeStep[VocalTract::HY] = STEP_SIZE_CM;
  changeStep[VocalTract::JX] = STEP_SIZE_CM;
  changeStep[VocalTract::JA] = 0.05;    // -5 .. 0 deg
  changeStep[VocalTract::LP] = 0.05;    // -1 .. +1
  changeStep[VocalTract::LD] = STEP_SIZE_CM;
  changeStep[VocalTract::VS] = 1.0 / (0.5/STEP_SIZE_CM);   // 0 .. 1 corr. to diff. of 0.5 cm

  changeStep[VocalTract::TCX] = STEP_SIZE_CM;
  changeStep[VocalTract::TCY] = STEP_SIZE_CM;
  changeStep[VocalTract::TTX] = STEP_SIZE_CM;
  changeStep[VocalTract::TTY] = STEP_SIZE_CM;

  changeStep[VocalTract::TBX] = STEP_SIZE_CM;
  changeStep[VocalTract::TBY] = STEP_SIZE_CM;
  changeStep[VocalTract::TRX] = STEP_SIZE_CM;
  changeStep[VocalTract::TRY] = STEP_SIZE_CM;

  changeStep[VocalTract::TS1] = 0.05;    // -1 .. +1
  changeStep[VocalTract::TS2] = 0.05;    // -1 .. +1
  changeStep[VocalTract::TS3] = 0.05;    // -1 .. +1

  // Set the change step to zero for parameters that are supposed to be fixed.

  for (i=0; i < VocalTract::NUM_PARAMS; i++)
  {
    if (paramFixed[i])
    {
      changeStep[i] = 0.0;
    }
  }
  

  // ****************************************************************
  // Init. the progress dialog.
  // ****************************************************************

  wxGenericProgressDialog progressDialog("Please wait", "The formant optimization is running...",
    MAX_RUNS, NULL, wxPD_CAN_ABORT | wxPD_APP_MODAL | wxPD_AUTO_HIDE);

  // ****************************************************************
  // ****************************************************************

  bool paramChanged = false;
  bool doContinue = false;
  int runCounter = 0;

  do
  {
    paramChanged = false;
    getConsonantFormants(tract, contextVowel, releaseArea_cm2, F1, F2, F3);
    currError = getFormantError(F1, F2, F3, targetF1, targetF2, targetF3);

    // **************************************************************
    // Find out the improvement of the error when each parameter is 
    // changed individually by a positive changeStep[i] starting 
    // from the current configuration.
    // **************************************************************
    
    bestError = currError;
    bestParam = -1;
    bestParamChange = 0.0;

    for (i=0; (i < VocalTract::NUM_PARAMS) && (progressDialog.Update(runCounter)); i++)
    {
      currParamValue = tract->params[i].x;

      if (changeStep[i] > 0.0)
      {
        // Apply a POSITIVE change to parameter i.
        
        if (stepsTaken[i] < maxSteps)
        {
          tract->params[i].x = currParamValue + changeStep[i];

          // Do nothing when the VO parameter is changed above the threshold (velum open).
          if ((i == VocalTract::VO) && (tract->params[i].x > 0.0))
          {
            // do nothing.
          }
          else
          {
            if ((getMinAreaOutsideConstriction_cm2(tract, constrictionStartPos_cm, constrictionEndPos_cm) >= minArea_cm2) &&
                (getConsonantFormants(tract, contextVowel, releaseArea_cm2, F1, F2, F3)))
            {
              newError = getFormantError(F1, F2, F3, targetF1, targetF2, targetF3);
              if (newError < bestError)
              {
                bestError = newError;
                bestParam = i;
                bestParamChange = changeStep[i];
              }
            }
          }

        }

        // Apply a NEGATIVE change to parameter i.

        if (stepsTaken[i] > -maxSteps)
        {
          tract->params[i].x = currParamValue - changeStep[i];
          if ((getMinAreaOutsideConstriction_cm2(tract, constrictionStartPos_cm, constrictionEndPos_cm) >= minArea_cm2) &&
              (getConsonantFormants(tract, contextVowel, releaseArea_cm2, F1, F2, F3)))
          {
            newError = getFormantError(F1, F2, F3, targetF1, targetF2, targetF3);
            if (newError < bestError)
            {
              bestError = newError;
              bestParam = i;
              bestParamChange = -changeStep[i];
            }
          }
        }

        // Set the parameter value back to its original value.
        tract->params[i].x = currParamValue;

        // Some progress displaying...
        wxPrintf(".");
      }
    }
    wxPrintf("\n");   // A line break after all the points.

    // **************************************************************
    // Change the parameter with the best error reduction.
    // **************************************************************

    sprintf(st, "no change");

    if ((bestParam != -1) && (bestError < currError))
    {
      tract->params[bestParam].x+= bestParamChange;
      if (bestParamChange > 0.0)
      {
        stepsTaken[bestParam]++;
        sprintf(st, "%s up", tract->params[bestParam].abbr.c_str());
      }
      else
      {
        stepsTaken[bestParam]--;
        sprintf(st, "%s down", tract->params[bestParam].abbr.c_str());
      }

      paramChanged = true;
    }

    getConsonantFormants(tract, contextVowel, releaseArea_cm2, F1, F2, F3);
    wxPrintf("Run %d: %s. Formants: %d, %d, %d  Error=%2.2f\n",
      runCounter + 1, st, (int)F1, (int)F2, (int)F3, bestError);

    // Update the pictures on the parent page.

    if (updateParent != NULL)
    {
      // Calculate the vocal tract area function.
      tract->calculateAll();
      // Set the latest vocal tract geometry for the transmission line model.   
      updateTlModelGeometry(tract);

      event.SetInt(UPDATE_PICTURES_AND_CONTROLS);
      wxPostEvent(updateParent, event);

      if (vocalTractDialog->IsShown())
      {
        vocalTractDialog->Refresh();
        vocalTractDialog->Update();
      }

      wxYield();
    }

    doContinue = progressDialog.Update(runCounter);

    runCounter++;

  } while ((paramChanged) && (doContinue) && (runCounter < MAX_RUNS));

  // Hide the progress dialog.
  progressDialog.Update(MAX_RUNS);

  wxPrintf("\n");

  // ****************************************************************
  // ****************************************************************

  getConsonantFormants(tract, contextVowel, releaseArea_cm2, F1, F2, F3);
  double finalError = getFormantError(F1, F2, F3, targetF1, targetF2, targetF3);

  wxPrintf("=== After consonant formant optimization ===\n");
  wxPrintf("F1:%d  F2:%d  F3:%d   F1':%d  F2':%d  F3':%d   Error:%2.2f percent\n",
    (int)F1, (int)F2, (int)F3, (int)targetF1, (int)targetF2, (int)targetF3, finalError);
  wxPrintf("The error reduced from %2.2f to %2.2f percent.\n", initialError, finalError);

}


// ****************************************************************************
/// Finds the vocal tract shape on the linear transition from the given 
/// consonant shape to the given vowel shape where the minimal cross-sectional
/// area is just the given releaseArea_cm2.
/// The position on the transition (between 0.0 and 1.0) is returned in
/// releasePos, and the resulting shape parameters in releaseParams.
// ****************************************************************************

bool Data::getReleaseShape(VocalTract *vt, double *consonantParams, double *vowelParams,
  double *releaseParams, double &releasePos, double releaseArea_cm2)
{
  int i;
  double minAreaVowel_cm2 = 0.0;
  double minAreaConsonant_cm2 = 0.0;
  // Start and end pos. of the min. area search region.
  double startPos_cm = 0.0;   
  double endPos_cm = 1000.0;

  // ****************************************************************
  // Keep in mind the original VT parameters.
  // ****************************************************************

  double origParams[VocalTract::NUM_PARAMS];
  for (i=0; i < VocalTract::NUM_PARAMS; i++)
  {
    origParams[i] = vt->params[i].x;
  }

  // ****************************************************************
  // Check the initial min. area of the vowel.
  // ****************************************************************

  for (i=0; i < VocalTract::NUM_PARAMS; i++)
  {
    vt->params[i].x = vowelParams[i];
  }
  minAreaVowel_cm2 = getMinArea_cm2(vt, 0.0, 1000.0);

  // ****************************************************************
  // Check the initial min. area of the consonant.
  // ****************************************************************

  for (i=0; i < VocalTract::NUM_PARAMS; i++)
  {
    vt->params[i].x = consonantParams[i];
  }
  minAreaConsonant_cm2 = getMinArea_cm2(vt, 0.0, 1000.0);

  // Get the start and end point of the region along the center line
  // around this minimum.

  const double AREA_EPSILON_CM2 = 0.01;   // = 1 mm^2
  const double POS_MARGIN_CM = 2.0;
  i = 0;
  while ((i < VocalTract::NUM_CENTERLINE_POINTS) && 
         (vt->crossSection[i].area > minAreaConsonant_cm2 + AREA_EPSILON_CM2))
  {
    i++;
  }
  if (i < VocalTract::NUM_CENTERLINE_POINTS)
  {
    startPos_cm = vt->crossSection[i].pos - POS_MARGIN_CM;
  }
  else
  {
    startPos_cm = 0.0;
  }

  // ****************************************************************

  i = VocalTract::NUM_CENTERLINE_POINTS - 1;
  while ((i >= 0) && (vt->crossSection[i].area > minAreaConsonant_cm2 + AREA_EPSILON_CM2))
  {
    i--;
  }
  if (i >= 0)
  {
    endPos_cm = vt->crossSection[i].pos + POS_MARGIN_CM;
  }
  else
  {
    endPos_cm = 1000.0;
  }

  // The min. vowel area must be greater than the release area, and
  // the min. consonant area must be less.

  if ((minAreaConsonant_cm2 >= releaseArea_cm2) || (minAreaVowel_cm2 <= releaseArea_cm2))
  {
    // Set back the original parameter values.
    for (i=0; i < VocalTract::NUM_PARAMS; i++)
    {
      vt->params[i].x = origParams[i];
    }
    vt->calculateAll();
    return false;
  }

  // ****************************************************************
  // Find the transition position where the minimum area is the
  // given release are using the bisection method.
  // ****************************************************************

  const double AREA_TOLERANCE_CM2 = 0.001;    // = 0.1 mm^2
  const int MAX_RUNS = 40;
  int run = 0;
  double intervalBegin = 0.0;
  double intervalEnd = 1.0;
  double intervalMidpoint = 0.5;
  double minAreaIntervalBegin = minAreaConsonant_cm2;
  double minAreaIntervalEnd = minAreaVowel_cm2;
  double minAreaIntervalMidpoint = 0.0;

  bool solutionFound = false;

  while ((run < MAX_RUNS) && (solutionFound == false))
  {
    intervalMidpoint = 0.5*(intervalBegin + intervalEnd);

    // Find the min. area at the interval midpoint in the region
    // between startPos_cm and endPos_cm in the area function.
    
    for (i=0; i < VocalTract::NUM_PARAMS; i++)
    {
      vt->params[i].x = (1.0-intervalMidpoint)*consonantParams[i] + intervalMidpoint*vowelParams[i];
    }
    minAreaIntervalMidpoint = getMinArea_cm2(vt, startPos_cm, endPos_cm);

    if (fabs(minAreaIntervalMidpoint - releaseArea_cm2) < AREA_TOLERANCE_CM2)
    {
      solutionFound = true;
    }

    // Change either the beginning or the end of the interval.

    if (minAreaIntervalMidpoint < releaseArea_cm2)
    {
      intervalBegin = intervalMidpoint;
      minAreaIntervalBegin = minAreaIntervalMidpoint;
    }
    else
    {
      intervalEnd = intervalMidpoint;
      minAreaIntervalEnd = minAreaIntervalMidpoint;
    }

    run++;
  }

  // Set the return values.

  releasePos = intervalMidpoint;
  releaseArea_cm2 = minAreaIntervalMidpoint;

  for (i=0; i < VocalTract::NUM_PARAMS; i++)
  {
    releaseParams[i] = vt->params[i].x;
  }

//  printf("runs: %d  is: %f cm2  target: %f cm2\n", run, minAreaIntervalMidpoint, releaseArea_cm2);

  return true;
}


// ****************************************************************************
/// This function adjusts the parameters of the given vocal tract as long as
/// the minimal cross-sectional area is smaller than the recommended minimal
/// area for vowels.
// ****************************************************************************

void Data::createMinVocalTractArea(wxWindow *updateParent, VocalTract *tract, double minAdvisedArea_cm2,
  double skipRegionStart_cm, double skipRegionEnd_cm)
{
  const int MAX_RUNS = 30;

  int i;
  double minArea_cm2;
  int numRuns = 0;
  int bestParam = -1;
  double bestParamChange = 0.0;
  double bestAreaChange = 0.0;
  double currParamValue = 0.0;
  double currMinArea_cm2 = 0.0;
  double deltaMinArea_cm2 = 0.0;
  char st[1024];
  wxCommandEvent event(updateRequestEvent);
  
  VocalTractDialog *vocalTractDialog = VocalTractDialog::getInstance(NULL);

  // ****************************************************************
  // Determine the change step (increment) for the individual 
  // parameters.
  // ****************************************************************

  const double STEP_SIZE_CM = 0.025;   // = 1/4 mm
  double changeStep[VocalTract::NUM_PARAMS];

  for (i=0; i < VocalTract::NUM_PARAMS; i++)
  {
    changeStep[i] = 0.0;
  }

  changeStep[VocalTract::HX] = STEP_SIZE_CM / 1.5;
  changeStep[VocalTract::HY] = STEP_SIZE_CM;
  changeStep[VocalTract::JX] = STEP_SIZE_CM;
  changeStep[VocalTract::JA] = 0.05;    // -5 .. 0 deg
  changeStep[VocalTract::LP] = 0.05;    // -1 .. +1
  changeStep[VocalTract::LD] = STEP_SIZE_CM;
  changeStep[VocalTract::VS] = 1.0 / (0.5/STEP_SIZE_CM);   // 0 .. 1 corr. to diff. of 0.5 cm

  changeStep[VocalTract::TCX] = STEP_SIZE_CM;
  changeStep[VocalTract::TCY] = STEP_SIZE_CM;
  changeStep[VocalTract::TTX] = STEP_SIZE_CM;
  changeStep[VocalTract::TTY] = STEP_SIZE_CM;

  changeStep[VocalTract::TBX] = STEP_SIZE_CM;
  changeStep[VocalTract::TBY] = STEP_SIZE_CM;
  changeStep[VocalTract::TRX] = STEP_SIZE_CM;
  changeStep[VocalTract::TRY] = STEP_SIZE_CM;

  changeStep[VocalTract::TS1] = 0.05;    // -1 .. +1
  changeStep[VocalTract::TS2] = 0.05;    // -1 .. +1
  changeStep[VocalTract::TS3] = 0.05;    // -1 .. +1

  // ****************************************************************
  // Init. the progress dialog.
  // ****************************************************************

  wxGenericProgressDialog progressDialog("Please wait", "The correction is running...",
    MAX_RUNS, NULL, wxPD_CAN_ABORT | wxPD_APP_MODAL | wxPD_AUTO_HIDE);

  bool doContinue = true;

  // ****************************************************************
  // The main loop.
  // ****************************************************************

  // Get the initial min area.
  minArea_cm2 = getMinAreaOutsideConstriction_cm2(tract, skipRegionStart_cm, skipRegionEnd_cm);
  //getVowelFormants(tract, F1, F2, F3, minArea_cm2);

  while ((minArea_cm2 < minAdvisedArea_cm2) && (numRuns < MAX_RUNS) && (doContinue))
  {
    currMinArea_cm2 = minArea_cm2;

    // **************************************************************
    // Find the parameter for which an incremental change in either
    // of both directions gives the most increae of the minimal area.
    // **************************************************************

    bestParam = -1;
    bestParamChange = 0.0;
    bestAreaChange = 0.0;

    for (i=0; i < VocalTract::NUM_PARAMS; i++)
    {
      currParamValue = tract->params[i].x;

      if (changeStep[i] > 0.0)
      {
        // Apply a POSITIVE change to parameter i.
        
        tract->params[i].x = currParamValue + changeStep[i];
//        getVowelFormants(tract, F1, F2, F3, minArea_cm2);
        minArea_cm2 = getMinAreaOutsideConstriction_cm2(tract, skipRegionStart_cm, skipRegionEnd_cm);
        deltaMinArea_cm2 = minArea_cm2 - currMinArea_cm2;

        if (deltaMinArea_cm2 > bestAreaChange)
        {
          bestParam = i;
          bestParamChange = changeStep[i];
          bestAreaChange = deltaMinArea_cm2;
        }

        // Apply a NEGATIVE change to parameter i.
        
        tract->params[i].x = currParamValue - changeStep[i];
//        getVowelFormants(tract, F1, F2, F3, minArea_cm2);
        minArea_cm2 = getMinAreaOutsideConstriction_cm2(tract, skipRegionStart_cm, skipRegionEnd_cm);
        deltaMinArea_cm2 = minArea_cm2 - currMinArea_cm2;

        if (deltaMinArea_cm2 > bestAreaChange)
        {
          bestParam = i;
          bestParamChange = -changeStep[i];
          bestAreaChange = deltaMinArea_cm2;
        }

        // Set the parameter value back to its original value.
        tract->params[i].x = currParamValue;
      }
    }

    // **************************************************************
    // Change the parameter with the best error reduction.
    // **************************************************************

    sprintf(st, "no change");

    if (bestParam != -1)
    {
      tract->params[bestParam].x+= bestParamChange;

      if (bestParamChange > 0.0)
      {
        sprintf(st, "%s up.", tract->params[bestParam].abbr.c_str());
      }
      else
      {
        sprintf(st, "%s down.", tract->params[bestParam].abbr.c_str());
      }
    }

    // Get the minimal area after the latest change.
//    getVowelFormants(tract, F1, F2, F3, minArea_cm2);
    minArea_cm2 = getMinAreaOutsideConstriction_cm2(tract, skipRegionStart_cm, skipRegionEnd_cm);

    printf("Run %d: %s. Min area=%2.2f cm^2\n", numRuns, st, minArea_cm2);
    
    // Terminate the loop when there was no improvement.
    if (bestAreaChange <= 0.0)
    {
      numRuns = MAX_RUNS;
    }

    // Update the pictures on the parent page.

    if (updateParent != NULL)
    {
      event.SetInt(UPDATE_PICTURES_AND_CONTROLS);
      wxPostEvent(updateParent, event);
      
      if (vocalTractDialog->IsShown())
      {
        vocalTractDialog->Refresh();
        vocalTractDialog->Update();
      }

      wxYield();
    }

    doContinue = progressDialog.Update(numRuns);

    numRuns++;
  }
}


// ****************************************************************************
/// Returns the mean squared difference between the current and the target
/// formant values. If a target formant value is zero, it is not included in 
/// the calculation ("don't care" value).
// ****************************************************************************

double Data::getFormantError(double currentF1, double currentF2, 
  double currentF3, double targetF1, double targetF2, double targetF3)
{
  const double EPSILON = 1.0;
  double e = 0.0;
  int numIncludedFormants = 0;

  if (targetF1 >= EPSILON)
  {
    e += (1.0 - currentF1 / targetF1) * (1.0 - currentF1 / targetF1);
    numIncludedFormants++;
  }

  if (targetF2 >= EPSILON)
  {
    e += (1.0 - currentF2 / targetF2) * (1.0 - currentF2 / targetF2);
    numIncludedFormants++;
  }

  if (targetF3 >= EPSILON)
  {
    e += (1.0 - currentF3 / targetF3) * (1.0 - currentF3 / targetF3);
    numIncludedFormants++;
  }

  if (numIncludedFormants >= 1)
  {
    e /= (double)numIncludedFormants;
  }
  else
  {
    e = 0.0;
  }

  e = sqrt(e);
  e = e*100.0;    // The result is in percent

  return e;
}


// ****************************************************************************
/// Calculates the first three formants of the given vocal tract and the 
/// minimum area of the corresponding area function. If the minimum area is 
/// below a certain threshold, the formant values may not be useful.
/// \param tract The vocal tract with adjusted variables.
/// \param F1 Returns the first formant frequency.
/// \param F2 Returns the second formant frequency.
/// \param F3 Returns the third formant frequency.
/// \param minArea_cm2 Returns the minimum area of the area function.
// ****************************************************************************

bool Data::getVowelFormants(VocalTract *tract, double &F1_Hz, double &F2_Hz, double &F3_Hz, double &minArea_cm2)
{
  const int MAX_FORMANTS = 3;
  double formantFreq[MAX_FORMANTS];
  double formantBw[MAX_FORMANTS];
  int numFormants;
  bool frictionNoise;
  bool isClosure;
  bool isNasal;
  int i;

  // Default return values.
  F1_Hz = 0.0;
  F2_Hz = 0.0;
  F3_Hz = 0.0;
  minArea_cm2 = 0.0;

  // The velo-pharyngeal port must be closed.
  if (tract->params[VocalTract::VO].x > 0.0)
  {
    tract->params[VocalTract::VO].x = 0.0;
  }

  // Calculate the vocal tract area function.
  tract->calculateAll();

  // Set the latest vocal tract geometry for the transmission line model.   
  updateTlModelGeometry(tract);

  // Find the minimum cross-sectional area.
  minArea_cm2 = 10000.0;    // = extremely high
  for (i = 0; i < VocalTract::NUM_TUBE_SECTIONS; i++)
  {
    if (tract->tubeSection[i].area < minArea_cm2)
    {
      minArea_cm2 = tract->tubeSection[i].area;
    }
  }

  // Get the formant data.
  tlModel->getFormants(formantFreq, formantBw, numFormants, MAX_FORMANTS, frictionNoise, isClosure, isNasal);
  if (numFormants < MAX_FORMANTS)
  {
    return false;
  }

  F1_Hz = formantFreq[0];
  F2_Hz = formantFreq[1];
  F3_Hz = formantFreq[2];

  return true;
}


// ****************************************************************************
/// Calculates the first three formants of the given consonantal vocal tract 
/// when it is shifted towards the given context vowel target until the minimal
/// cross-sectional area is releaseArea_cm2.
// ****************************************************************************

bool Data::getConsonantFormants(VocalTract *tract, const wxString &contextVowel, 
	double releaseArea_cm2,	double &F1_Hz, double &F2_Hz, double &F3_Hz)
{
  int i;
  double consonantParams[VocalTract::NUM_PARAMS];
  double vowelParams[VocalTract::NUM_PARAMS];
  double releaseParams[VocalTract::NUM_PARAMS];
  double releasePos;

  // ****************************************************************
  // Init the return parameters for the error case.
  // ****************************************************************
  
  F1_Hz = 0.0;
  F2_Hz = 0.0;
  F3_Hz = 0.0;

  // ****************************************************************
  // Fill in the parameter values for the consonant, vowel, and 
  // release shape.
  // ****************************************************************

  int vowelIndex = tract->getShapeIndex(contextVowel.ToStdString());
  if (vowelIndex == -1)
  {
    return false;
  }

  for (i=0; i < VocalTract::NUM_PARAMS; i++)
  {
    consonantParams[i] = tract->params[i].x;
    vowelParams[i] = tract->shapes[vowelIndex].param[i];
    releaseParams[i] = tract->params[i].neutral;   // Default for error case.
  }

  // ****************************************************************
  // Determine the vocal tract params for the release area.
  // ****************************************************************

  if (getReleaseShape(tract, consonantParams, vowelParams, releaseParams, 
    releasePos, releaseArea_cm2) == false)
  {
    // Error: Set back the original parameters in the vocal tract 
    // model and return.
    for (i=0; i < VocalTract::NUM_PARAMS; i++)
    {
      tract->params[i].x = consonantParams[i];
    }
    tract->calculateAll();

    return false;
  }
   
  // ****************************************************************
  // For the release shape, determine the first three formants.
  // ****************************************************************

  const int MAX_FORMANTS = 3;
  double formantFreq[MAX_FORMANTS];
  double formantBw[MAX_FORMANTS];
  int numFormants;
  bool frictionNoise;
  bool isClosure;
  bool isNasal;

  // Set the release shape params for the vocal tract model.
  for (i=0; i < VocalTract::NUM_PARAMS; i++)
  {
    tract->params[i].x = releaseParams[i];
  }

  // The velo-pharyngeal port must be closed.
  if (tract->params[VocalTract::VO].x > 0.0)
  {
    tract->params[VocalTract::VO].x = 0.0;
  }

  // Calculate the vocal tract area function.
  tract->calculateAll();

  // Set the latest vocal tract geometry for the transmission line model.   
  updateTlModelGeometry(tract);

  // Get the formant data.
  tlModel->getFormants(formantFreq, formantBw, numFormants, MAX_FORMANTS, frictionNoise, isClosure, isNasal);

  // Set back the original parameters in the vocal tract model.

  for (i=0; i < VocalTract::NUM_PARAMS; i++)
  {
    tract->params[i].x = consonantParams[i];
  }
  tract->calculateAll();

  // Set the final formant values.

  if (numFormants < MAX_FORMANTS)
  {
    return false;
  }

  F1_Hz = formantFreq[0];
  F2_Hz = formantFreq[1];
  F3_Hz = formantFreq[2];

  return true;
}


// ****************************************************************************
/// Returns the minimum cross-sectional area of the current vocal tract.
/// It also calculates the area function of the vocal tract first.
// ****************************************************************************

double Data::getMinArea_cm2(VocalTract *tract, double startPos_cm, double endPos_cm)
{
  int i;

  // Calculate the vocal tract area function.
  tract->calculateAll();

  // Find the minimum cross-sectional area.
  double minArea_cm2 = 10000.0;    // = extremely high

  for (i=0; i < VocalTract::NUM_CENTERLINE_POINTS; i++)
  {
    if ((tract->crossSection[i].pos >= startPos_cm) &&
        (tract->crossSection[i].pos <= endPos_cm) &&
        (tract->crossSection[i].area < minArea_cm2))
    {
      minArea_cm2 = tract->crossSection[i].area;
    }
  }

  return minArea_cm2;
}


// ****************************************************************************
/// Returns the minimum cross-sectional area of the current vocal tract outside
/// the given region.
/// It also calculates the area function of the vocal tract first.
// ****************************************************************************

double Data::getMinAreaOutsideConstriction_cm2(VocalTract *tract, double constrictionStartPos_cm, double constrictionEndPos_cm)
{
  int i;

  // Calculate the vocal tract area function.
  tract->calculateAll();

  // Find the minimum cross-sectional area.
  double minArea_cm2 = 10000.0;    // = extremely high

  for (i=0; i < VocalTract::NUM_CENTERLINE_POINTS; i++)
  {
    if (((tract->crossSection[i].pos < constrictionStartPos_cm) ||
        (tract->crossSection[i].pos > constrictionEndPos_cm)) &&
        (tract->crossSection[i].area < minArea_cm2))
    {
      minArea_cm2 = tract->crossSection[i].area;
    }
  }

  return minArea_cm2;
}


// ****************************************************************************
/// Returns an instance of the selected tube sequence (the kind of synthesis).
// ****************************************************************************

TubeSequence *Data::getSelectedTubeSequence()
{
  TubeSequence *ts = staticPhone;

  switch (synthesisType)
  {
    case SYNTHESIS_PHONE:  ts = staticPhone; break;
    case SYNTHESIS_GESMOD: ts = gesturalScore; break;
    default: break;
  }

  return ts;
}


// ****************************************************************************
/// Returns a pointer to the selected gesture in the editor, or NULL, if no 
/// gesture is selected.
// ****************************************************************************

Gesture *Data::getSelectedGesture()
{
  if ((selectedGestureType >= 0) && (selectedGestureType < GesturalScore::NUM_GESTURE_TYPES))
  {
    GestureSequence *s = &gesturalScore->gestures[selectedGestureType];
    if ((selectedGestureIndex >= 0) && (selectedGestureIndex < s->numGestures()))
    {
      return s->getGesture(selectedGestureIndex);
    }
  }
  
  return NULL;
}


// ****************************************************************************
/// Returns the currently selected glottis.
// ****************************************************************************

Glottis *Data::getSelectedGlottis()
{
  return glottis[selectedGlottis];
}

// ****************************************************************************
/// Returns the index of the selected glottis.
// ****************************************************************************

int Data::getSelectedGlottisIndex()
{
  return selectedGlottis;
}

// ****************************************************************************
/// Set the selected glottis in a save way.
// ****************************************************************************

void Data::selectGlottis(int index)
{
  if (index < 0)
  {
    index = 0;
  }
  if (index >= NUM_GLOTTIS_MODELS)
  {
    index = NUM_GLOTTIS_MODELS - 1;
  }

  selectedGlottis = index;
  // Also set the glottis for the gestural score !
  gesturalScore->glottis = glottis[index];
}


// ****************************************************************************
// Updates the geometry data in the TL-model of the vocal tract.
// ****************************************************************************

void Data::updateTlModelGeometry(VocalTract *tract)
{
  tract->getTube(&tlModel->tube);
  tlModel->tube.resetGlottisSections(0.0);
}


// ****************************************************************************
/// Updates the parameters of the vocal tract and the glottis and repaint them
/// in the dialogs.
// ****************************************************************************

void Data::updateModelsFromGesturalScore()
{
  int i;
  int numGlottisParams = (int)gesturalScore->glottis->controlParams.size();
  double tractParams[VocalTract::NUM_PARAMS];
  double glottisParams[128];

  // ****************************************************************
  // Set the parameters for the glottis and vocal tract model.
  // ****************************************************************

  gesturalScore->getParams(gesturalScoreMark_s, tractParams, glottisParams);

  for (i=0; i < VocalTract::NUM_PARAMS; i++)
  {
    gesturalScore->vocalTract->params[i].x = tractParams[i];
  }
  gesturalScore->vocalTract->calculateAll();

  for (i=0; i < numGlottisParams; i++)
  {
    gesturalScore->glottis->controlParams[i].x = glottisParams[i];
  }
  gesturalScore->glottis->calcGeometry();

  // ****************************************************************
  // Update the pictures in the glottis/vocal tract dialogs.
  // ****************************************************************

  GlottisDialog *glottisDialog = GlottisDialog::getInstance();
  if (glottisDialog->IsShown())
  {
    glottisDialog->updateWidgets();
    glottisDialog->Refresh();
    glottisDialog->Update();
  }

  VocalTractDialog *vocalTractDialog = VocalTractDialog::getInstance(NULL);
  if (vocalTractDialog->IsShown())
  {
    vocalTractDialog->updateWidgets();
    vocalTractDialog->Refresh();
    vocalTractDialog->Update();
  }
}


// ****************************************************************************
// Returns the value of the selected quantity on the TDS page in the given
// tube section.
// ****************************************************************************

void Data::getTubeSectionQuantity(TdsModel *model, int sectionIndex, double &leftValue, double &rightValue)
{
  leftValue = 0.0;
  rightValue = 0.0;

  if (quantity == QUANTITY_FLOW)
  {
    model->getSectionFlow(sectionIndex, leftValue, rightValue);
  }
  else

  if (quantity == QUANTITY_PRESSURE)
  {
    leftValue = model->getSectionPressure(sectionIndex);
    rightValue = leftValue;
  }
  else

  if (quantity == QUANTITY_AREA)
  {
    if ((sectionIndex >= 0) && (sectionIndex < Tube::NUM_SECTIONS))
    {
      leftValue = model->tubeSection[sectionIndex].area;
      rightValue = leftValue;
    }
  }
  else

  if (quantity == QUANTITY_VELOCITY)
  {
    if ((sectionIndex >= 0) && (sectionIndex < Tube::NUM_SECTIONS))
    {
      model->getSectionFlow(sectionIndex, leftValue, rightValue);
      double area = model->tubeSection[sectionIndex].area;
      if (area < TdsModel::MIN_AREA_CM2)
      {
        area = TdsModel::MIN_AREA_CM2;
      }
      leftValue /= area;
      rightValue /= area;
    }
  }
}


// ****************************************************************************
/// Determines the geometric vocal tract parameters from the phonetic 
/// parameters.
// ****************************************************************************

void Data::phoneticParamsToVocalTract()
{
  int i;
  double x;

  // ****************************************************************
  // Interpolate the vocal tract shape between [i], [a], and [u].
  // ****************************************************************

  int a_index = vocalTract->getShapeIndex("a");
  int i_index = vocalTract->getShapeIndex("i");
  int u_index = vocalTract->getShapeIndex("u");

  if ((a_index != -1) && (i_index != -1) && (u_index != -1))
  {
    double frontness = phoneticParamValue[TONGUE_FRONTNESS];
    double height = phoneticParamValue[TONGUE_HEIGHT];

    for (i=0; i < VocalTract::NUM_PARAMS; i++)
    {
      x = frontness * vocalTract->shapes[i_index].param[i] + 
        (1.0-frontness) * vocalTract->shapes[u_index].param[i];

      x = height*x + (1.0-height)*vocalTract->shapes[a_index].param[i];
      
      vocalTract->params[i].x = x;
    }
  }

  // ****************************************************************
  // Set the velum position and the rounding of the lips.
  // ****************************************************************

  double vel = phoneticParamValue[VELUM_POSITION];
  vocalTract->params[VocalTract::VO].x = vocalTract->params[VocalTract::VO].min + 
    vel*(vocalTract->params[VocalTract::VO].max - vocalTract->params[VocalTract::VO].min);

  double rounding = phoneticParamValue[LIP_ROUNDING];
  vocalTract->params[VocalTract::LP].x = vocalTract->params[VocalTract::LP].min + 
    rounding*(vocalTract->params[VocalTract::LP].max - vocalTract->params[VocalTract::LP].min);

  vocalTract->params[VocalTract::LD].x = 1.2 - rounding*1.0;

  // ****************************************************************
  // Superimpose the consonantal constrictions.
  // ****************************************************************

  // Coordinates of the base vowel in the 2D-vowel subspace
  double alphaTongue;
  double betaTongue;
  double alphaLips;
  double betaLips;

  double vowelParams[VocalTract::NUM_PARAMS];
  double bParams[VocalTract::NUM_PARAMS];
  double dParams[VocalTract::NUM_PARAMS];
  double gParams[VocalTract::NUM_PARAMS];

  for (i=0; i < VocalTract::NUM_PARAMS; i++)
  {
    vowelParams[i] = vocalTract->params[i].x;
  }

  // Get the coordinates of the vowel shape in the 2D-vowel subspace.
  GesturalScore::mapToVowelSubspace(vocalTract, vowelParams, alphaTongue, betaTongue, alphaLips, betaLips);
  GesturalScore::limitVowelSubspaceCoord(alphaTongue, betaTongue, alphaLips, betaLips);

  // Get the context-dependent consonsnt targets for /b/, /d/, /g/.
  GesturalScore::getContextDependentConsonant(vocalTract, "ll-labial-closure", 
    alphaTongue, betaTongue, alphaLips, betaLips, bParams);

  GesturalScore::getContextDependentConsonant(vocalTract, "tt-alveolar-closure", 
    alphaTongue, betaTongue, alphaLips, betaLips, dParams);

  GesturalScore::getContextDependentConsonant(vocalTract, "tb-velar-closure", 
    alphaTongue, betaTongue, alphaLips, betaLips, gParams);

  // Get the parameter vector difference between the consonant shapes and the vowel shape.
  
  double bDeltaParams[VocalTract::NUM_PARAMS];
  double dDeltaParams[VocalTract::NUM_PARAMS];
  double gDeltaParams[VocalTract::NUM_PARAMS];

  for (i=0; i < VocalTract::NUM_PARAMS; i++)
  {
    bDeltaParams[i] = bParams[i] - vowelParams[i];
    dDeltaParams[i] = dParams[i] - vowelParams[i];
    gDeltaParams[i] = gParams[i] - vowelParams[i];
  }

  double b_degree = phoneticParamValue[LABIAL_CONSTRICTION_DEGREE];
  double d_degree = phoneticParamValue[ALVEOLAR_CONSTRICTION_DEGREE];
  double g_degree = phoneticParamValue[VELAR_CONSTRICTION_DEGREE];
  
  double alphaB, alphaD, alphaG;
  double alphaSum;
  double sum;
  const double EPSILON = 0.000001;

  // ****************************************************************
  // Mix the differences to the three shapes for /b,d,g/ depending
  // on the parameter under consideration.
  // ****************************************************************

  for (i=0; i < VocalTract::NUM_PARAMS; i++)
  {
    alphaB = b_degree;
    alphaD = d_degree;
    alphaG = g_degree;
    
    alphaSum = alphaB + alphaD + alphaG;

    // The sum of the consonantal contributions may be not higher than 1.0.
    if (alphaSum > 1.0)
    {
      // For the parameters LP, LD, the consonant "b" has priority.
      if ((i == VocalTract::LP) || (i == VocalTract::LD))
      {
        // Reduce the contribution of "d" and "g".
        sum = alphaD + alphaG;
        if (sum < EPSILON)
        {
          sum = EPSILON;
        }
        alphaD*= (1.0 - alphaB) / sum;
        alphaG*= (1.0 - alphaB) / sum;
      }
      else

      // For the parameters TTX, TTY, TS4, the consonant "d" has priority.
      if ((i == VocalTract::TTX) || (i == VocalTract::TTY) || (i == VocalTract::TS3))
      {
        // Reduce the contribution of "b" and "g".
        sum = alphaB + alphaG;
        if (sum < EPSILON)
        {
          sum = EPSILON;
        }
        alphaB*= (1.0 - alphaD) / sum;
        alphaG*= (1.0 - alphaD) / sum;
      }
      else

      // For the parameters TCX, TCY, TS2, the consonant "g" has priority.
      if ((i == VocalTract::TCX) || (i == VocalTract::TCY) || (i == VocalTract::TS2))
      {
        // Reduce the contribution of "b" and "d".
        sum = alphaB + alphaD;
        if (sum < EPSILON)
        {
          sum = EPSILON;
        }
        alphaB*= (1.0 - alphaG) / sum;
        alphaD*= (1.0 - alphaG) / sum;
      }
      else

      // Reduce all 3 contributions by equal amounts.
      {
        sum = alphaSum;
        if (sum < EPSILON)
        {
          sum = EPSILON;
        }
        alphaB/= sum;
        alphaD/= sum;
        alphaG/= sum;
      }
    }

    vocalTract->params[i].x = vowelParams[i] + 
      alphaB*bDeltaParams[i] + alphaD*dDeltaParams[i] + alphaG*gDeltaParams[i];
  }

  // ****************************************************************

  vocalTract->calculateAll();
}


// ****************************************************************************
/// Normalize the audio amplitude in the given track to -1 dB below the max.
// ****************************************************************************

void Data::normalizeAudioAmplitude(int trackIndex)
{
  if ((trackIndex < 0) || (trackIndex >= NUM_TRACKS))
  {
    return;
  }

  int i = 0;
  int maxValue = 0;
  int firstSample = 0;
  int lastSample = track[trackIndex]->N - 1;

  for (i = firstSample; i < lastSample; i++)
  {
    if (abs(track[trackIndex]->getValue(i)) > maxValue)
    {
      maxValue = abs(track[trackIndex]->getValue(i));
    }
  }

  if (maxValue >= 1)
  {
    // 0.89 corresponds to -1 dB.
    double factor = 0.89*32767.0 / maxValue;
    for (i = firstSample; i < lastSample; i++)
    {
      track[trackIndex]->setValue(i, (int)(factor*track[trackIndex]->getValue(i)));
    }
  }
}


// ****************************************************************************
/// Normalize the audio amplitude in the given signal to -1 dB below the max.
// ****************************************************************************

void Data::normalizeAudioAmplitude(vector<double>& audio)
{
  int numAudioSamples = (int)audio.size();

  // Normalize the audio signal.

  double maxValue = 0.0;
  int i = 0;

  for (i = 0; i < numAudioSamples; i++)
  {
    if (fabs(audio[i]) > maxValue)
    {
      maxValue = fabs(audio[i]);
    }
  }

  if (maxValue >= 0.00000001)
  {
    // 0.89 corresponds to -1 dB.
    double factor = 0.89 / maxValue;
    for (i = 0; i < numAudioSamples; i++)
    {
      audio[i] *= factor;
    }
  }
}


// ****************************************************************************
// Save the given audio signal as a WAV file.
// ****************************************************************************

bool Data::saveWavFile(const wxString& fileName, vector<double>& audio)
{
  if (fileName.empty())
  {
    wxPrintf("Error in Data::saveWavFile(): File name is empty.");
    return false;
  }

  int i;
  int numSamples = (int)audio.size();
  AudioFile<double> audioFile;
  audioFile.setAudioBufferSize(1, numSamples);
  audioFile.setBitDepth(16);
  audioFile.setSampleRate(AUDIO_SAMPLING_RATE_HZ);

  for (i = 0; i < numSamples; i++)
  {
    audioFile.samples[0][i] = audio[i];
  }

  if (audioFile.save(fileName.ToStdString()) == false)
  {
    printf("Error in Data::saveWavFile(): The WAV file could not be saved!\n");
    return false;
  }

  return true;
}


// ****************************************************************************
/// Load all data that comprise a "speaker".
// ****************************************************************************

bool Data::loadSpeaker(const wxString &fileName)
{
  speakerFileName = fileName;

  // ****************************************************************
  // Load the XML data.
  // ****************************************************************

  vector<XmlError> xmlErrors;
  XmlNode *rootNode = xmlParseFile(fileName.ToStdString(), "speaker", &xmlErrors);
  if (rootNode == NULL)
  {
    xmlPrintErrors(xmlErrors);
    return false;
  }

  // ****************************************************************
  // Load the data for the glottis models.
  // ****************************************************************

  XmlNode *glottisModelsNode = rootNode->getChildElement("glottis_models");
  if (glottisModelsNode != NULL)
  {
    int i;
    XmlNode *glottisNode;

    for (i=0; (i < (int)glottisModelsNode->childElement.size()) && (i < NUM_GLOTTIS_MODELS); i++)
    {
      glottisNode = glottisModelsNode->childElement[i];
      if (glottisNode->getAttributeString("type") == glottis[i]->getName())
      {
        if (glottisNode->getAttributeInt("selected") == 1)
        {
          selectGlottis(i);
        }
        if (glottis[i]->readFromXml(*glottisNode) == false)
        {
          wxPrintf("Error: Failed to read glottis data for glottis model %d!\n", i);
        }
      }
      else
      {
        wxPrintf("Error: The type of the glottis model %d in the speaker file is '%s' "
          "but should be '%s'!\n", i, 
          glottisNode->getAttributeString("type").c_str(), 
          glottis[i]->getName().c_str());
      }
    }
  }
  else
  {
    wxPrintf("Warning: No glottis model data found in the speaker file %s!\n", fileName.c_str());
  }

  // Free the memory of the XML tree !
  delete rootNode;

  // ****************************************************************
  // Load the vocal tract anatomy and vocal tract shapes.
  // ****************************************************************

  try
  {
    vocalTract->readFromXml(fileName.ToStdString());
    vocalTract->calculateAll();
  }
  catch (std::string st)
  {
    wxMessageBox(wxString(st), 
      wxString("Error reading the anatomy data from ") + fileName + wxString("."));
  }

  return true;
}


// ****************************************************************************
/// Save all data that comprise a "speaker".
// ****************************************************************************

bool Data::saveSpeaker(const wxString &fileName)
{
  speakerFileName = fileName;

  ofstream os(fileName.ToStdString());
  int i;

  if (!os)
  {
    wxMessageBox(wxString("Could not open ") + fileName + wxString(" for writing."),
      wxString("Error!"));
    return false;
  }

  // ****************************************************************
  // Open the <speaker> element and write the data.
  // ****************************************************************

  os << "<speaker>" << endl;
  
  vocalTract->writeToXml(os, 2);

  // Output the data for the glottis models.

  os << "  <glottis_models>" << endl;
  
  for (i=0; i < NUM_GLOTTIS_MODELS; i++)
  {
    glottis[i]->writeToXml(os, 4, (i == (int)selectedGlottis));
  }
  
  os << "  </glottis_models>" << endl;

  os << "</speaker>" << endl;

  // ****************************************************************
  // Close the file
  // ****************************************************************

  os.close();

  return true;
}


// ****************************************************************************
/// Performs the F0 estimation.
// ****************************************************************************

void Data::estimateF0(wxWindow *parent, int trackIndex)
{
  // ****************************************************************
  // If no proper track index is given, let the user select for which 
  // track to estimate the F0 curve.
  // ****************************************************************

  if ((trackIndex < 0) || (trackIndex >= NUM_TRACKS))
  {
    trackIndex = selectTrack(parent, wxString("For which track do you want to estimate F0?"));
    if (trackIndex == -1)
    {
      return;
    }
  }

  // ****************************************************************
  // Determine the "region of interest", where the signal is != 0.
  // ****************************************************************

  int N = track[trackIndex]->N;
  int firstRoiSample = 0;
  int lastRoiSample = N-1;
  int numRoiSamples = 0;

  while ((firstRoiSample < N) && (track[trackIndex]->x[firstRoiSample] == 0))
  {
    firstRoiSample++;
  }

  while ((lastRoiSample >= firstRoiSample) && (track[trackIndex]->x[lastRoiSample] == 0))
  {
    lastRoiSample--;
  }

  // Is the whole signal zero ?
  if (firstRoiSample >= lastRoiSample)
  {
    wxPrintf("Signal is all zero -> no need for F0 estimation.\n");
    return;
  }

  numRoiSamples = lastRoiSample - firstRoiSample;

  wxPrintf("F0 estimation started...\n");
  wxPrintf("The region of interest is %2.3f to %2.3f s.\n",
    (double)firstRoiSample / (double)AUDIO_SAMPLING_RATE_HZ, 
    (double)(firstRoiSample + numRoiSamples) / (double)AUDIO_SAMPLING_RATE_HZ);

  // ****************************************************************
  // Do the estimation with a progress dialog.
  // ****************************************************************

  // Do the pre-processing for F0-estimation.
  
  f0EstimatorYin->init(track[trackIndex], firstRoiSample, numRoiSamples);
  int numChunkSamples = AUDIO_SAMPLING_RATE_HZ / 2;

  // Show the progress dialog

  wxGenericProgressDialog dialog("Please wait", "Please wait for the F0 estimation to finish.",
    numRoiSamples/numChunkSamples, parent, 
    wxPD_CAN_ABORT | wxPD_APP_MODAL | wxPD_ELAPSED_TIME | wxPD_AUTO_HIDE);
  
  // Process chunks of numChunkSamples samples.
  
  bool finished = false;
  bool cont = true;
  int chunkCounter = 1;

  do
  {
    finished = f0EstimatorYin->processChunk(numChunkSamples);
    cont = dialog.Update(chunkCounter);
    chunkCounter++;
  } while ((finished == false) && (cont));

  // Do the post-processing and get the vector of F0 estimates.

  if (finished)
  {
    f0Signal[trackIndex] = f0EstimatorYin->finish();
    // Take over the time step of this F0 signal.
    f0TimeStep_s = f0EstimatorYin->timeStep_s;
    wxPrintf("F0 estimation finished.\n");
  }
  else
  {
    wxPrintf("F0 estimation aborted.\n");
  }

}


// ****************************************************************************
/// Performs the voice quality estimation.
// ****************************************************************************

void Data::estimateVoiceQuality(wxWindow *parent, int trackIndex)
{
  // ****************************************************************
  // If no proper track index is given, let the user select for which 
  // track to estimate the F0 curve.
  // ****************************************************************

  if ((trackIndex < 0) || (trackIndex >= NUM_TRACKS))
  {
    trackIndex = selectTrack(parent, wxString("For which track do you want to estimate the voice quality?"));
    if (trackIndex == -1)
    {
      return;
    }
  }

  // ****************************************************************
  // Determine the "region of interest", where the signal is != 0.
  // ****************************************************************

  int N = track[trackIndex]->N;
  int firstRoiSample = 0;
  int lastRoiSample = N-1;
  int numRoiSamples = 0;

  // Skip the samples that are zero at the beginning.
  while ((firstRoiSample < N) && (track[trackIndex]->x[firstRoiSample] == 0))
  {
    firstRoiSample++;
  }

  // Skip the samples that are zero at the end.
  while ((lastRoiSample >= firstRoiSample) && (track[trackIndex]->x[lastRoiSample] == 0))
  {
    lastRoiSample--;
  }

  // Is the whole signal zero ?
  if (firstRoiSample >= lastRoiSample)
  {
    wxPrintf("Signal is all zero -> no need for voice quality estimation.\n");
    return;
  }

  numRoiSamples = lastRoiSample - firstRoiSample;

  wxPrintf("Voice quality estimation started...\n");
  wxPrintf("The region of interest is %2.3f to %2.3f s.\n",
    (double)firstRoiSample / (double)AUDIO_SAMPLING_RATE_HZ, 
    (double)(firstRoiSample + numRoiSamples) / (double)AUDIO_SAMPLING_RATE_HZ);

  // ****************************************************************
  // Do the estimation with a progress dialog.
  // ****************************************************************

  // Do the initialization.
  
  voiceQualityEstimator->init(track[trackIndex], firstRoiSample, numRoiSamples);
  int numChunkSamples = AUDIO_SAMPLING_RATE_HZ / 2;

  // Show the progress dialog

  wxGenericProgressDialog dialog("Please wait", "Please wait for the voice quality estimation to finish.",
    numRoiSamples/numChunkSamples, parent, 
    wxPD_CAN_ABORT | wxPD_APP_MODAL | wxPD_ELAPSED_TIME | wxPD_AUTO_HIDE);
  
  // Process chunks of numChunkSamples samples.
  
  bool finished = false;
  bool cont = true;
  int chunkCounter = 1;

  do
  {
    finished = voiceQualityEstimator->processChunk(numChunkSamples);
    cont = dialog.Update(chunkCounter);
    chunkCounter++;
  } while ((finished == false) && (cont));

  // Do the post-processing and get the vector of voice quality estimates.

  if (finished)
  {
    voiceQualitySignal[trackIndex] = voiceQualityEstimator->finish();
    // Take over the time step of this F0 signal.
    voiceQualityTimeStep_s = voiceQualityEstimator->timeStep_s;
    wxPrintf("Voice quality estimation finished.\n");
  }
  else
  {
    wxPrintf("Voice quality estimation aborted.\n");
  }
}


// ****************************************************************************
/// Determines the natural frequency and the F0 derivative with respect to
/// tension Q of the triangular glottis model.
// ****************************************************************************

void Data::calcTriangularGlottisF0Params()
{
  wxPrintf("\n");
  wxPrintf("Calculation of the F0 parameters for the triangular glottis model\n");
  wxPrintf("=================================================================\n");

  int duration_samples = 0;
  double duration_s = 0.0;
  TriangularGlottis *g = (TriangularGlottis*)glottis[TRIANGULAR_GLOTTIS];
  int i;
  vector<double> audio;

  // ****************************************************************
  // Clear the main track.
  // ****************************************************************

  track[MAIN_TRACK]->setZero();

  // ****************************************************************
  // Keep in mind the control parameters for F0 and pressure.
  // ****************************************************************

  double storedControlParams[Glottis::MAX_CONTROL_PARAMS];
  for (i = 0; i < (int)g->controlParams.size(); i++)
  {
    storedControlParams[i] = g->controlParams[i].x;
  }


  // ****************************************************************
  // Get the produced F0 for Q=1 and P=800 Pa.
  // ****************************************************************

  wxPrintf("Synthesizing vowel for Q = 1 and P = 800 Pa ");

  g->staticParams[TriangularGlottis::NATURAL_F0].x = 100.0;
  g->staticParams[TriangularGlottis::F0_DIV_Q].x = 100.0;

  g->controlParams[TriangularGlottis::FREQUENCY].x = 100.0;
  g->controlParams[TriangularGlottis::PRESSURE].x = 8000.0;

  Synthesizer::synthesizeStaticPhoneme(g, vocalTract, tdsModel, true, true, audio);
  Synthesizer::copySignal(audio, *track[MAIN_TRACK], 0);
  duration_samples = (int)audio.size();
  duration_s = (double)duration_samples / AUDIO_SAMPLING_RATE_HZ;

  // Play the vowel for auditory checking.
  waveStartPlaying(track[MAIN_TRACK]->x, track[MAIN_TRACK]->N, false);
  wxThread::Sleep((int)(duration_samples * 1000 / AUDIO_SAMPLING_RATE_HZ));
  waveStopPlaying();

  // Determine the F0 in the middle of the synthesized vowel

  f0EstimatorYin->init(track[MAIN_TRACK], 0, duration_samples);
  f0EstimatorYin->processChunk(duration_samples);
  f0Signal[MAIN_TRACK] = f0EstimatorYin->finish();
  f0TimeStep_s = f0EstimatorYin->timeStep_s;
  i = (int)(0.5*duration_s / f0TimeStep_s);

  double f0Neutral = f0Signal[MAIN_TRACK][i];
  wxPrintf("F0 = %2.1f Hz\n\n", f0Neutral);

  // ****************************************************************
  // Get the produced F0 for Q = 0.7 and P = 800 Pa.
  // ****************************************************************

  wxPrintf("Synthesizing vowel for Q = 0.7 and P = 800 Pa ");

  g->staticParams[TriangularGlottis::NATURAL_F0].x = 100.0;
  g->staticParams[TriangularGlottis::F0_DIV_Q].x = 100.0;

  g->controlParams[TriangularGlottis::FREQUENCY].x = 70.0;
  g->controlParams[TriangularGlottis::PRESSURE].x = 8000.0;

  Synthesizer::synthesizeStaticPhoneme(g, vocalTract, tdsModel, true, true, audio);
  Synthesizer::copySignal(audio, *track[MAIN_TRACK], 0);
  duration_samples = (int)audio.size();
  duration_s = (double)duration_samples / AUDIO_SAMPLING_RATE_HZ;

  // Play the vowel for auditory checking.
  waveStartPlaying(track[MAIN_TRACK]->x, track[MAIN_TRACK]->N, false);
  wxThread::Sleep((int)(duration_s * 1000));
  waveStopPlaying();

  // Determine the F0 in the middle of the synthesized vowel

  f0EstimatorYin->init(track[MAIN_TRACK], 0, duration_samples);
  f0EstimatorYin->processChunk(duration_samples);
  f0Signal[MAIN_TRACK] = f0EstimatorYin->finish();
  f0TimeStep_s = f0EstimatorYin->timeStep_s;
  i = (int)(0.5*duration_s / f0TimeStep_s);

  double f0AtLowQ = f0Signal[MAIN_TRACK][i];
  wxPrintf("F0 = %2.1f Hz\n\n", f0AtLowQ);

  // ****************************************************************
  // Get the produced F0 for Q = 1.3 and P = 800 Pa.
  // ****************************************************************

  wxPrintf("Synthesizing vowel for Q = 1.3 and P = 800 Pa ");

  g->staticParams[TriangularGlottis::NATURAL_F0].x = 100.0;
  g->staticParams[TriangularGlottis::F0_DIV_Q].x = 100.0;

  g->controlParams[TriangularGlottis::FREQUENCY].x = 130.0;
  g->controlParams[TriangularGlottis::PRESSURE].x = 8000.0;

  Synthesizer::synthesizeStaticPhoneme(g, vocalTract, tdsModel, true, true, audio);
  Synthesizer::copySignal(audio, *track[MAIN_TRACK], 0);
  duration_samples = (int)audio.size();
  duration_s = (double)duration_samples / AUDIO_SAMPLING_RATE_HZ;

  // Play the vowel for auditory checking.
  waveStartPlaying(track[MAIN_TRACK]->x, track[MAIN_TRACK]->N, false);
  wxThread::Sleep((int)(duration_s * 1000));
  waveStopPlaying();

  // Determine the F0 in the middle of the synthesized vowel

  f0EstimatorYin->init(track[MAIN_TRACK], 0, duration_samples);
  f0EstimatorYin->processChunk(duration_samples);
  f0Signal[MAIN_TRACK] = f0EstimatorYin->finish();
  f0TimeStep_s = f0EstimatorYin->timeStep_s;
  i = (int)(0.5*duration_s / f0TimeStep_s);

  double f0AtHighQ = f0Signal[MAIN_TRACK][i];
  wxPrintf("F0 = %2.1f Hz\n\n", f0AtHighQ);


  // ****************************************************************
  // Calculate the slopes and set the values for the glottis model.
  // ****************************************************************

  double f0DivQ = (f0AtHighQ - f0AtLowQ) / 0.6;

  // Make some plausibility checks: All F0 values must be > 0 (=voiced)
  // and dF0/dQ must be positive.

  const double EPSILON = 0.1;
  if ((f0Neutral > EPSILON) && (f0AtHighQ > EPSILON) && (f0AtLowQ > EPSILON) && (f0DivQ >= 0.0))
  {
    wxPrintf("The following static parameters will be set for the triangular glottis model:\n");
    wxPrintf("Natural F0: %2.1f  dF0/dQ: %2.2f\n",
      f0Neutral, f0DivQ);

    g->staticParams[TriangularGlottis::NATURAL_F0].x = f0Neutral;
    g->staticParams[TriangularGlottis::F0_DIV_Q].x = f0DivQ;
  }
  else
  {
    wxPrintf("The measured F0 values or slopes are not plausible. "
      "The values are NOT set for the triangular glottis model!\n");
  }

  // ****************************************************************
  // Reset the original parameter values for F0 and pressure.
  // ****************************************************************

  for (i = 0; i < (int)g->controlParams.size(); i++)
  {
    g->controlParams[i].x = storedControlParams[i];
  }
}

// ****************************************************************************
// Create the stimuli for the study that matches the vocal tract wall parameters
// to recordings of voicebars of human subjects.
// ****************************************************************************

void Data::experiment1WallParameters(wxWindow *updateParent)
{
  const int NUM_VC_ITEMS = 24;
  const wxString VOWEL[NUM_VC_ITEMS] = 
    { "a", "a", "a", "e", "e", "e", "i", "i", "i", "o", "o", "o", 
      "u", "u", "u", "E:", "E:", "E:", "2", "2", "2", "y", "y", "y" };
  
  const wxString CONSONANT[NUM_VC_ITEMS] =
    { "b", "d", "g", "b", "d", "g", "b", "d", "g", "b", "d", "g", 
      "b", "d", "g", "b", "d", "g", "b", "d", "g", "b", "d", "g" };

  const wxString ITEM_NAMES[NUM_VC_ITEMS] =
    { "aba", "ada", "aga", "ebe", "ede", "ege", 
      "ibi", "idi", "igi", "obo", "odo", "ogo",
      "ubu", "udu", "ugu", "aebae", "aedae", "aegae",
      "oeboe", "oedoe", "oegoe", "uebue", "uedue", "uegue" };

  // This is the target folder, which must end with "//".
  const wxString FOLDER_NAME = 
    "C://Arbeit//Forschungsprojekte-VocalTractLab//Schallabstrahlung-von-der-Haut-2024//"
//    "Speaker1-Dominik-Schaefer-103Hz//Simulations//";
//    "Speaker2-Peter-Birkholz-111Hz//Simulations//";
//    "Speaker3-Arne-Lukas-Fietkau-115Hz//Simulations//";
//    "Speaker4-Christian-Kleiner-101Hz//Simulations//";
//    "Speaker5-Patrick-Haesner-127Hz//Simulations//";
      "Speaker6-Simon-Sieben-130Hz//Simulations//";

  wxCommandEvent event(updateRequestEvent);
  event.SetInt(REFRESH_PICTURES);
  wxString fileName;
  int itemIndex;
  int conditionIndex;

  // Save only few relevant parameters to the TXT files.
  gesturalScore->glottis->saveReducedParamsSet = true;

  const int NUM_CONDITIONS = 27;
  const int NUM_PARAMS = 5;

  // Values for Speaker1 - Dominik Schaefer:
//  const double REF_F0 = 103.0;
//  const double REF_ABD = 0.6;

  // Values for Speaker2 - Peter Birkholz:
//  const double REF_F0 = 111.0;
//  const double REF_ABD = 0.8;

  // Values for Speaker3 - Arne-Lukas Fietkau:
//  const double REF_F0 = 115.0;
//  const double REF_ABD = 0.2;

  // Values for Speaker4 - Christian Kleiner:
//  const double REF_F0 = 101.0;
//  const double REF_ABD = 0.9;

  // Values for Speaker5 - Patrick H�sner:
//  const double REF_F0 = 127.0;
//  const double REF_ABD = 0.3;

  // Values for Speaker6 - Simon Sieben:
  const double REF_F0 = 130.0;
  const double REF_ABD = 0.6;

  // Parameter order: K (cgs), B (cgs), M (cgs), f0 (Hz), abduction (mm)
  const double PARAMS[NUM_CONDITIONS][NUM_PARAMS] =
  {
    { 30000, 800, 1.0, REF_F0, REF_ABD },
    { 30000, 800, 1.5, REF_F0, REF_ABD },
    { 30000, 800, 2.0, REF_F0, REF_ABD },
    { 30000, 1400, 1.0, REF_F0, REF_ABD },
    { 30000, 1400, 1.5, REF_F0, REF_ABD },
    { 30000, 1400, 2.0, REF_F0, REF_ABD },
    { 30000, 2000, 1.0, REF_F0, REF_ABD },
    { 30000, 2000, 1.5, REF_F0, REF_ABD },
    { 30000, 2000, 2.0, REF_F0, REF_ABD },

    { 65000, 800, 1.0, REF_F0, REF_ABD },
    { 65000, 800, 1.5, REF_F0, REF_ABD },
    { 65000, 800, 2.0, REF_F0, REF_ABD },
    { 65000, 1400, 1.0, REF_F0, REF_ABD },
    { 65000, 1400, 1.5, REF_F0, REF_ABD },
    { 65000, 1400, 2.0, REF_F0, REF_ABD },
    { 65000, 2000, 1.0, REF_F0, REF_ABD },
    { 65000, 2000, 1.5, REF_F0, REF_ABD },
    { 65000, 2000, 2.0, REF_F0, REF_ABD },

    { 100000, 800, 1.0, REF_F0, REF_ABD },
    { 100000, 800, 1.5, REF_F0, REF_ABD },
    { 100000, 800, 2.0, REF_F0, REF_ABD },
    { 100000, 1400, 1.0, REF_F0, REF_ABD },
    { 100000, 1400, 1.5, REF_F0, REF_ABD },
    { 100000, 1400, 2.0, REF_F0, REF_ABD },
    { 100000, 2000, 1.0, REF_F0, REF_ABD },
    { 100000, 2000, 1.5, REF_F0, REF_ABD },
    { 100000, 2000, 2.0, REF_F0, REF_ABD }
  };


  // ****************************************************************
  // Do the simulations for all conditions and save the resulting 
  // files.
  // ****************************************************************

  for (conditionIndex = 0; conditionIndex < NUM_CONDITIONS; conditionIndex++)
  {
    double K_cgs = PARAMS[conditionIndex][0];
    double B_cgs = PARAMS[conditionIndex][1];
    double M_cgs = PARAMS[conditionIndex][2];
    double f0_Hz = PARAMS[conditionIndex][3];
    double abduction_mm = PARAMS[conditionIndex][4];

    for (itemIndex = 0; itemIndex < NUM_VC_ITEMS; itemIndex++)
    {
      experiment1CreateVCV(FOLDER_NAME, ITEM_NAMES[itemIndex], VOWEL[itemIndex], 
        CONSONANT[itemIndex], K_cgs, B_cgs, M_cgs, f0_Hz, abduction_mm);

      // Update the screen.
      wxPostEvent(updateParent, event);
      wxYield();
    }
  }
}


// ****************************************************************************
// Create a gestural score for a VCV sequence with a dfined constant pitch,
// glottal shape, and wall damping parameter.
// ****************************************************************************

void Data::experiment1CreateVCV(wxString folderName, wxString itemName, wxString vowel, wxString consonant,
  double K_cgs, double B_cgs, double M_cgs, double f0_Hz, double abduction_mm)
{
  // ****************************************************************
  // Create the target file name from the parameters and the name of
  // the glottal gesture.
  // ****************************************************************

  wxString glottalGestureName;
  wxString fileName;

  glottalGestureName.Printf("abduction-%2.1fmm", abduction_mm);
  fileName.Printf("%s%s_K=%d_R=%d_M=%2.1f_f0=%d_abd=%2.1f.txt",
    folderName.c_str(), itemName.c_str(),
    (int)K_cgs, (int)B_cgs, M_cgs, (int)f0_Hz, abduction_mm);

  // ****************************************************************
  // Generate a segment sequence for the VCV utterance.
  // ****************************************************************

  segmentSequence->clear();
  segmentSequence->appendSegment("", 0.050);
  segmentSequence->appendSegment(vowel.ToStdString(), 0.100);
  segmentSequence->appendSegment(consonant.ToStdString(), 0.100);
  segmentSequence->appendSegment(vowel.ToStdString(), 0.150);

  // ****************************************************************
  // Create and modify the gestural score from the segment sequence.
  // ****************************************************************

  gesturalScore->createFromSegmentSequence(segmentSequence);
  double totalLength_s = gesturalScore->gestures[GesturalScore::VOWEL_GESTURE].getDuration_s();
  
  Gesture g;

  // Create a single f0 gesture.

  g.duration_s = totalLength_s;
  g.sVal = "";
  g.dVal = hertzToSemitones(f0_Hz);
  g.slope = 0.0;
  g.tau_s = 0.005;
  g.neutral = false;

  gesturalScore->gestures[GesturalScore::F0_GESTURE].clear();
  gesturalScore->gestures[GesturalScore::F0_GESTURE].appendGesture(g);

  // Create a single glottal shape gesture.

  g.duration_s = totalLength_s;
  g.sVal = glottalGestureName.ToStdString();
  g.dVal = 0.0;
  g.slope = 0.0;
  g.tau_s = 0.005;
  g.neutral = false;

  gesturalScore->gestures[GesturalScore::GLOTTAL_SHAPE_GESTURE].clear();
  gesturalScore->gestures[GesturalScore::GLOTTAL_SHAPE_GESTURE].appendGesture(g);

  // Important !!!
  gesturalScore->calcCurves();

  // ****************************************************************
  // Synthesize the utterance while writing the file with the 
  // glottal signals.
  // ****************************************************************

  // Overwrite the global wall parameters.

  WALL_MASS_PER_UNIT_AREA_CGS = M_cgs;
  WALL_DAMPING_PER_UNIT_AREA_CGS = B_cgs;
  WALL_STIFFNESS_PER_UNIT_AREA_CGS = K_cgs;

  // Synthesize the audio.

  Synthesizer* synthesizer = new Synthesizer();
  vector<double> audio;

  bool ok = synthesizer->synthesizeGesturalScore(gesturalScore, tdsModel, audio, fileName, true);

  // Clean up.
  delete synthesizer;

  int numAudioSamples = (int)audio.size();

  if (!ok)
  {
    wxPrintf("Error: The misc file could not be saved!\n");
  }

  // Normalize the audio signal.

  double maxValue = 0.0;
  int i = 0;

  for (i = 0; i < numAudioSamples; i++)
  {
    if (fabs(audio[i]) > maxValue)
    {
      maxValue = fabs(audio[i]);
    }
  }

  if (maxValue >= 0.00000001)
  {
    // 0.89 corresponds to -1 dB.
    double factor = 0.89 / maxValue;
    for (i = 0; i < numAudioSamples; i++)
    {
      audio[i] *= factor;
    }
  }

  // ****************************************************************
  // Copy the audio signal the the main track and play.
  // ****************************************************************

  track[MAIN_TRACK]->setZero();
  for (i = 0; i < numAudioSamples; i++)
  {
    track[MAIN_TRACK]->x[i] = (int)(audio[i] * 32767.0);
  }

/*
  if (waveStartPlaying(track[MAIN_TRACK]->x, numAudioSamples, false))
  {
    wxMilliSleep((int)(totalLength_s * 1000) + 200);
    waveStopPlaying();
  }
*/
}


// ****************************************************************************
// Create audio stimuli for the experiment to compare the old and new geometric
// glottis models (2019 and 2025).
// ****************************************************************************

void Data::experiment2NewGlottisModel(wxWindow* updateParent)
{
  // This is the target folder, which must end with "//".
  const wxString FOLDER_NAME =
    "C://Arbeit//Forschungsprojekte-VocalTractLab//Geometrisches-Glottismodell-2025//"
    "Perceptual-comparison-with-2019-model//Stimuli//";

  double lowerDisplacement_cm, upperDisplacement_cm, phaseLag_rad, shapeParam;

  const int NUM_SENTENCES = 10;
  const int NUM_LOWER_DISPLACEMENT_VALUES = 3;
  const int NUM_UPPER_DISPLACEMENT_VALUES = 3;
  const int NUM_PHASE_LAG_VALUES_VALUES = 3;
  const int NUM_PULSE_SHAPE_VALUES = 3;
  const double LOWER_DISPLACEMENTS_CM[NUM_LOWER_DISPLACEMENT_VALUES] = { 0.01, 0.04, 0.07 };
  const double UPPER_DISPLACEMENTS_CM[NUM_UPPER_DISPLACEMENT_VALUES] = { 0.01, 0.04, 0.07 };
  const double PHASE_LAG_DEG[NUM_PHASE_LAG_VALUES_VALUES] = { 40.0, 70.0, 100.0 };
  const double PULSE_SHAPE[NUM_PULSE_SHAPE_VALUES] = { -1.0, 0.0, 1.0 };

  int k1, k2, k3, k4, k5;

  for (k1 = 1; k1 <= NUM_SENTENCES; k1++)
  {
    for (k2 = 0; k2 < NUM_LOWER_DISPLACEMENT_VALUES; k2++)
    {
      for (k3 = 0; k3 < NUM_UPPER_DISPLACEMENT_VALUES; k3++)
      {
        for (k4 = 0; k4 < NUM_PHASE_LAG_VALUES_VALUES; k4++)
        {
          for (k5 = 0; k5 < NUM_PULSE_SHAPE_VALUES; k5++)
          {
            lowerDisplacement_cm = LOWER_DISPLACEMENTS_CM[k2];
            upperDisplacement_cm = LOWER_DISPLACEMENTS_CM[k3];
            phaseLag_rad = PHASE_LAG_DEG[k4] * M_PI / 180.0;
            shapeParam = PULSE_SHAPE[k5];

            experiment2CreateSentence(FOLDER_NAME, k1, lowerDisplacement_cm, 
              upperDisplacement_cm, phaseLag_rad, shapeParam);

            // Update the screen.
            wxCommandEvent event(updateRequestEvent);
            event.SetInt(REFRESH_PICTURES);
            wxPostEvent(updateParent, event);
            wxYield();
          }
        }
      }
    }
  }

  wxPrintf("Finished generating all sentences for experiment 2.\n");
}


// ****************************************************************************
// Create and save a sentence with the old and the new glottis model with the
// given settings for the glottal shapes.
// ****************************************************************************

void Data::experiment2CreateSentence(wxString folderName, int sentenceIndex,
    double lowerDisplacement_cm, double upperDisplacement_cm,
    double phaseLag_rad, double shapeParam)
{
  wxString indexString = wxString::Format("%d", sentenceIndex);
  wxString segFileName = folderName + "sentence" + indexString + ".seg";
  wxString paramString = wxString::Format("lower%2.1f_upper%2.1f_phase%d_shape%2.1f",
    lowerDisplacement_cm * 10.0, upperDisplacement_cm * 10.0, 
    (int)(phaseLag_rad * 180.0 / M_PI), shapeParam);
  wxString wavFileNameOld = wxString::Format("%sitem%d_%s_OLD.wav", folderName, sentenceIndex, paramString);
  wxString wavFileNameNew = wxString::Format("%sitem%d_%s_NEW.wav", folderName, sentenceIndex, paramString);

  // Load the segment sequence file.

  if (segmentSequence->readFromFile(segFileName.ToStdString()) == false)
  {
    printf("Error in experiment2CreateSentence(): Segment sequence file could not be loaded.\n");
    return;
  }

  static double DEFAULT_FLUTTER_PERCENT = 25.0;
  static double ZERO_DOUBLE_PULSING = 0.0;
  vector<double> audio;
  bool ok = false;

  // ****************************************************************
  // Generate and save audio with the NEW glottis model.
  // ****************************************************************

  selectGlottis(GEOMETRIC_GLOTTIS_2025);

  // Define glottal shapes with the given parameters for new model.

  Glottis *g = getSelectedGlottis();
  Glottis::Shape* s = nullptr;

  if ((g->getShape("modal") == nullptr) || (g->getShape("h") == nullptr) || (g->getShape("stop") == nullptr) ||
    (g->getShape("voiced-fricative") == nullptr) || (g->getShape("voiced-plosive") == nullptr) ||
    (g->getShape("voiceless-fricative") == nullptr) || (g->getShape("voiceless-plosive") == nullptr))
  {
    printf("Error in experiment2CreateSentence(): Glottis shape not found.\n");
    return;
  }

  s = g->getShape("modal");
  s->controlParam[2] = lowerDisplacement_cm;
  s->controlParam[3] = upperDisplacement_cm;
  s->controlParam[4] = 0.02;    // Chink area in cm^2
  s->controlParam[5] = phaseLag_rad;    // Phase lag in rad
  s->controlParam[6] = 1.0;    // Relative amplitude
  s->controlParam[7] = shapeParam;    // Pulse shape in [-1; +1]
  s->controlParam[8] = DEFAULT_FLUTTER_PERCENT;    // Flutter in %

  s = g->getShape("h");
  s->controlParam[2] = 0.045;
  s->controlParam[3] = 0.045;
  s->controlParam[4] = 0.1;    // Chink area in cm^2
  s->controlParam[5] = phaseLag_rad;    // Phase lag in rad
  s->controlParam[6] = 0.0;    // Relative amplitude
  s->controlParam[7] = shapeParam;    // Pulse shape in [-1; +1]
  s->controlParam[8] = DEFAULT_FLUTTER_PERCENT;    // Flutter in %

  s = g->getShape("stop");
  s->controlParam[2] = -0.01;
  s->controlParam[3] = -0.01;
  s->controlParam[4] = -0.001;    // Chink area in cm^2
  s->controlParam[5] = phaseLag_rad;    // Phase lag in rad
  s->controlParam[6] = -0.2;    // Relative amplitude
  s->controlParam[7] = shapeParam;    // Pulse shape in [-1; +1]
  s->controlParam[8] = DEFAULT_FLUTTER_PERCENT;    // Flutter in %

  s = g->getShape("voiced-fricative");
  s->controlParam[2] = 0.1;
  s->controlParam[3] = 0.1;
  s->controlParam[4] = 0.1;    // Chink area in cm^2
  s->controlParam[5] = phaseLag_rad;    // Phase lag in rad
  s->controlParam[6] = 1.0;    // Relative amplitude
  s->controlParam[7] = shapeParam;    // Pulse shape in [-1; +1]
  s->controlParam[8] = DEFAULT_FLUTTER_PERCENT;    // Flutter in %

  s = g->getShape("voiced-plosive");
  s->controlParam[2] = 0.1;
  s->controlParam[3] = 0.1;
  s->controlParam[4] = 0.0;    // Chink area in cm^2
  s->controlParam[5] = phaseLag_rad;    // Phase lag in rad
  s->controlParam[6] = 1.0;    // Relative amplitude
  s->controlParam[7] = shapeParam;    // Pulse shape in [-1; +1]
  s->controlParam[8] = DEFAULT_FLUTTER_PERCENT;    // Flutter in %

  s = g->getShape("voiceless-fricative");
  s->controlParam[2] = 0.15;
  s->controlParam[3] = 0.15;
  s->controlParam[4] = 0.0;    // Chink area in cm^2
  s->controlParam[5] = phaseLag_rad;    // Phase lag in rad
  s->controlParam[6] = 0.0;    // Relative amplitude
  s->controlParam[7] = shapeParam;    // Pulse shape in [-1; +1]
  s->controlParam[8] = DEFAULT_FLUTTER_PERCENT;    // Flutter in %

  s = g->getShape("voiceless-plosive");
  s->controlParam[2] = 0.25;
  s->controlParam[3] = 0.25;
  s->controlParam[4] = 0.0;    // Chink area in cm^2
  s->controlParam[5] = phaseLag_rad;    // Phase lag in rad
  s->controlParam[6] = -1.0;    // Relative amplitude
  s->controlParam[7] = shapeParam;    // Pulse shape in [-1; +1]
  s->controlParam[8] = DEFAULT_FLUTTER_PERCENT;    // Flutter in %

  // Set static params to well defined values.
  
  g->staticParams[0].x = 0.45;    // Rest thickness in cm
  g->staticParams[1].x = 1.6;    // Rest length in cm
  g->staticParams[2].x = 120.0;    // Rest f0 in Hz
  g->staticParams[3].x = 0.4;    // Chink length in cm

  // Generate, normalize and save the audio.

  wxPrintf("Generating %s...\n", wavFileNameNew);

  gesturalScore->createFromSegmentSequence(segmentSequence);

  ok = synthesizer->synthesizeGesturalScore(gesturalScore, tdsModel, audio, "", true);
  normalizeAudioAmplitude(audio);
  if (saveWavFile(wavFileNameNew, audio) == false)
  {
    printf("Error in experiment2CreateSentence(): WAV file could not be saved.\n");
    return;
  }

  // ****************************************************************
  // Generate and save audio with the OLD glottis model.
  // ****************************************************************

  selectGlottis(GEOMETRIC_GLOTTIS_2019);

  // Define glottal shapes with the given parameters for new model.

  g = getSelectedGlottis();

  if ((g->getShape("modal") == nullptr) || (g->getShape("h") == nullptr) || (g->getShape("stop") == nullptr) ||
    (g->getShape("voiced-fricative") == nullptr) || (g->getShape("voiced-plosive") == nullptr) ||
    (g->getShape("voiceless-fricative") == nullptr) || (g->getShape("voiceless-plosive") == nullptr))
  {
    printf("Error in experiment2CreateSentence(): Glottis shape not found.\n");
    return;
  }

  s = g->getShape("modal");
  s->controlParam[2] = lowerDisplacement_cm;
  s->controlParam[3] = upperDisplacement_cm;
  s->controlParam[4] = 0.02;    // Chink area in cm^2
  s->controlParam[5] = phaseLag_rad;    // Phase lag in rad
  s->controlParam[6] = 1.0;    // Relative amplitude
  s->controlParam[7] = ZERO_DOUBLE_PULSING;    // Double pulsing
  s->controlParam[8] = shapeParam * 0.5;    // Pulse skewness in [-0.5; +0.5]
  s->controlParam[9] = DEFAULT_FLUTTER_PERCENT;    // Flutter in %

  s = g->getShape("h");
  s->controlParam[2] = 0.045;
  s->controlParam[3] = 0.045;
  s->controlParam[4] = 0.1;    // Chink area in cm^2
  s->controlParam[5] = phaseLag_rad;    // Phase lag in rad
  s->controlParam[6] = 0.0;    // Relative amplitude
  s->controlParam[7] = ZERO_DOUBLE_PULSING;    // Double pulsing
  s->controlParam[8] = shapeParam * 0.5;    // Pulse skewness in [-0.5; +0.5]
  s->controlParam[9] = DEFAULT_FLUTTER_PERCENT;    // Flutter in %

  s = g->getShape("stop");
  s->controlParam[2] = -0.01;
  s->controlParam[3] = -0.01;
  s->controlParam[4] = -0.001;    // Chink area in cm^2
  s->controlParam[5] = phaseLag_rad;    // Phase lag in rad
  s->controlParam[6] = -0.2;    // Relative amplitude
  s->controlParam[7] = ZERO_DOUBLE_PULSING;    // Double pulsing
  s->controlParam[8] = shapeParam * 0.5;    // Pulse skewness in [-0.5; +0.5]
  s->controlParam[9] = DEFAULT_FLUTTER_PERCENT;    // Flutter in %

  s = g->getShape("voiced-fricative");
  s->controlParam[2] = 0.1;
  s->controlParam[3] = 0.1;
  s->controlParam[4] = 0.1;    // Chink area in cm^2
  s->controlParam[5] = phaseLag_rad;    // Phase lag in rad
  s->controlParam[6] = 1.0;    // Relative amplitude
  s->controlParam[7] = ZERO_DOUBLE_PULSING;    // Double pulsing
  s->controlParam[8] = shapeParam * 0.5;    // Pulse skewness in [-0.5; +0.5]
  s->controlParam[9] = DEFAULT_FLUTTER_PERCENT;    // Flutter in %

  s = g->getShape("voiced-plosive");
  s->controlParam[2] = 0.1;
  s->controlParam[3] = 0.1;
  s->controlParam[4] = 0.0;    // Chink area in cm^2
  s->controlParam[5] = phaseLag_rad;    // Phase lag in rad
  s->controlParam[6] = 1.0;    // Relative amplitude
  s->controlParam[7] = ZERO_DOUBLE_PULSING;    // Double pulsing
  s->controlParam[8] = shapeParam * 0.5;    // Pulse skewness in [-0.5; +0.5]
  s->controlParam[9] = DEFAULT_FLUTTER_PERCENT;    // Flutter in %

  s = g->getShape("voiceless-fricative");
  s->controlParam[2] = 0.15;
  s->controlParam[3] = 0.15;
  s->controlParam[4] = 0.0;    // Chink area in cm^2
  s->controlParam[5] = phaseLag_rad;    // Phase lag in rad
  s->controlParam[6] = 0.0;    // Relative amplitude
  s->controlParam[7] = ZERO_DOUBLE_PULSING;    // Double pulsing
  s->controlParam[8] = shapeParam * 0.5;    // Pulse skewness in [-0.5; +0.5]
  s->controlParam[9] = DEFAULT_FLUTTER_PERCENT;    // Flutter in %

  s = g->getShape("voiceless-plosive");
  s->controlParam[2] = 0.25;
  s->controlParam[3] = 0.25;
  s->controlParam[4] = 0.0;    // Chink area in cm^2
  s->controlParam[5] = phaseLag_rad;    // Phase lag in rad
  s->controlParam[6] = -1.0;    // Relative amplitude
  s->controlParam[7] = ZERO_DOUBLE_PULSING;    // Double pulsing
  s->controlParam[8] = shapeParam * 0.5;    // Pulse skewness in [-0.5; +0.5]
  s->controlParam[9] = DEFAULT_FLUTTER_PERCENT;    // Flutter in %

  // Set static params to well defined values.

  g->staticParams[0].x = 0.45;    // Rest thickness in cm
  g->staticParams[1].x = 1.6;    // Rest length in cm
  g->staticParams[2].x = 120.0;    // Rest f0 in Hz
  g->staticParams[3].x = 0.4;    // Chink length in cm

  // Generate, normalize and save the audio.

  wxPrintf("Generating %s...\n", wavFileNameOld);

  gesturalScore->createFromSegmentSequence(segmentSequence);

  ok = synthesizer->synthesizeGesturalScore(gesturalScore, tdsModel, audio, "", true);
  normalizeAudioAmplitude(audio);
  if (saveWavFile(wavFileNameOld, audio) == false)
  {
    printf("Error in experiment2CreateSentence(): WAV file could not be saved.\n");
    return;
  }
}


// ****************************************************************************
// Testing something... wit NEW glottis model.
// ****************************************************************************

void Data::test1()
{
  wxString folderName = "C://Arbeit//Forschungsprojekte-VocalTractLab//Geometrisches-Glottismodell-2025//"
    "Perceptual-comparison-with-2019-model//Stimuli//";
  int sentenceIndex = 1;
  double lowerDisplacement_cm = 0.07;
  double upperDisplacement_cm = 0.01;
  double phaseLag_rad = 100.0 * M_PI / 180.0;
  double shapeParam = 1.0;

  wxString indexString = wxString::Format("%d", sentenceIndex);
  wxString segFileName = folderName + "sentence" + indexString + ".seg";
  wxString paramString = wxString::Format("lower%2.1f_upper%2.1f_phase%d_shape%2.1f",
    lowerDisplacement_cm * 10.0, upperDisplacement_cm * 10.0,
    (int)(phaseLag_rad * 180.0 / M_PI), shapeParam);

  // Load the segment sequence file.

  if (segmentSequence->readFromFile(segFileName.ToStdString()) == false)
  {
    printf("Error in experiment2CreateSentence(): Segment sequence file could not be loaded.\n");
    return;
  }

  static double DEFAULT_FLUTTER_PERCENT = 25.0;
  static double ZERO_DOUBLE_PULSING = 0.0;
  vector<double> audio;
  bool ok = false;

  // ****************************************************************
  // Generate and save audio with the NEW glottis model.
  // ****************************************************************

  selectGlottis(GEOMETRIC_GLOTTIS_2025);

  // Define glottal shapes with the given parameters for new model.

  Glottis* g = getSelectedGlottis();
  Glottis::Shape* s = nullptr;

  if ((g->getShape("modal") == nullptr) || (g->getShape("h") == nullptr) || (g->getShape("stop") == nullptr) ||
    (g->getShape("voiced-fricative") == nullptr) || (g->getShape("voiced-plosive") == nullptr) ||
    (g->getShape("voiceless-fricative") == nullptr) || (g->getShape("voiceless-plosive") == nullptr))
  {
    printf("Error in experiment2CreateSentence(): Glottis shape not found.\n");
    return;
  }

  s = g->getShape("modal");
  s->controlParam[2] = lowerDisplacement_cm;
  s->controlParam[3] = upperDisplacement_cm;
  s->controlParam[4] = 0.02;    // Chink area in cm^2
  s->controlParam[5] = phaseLag_rad;    // Phase lag in rad
  s->controlParam[6] = 1.0;    // Relative amplitude
  s->controlParam[7] = shapeParam;    // Pulse shape in [-1; +1]
  s->controlParam[8] = DEFAULT_FLUTTER_PERCENT;    // Flutter in %

  s = g->getShape("h");
  s->controlParam[2] = 0.045;
  s->controlParam[3] = 0.045;
  s->controlParam[4] = 0.1;    // Chink area in cm^2
  s->controlParam[5] = phaseLag_rad;    // Phase lag in rad
  s->controlParam[6] = 0.0;    // Relative amplitude
  s->controlParam[7] = shapeParam;    // Pulse shape in [-1; +1]
  s->controlParam[8] = DEFAULT_FLUTTER_PERCENT;    // Flutter in %

  s = g->getShape("stop");
  s->controlParam[2] = -0.01;
  s->controlParam[3] = -0.01;
  s->controlParam[4] = -0.001;    // Chink area in cm^2
  s->controlParam[5] = phaseLag_rad;    // Phase lag in rad
  s->controlParam[6] = -0.2;    // Relative amplitude
  s->controlParam[7] = shapeParam;    // Pulse shape in [-1; +1]
  s->controlParam[8] = DEFAULT_FLUTTER_PERCENT;    // Flutter in %

  s = g->getShape("voiced-fricative");
  s->controlParam[2] = 0.1;
  s->controlParam[3] = 0.1;
  s->controlParam[4] = 0.1;    // Chink area in cm^2
  s->controlParam[5] = phaseLag_rad;    // Phase lag in rad
  s->controlParam[6] = 1.0;    // Relative amplitude
  s->controlParam[7] = shapeParam;    // Pulse shape in [-1; +1]
  s->controlParam[8] = DEFAULT_FLUTTER_PERCENT;    // Flutter in %

  s = g->getShape("voiced-plosive");
  s->controlParam[2] = 0.1;
  s->controlParam[3] = 0.1;
  s->controlParam[4] = 0.0;    // Chink area in cm^2
  s->controlParam[5] = phaseLag_rad;    // Phase lag in rad
  s->controlParam[6] = 1.0;    // Relative amplitude
  s->controlParam[7] = shapeParam;    // Pulse shape in [-1; +1]
  s->controlParam[8] = DEFAULT_FLUTTER_PERCENT;    // Flutter in %

  s = g->getShape("voiceless-fricative");
  s->controlParam[2] = 0.15;
  s->controlParam[3] = 0.15;
  s->controlParam[4] = 0.0;    // Chink area in cm^2
  s->controlParam[5] = phaseLag_rad;    // Phase lag in rad
  s->controlParam[6] = 0.0;    // Relative amplitude
  s->controlParam[7] = shapeParam;    // Pulse shape in [-1; +1]
  s->controlParam[8] = DEFAULT_FLUTTER_PERCENT;    // Flutter in %

  s = g->getShape("voiceless-plosive");
  s->controlParam[2] = 0.25;
  s->controlParam[3] = 0.25;
  s->controlParam[4] = 0.0;    // Chink area in cm^2
  s->controlParam[5] = phaseLag_rad;    // Phase lag in rad
  s->controlParam[6] = -1.0;    // Relative amplitude
  s->controlParam[7] = shapeParam;    // Pulse shape in [-1; +1]
  s->controlParam[8] = DEFAULT_FLUTTER_PERCENT;    // Flutter in %

  // Set static params to well defined values.

  g->staticParams[0].x = 0.45;    // Rest thickness in cm
  g->staticParams[1].x = 1.6;    // Rest length in cm
  g->staticParams[2].x = 120.0;    // Rest f0 in Hz
  g->staticParams[3].x = 0.4;    // Chink length in cm

  // Create the gestural score.

  gesturalScore->createFromSegmentSequence(segmentSequence);
}


// ****************************************************************************
// Testing something... wit OLD glottis model.
// ****************************************************************************

void Data::test2()
{
  wxString folderName = "C://Arbeit//Forschungsprojekte-VocalTractLab//Geometrisches-Glottismodell-2025//"
    "Perceptual-comparison-with-2019-model//Stimuli//";
  int sentenceIndex = 3;
  double lowerDisplacement_cm = 0.07;
  double upperDisplacement_cm = 0.01;
  double phaseLag_rad = 70.0 * M_PI / 180.0;
  double shapeParam = 1.0;

  wxString indexString = wxString::Format("%d", sentenceIndex);
  wxString segFileName = folderName + "sentence" + indexString + ".seg";
  wxString paramString = wxString::Format("lower%2.1f_upper%2.1f_phase%d_shape%2.1f",
    lowerDisplacement_cm * 10.0, upperDisplacement_cm * 10.0,
    (int)(phaseLag_rad * 180.0 / M_PI), shapeParam);

  // Load the segment sequence file.

  if (segmentSequence->readFromFile(segFileName.ToStdString()) == false)
  {
    printf("Error in experiment2CreateSentence(): Segment sequence file could not be loaded.\n");
    return;
  }

  static double DEFAULT_FLUTTER_PERCENT = 25.0;
  static double ZERO_DOUBLE_PULSING = 0.0;
  vector<double> audio;
  bool ok = false;

  // ****************************************************************
  // Generate and save audio with the NEW glottis model.
  // ****************************************************************

  selectGlottis(GEOMETRIC_GLOTTIS_2019);

  // Define glottal shapes with the given parameters for new model.

  Glottis* g = getSelectedGlottis();
  Glottis::Shape* s = nullptr;

  if ((g->getShape("modal") == nullptr) || (g->getShape("h") == nullptr) || (g->getShape("stop") == nullptr) ||
    (g->getShape("voiced-fricative") == nullptr) || (g->getShape("voiced-plosive") == nullptr) ||
    (g->getShape("voiceless-fricative") == nullptr) || (g->getShape("voiceless-plosive") == nullptr))
  {
    printf("Error in experiment2CreateSentence(): Glottis shape not found.\n");
    return;
  }

  s = g->getShape("modal");
  s->controlParam[2] = lowerDisplacement_cm;
  s->controlParam[3] = upperDisplacement_cm;
  s->controlParam[4] = 0.02;    // Chink area in cm^2
  s->controlParam[5] = phaseLag_rad;    // Phase lag in rad
  s->controlParam[6] = 1.0;    // Relative amplitude
  s->controlParam[7] = ZERO_DOUBLE_PULSING;    // Double pulsing
  s->controlParam[8] = shapeParam * 0.5;    // Pulse skewness in [-0.5; +0.5]
  s->controlParam[9] = DEFAULT_FLUTTER_PERCENT;    // Flutter in %

  s = g->getShape("h");
  s->controlParam[2] = 0.045;
  s->controlParam[3] = 0.045;
  s->controlParam[4] = 0.1;    // Chink area in cm^2
  s->controlParam[5] = phaseLag_rad;    // Phase lag in rad
  s->controlParam[6] = 0.0;    // Relative amplitude
  s->controlParam[7] = ZERO_DOUBLE_PULSING;    // Double pulsing
  s->controlParam[8] = shapeParam * 0.5;    // Pulse skewness in [-0.5; +0.5]
  s->controlParam[9] = DEFAULT_FLUTTER_PERCENT;    // Flutter in %

  s = g->getShape("stop");
  s->controlParam[2] = -0.01;
  s->controlParam[3] = -0.01;
  s->controlParam[4] = -0.001;    // Chink area in cm^2
  s->controlParam[5] = phaseLag_rad;    // Phase lag in rad
  s->controlParam[6] = -0.2;    // Relative amplitude
  s->controlParam[7] = ZERO_DOUBLE_PULSING;    // Double pulsing
  s->controlParam[8] = shapeParam * 0.5;    // Pulse skewness in [-0.5; +0.5]
  s->controlParam[9] = DEFAULT_FLUTTER_PERCENT;    // Flutter in %

  s = g->getShape("voiced-fricative");
  s->controlParam[2] = 0.1;
  s->controlParam[3] = 0.1;
  s->controlParam[4] = 0.1;    // Chink area in cm^2
  s->controlParam[5] = phaseLag_rad;    // Phase lag in rad
  s->controlParam[6] = 1.0;    // Relative amplitude
  s->controlParam[7] = ZERO_DOUBLE_PULSING;    // Double pulsing
  s->controlParam[8] = shapeParam * 0.5;    // Pulse skewness in [-0.5; +0.5]
  s->controlParam[9] = DEFAULT_FLUTTER_PERCENT;    // Flutter in %

  s = g->getShape("voiced-plosive");
  s->controlParam[2] = 0.1;
  s->controlParam[3] = 0.1;
  s->controlParam[4] = 0.0;    // Chink area in cm^2
  s->controlParam[5] = phaseLag_rad;    // Phase lag in rad
  s->controlParam[6] = 1.0;    // Relative amplitude
  s->controlParam[7] = ZERO_DOUBLE_PULSING;    // Double pulsing
  s->controlParam[8] = shapeParam * 0.5;    // Pulse skewness in [-0.5; +0.5]
  s->controlParam[9] = DEFAULT_FLUTTER_PERCENT;    // Flutter in %

  s = g->getShape("voiceless-fricative");
  s->controlParam[2] = 0.15;
  s->controlParam[3] = 0.15;
  s->controlParam[4] = 0.0;    // Chink area in cm^2
  s->controlParam[5] = phaseLag_rad;    // Phase lag in rad
  s->controlParam[6] = 0.0;    // Relative amplitude
  s->controlParam[7] = ZERO_DOUBLE_PULSING;    // Double pulsing
  s->controlParam[8] = shapeParam * 0.5;    // Pulse skewness in [-0.5; +0.5]
  s->controlParam[9] = DEFAULT_FLUTTER_PERCENT;    // Flutter in %

  s = g->getShape("voiceless-plosive");
  s->controlParam[2] = 0.25;
  s->controlParam[3] = 0.25;
  s->controlParam[4] = 0.0;    // Chink area in cm^2
  s->controlParam[5] = phaseLag_rad;    // Phase lag in rad
  s->controlParam[6] = -1.0;    // Relative amplitude
  s->controlParam[7] = ZERO_DOUBLE_PULSING;    // Double pulsing
  s->controlParam[8] = shapeParam * 0.5;    // Pulse skewness in [-0.5; +0.5]
  s->controlParam[9] = DEFAULT_FLUTTER_PERCENT;    // Flutter in %

  // Set static params to well defined values.

  g->staticParams[0].x = 0.45;    // Rest thickness in cm
  g->staticParams[1].x = 1.6;    // Rest length in cm
  g->staticParams[2].x = 120.0;    // Rest f0 in Hz
  g->staticParams[3].x = 0.4;    // Chink length in cm

  // Create the gestural score.

  gesturalScore->createFromSegmentSequence(segmentSequence);
}


// ****************************************************************************
/// Constructor.
// ****************************************************************************

Data::Data()
{
  // Do nothing. Initialization is done in init().
}

// ****************************************************************************
