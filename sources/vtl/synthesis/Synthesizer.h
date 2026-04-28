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

#ifndef __SYNTHESIZER_H__
#define __SYNTHESIZER_H__

#include "acoustics/TdsModel.h"
#include "acoustics/TlModel.h"
#include "acoustics/Tube.h"
#include "glottis/Glottis.h"
#include "anatomy/VocalTract.h"
#include "synthesis/GesturalScore.h"
#include "dsp/Dsp.h"
#include "dsp/IirFilter.h"
#include <vector>

using namespace std;

// ****************************************************************************
/// With this class, the user can incrementally synthesize a speech signal on 
/// the basis of a sequence of states of the vocal fold model and the vocal 
/// tract model or the tube shape.
/// In the time intervals between the provided states, the tube shapes are
/// linearly interpolated for the synthesis.
// ****************************************************************************

class Synthesizer
{
  // **************************************************************************
  // Public data.
  // **************************************************************************

public:
  // This is the default step size for the incremental synthesis 
  // corresponding to 2.5 ms at our sampling rate of 48000 Hz.
  static const int NUM_CHUNCK_SAMPLES = 120;
  
  static const int IMPULSE_RESPONSE_LENGTH = 32768;
  static const int IMPULSE_RESPONSE_EXPONENT = 15;
  static const int TDS_BUFFER_LENGTH = 32768;
  static const int TDS_BUFFER_MASK = 32767;

  int userProbeSection{ 20 };   // Arbitrary section for initialization.

  // Ring buffers for different time signals. All units are CGS units!
  double* userProbeFlow;
  double* userProbePressure;
  double* userProbeArea;
  double* userProbeVelocity;

  // **************************************************************************
  // Public functions.
  // **************************************************************************

public:
  Synthesizer();
  ~Synthesizer();

  void init(Glottis *glottis, VocalTract *vocalTract, TdsModel *tdsModel);
  void reset();

  void addChunk(double *newGlottisParams, double *newTractParams, 
    int numSamples, vector<double> &audio, ofstream *miscFileStream = NULL);

  void addChunk(double *newGlottisParams, Tube *newTube, 
    int numSamples, vector<double> &audio, ofstream* miscFileStream = NULL);

  double addSample(double* glottisParams, Tube* tube, ofstream* miscFileStream = NULL);

  // **************************************************************************

  static void normalizeAmplitude(vector<double>& signal);
  static void copySignal(vector<double> &sourceSignal, Signal16 &targetSignal, int startPosInTarget);

  static bool synthesizeGesturalScore(GesturalScore *gesturalScore, 
    TdsModel *tdsModel, vector<double> &audio, const char* miscFileName = "", bool enableConsoleOutput = true);

  static bool synthesizeTubeSequence(string fileName, TdsModel *tdsModel, vector<double> &audio);

  static void synthesizeStaticPhoneme(Glottis *glottis, VocalTract *vocalTract,
    TdsModel *tdsModel, bool shortLength, bool useConstantF0, vector<double> &audio);

  static void calcSubglottalImpedance(TdsModel* tdsModel, 
    vector<double>& impulseResponse, ComplexSignal* spectrum);

  static void calcSupraglottalImpedance(TdsModel* tdsModel,
    vector<double>& impulseResponse, ComplexSignal* spectrum);

  static void calcTransferFunction(TdsModel* tdsModel,
    vector<double>& impulseResponse, ComplexSignal* spectrum);

  static bool gesturalScoreToTubeSequenceFile(GesturalScore *gesturalScore, string fileName);
  static bool gesturalScoreToTractContourFiles(GesturalScore *gesturalScore, string folderName);
  static bool gesturalScoreToEmaTrajectories(GesturalScore* gesturalScore, string fileName);
  static bool gesturalScoreToTransferFunctions(GesturalScore* gesturalScore, TlModel* tlModel, string fileName);

  // **************************************************************************
  // Private data.
  // **************************************************************************

private:
  // The three models are *not* owned by this class, but just used.
  Glottis* glottis{ nullptr };
  VocalTract *vocalTract{ nullptr };
  TdsModel *tdsModel{ nullptr };

  Tube prevTube;
  Tube tube;
  double prevGlottisParams[Glottis::MAX_CONTROL_PARAMS];

  // Cache for the tract-params -> Tube path. The tract-overload of
  // addChunk would otherwise call vocalTract->calculateAll() and
  // vocalTract->getTube() on every call (~2.4 ms WASM, ~3.5 ms tablet)
  // even when nothing changed. In live-synth use the user holds a slider
  // for seconds at a time, so almost every chunk has identical params.
  // Invalidated by reset(); kept tied to the Synthesizer because that's
  // the layer that owns prevTube/prevGlottisParams already.
  double cachedTractParams[VocalTract::NUM_PARAMS] = {};
  Tube cachedTube;
  bool tubeCacheValid{ false };

  double *outputFlow;
  double *outputPressure;
  IirFilter outputPressureFilter;
  bool initialShapesSet{ false };

  // **************************************************************************
  // Private functions.
  // **************************************************************************

private:
  static bool parseTextLine(string line, int numValues, double *values);

};

#endif

// ****************************************************************************
