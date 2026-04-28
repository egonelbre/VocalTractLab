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

#include "synthesis/Synthesizer.h"
#include "glottis/TriangularGlottis.h"
#include <cstring>
#include <iomanip>

// ****************************************************************************
/// Constructor.
// ****************************************************************************

Synthesizer::Synthesizer()
{
  // Ring buffers for different time signals

  userProbeFlow = new double[TDS_BUFFER_LENGTH];
  userProbePressure = new double[TDS_BUFFER_LENGTH];
  userProbeArea = new double[TDS_BUFFER_LENGTH];
  userProbeVelocity = new double[TDS_BUFFER_LENGTH];

  outputFlow = new double[TDS_BUFFER_LENGTH];
  outputPressure = new double[TDS_BUFFER_LENGTH];

  outputPressureFilter.createChebyshev((double)SYNTHETIC_SPEECH_BANDWIDTH_HZ / (double)AUDIO_SAMPLING_RATE_HZ, false, 8);

  for (int i = 0; i < Glottis::MAX_CONTROL_PARAMS; i++)
  {
    prevGlottisParams[i] = 0.0;
  }
}


// ****************************************************************************
/// Destructor.
// ****************************************************************************

Synthesizer::~Synthesizer()
{
  delete[] userProbeFlow;
  delete[] userProbePressure;
  delete[] userProbeArea;
  delete[] userProbeVelocity;

  delete [] outputFlow;
  delete [] outputPressure;
}


// ****************************************************************************
/// Initializes the synthesizer with the given objects.
// ****************************************************************************

void Synthesizer::init(Glottis *glottis, VocalTract *vocalTract, TdsModel *tdsModel)
{
  this->glottis = glottis;
  this->vocalTract = vocalTract;
  this->tdsModel = tdsModel;

  // Reset the dynamic state of all models and clear the buffers.
  reset();
}


// ****************************************************************************
// Reset all models for a new synthesis.
// ****************************************************************************

void Synthesizer::reset()
{
  if ((glottis != nullptr) && (tdsModel != nullptr))
  {
    glottis->resetMotion();
    tdsModel->resetMotion();
  }

  outputPressureFilter.resetBuffers();
  initialShapesSet = false;
  tubeCacheValid = false;

  int i;

  for (i = 0; i < TDS_BUFFER_LENGTH; i++)
  {
    userProbeFlow[i] = 0.0;
    userProbePressure[i] = 0.0;
    userProbeArea[i] = 0.0;
    userProbeVelocity[i] = 0.0;

    outputFlow[i] = 0.0;
    outputPressure[i] = 0.0;
  }
}


// ****************************************************************************
/// Generate an incremental part of the signal with a duration of numSamples
/// during which the vocal tract and glottis shapes are interpolated between 
/// the previous shapes (passed to the previous call of this function) and the 
/// given new shapes (i.e., parameters).
/// In the first call of this function (after reset()) the states of the 
/// glottis and vocal tract are initialized, and no audio is generated.
/// The value range of the generated audio samples is [-1; +1].
// ****************************************************************************

void Synthesizer::addChunk(double *newGlottisParams, double *newTractParams,
  int numSamples, vector<double> &audio, ofstream* miscFileStream)
{
  if (vocalTract == NULL)
  {
    return;
  }

  Tube localTube;
  int i;

  // Cache hit: the tract-params block is bit-identical to the previous
  // call's, so vocalTract->calculateAll() + getTube() would produce a
  // Tube identical to the cached one. Skip both. (Anatomy is immutable
  // outside reset(); reset() invalidates the cache.) Saves ~2.4 ms wall
  // (M1 WASM) per chunk in the steady-state case.
  const bool hit = tubeCacheValid &&
    std::memcmp(newTractParams, cachedTractParams,
                sizeof(cachedTractParams)) == 0;

  if (hit)
  {
    localTube = cachedTube;
  }
  else
  {
    for (i = 0; i < VocalTract::NUM_PARAMS; i++)
    {
      vocalTract->params[i].x = newTractParams[i];
    }
    vocalTract->calculateAll();
    vocalTract->getTube(&localTube);

    std::memcpy(cachedTractParams, newTractParams, sizeof(cachedTractParams));
    cachedTube = localTube;
    tubeCacheValid = true;
  }

  // Synthesize the new audio samples based on the tube model.
  addChunk(newGlottisParams, &localTube, numSamples, audio, miscFileStream);
}


// ****************************************************************************
/// Generate an incremental part of the signal with a duration of numSamples
/// during which the tube and glottis shapes are interpolated between 
/// the previous shapes (passed to the previous call of this function) and the 
/// given new shapes.
/// In the first call of this function (after reset()) the states of the 
/// glottis and tube are initialized, and no audio is generated.
/// The value range of the generated audio samples is [-1; +1].
// ****************************************************************************

void Synthesizer::addChunk(double *newGlottisParams, Tube *newTube, 
  int numSamples, vector<double> &audio, ofstream* miscFileStream)
{
  int i, k;
  double ratio, ratio1;
  int numGlottisParams = (int)glottis->controlParams.size();
  double glottisParams[Glottis::MAX_CONTROL_PARAMS];

  if (initialShapesSet == false)
  {
    prevTube = *newTube;
    for (i = 0; i < numGlottisParams; i++)
    {
      prevGlottisParams[i] = newGlottisParams[i];
    }
    initialShapesSet = true;
    return;
  }

  if (numSamples < 1)
  {
    return;
  }

  // ****************************************************************
  // Run through all new samples.
  //
  // When prevTube == newTube — the steady-state case while the user
  // holds a vocal tract shape — skip the per-sample tube interpolation
  // entirely and just point at the constant tube. The 480 calls per
  // chunk to Tube::interpolate (memberwise lerps over 103 sections)
  // collapse to one memberwise copy.
  // ****************************************************************

  audio.resize(numSamples);

  const bool tubeUnchanged = (prevTube == *newTube);
  if (tubeUnchanged)
  {
    tube = *newTube;
  }

  for (i = 0; i < numSamples; i++)
  {
    ratio = (double)i / (double)numSamples;
    ratio1 = 1.0 - ratio;

    if (!tubeUnchanged)
    {
      // Interpolate the tube.
      tube.interpolate(&prevTube, newTube, ratio);
    }

    // Interpolate the glottis geometry.
    for (k = 0; k < numGlottisParams; k++)
    {
      glottisParams[k] = ratio1 * prevGlottisParams[k] + ratio * newGlottisParams[k];
    }

    audio[i] = addSample(glottisParams, &tube, miscFileStream);
  }

  // ****************************************************************

  prevTube = *newTube;
  for (i = 0; i < numGlottisParams; i++)
  {
    prevGlottisParams[i] = newGlottisParams[i];
  }
}


// ****************************************************************************
// Add a single audio sample, i.e. proceed a single time step in the simulation.
// The return value is the new audio sample in the range [-1, +1].
// ****************************************************************************

double Synthesizer::addSample(double* glottisParams, Tube* tube, ofstream* miscFileStream)
{
  // Lengths and areas of the glottis sections.
  double length_cm[Tube::NUM_GLOTTIS_SECTIONS];
  double area_cm2[Tube::NUM_GLOTTIS_SECTIONS];
  bool filtering;
  double pressure_dPa[4];
  double inflow_cm3_s;
  double outflow_cm3_s;

  double totalFlow_cm3_s;
  double mouthFlow_cm3_s;
  double nostrilFlow_cm3_s;
  double skinFlow_cm3_s;

  // ****************************************************************
  // Set the glottis geometry for the glottal tube sections.
  // ****************************************************************

  int k;
  int numGlottisParams = (int)glottis->controlParams.size();

  for (k = 0; k < numGlottisParams; k++)
  {
    glottis->controlParams[k].x = glottisParams[k];
  }

  glottis->calcGeometry();
  glottis->getTubeData(length_cm, area_cm2);
  tube->setGlottisSections(length_cm, area_cm2);

  // ****************************************************************
  // Do the acoustic simulation.
  // ****************************************************************

  if (tdsModel->getSampleIndex() == 0)
  {
    filtering = false;
  }
  else
  {
    filtering = true;
  }

  tdsModel->setTube(tube, filtering);
  tdsModel->setFlowSource(0.0, -1);
  tdsModel->setPressureSource(glottis->controlParams[Glottis::PRESSURE].x, Tube::FIRST_TRACHEA_SECTION);

  // Get the four relevant pressure values for the glottis model:
  // subglottal, lower glottis, upper glottis, supraglottal.

  pressure_dPa[0] = tdsModel->getSectionPressure(Tube::LAST_TRACHEA_SECTION);
  pressure_dPa[1] = tdsModel->getSectionPressure(Tube::LOWER_GLOTTIS_SECTION);
  pressure_dPa[2] = tdsModel->getSectionPressure(Tube::UPPER_GLOTTIS_SECTION);
  pressure_dPa[3] = tdsModel->getSectionPressure(Tube::FIRST_PHARYNX_SECTION);

  // Increment the time/sample number
  glottis->incTime(1.0 / (double)AUDIO_SAMPLING_RATE_HZ, pressure_dPa);
  totalFlow_cm3_s = tdsModel->proceedTimeStep(mouthFlow_cm3_s, nostrilFlow_cm3_s, skinFlow_cm3_s);

  // The current time pos is that of the tdsModel minus 1, because 
  // the pos. in the tdsModel was already increased in proceedTimeStep().

  int pos = tdsModel->getSampleIndex() - 1;

  k = pos & TDS_BUFFER_MASK;
  outputFlow[k] = totalFlow_cm3_s;

  // **************************************************************
  // Apply the radiation characteristic.
  // **************************************************************

  if (tdsModel->options.radiationCharacteristic == TdsModel::PISTON_SPHERE_RADIATION_CHARACTERISTIC)
  {
    // Improved FIR radiation filter based on a piston in a sphere (+5 dB extra boost at higher frequencies).
    double sum = 0.0;
    int index;
    for (index = 0; index < NUM_PISTON_SPHERE_RADIATION_FILTER_SAMPLES; index++)
    {
      sum += PISTON_SPHERE_RADIATION_FILTER[index] * outputFlow[(pos - index) & TDS_BUFFER_MASK];
    }
    // Multiplication with AUDIO_SAMPLING_RATE_HZ is for a scaling like with the simple radiation characteristic.
    outputPressure[k] = (double)AUDIO_SAMPLING_RATE_HZ * sum;
  }
  else

    if (tdsModel->options.radiationCharacteristic == TdsModel::HEAD_TORSO_RADIATION_CHARACTERISTIC)
    {
      // Improved FIR radiation filter based on an FEM simulation of a head-torso model.
      double sum = 0.0;
      int index;
      for (index = 0; index < NUM_HEAD_TORSO_RADIATION_FILTER_SAMPLES; index++)
      {
        sum += HEAD_TORSO_RADIATION_FILTER[index] * outputFlow[(pos - index) & TDS_BUFFER_MASK];
      }
      // Multiplication with AUDIO_SAMPLING_RATE_HZ is for a scaling like with the simple radiation characteristic.
      outputPressure[k] = (double)AUDIO_SAMPLING_RATE_HZ * sum;
    }
    else
    {
      // Radiation from a simple spherical source with a 6 dB/oct slope.
      outputPressure[k] = (outputFlow[k] - outputFlow[(k - 1) & TDS_BUFFER_MASK]) / tdsModel->timeStep;
    }

  // Scale the output to the range [-1, +1].
  double audioSample = outputPressureFilter.getOutputSample(outputPressure[k]) * 1e-7;

  // **************************************************************
  // Write different quantities (area, pressure, flow, velocity) at 
  // a selected tube section (userProbeSection) to ring buffers.
  // **************************************************************

  if ((userProbeSection >= 0) && (userProbeSection < Tube::NUM_SECTIONS))
  {
    tdsModel->getSectionFlow(userProbeSection, inflow_cm3_s, outflow_cm3_s);
    userProbeFlow[k] = inflow_cm3_s;
    userProbePressure[k] = tdsModel->getSectionPressure(userProbeSection);

    double area = tdsModel->tubeSection[userProbeSection].area;
    if (area < TdsModel::MIN_AREA_CM2)
    {
      area = TdsModel::MIN_AREA_CM2;
    }

    userProbeArea[k] = area;
    userProbeVelocity[k] = inflow_cm3_s / area;
  }

  // **************************************************************
  // Output the glottis parameters in a file.
  // **************************************************************

  if ((miscFileStream != NULL) && (miscFileStream->is_open()) && (!miscFileStream->fail()))
  {
    double sectionPressures_dPa[Tube::NUM_PHARYNX_MOUTH_SECTIONS] = { 0.0 };
    double sectionLengths_cm[Tube::NUM_PHARYNX_MOUTH_SECTIONS] = { 0.0 };
    double sectionAreas_cm2[Tube::NUM_PHARYNX_MOUTH_SECTIONS] = { 0.0 };

    double subglottalPressure_dPa = tdsModel->getSectionPressure(Tube::LAST_TRACHEA_SECTION);
    double intraglottalPressure1_dPa = tdsModel->getSectionPressure(Tube::LOWER_GLOTTIS_SECTION);
    double intraglottalPressure2_dPa = tdsModel->getSectionPressure(Tube::UPPER_GLOTTIS_SECTION);

    for (int m = 0; m < Tube::NUM_PHARYNX_MOUTH_SECTIONS; m++)
    {
      sectionPressures_dPa[m] = tdsModel->getSectionPressure(Tube::FIRST_PHARYNX_SECTION + m);
      sectionLengths_cm[m] = tdsModel->tubeSection[Tube::FIRST_PHARYNX_SECTION + m].length;
      sectionAreas_cm2[m] = tdsModel->tubeSection[Tube::FIRST_PHARYNX_SECTION + m].area;
    }

    tdsModel->getSectionFlow(Tube::UPPER_GLOTTIS_SECTION, inflow_cm3_s, outflow_cm3_s);
    double glottisFlow_cm3_s = inflow_cm3_s;

    glottis->printParamValues(*miscFileStream, glottisFlow_cm3_s,
      mouthFlow_cm3_s, nostrilFlow_cm3_s, skinFlow_cm3_s, audioSample,
      subglottalPressure_dPa, intraglottalPressure1_dPa, intraglottalPressure2_dPa,
      sectionPressures_dPa, sectionLengths_cm, sectionAreas_cm2);
  }

  return audioSample;
}


// ****************************************************************************
// Normalize the audio amplitude of the given signal to -1 dB below the max.
// ****************************************************************************

void Synthesizer::normalizeAmplitude(vector<double>& signal)
{
  int i = 0;
  double maxValue = 0.0;
  int numSamples = (int)signal.size();

  for (i = 0; i < numSamples; i++)
  {
    if (fabs(signal[i]) > maxValue)
    {
      maxValue = fabs(signal[i]);
    }
  }

  const double EPSILON = 0.000000001;
  if (maxValue >= EPSILON)
  {
    // 0.89 corresponds to -1 dB.
    double factor = 0.89 / maxValue;
    for (i = 0; i < numSamples; i++)
    {
      signal[i] *= factor;
    }
  }
}


// ****************************************************************************
/// Static function that takes the samples of type double of the source signal
/// and copies them to the position startPosInTarget in the target signal with
/// samples of type signed short.
/// The value range [-1, +1] from the source signal is linearly scaled to the
/// range [-32768, 32767] of the target signal.
// ****************************************************************************

void Synthesizer::copySignal(vector<double> &sourceSignal, Signal16 &targetSignal,
  int startPosInTarget)
{
  int i;
  signed short value = 0;
  int length = (int)sourceSignal.size();

  for (i = 0; i < length; i++)
  {
    value = (signed short)(sourceSignal[i] * 32767.0);
    targetSignal.setValue(startPosInTarget + i, value);
  }
}


// ****************************************************************************
/// Synthesis of a complete gestural score (blocking synthesis).
/// If miscFileName != "", the glottis data are written to this text file.
/// Returns false, if the misc file could not be written.
// ****************************************************************************

bool Synthesizer::synthesizeGesturalScore(GesturalScore *gesturalScore,
  TdsModel *tdsModel, vector<double> &audio, const char* miscFileName, bool enableConsoleOutput)
{
  int i;
  vector<double> signalPart;
  Glottis *glottis = gesturalScore->glottis;
  VocalTract *vocalTract = gesturalScore->vocalTract;
  double tractParams[VocalTract::NUM_PARAMS];
  double glottisParams[Glottis::MAX_CONTROL_PARAMS];
  int scoreLength_pt = gesturalScore->getDuration_pt();
  int numChunks = (int)(scoreLength_pt / NUM_CHUNCK_SAMPLES) + 1;
  double pos_s = 0.0;

  Synthesizer *synth = new Synthesizer();

  // ****************************************************************
  // Save the current state of the glottis and the vocal tract.
  // ****************************************************************

  glottis->storeControlParams();
  vocalTract->storeControlParams();

  // Important: Calc. the parameter curves from the gestural score.
  gesturalScore->calcCurves();

  // ****************************************************************
  // Shall we write out the misc (glottis) data ?
  // ****************************************************************

  ofstream miscFileStream;
  if (miscFileName[0] != '\0')
  {
    miscFileStream.open(miscFileName);
    if (!miscFileStream)
    {
      delete synth;
      return false;
    }
    else
    {
      glottis->printParamNames(miscFileStream);
    }
  }

  // ****************************************************************
  // Generate the audio signal in small sections of 2.5 ms length 
  // each (= 120 samples @ 48 kHz).
  // ****************************************************************

  synth->init(glottis, vocalTract, tdsModel);
  audio.resize(0);

  if (enableConsoleOutput)
  {
    printf("Synthesis of gestural score startet ");
  }

  // Get the parameters right at the beginning.
  gesturalScore->getParams(0.0, tractParams, glottisParams);
  synth->addChunk(glottisParams, tractParams, 0, signalPart, &miscFileStream);

  for (i = 1; i <= numChunks; i++)
  {
    if (((i & 63) == 0) && (enableConsoleOutput))
    {
      printf(".");
    }

    pos_s = (double)i * NUM_CHUNCK_SAMPLES / AUDIO_SAMPLING_RATE_HZ;
    gesturalScore->getParams(pos_s, tractParams, glottisParams);
    synth->addChunk(glottisParams, tractParams, NUM_CHUNCK_SAMPLES, signalPart, &miscFileStream);
    audio.insert(audio.end(), signalPart.begin(), signalPart.end());
  }

  if (enableConsoleOutput)
  {
    printf(" finished.\n");
  }

  // ****************************************************************
  // Close the misc file with the glottis signals.
  // ****************************************************************

  if (miscFileStream.is_open())
  {
    miscFileStream.close();
  }

  // ****************************************************************
  // Restore the current state of the glottis and the vocal tract.
  // ****************************************************************

  glottis->restoreControlParams();
  vocalTract->restoreControlParams();

  // Free the memory.

  delete synth;

  return true;
}


// ****************************************************************************
/// Synthesis (blocking) of a tube sequence from the data in a TXT file.
// ****************************************************************************

bool Synthesizer::synthesizeTubeSequence(string fileName, TdsModel *tdsModel, vector<double> &audio)
{
  // ****************************************************************
  // Open the file.
  // ****************************************************************

  ifstream file(fileName);

  if (file.is_open() == false)
  {
    printf("Error in synthesizeTubeSequence(): File could not be opened.\n");
    return false;
  }

  // ****************************************************************
  // Read the 14 comment lines, the glottis model type, and the 
  // number of states.
  // ****************************************************************

  int i, k;
  string line;

  for (i = 0; i < 14; i++)
  {
    getline(file, line);    // Just comment lines.
  }

  // ****************************************************************
  // Read the type of glottis model, the static glottis parameters,
  // and create the corresponding glottis model.
  // ****************************************************************

  getline(file, line);

  Glottis* glottis{ nullptr };
  GeometricGlottis2025* geometricGlottis2025 = new GeometricGlottis2025();
  GeometricGlottis2019 *geometricGlottis2019 = new GeometricGlottis2019();
  TriangularGlottis *triangularGlottis = new TriangularGlottis();

  if (line == geometricGlottis2025->getName())
  {
    glottis = geometricGlottis2025;
  }
  else if (line == geometricGlottis2019->getName())
  {
    glottis = geometricGlottis2019;
  }
  else if (line == triangularGlottis->getName())
  {
    glottis = triangularGlottis;
  }

  if (glottis == nullptr)
  {
    printf("Error in synthesizeTubeSequence(): The glottis model name does not exist.\n");
    delete geometricGlottis2025;
    delete geometricGlottis2019;
    delete triangularGlottis;
    return false;
  }

  // ****************************************************************

  bool paramsOk;
  double staticGlottisParams[256];
  int numStaticGlottisParams = (int)glottis->staticParams.size();
  getline(file, line);
  paramsOk = parseTextLine(line, numStaticGlottisParams, staticGlottisParams);

  if (paramsOk == false)
  {
    printf("Error in synthesizeTubeSequence(): Parsing the static glottis parameters failed.\n");
    delete geometricGlottis2025;
    delete geometricGlottis2019;
    delete triangularGlottis;
    return false;
  }

  for (i = 0; i < numStaticGlottisParams; i++)
  {
    glottis->staticParams[i].x = staticGlottisParams[i];
  }

  // ****************************************************************

  const int NUM_STATIC_TUBE_PARAMS = 4;
  double staticTubeParams[NUM_STATIC_TUBE_PARAMS];
  getline(file, line);
  paramsOk = parseTextLine(line, NUM_STATIC_TUBE_PARAMS, staticTubeParams);

  if (paramsOk == false)
  {
    printf("Error in synthesizeTubeSequence(): Parsing the static tube parameters failed.\n");
    delete geometricGlottis2025;
    delete geometricGlottis2019;
    delete triangularGlottis;
    return false;
  }

  // ****************************************************************

  double temp = 0.0;
  getline(file, line);
  if (parseTextLine(line, 1, &temp) == false)
  {
    printf("Error in synthesizeTubeSequence(): Invalid number of states.\n");
    delete geometricGlottis2025;
    delete geometricGlottis2019;
    delete triangularGlottis;
    return false;
  }

  int numStates = (int)temp;

  // ****************************************************************
  // Generate the audio signal.
  // ****************************************************************

  vector<double> signalPart;
  int numGlottisParams = (int)glottis->controlParams.size();
  Tube tube;
  double incisorPos = 0.0;
  double velumOpening = 0.0;
  double tongueTipSideElevation = 0.0;

  double glottisParams[Glottis::MAX_CONTROL_PARAMS];
  double miscData[3];    // For incisor position, nasal port area, and tongue tip side elevation.
  double tubeArea[Tube::NUM_PHARYNX_MOUTH_SECTIONS];
  double tubeLength[Tube::NUM_PHARYNX_MOUTH_SECTIONS];
  double tubeArticulatorDouble[Tube::NUM_PHARYNX_MOUTH_SECTIONS];
  Tube::Articulator tubeArticulator[Tube::NUM_PHARYNX_MOUTH_SECTIONS];
  
  bool glottisParamsOk = true;
  bool miscDataOk = true;
  bool tubeAreaOk = true;
  bool tubeLengthOk = true;
  bool tubeArticulatorOk = true;
  bool stateOk = true;

  Synthesizer *synth = new Synthesizer();
  synth->init(glottis, NULL, tdsModel);     // Vocal tract model is not needed here (= NULL).
  audio.resize(0);

  // Set static tube params only once.
  tube.initStaticSections(staticTubeParams[0], staticTubeParams[1], staticTubeParams[2], staticTubeParams[3]);

  for (i = 0; (i < numStates) && (stateOk); i++)
  {
    getline(file, line);
    glottisParamsOk = parseTextLine(line, numGlottisParams, glottisParams);

    getline(file, line);
    miscDataOk = parseTextLine(line, 3, miscData);

    getline(file, line);
    tubeAreaOk = parseTextLine(line, Tube::NUM_PHARYNX_MOUTH_SECTIONS, tubeArea);

    getline(file, line);
    tubeLengthOk = parseTextLine(line, Tube::NUM_PHARYNX_MOUTH_SECTIONS, tubeLength);

    getline(file, line);
    tubeArticulatorOk = parseTextLine(line, Tube::NUM_PHARYNX_MOUTH_SECTIONS, tubeArticulatorDouble);

    // **************************************************************

    if ((glottisParamsOk) && (miscDataOk) && (tubeAreaOk) && (tubeLengthOk) && (tubeArticulatorOk))
    {
      stateOk = true;

      // Convert the type of the tube articulators.
      for (k = 0; k < Tube::NUM_PHARYNX_MOUTH_SECTIONS; k++)
      {
        tubeArticulator[k] = (Tube::Articulator)((int)tubeArticulatorDouble[k]);
      }
      incisorPos = miscData[0];
      velumOpening = miscData[1];
      tongueTipSideElevation = miscData[2];

      tube.setPharynxMouthSections(tubeLength, tubeArea, tubeArticulator);
      tube.setAuxParams(velumOpening, incisorPos, tongueTipSideElevation);

      synth->addChunk(glottisParams, &tube, NUM_CHUNCK_SAMPLES, signalPart);
      audio.insert(audio.end(), signalPart.begin(), signalPart.end());
    }
    else
    {
      stateOk = false;
    }
  }

  // Close the file.
  file.close();

  // Delete the Synthesizer and Glottis objects.
  delete synth;
  delete geometricGlottis2025;
  delete geometricGlottis2019;
  delete triangularGlottis;

  if (stateOk)
  {
    printf("The tube sequence was synthesized with %d states.\n", numStates);
    return true;
  }
  else
  {
    printf("Error: The tube sequence file was corrupted.\n");
    return false;
  }
}


// ****************************************************************************
/// Synthesize (blocking) a static sound with the given glottis and vocal tract.
/// If (useConstantF0 == true), the synthesis will use the (constant) f0 that is
/// set in the glottis model. Otherwise, a new f0 trajectory is imposed.
// ****************************************************************************

void Synthesizer::synthesizeStaticPhoneme(Glottis *glottis, VocalTract *vocalTract,
  TdsModel *tdsModel, bool shortLength, bool useConstantF0, vector<double> &audio)
{
  int i;
  Synthesizer *synth = new Synthesizer();
  vector<double> signalPart;

  // ****************************************************************
  // Obtain the arrays with tract and glottis parameters.
  // ****************************************************************

  double tractParams[VocalTract::NUM_PARAMS];
  double glottisParams[Glottis::MAX_CONTROL_PARAMS];

  int numGlottisParams = (int)glottis->controlParams.size();
  for (i = 0; i < numGlottisParams; i++)
  {
    glottisParams[i] = glottis->controlParams[i].x;
  }
  
  for (i = 0; i < VocalTract::NUM_PARAMS; i++)
  {
    tractParams[i] = vocalTract->params[i].x;
  }

  // ****************************************************************
  // Save the current state of the glottis and the vocal tract.
  // ****************************************************************

  glottis->storeControlParams();
  vocalTract->storeControlParams();

  // ****************************************************************
  // Generate the audio signal in three sections.
  // ****************************************************************

  synth->init(glottis, vocalTract, tdsModel);
  audio.resize(0);

  // Pressure starts at 0 dPa.

  glottisParams[Glottis::PRESSURE] = 0.0;
  if (useConstantF0 == false)
  {
    glottisParams[Glottis::FREQUENCY] = 110;
  }
  synth->addChunk(glottisParams, tractParams, 0, signalPart);
  audio.insert(audio.end(), signalPart.begin(), signalPart.end());

  // Pressure rises up to 800 Pa;

  for (i = 0; i < 10; i++)
  {
    double factor = 0.5 * (-cos(M_PI * i / 10.0) + 1.0);
    glottisParams[Glottis::PRESSURE] = 8000.0 * factor;    // in dPa
    synth->addChunk(glottisParams, tractParams, (int)(0.005 * AUDIO_SAMPLING_RATE_HZ), signalPart);
    audio.insert(audio.end(), signalPart.begin(), signalPart.end());
  }

  // Stationary part.

  glottisParams[Glottis::PRESSURE] = 8000.0;    // in dPa
  if (useConstantF0 == false)
  {
    glottisParams[Glottis::FREQUENCY] = 100;
  }

  int numSamples = 0;
  if (shortLength)
  {
    numSamples = (int)(0.200 * AUDIO_SAMPLING_RATE_HZ);    // 300 ms
  }
  else
  {
    numSamples = (int)(0.400 * AUDIO_SAMPLING_RATE_HZ);    // 600 ms
  }
  synth->addChunk(glottisParams, tractParams, numSamples, signalPart);
  audio.insert(audio.end(), signalPart.begin(), signalPart.end());

  // Pressure is falling back to zero.

  for (i = 0; i < 10; i++)
  {
    double factor = 0.5 * (cos(M_PI * (i + 1) / 10.0) + 1.0);
    glottisParams[Glottis::PRESSURE] = 8000.0 * factor;    // in dPa
    synth->addChunk(glottisParams, tractParams, (int)(0.005 * AUDIO_SAMPLING_RATE_HZ), signalPart);
    audio.insert(audio.end(), signalPart.begin(), signalPart.end());
  }

  // Pressure stays zero until the impulse response of the vocal tract
  // completely decayed.

  glottisParams[Glottis::PRESSURE] = 0.0;    // in dPa
  synth->addChunk(glottisParams, tractParams, (int)(0.030 * AUDIO_SAMPLING_RATE_HZ), signalPart);
  audio.insert(audio.end(), signalPart.begin(), signalPart.end());

  // ****************************************************************
  // Restore the previous state of the vocal tract and glottis.
  // ****************************************************************

  glottis->restoreControlParams();
  vocalTract->restoreControlParams();

  delete synth;
}


// ****************************************************************************
// Determine the subglottal impedance in the time domain.
// ****************************************************************************

void Synthesizer::calcSubglottalImpedance(TdsModel* tdsModel, 
  vector<double>& impulseResponse, ComplexSignal* spectrum)
{
  double flowSource_cm3_s;
  double mouthFlow_cm3_s;
  double nostrilFlow_cm3_s;
  double skinFlow_cm3_s;
  Tube tube;
  int i;
  
  impulseResponse.resize(IMPULSE_RESPONSE_LENGTH);

  // Reset the TDS model for the synthesis and close the glottis.
  tdsModel->resetMotion();
  tdsModel->getTube(&tube);
  tube.resetGlottisSections(0.0);
  tdsModel->setTube(&tube);

  // ****************************************************************
  // Main loop.
  // ****************************************************************

  for (i = 0; i < IMPULSE_RESPONSE_LENGTH; i++)
  {
    flowSource_cm3_s = 0.0;
    if (i == 0)
    {
      flowSource_cm3_s = -1.0;  // Flow pulse in upstream direction.
    }
    tdsModel->setFlowSource(flowSource_cm3_s, Tube::LAST_TRACHEA_SECTION);
    tdsModel->setPressureSource(0.0, -1);
    tdsModel->proceedTimeStep(mouthFlow_cm3_s, nostrilFlow_cm3_s, skinFlow_cm3_s);
    impulseResponse[i] = tdsModel->getSectionPressure(Tube::LAST_TRACHEA_SECTION);
  }

  spectrum->reset(IMPULSE_RESPONSE_LENGTH);
  for (i = 0; i < IMPULSE_RESPONSE_LENGTH; i++)
  {
    spectrum->re[i] = impulseResponse[i];
    spectrum->im[i] = 0.0;
  }
  complexFFT(*spectrum, IMPULSE_RESPONSE_EXPONENT, true);
  (*spectrum) *= 3000.0;
}


// ****************************************************************************
// Determine the supraglottal impedance in the time domain.
// ****************************************************************************

void Synthesizer::calcSupraglottalImpedance(TdsModel* tdsModel,
  vector<double>& impulseResponse, ComplexSignal* spectrum)
{
  double flowSource_cm3_s;
  double mouthFlow_cm3_s;
  double nostrilFlow_cm3_s;
  double skinFlow_cm3_s;
  Tube tube;
  int i;

  impulseResponse.resize(IMPULSE_RESPONSE_LENGTH);

  // Reset the TDS model for the synthesis and close the glottis.
  tdsModel->resetMotion();
  tdsModel->getTube(&tube);
  tube.resetGlottisSections(0.0);
  tdsModel->setTube(&tube);

  // ****************************************************************
  // Main loop.
  // ****************************************************************

  for (i = 0; i < IMPULSE_RESPONSE_LENGTH; i++)
  {
    flowSource_cm3_s = 0.0;
    if (i == 0)
    {
      flowSource_cm3_s = 1.0;  // Flow pulse in downstream direction.
    }
    tdsModel->setFlowSource(flowSource_cm3_s, Tube::FIRST_PHARYNX_SECTION);
    tdsModel->setPressureSource(0.0, -1);
    tdsModel->proceedTimeStep(mouthFlow_cm3_s, nostrilFlow_cm3_s, skinFlow_cm3_s);
    impulseResponse[i] = tdsModel->getSectionPressure(Tube::FIRST_PHARYNX_SECTION);
  }

  spectrum->reset(IMPULSE_RESPONSE_LENGTH);
  for (i = 0; i < IMPULSE_RESPONSE_LENGTH; i++)
  {
    spectrum->re[i] = impulseResponse[i];
    spectrum->im[i] = 0.0;
  }
  complexFFT(*spectrum, IMPULSE_RESPONSE_EXPONENT, true);
  (*spectrum) *= 3000.0;
}


// ****************************************************************************
// Determine the volume-velocity transfer function in the time domain.
// ****************************************************************************

void Synthesizer::calcTransferFunction(TdsModel* tdsModel,
  vector<double>& impulseResponse, ComplexSignal* spectrum)
{
  double flowSource_cm3_s;
  double mouthFlow_cm3_s;
  double nostrilFlow_cm3_s;
  double skinFlow_cm3_s;
  Tube tube;
  int i;

  impulseResponse.resize(IMPULSE_RESPONSE_LENGTH);

  // Reset the TDS model for the synthesis and close the glottis.
  tdsModel->resetMotion();
  tdsModel->getTube(&tube);
  tube.resetGlottisSections(0.0);
  tdsModel->setTube(&tube);

  // ****************************************************************
  // Main loop.
  // ****************************************************************

  for (i = 0; i < IMPULSE_RESPONSE_LENGTH; i++)
  {
    flowSource_cm3_s = 0.0;
    if (i == 0)
    {
      flowSource_cm3_s = 1.0;  // Flow pulse in downstream direction.
    }
    tdsModel->setFlowSource(flowSource_cm3_s, Tube::FIRST_PHARYNX_SECTION);
    tdsModel->setPressureSource(0.0, -1);
    impulseResponse[i] = tdsModel->proceedTimeStep(mouthFlow_cm3_s, nostrilFlow_cm3_s, skinFlow_cm3_s);
  }

  spectrum->reset(IMPULSE_RESPONSE_LENGTH);
  for (i = 0; i < IMPULSE_RESPONSE_LENGTH; i++)
  {
    spectrum->re[i] = impulseResponse[i];
    spectrum->im[i] = 0.0;
  }
  complexFFT(*spectrum, IMPULSE_RESPONSE_EXPONENT, true);
  (*spectrum) *= 30000.0;
}


// ****************************************************************************
/// Generate a text file that contains a sequence of vocal fold control 
/// parameters and enhanced area functions (in steps of 120 samples or 2.5 ms
/// @48 kHz) from the given gestural score.
// ****************************************************************************

bool Synthesizer::gesturalScoreToTubeSequenceFile(GesturalScore *gesturalScore, string fileName)
{
  int i, k;
  double tractParams[VocalTract::NUM_PARAMS];
  double glottisParams[Glottis::MAX_CONTROL_PARAMS];
  int numGlottisParams = (int)gesturalScore->glottis->controlParams.size();
  int scoreLength_pt = gesturalScore->getDuration_pt();
  int numStates = (int)(scoreLength_pt / NUM_CHUNCK_SAMPLES) + 2;
  double pos_s = 0.0;
  Tube tube;

  ofstream file;
  file.open(fileName);
  if (file.is_open() == false)
  {
    printf("Error in gesturalScoreToTubeSequenceFile(): The file could not be opened!\n");
    return false;
  }

  // ****************************************************************
  // Save the current state of the vocal tract.
  // ****************************************************************

  gesturalScore->vocalTract->storeControlParams();

  // ****************************************************************
  // Write some header data into the file.
  // ****************************************************************

  file << "# The first four lines (below the comment lines) contain:" << endl;
  file << "#   The name of the vocal fold model" << endl;
  file << "#   The static vocal fold model parameters" << endl;
  file << "#   The static tube model parameters" << endl;
  file << "#   The number of states." << endl;
  file << "# The following lines contain a sequence of states of the vocal folds and the tube geometry" << endl;
  file << "# in steps of 120 audio samples (corresponding to 2.5 ms for the sampling rate of 48000 Hz)." << endl;
  file << "# Each state is represented in terms of five lines:" << endl;
  file << "#   Line 1: glottis_param_0 glottis_param_1 ..." << endl;
  file << "#   Line 2: incisor_position_in_cm, velo_pharyngeal_opening_in_cm^2, tongue_tip_side_elevation[-1...1]" << endl;
  file << "#   Line 3: area0 area1 area2 area3 ... (Areas of the tube sections in cm^2 from glottis to mouth)" << endl;
  file << "#   Line 4: length0 length1 length2 length3 ... (Lengths of the tube sections in cm from glottis to mouth)" << endl;
  file << "#   Line 5: artic0 artic1 artic2 artic3 ... (Articulators of the tube sections between glottis and lips : 1 = tongue; 2 = lower incisors; 3 = lower lip; 4 = other)" << endl;
  file << "#" << endl;

  // Make that we write four post-decimal positions for floating point values.
  file << std::fixed << std::setprecision(4);

  // Write the name of the glottis model.
  Glottis* glottis = gesturalScore->glottis;
  file << glottis->getName() << endl;

  // Write the static glottis parameters.
  
  for (k = 0; k < (int)glottis->staticParams.size(); k++)
  {
    file << glottis->staticParams[k].x << " ";
  }
  file << endl;

  // Write the static tube parameters.
  
  gesturalScore->vocalTract->calculateAll();
  gesturalScore->vocalTract->getTube(&tube);

  double tracheaLength_cm;
  double noseLength_cm;
  double piriformFossaLength_cm;
  double piriformFossaVolume_cm3;

  tube.getStaticParams(tracheaLength_cm, noseLength_cm, 
    piriformFossaLength_cm, piriformFossaVolume_cm3);

  file << tracheaLength_cm << " ";
  file << noseLength_cm << " ";
  file << piriformFossaLength_cm << " ";
  file << piriformFossaVolume_cm3 << " ";
  file << endl;

  // Write the number of states.
  file << numStates << endl;

  // Important: Calc. the parameter curves from the gestural score.
  gesturalScore->calcCurves();

  // ****************************************************************
  // Write the vocal tract and glottis parameters to the file every
  // 120 samples (=2.5 ms).
  // ****************************************************************

  double nasalPortArea_cm2;
  double incisorPos_cm;
  double tongueTipSideElevation;

  printf("Writing the tube sequence file started ...");

  for (i = 0; i < numStates; i++)
  {
    if ((i & 63) == 0)
    {
      printf(".");
    }

    pos_s = (double)i * NUM_CHUNCK_SAMPLES / AUDIO_SAMPLING_RATE_HZ;
    gesturalScore->getParams(pos_s, tractParams, glottisParams);

    // Calculate the vocal tract with the new parameters.

    for (k = 0; k < VocalTract::NUM_PARAMS; k++)
    {
      gesturalScore->vocalTract->params[k].x = tractParams[k];
    }
    gesturalScore->vocalTract->calculateAll();

    // Transform the vocal tract model into a tube.
    gesturalScore->vocalTract->getTube(&tube);

    // Write the new parameters to the file

    for (k = 0; k < numGlottisParams; k++)
    {
      file << glottisParams[k] << " ";
    }
    file << endl;

    tube.getAuxParams(nasalPortArea_cm2, incisorPos_cm, tongueTipSideElevation);
    file << incisorPos_cm << " " << nasalPortArea_cm2 << " " << tongueTipSideElevation << endl;

    for (k = 0; k < Tube::NUM_PHARYNX_MOUTH_SECTIONS; k++)
    {
      file << tube.pharynxMouthSections[k].area_cm2 << " ";
    }
    file << endl;

    for (k = 0; k < Tube::NUM_PHARYNX_MOUTH_SECTIONS; k++)
    {
      file << tube.pharynxMouthSections[k].length_cm << " ";
    }
    file << endl;

    for (k = 0; k < Tube::NUM_PHARYNX_MOUTH_SECTIONS; k++)
    {
      file << tube.pharynxMouthSections[k].articulator << " ";
    }
    file << endl;
  }

  // ****************************************************************
  // Restore the current state of the vocal tract.
  // ****************************************************************

  gesturalScore->vocalTract->restoreControlParams();

  // Close the file.
  file.close();

  printf("finished.\n");

  return true;
}


// ****************************************************************************
/// Export text files with the coordinates of the vocal tract outline from a
/// gestural score at a frame rate of 100 Hz.
// ****************************************************************************

bool Synthesizer::gesturalScoreToTractContourFiles(GesturalScore* gesturalScore, string folderName)
{
  const double FRAME_RATE_HZ = 100;
  double duration_s = gesturalScore->getScoreDuration_s();
  int numFrames = (int)(duration_s * FRAME_RATE_HZ);
  int i;
  int frameIndex;
  double time_s;
  double oldVocalTractParams[VocalTract::NUM_PARAMS];
  double vocalTractParams[VocalTract::NUM_PARAMS];
  double glottisParams[Glottis::MAX_CONTROL_PARAMS];  // only needed as dummy array.
  VocalTract* vocalTract = gesturalScore->vocalTract;

  // ****************************************************************
  // Keep in mind the current vocal tract state.
  // ****************************************************************

  for (i = 0; i < VocalTract::NUM_PARAMS; i++)
  {
    oldVocalTractParams[i] = vocalTract->params[i].x;
  }

  // ****************************************************************
  // Save one text file with the contour coordinates for each frame.
  // ****************************************************************

  if (folderName.empty())
  {
    return false;
  }
  // Potentiall add a path separator at the end.
  if ((folderName.back() != '/') && (folderName.back() != '\\'))
  {
    folderName += '/';    // Universal path separator that works for all OS.
  }

  std::string fileName;
  char st[256];

  for (frameIndex = 0; frameIndex < numFrames; frameIndex++)
  {
    // Calculate the vocal tract shape at the current frame.

    time_s = (double)frameIndex / (double)FRAME_RATE_HZ;
    gesturalScore->getParams(time_s, vocalTractParams, glottisParams);

    for (i = 0; i < VocalTract::NUM_PARAMS; i++)
    {
      vocalTract->params[i].x = vocalTractParams[i];
    }

    vocalTract->calculateAll();

    // Save the current vocal tract contour as a text file.
    sprintf(st, "%04d", frameIndex);
    fileName = folderName + "contour" + std::string(st) + ".txt";
    vocalTract->exportTractContourTxt(fileName);
  }

  // ****************************************************************
  // Restore the old vocal tract state.
  // ****************************************************************

  for (i = 0; i < VocalTract::NUM_PARAMS; i++)
  {
    vocalTract->params[i].x = oldVocalTractParams[i];
  }
  vocalTract->calculateAll();

  printf("Finished exporting %d vocal tract contour files at a frame rate of %2.2f Hz.\n",
    numFrames, FRAME_RATE_HZ);

  return true;
}


// ****************************************************************************
/// Exports the virtual EMA trajectories calculated from the current gestural
/// score at a rate of 200 Hz.
// ****************************************************************************

bool Synthesizer::gesturalScoreToEmaTrajectories(GesturalScore* gesturalScore, string fileName)
{
  int i, k;
  VocalTract* vocalTract = gesturalScore->vocalTract;

  if (fileName.empty())
  {
    return false;
  }

  ofstream os(fileName);
  if (!os)
  {
    printf("Error in gesturalScoreToEmaTrajectories(): The file could not be opened!\n");
    return false;
  }

  // ****************************************************************
  // Write the header.
  // ****************************************************************

  VocalTract::EmaPoint* p;
  int numEmaPoints = (int)vocalTract->emaPoints.size();

  os << "time[s] ";
  for (i = 0; i < numEmaPoints; i++)
  {
    p = &vocalTract->emaPoints[i];
    os << p->name << "-x[cm] " << p->name << "-y[cm] ";
  }
  os << endl;

  // ****************************************************************
  // Keep in mind the current vocal tract state.
  // ****************************************************************

  double oldTractParams[VocalTract::NUM_PARAMS];
  for (i = 0; i < VocalTract::NUM_PARAMS; i++)
  {
    oldTractParams[i] = vocalTract->params[i].x;
  }

  // ****************************************************************
  // Write the data.
  // ****************************************************************

  const double EMA_SAMPLING_RATE_HZ = 200.0;
  double tractParams[VocalTract::NUM_PARAMS];
  double glottisParams[256];
  double t_s;
  Point3D Q;

  int numFrames = (int)(gesturalScore->getScoreDuration_s() * EMA_SAMPLING_RATE_HZ);

  os << setprecision(8);

  for (k = 0; k < numFrames; k++)
  {
    t_s = (double)k / (double)EMA_SAMPLING_RATE_HZ;
    gesturalScore->getParams(t_s, tractParams, glottisParams);

    for (i = 0; i < VocalTract::NUM_PARAMS; i++)
    {
      vocalTract->params[i].x = tractParams[i];
    }
    vocalTract->calculateAll();

    // Write data to file.

    os << t_s << " ";
    for (i = 0; i < numEmaPoints; i++)
    {
      Q = vocalTract->getEmaPointCoord(i);
      os << Q.x << " " << Q.y << " ";
    }
    os << endl;
  }

  printf("Finished exporting the EMA trajectories.\n");

  // ****************************************************************
  // Set back the old vocal tract state.
  // ****************************************************************

  for (i = 0; i < VocalTract::NUM_PARAMS; i++)
  {
    vocalTract->params[i].x = oldTractParams[i];
  }
  vocalTract->calculateAll();

  // ****************************************************************
  // Close the file.
  // ****************************************************************

  os.close();

  return true;
}


// ****************************************************************************
/// Exports the volume velocity transfer functions (closed glottis condition)
/// for every single millisecond in a gestural score (sampling rate = 1000 Hz).
// ****************************************************************************

bool Synthesizer::gesturalScoreToTransferFunctions(GesturalScore* gesturalScore, TlModel *tlModel, string fileName)
{
  const int SPECTRUM_LENGTH = 8192;
  const int FRAME_RATE = 1000;
  VocalTract* vocalTract = gesturalScore->vocalTract;
  double duration_s = gesturalScore->getScoreDuration_s();
  int numFrames = (int)(duration_s * FRAME_RATE);
  int i;
  int frameIndex;
  double time_s;
  double oldVocalTractParams[VocalTract::NUM_PARAMS];
  double vocalTractParams[VocalTract::NUM_PARAMS];
  double glottisParams[256];
  ComplexSignal spectrum(SPECTRUM_LENGTH);

  // ****************************************************************
  // Keep in mind the current vocal tract state.
  // ****************************************************************

  for (i = 0; i < VocalTract::NUM_PARAMS; i++)
  {
    oldVocalTractParams[i] = vocalTract->params[i].x;
  }

  // ****************************************************************
  // Save one transfer function spectrum every millisecond
  // (all into the same file).
  // ****************************************************************

  // Open the text file.

  ofstream os(fileName);
  if (!os)
  {
    printf("Error in gesturalScoreToTransferFunctions(): The file could not be opened!\n");
    return false;
  }

  // Write the header.

  os << "# This file contains " << numFrames << " volume velocity transfer functions between "
    "the glottis and the lips for the closed-glottis condition obtained from a gestural score. "
    "There is one transfer function for every millisecond of the gestural score. "
    "Each transfer function has 8192 samples that represent the frequencies from 0 to 48000 Hz. "
    "The samples 0 ... 4095 (= 0 ... 24000 Hz) represent the positive frequencies, and the "
    "remaining samples the negative frequencies (mirror image of the positive frequency part). "
    "The frequency resolution of the transfer functions is hence 48000/8192 = 5.86 Hz. "
    "There are two text lines per transfer function: The first line contains the magnitude "
    "samples and the second line contains the phase samples in rad." << endl;

  os << setprecision(5);    // 5 post-decimal positions for floating point numbers

  // Write all the transfer functions in a loop.

  printf("Writing %d transfer functions to text file .", numFrames);

  for (frameIndex = 0; frameIndex < numFrames; frameIndex++)
  {
    if ((frameIndex % 10) == 0)
    {
      printf(".");
    }

    // Calculate the vocal tract shape at the current frame.

    time_s = (double)frameIndex / (double)FRAME_RATE;
    gesturalScore->getParams(time_s, vocalTractParams, glottisParams);

    for (i = 0; i < VocalTract::NUM_PARAMS; i++)
    {
      vocalTract->params[i].x = vocalTractParams[i];
    }
    vocalTract->calculateAll();

    // Set the latest vocal tract geometry for the transmission line model.   
    vocalTract->getTube(&tlModel->tube);
    tlModel->tube.resetGlottisSections(0.0);

    // Obtain the volume velocity transfer function.
    tlModel->getSpectrum(TlModel::FLOW_SOURCE_TF, &spectrum, SPECTRUM_LENGTH, Tube::FIRST_PHARYNX_SECTION);

    // Write magnitude.
    for (i = 0; i < SPECTRUM_LENGTH; i++)
    {
      os << spectrum.getMagnitude(i) << " ";
    }
    os << endl;

    // Write phase.
    for (i = 0; i < SPECTRUM_LENGTH; i++)
    {
      os << spectrum.getPhase(i) << " ";
    }
    os << endl;
  }

  printf(" done.\n");

  // ****************************************************************
  // Restore the old vocal tract state.
  // ****************************************************************

  for (i = 0; i < VocalTract::NUM_PARAMS; i++)
  {
    vocalTract->params[i].x = oldVocalTractParams[i];
  }
  vocalTract->calculateAll();
  vocalTract->getTube(&tlModel->tube);
  tlModel->tube.resetGlottisSections(0.0);

  return true;
}


// ****************************************************************************
/// Parse a text line (string) and obtain the space-separated numeric values.
// ****************************************************************************

bool Synthesizer::parseTextLine(string line, int numValues, double *values)
{
  int i;
  bool ok = true;
  istringstream iss(line);

  for (i = 0; (i < numValues) && (ok); i++)
  {
    if (!(iss >> values[i]))
    {
      ok = false;
    }
  }

  return ok;
}

// ****************************************************************************
