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

#include "api/VocalTractLabApi.h"
#include "dsp/Dsp.h"
#include "io/SoundLib.h"
#include "io/AudioFile.h"
#include "synthesis/Synthesizer.h"
#include "phonetics/SegmentSequence.h"

#include "glottis/GeometricGlottis2019.h"
#include "glottis/GeometricGlottis2025.h"
#include "glottis/TriangularGlottis.h"

#include "anatomy/VocalTract.h"
#include "acoustics/TdsModel.h"
#include "synthesis/GesturalScore.h"
#include "io/XmlHelper.h"
#include "io/XmlNode.h"
#include "acoustics/TlModel.h"

#include <iostream>
#include <fstream>

enum GlottisModel
{
  GEOMETRIC_GLOTTIS_2025,
  GEOMETRIC_GLOTTIS_2019,
  TRIANGULAR_GLOTTIS,
  NUM_GLOTTIS_MODELS
};

// For the tract-based synthesis.
static Glottis *glottises[NUM_GLOTTIS_MODELS];
static int selectedGlottis;

// For the tube-based synthesis.
static Glottis* tubeGlottis = NULL;
static Tube* tube = NULL;

static VocalTract *vocalTract = NULL;
static TdsModel *tdsModel = NULL;
static Synthesizer *synthesizer = NULL;

static bool vtlApiInitialized = false;


#if defined(WIN32) && defined(_USRDLL) 

// ****************************************************************************
/// Windows entry point for the DLL.
// ****************************************************************************

// Windows Header Files
#include <windows.h>

BOOL APIENTRY DllMain( HANDLE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved)
{
  switch (ul_reason_for_call)
	{
		case DLL_PROCESS_ATTACH:
		case DLL_THREAD_ATTACH:
		case DLL_THREAD_DETACH:
		case DLL_PROCESS_DETACH:
	  break;
  }
  return TRUE;
}

#endif  // WIN32 && _USRDLL


// ****************************************************************************
// Loads the VT anatomy and the configurations for the different glottis 
// models from a speaker file.
// This function is not visible in the interface.
// ****************************************************************************

bool vtlLoadSpeaker(const char *speakerFileName, VocalTract *vocalTract, 
  Glottis *glottis[], int &selectedGlottis)
{

  // ****************************************************************
  // Load the XML data from the speaker file.
  // ****************************************************************

  vector<XmlError> xmlErrors;
  XmlNode *rootNode = xmlParseFile(string(speakerFileName), "speaker", &xmlErrors);
  if (rootNode == NULL)
  {
    xmlPrintErrors(xmlErrors);
    return false;
  }

  // ****************************************************************
  // Load the data for the glottis models.
  // ****************************************************************

  // This may be overwritten later.
  selectedGlottis = GEOMETRIC_GLOTTIS_2025;

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
          selectedGlottis = i;
        }
        if (glottis[i]->readFromXml(*glottisNode) == false)
        {
          printf("Error: Failed to read glottis data for glottis model %d!\n", i);
          delete rootNode;
          return false;
        }
      }
      else
      {
        printf("Error: The type of the glottis model %d in the speaker file is '%s' "
          "but should be '%s'!\n", i, 
          glottisNode->getAttributeString("type").c_str(), 
          glottis[i]->getName().c_str());

        delete rootNode;
        return false;
      }
    }
  }
  else
  {
    printf("Warning: No glottis model data found in the speaker file %s!\n", speakerFileName);
  }

  // Free the memory of the XML tree !
  delete rootNode;

  // ****************************************************************
  // Load the vocal tract anatomy and vocal tract shapes.
  // ****************************************************************

  try
  {
    vocalTract->readFromXml(string(speakerFileName));
    vocalTract->calculateAll();
  }
  catch (std::string st)
  {
    printf("%s\n", st.c_str());
    printf("Error reading the anatomy data from %s.\n", speakerFileName);
    return false;
  }

  return true;
}


// ****************************************************************************
// Init. the synthesis with the given speaker file name, e.g. "JD2.speaker".
// This function should be called before any other function of this API.
// Return values:
// 0: success.
// 1: Loading the speaker file failed.
// ****************************************************************************

int vtlInitialize(const char *speakerFileName)
{
  if (vtlApiInitialized)
  {
    vtlClose();
  }

  // ****************************************************************
  // Init the vocal tract.
  // ****************************************************************

  vocalTract = new VocalTract();
  vocalTract->calculateAll();

  // ****************************************************************
  // Init the list with glottis models
  // ****************************************************************

  glottises[GEOMETRIC_GLOTTIS_2025] = new GeometricGlottis2025();
  glottises[GEOMETRIC_GLOTTIS_2019] = new GeometricGlottis2019();
  glottises[TRIANGULAR_GLOTTIS] = new TriangularGlottis();
  
  selectedGlottis = GEOMETRIC_GLOTTIS_2025;

  bool ok = vtlLoadSpeaker(speakerFileName, vocalTract, glottises, selectedGlottis);

  if (ok == false)
  {
    int i;
    for (i = 0; i < NUM_GLOTTIS_MODELS; i++)
    {
      delete glottises[i];
    }
    delete vocalTract;

    printf("Error in vtlInitialize(): vtlLoadSpeaker() failed.\n");
    return 1;
  }

  // ****************************************************************
  // Init the object for the time domain simulation.
  // ****************************************************************

  tdsModel = new TdsModel();

  // ****************************************************************
  // Init the Synthesizer object.
  // ****************************************************************

  synthesizer = new Synthesizer();
  synthesizer->init(glottises[selectedGlottis], vocalTract, tdsModel);

  tube = new Tube();

  // We are now initialized!
  vtlApiInitialized = true;

  return 0;
}


// ****************************************************************************
// Clean up the memory and shut down the synthesizer.
// Return values:
// 0: success.
// 1: The API was not initialized.
// ****************************************************************************

int vtlClose()
{
  if (!vtlApiInitialized)
  {
    printf("Error: The API was not initialized.\n");
    return 1;
  }

  delete synthesizer;
  delete tdsModel;

  int i;
  for (i = 0; i < NUM_GLOTTIS_MODELS; i++)
  {
    delete glottises[i];
  }

  delete vocalTract;
  delete tube;

  if (tubeGlottis != NULL)
  {
    delete tubeGlottis;
    tubeGlottis = NULL;
  }

  vtlApiInitialized = false;

  return 0;
}


// ****************************************************************************
// Returns the version of this API as a string that contains the compile data.
// Reserve at least 32 chars for the string.
// ****************************************************************************

void vtlGetVersion(char *version)
{
  strcpy(version, __DATE__);
}


// ****************************************************************************
// Returns a couple of constants:
// o The audio sampling rate of the synthesized signal (should be 48000).
// o The number of supraglottal tube sections.
// o The number of vocal tract model parameters.
// o The number of glottis model parameters.
//
// Function return value:
// 0: success.
// 1: The API has not been initialized.
// ****************************************************************************

int vtlGetConstants(int *audioSamplingRate, int *numTubeSections,
  int *numVocalTractParams, int *numGlottisParams)
{
  if (!vtlApiInitialized)
  {
    printf("Error: The API has not been initialized.\n");
    return 1;
  }

  *audioSamplingRate = AUDIO_SAMPLING_RATE_HZ;
  *numTubeSections = Tube::NUM_PHARYNX_MOUTH_SECTIONS;
  *numVocalTractParams = VocalTract::NUM_PARAMS;
  *numGlottisParams = (int)glottises[selectedGlottis]->controlParams.size();

  return 0;
}


// ****************************************************************************
// Returns for each vocal tract parameter the minimum value, the maximum value,
// and the neutral value. Each vector passed to this function must have at 
// least as many elements as the number of vocal tract model parameters.
// The "names" string receives the abbreviated names of the parameters separated
// by spaces. This string should have at least 10*numParams elements.
//
// Function return value:
// 0: success.
// 1: The API has not been initialized.
// ****************************************************************************

int vtlGetTractParamInfo(char *names, double *paramMin, double *paramMax, double *paramNeutral)
{
  if (!vtlApiInitialized)
  {
    printf("Error: The API has not been initialized.\n");
    return 1;
  }

  int i;

  strcpy(names, "");

  for (i=0; i < VocalTract::NUM_PARAMS; i++)
  {
    strcat(names, vocalTract->params[i].abbr.c_str());
    if (i != VocalTract::NUM_PARAMS - 1)
    {
      strcat(names, " ");
    }

    paramMin[i] = vocalTract->params[i].min;
    paramMax[i] = vocalTract->params[i].max;
    paramNeutral[i] = vocalTract->params[i].neutral;
  }

  return 0;
}


// ****************************************************************************
// Returns for each glottis model parameter the minimum value, the maximum value,
// and the neutral value. Each vector passed to this function must have at 
// least as many elements as the number of glottis model parameters.
// The "names" string receives the abbreviated names of the parameters separated
// by spaces. This string should have at least 10*numParams elements.
//
// Function return value:
// 0: success.
// 1: The API has not been initialized.
// ****************************************************************************

int vtlGetGlottisParamInfo(char *names, double *paramMin, double *paramMax, double *paramNeutral)
{
  if (!vtlApiInitialized)
  {
    printf("Error: The API has not been initialized.\n");
    return 1;
  }

  int i;
  int numGlottisParams = (int)glottises[selectedGlottis]->controlParams.size();

  strcpy(names, "");

  for (i=0; i < numGlottisParams; i++)
  {
    strcat(names, glottises[selectedGlottis]->controlParams[i].abbr.c_str());
    if (i != VocalTract::NUM_PARAMS - 1)
    {
      strcat(names, " ");
    }

    paramMin[i] = glottises[selectedGlottis]->controlParams[i].min;
    paramMax[i] = glottises[selectedGlottis]->controlParams[i].max;
    paramNeutral[i] = glottises[selectedGlottis]->controlParams[i].neutral;
  }

  return 0;
}


// ****************************************************************************
// Returns the vocal tract parameters for the given shape as defined in the
// speaker file.
// The vector passed to this function must have at least as many elements as 
// the number of vocal tract model parameters.
//
// Function return value:
// 0: success.
// 1: The API has not been initialized.
// 2: A shape with the given name does not exist.
// ****************************************************************************

int vtlGetTractParams(const char *shapeName, double *param)
{
  if (!vtlApiInitialized)
  {
    printf("Error: The API has not been initialized.\n");
    return 1;
  }

  int index = vocalTract->getShapeIndex(string(shapeName));
  if (index == -1)
  {
    return 2;
  }

  int i;
  for (i=0; i < VocalTract::NUM_PARAMS; i++)
  {
    param[i] = vocalTract->shapes[index].param[i];
  }

  return 0;
}


// ****************************************************************************
// Exports the vocal tract contours for the given vector of vocal tract
// parameters as a SVG file (scalable vector graphics).
//
// Function return value:
// 0: success.
// 1: The API has not been initialized.
// 2: Writing the SVG file failed.
// ****************************************************************************

int vtlExportTractSvg(double *tractParams, const char *fileName)
{
  if (!vtlApiInitialized)
  {
    printf("Error: The API has not been initialized.\n");
    return 1;
  }

  // Store the current control parameter values.
  vocalTract->storeControlParams();

  // Set the given vocal tract parameters.
  int i;
  for (i = 0; i < VocalTract::NUM_PARAMS; i++)
  {
    vocalTract->params[i].x = tractParams[i];
  }
  vocalTract->calculateAll();
  
  // Save the contour as SVG file.
  bool ok = vocalTract->exportTractContourSvg(string(fileName), false, false);
  
  // Restore the previous control parameter values and 
  // recalculate the vocal tract shape.

  vocalTract->restoreControlParams();
  vocalTract->calculateAll();

  if (ok)
  {
    return 0;
  }
  else
  {
    return 2;
  }
}


// ****************************************************************************
// Provides the tube data (especially the area function) for the given vector
// of tractParams. The vectors tubeLength_cm, tubeArea_cm2, and tubeArticulator, 
// must each have as many elements as tube sections.
// The values incisorPos_cm, tongueTipSideElevation, and velumOpening_cm2 are 
// one double value each.
//
// Function return value:
// 0: success.
// 1: The API has not been initialized.
// ****************************************************************************

int vtlTractToTube(double *tractParams,
  double *tubeLength_cm, double *tubeArea_cm2, int *tubeArticulator,
  double *incisorPos_cm, double *tongueTipSideElevation, double *velumOpening_cm2)
{
  if (!vtlApiInitialized)
  {
    printf("Error: The API has not been initialized.\n");
    return 1;
  }

  // ****************************************************************
  // Store the current control parameter values.
  // ****************************************************************

  vocalTract->storeControlParams();

  // ****************************************************************
  // Set the given vocal tract parameters.
  // ****************************************************************

  int i;
  for (i = 0; i < VocalTract::NUM_PARAMS; i++)
  {
    vocalTract->params[i].x = tractParams[i];
  }

  // ****************************************************************
  // Get the tube for the new vocal tract shape.
  // ****************************************************************

  Tube tube;
  vocalTract->calculateAll();
  vocalTract->getTube(&tube);

  // ****************************************************************
  // Copy the tube parameters to the user arrays.
  // ****************************************************************

  Tube::Section *ts = NULL;
  for (i = 0; i < Tube::NUM_PHARYNX_MOUTH_SECTIONS; i++)
  {
    ts = &tube.pharynxMouthSections[i];

    tubeLength_cm[i] = ts->length_cm;
    tubeArea_cm2[i] = ts->area_cm2;
    tubeArticulator[i] = ts->articulator;
  }

  tube.getAuxParams(*velumOpening_cm2, *incisorPos_cm, *tongueTipSideElevation);

  // ****************************************************************
  // Restore the previous control parameter values and 
  // recalculate the vocal tract shape.
  // ****************************************************************

  vocalTract->restoreControlParams();
  vocalTract->calculateAll();

  return 0;
}


// ****************************************************************************
// Calculates the volume velocity transfer function of the vocal tract between 
// the glottis and the lips for the given vector of vocal tract parameters and
// returns the spectrum in terms of magnitude and phase.
//
// Parameters in:
// o tractParams: Is a vector of vocal tract parameters with 
//     numVocalTractParams elements.
// o numSpectrumSamples: The number of samples (points) in the requested 
//     spectrum. This number of samples includes the negative frequencies and
//     also determines the frequency spacing of the returned magnitude and
//     phase vectors. The frequency spacing is 
//     deltaFreq = AUDIO_SAMPLING_RATE_HZ / numSpectrumSamples.
//     For example, with the sampling rate of 48000 Hz and 
//     numSpectrumSamples = 512, the returned magnitude and phase values are 
//     at the frequencies 0.0, 93.75, 187.5, ... Hz.
//     The value of numSpectrumSamples should not be greater than 16384,
//     otherwise the returned spectrum will be bandlimited to below 10 kHz.
//
// Parameters out:
// o magnitude: Vector of spectral magnitudes at equally spaced discrete 
//     frequencies. This vector mus have at least numSpectrumSamples elements.
// o phase_rad: Vector of the spectral phase in radians at equally 
//     spaced discrete frequencies. This vector must have at least 
//     numSpectrumSamples elements.
//
// Function return value:
// 0: success.
// 1: The API has not been initialized.
// ****************************************************************************

int vtlGetTransferFunction(double *tractParams, int numSpectrumSamples,
  double *magnitude, double *phase_rad)
{
  if (!vtlApiInitialized)
  {
    printf("Error: The API has not been initialized.\n");
    return 1;
  }

  int i;
  ComplexSignal s;

  if (numSpectrumSamples < 16)
  {
    numSpectrumSamples = 16;
  }

  // Calculate the vocal tract shape from the vocal tract parameters.

  for (i = 0; i < VocalTract::NUM_PARAMS; i++)
  {
    vocalTract->params[i].x = tractParams[i];
  }
  vocalTract->calculateAll();

  // Calculate the transfer function.

  TlModel *tlModel = new TlModel();
  vocalTract->getTube(&tlModel->tube);
  tlModel->tube.resetGlottisSections(0.0);
  tlModel->getSpectrum(TlModel::FLOW_SOURCE_TF, &s, numSpectrumSamples, Tube::FIRST_PHARYNX_SECTION);

  // Separate the transfer function into magnitude and phase.

  for (i = 0; i < numSpectrumSamples; i++)
  {
    magnitude[i] = s.getMagnitude(i);
    phase_rad[i] = s.getPhase(i);
  }

  delete tlModel;

  return 0;
}


// ****************************************************************************
// Resets the time-domain synthesis of continuous speech (using the function
// vtlSynthesisAddTract()). This function must be called every time you start 
// a new synthesis.
//
// Function return value:
// 0: success.
// 1: The API has not been initialized.
// ****************************************************************************

int vtlResetTractSynthesis()
{
  if (!vtlApiInitialized)
  {
    printf("Error: The API has not been initialized.\n");
    return 1;
  }

  synthesizer->reset();

  return 0;
}


// ****************************************************************************
// Synthesize a part of a speech signal with numNewSamples samples, during 
// which the vocal tract changes linearly from the tract shape passed to
// the previous call of this function to the tract shape passed to this call.
// To synthesize parts of 2.5 ms duration, call this function with 
// numNewSamples = 120. The synthesized signal part is written to the array 
// audio (the caller must allocate the memory for the array).
// During the *first* call of this function after vtlResetTractSynthesis(), no 
// audio is synthesized, and numNewSamples should be 0. During the first call, 
// only the initial tube state is set.
//
// The new vocal tract state is given in terms of the following parameters:
// o tractParams: Vector of vocal tract parameters.
// o glottisParams: Vector of vocal fold model parameters.
//
// Function return value:
// 0: success.
// 1: The API has not been initialized.
// 2: Number of generated audio samples is wrong (may happen when 
//    numNewSamples != 0 during the first call of this function after reset).
// ****************************************************************************

int vtlSynthesisAddTract(int numNewSamples, double *audio,
  double *tractParams, double *glottisParams)
{
  if (!vtlApiInitialized)
  {
    printf("Error: The API has not been initialized.\n");
    return 1;
  }

  vector<double> audioVector;
  synthesizer->addChunk(glottisParams, tractParams, numNewSamples, audioVector);

  if ((int)audioVector.size() != numNewSamples)
  {
    printf("Error in vtlSynthesisAddTube(): Number of audio samples is wrong.\n");
    return 2;
  }

  // Copy the audio samples in the given buffer.

  int i;
  for (i = 0; i < numNewSamples; i++)
  {
    audio[i] = audioVector[i];
  }

  return 0;
}


// ****************************************************************************
// Resets the time-domain synthesis of continuous speech (using the function
// vtlSynthesisAddTube()). This function must be called every time you start 
// a new synthesis.
//
// Input parameters:
// o glottisName: "Geometric glottis 2025" | "Geometric glottis 2019" | "Triangular glottis"
// o staticGlottisParams: Array of static parameters of the selected glottis.
// o tracheaLength_cm, ..., piriformFossaVolume_cm3: Static tube params.
// 
// Function return value:
// 0: success.
// 1: The API has not been initialized.
// 2: Invalid glottis name.
// ****************************************************************************

C_EXPORT int vtlResetTubeSynthesis(
  char* glottisName, double* staticGlottisParams,
  double tracheaLength_cm, double noseLength_cm,
  double piriformFossaLength_cm, double piriformFossaVolume_cm3)
{
  if (!vtlApiInitialized)
  {
    printf("Error: The API has not been initialized.\n");
    return 1;
  }

  // Delete any potential previous glottis model.
  if (tubeGlottis != NULL)
  {
    delete tubeGlottis;
    tubeGlottis = NULL;
  }

  // strcmp() == 0 means that the two strings are equal.
  if (strcmp(glottisName, "Geometric glottis 2025") == 0)
  {
    tubeGlottis = new GeometricGlottis2025();
  }
  else if (strcmp(glottisName, "Geometric glottis 2019") == 0)
  {
    tubeGlottis = new GeometricGlottis2019();
  }
  else if (strcmp(glottisName, "Triangular glottis") == 0)
  {
    tubeGlottis = new TriangularGlottis();
  }

  if (tubeGlottis == NULL)
  {
    printf("Error: Invalid glottis name.\n");
    return 2;
  }

  // Set the static parameters for the glottis model.

  int numStaticGlottisParams = (int)tubeGlottis->staticParams.size();
  for (int i = 0; i < numStaticGlottisParams; i++)
  {
    tubeGlottis->staticParams[i].x = staticGlottisParams[i];
  }

  // Static tube parameters only need to be initialized once here.

  tube->initStaticSections(tracheaLength_cm, noseLength_cm, 
    piriformFossaLength_cm, piriformFossaVolume_cm3);
  
  tube->resetPharynxMouthSections();
  tube->resetAuxParams();
  tube->resetGlottisSections(0.0);

  return 0;
}


// ****************************************************************************
// Synthesize a part of a speech signal with numNewSamples samples, during 
// which the vocal tract tube changes linearly from the tube shape passed to
// the previous call of this function to the tube shape passed to this call.
// To synthesize parts of 2.5 ms duration, call this function with 
// numNewSamples = 120. The synthesized signal part is written to the array 
// audio (the caller must allocate the memory for the array).
// During the *first* call of this function after vtlResetTubeSynthesis(), no 
// audio is synthesized, and numNewSamples should be 0. During the first call, 
// only the initial tube state is set.
//
// The new tube state is given in terms of the following parameters:
// o tubeLength_cm: Vector of tube sections lengths from the glottis (index 0)
//     to the mouth (index numTubeSections; see vtlGetConstants()).
// o tubeArea_cm2: According vector of tube section areas in cm^2.
// o tubeArticulator: Vector of characters (letters) that denote the articulator 
//     that confines the vocal tract at the position of the tube. We discriminate
//     1 (tongue), 2 (lower incisors), 3 (lower lip), 4 (other articulator).
// o incisorPos_cm: Position of the incisors from the glottis.
// o velumOpening_cm2: Opening of the velo-pharyngeal port in cm^2.
// o tongueTipSideElevation: Corresponds to the TS3 parameter of the vocal tract.
// o newGlottisParams: vector with parameters of the glottis model.
//
// Function return value:
// 0: success.
// 1: The API has not been initialized.
// 2: Number of generated audio samples is wrong (may happen when 
//    numNewSamples != 0 during the first call of this function after reset).
// ****************************************************************************

int vtlSynthesisAddTube(int numNewSamples, double* audio,
  double* tubeLength_cm, double* tubeArea_cm2, int* tubeArticulator,
  double incisorPos_cm, double velumOpening_cm2, double tongueTipSideElevation,
  double* newGlottisParams)
{
  if (!vtlApiInitialized)
  {
    printf("Error: The API has not been initialized.\n");
    return 1;
  }

  Tube::Articulator articulator[Tube::NUM_PHARYNX_MOUTH_SECTIONS];
  int i;
  for (i = 0; i < Tube::NUM_PHARYNX_MOUTH_SECTIONS; i++)
  {
    articulator[i] = (Tube::Articulator)tubeArticulator[i];
  }

  // Set the properties of the target tube.

  tube->setPharynxMouthSections(tubeLength_cm, tubeArea_cm2, articulator);
  tube->setAuxParams(velumOpening_cm2, incisorPos_cm, tongueTipSideElevation);

  // Synthesize the speech signal part.

  vector<double> audioVector;
  synthesizer->addChunk(newGlottisParams, tube, numNewSamples, audioVector);

  if ((int)audioVector.size() != numNewSamples)
  {
    printf("Error in vtlSynthesisAddTube(): Number of audio samples is wrong.\n");
    return 2;
  }

  // Copy the audio samples in the given buffer.

  for (i = 0; i < numNewSamples; i++)
  {
    audio[i] = audioVector[i];
  }

  return 0;
}


// ****************************************************************************
// Test function for this API.
// Audio should contain at least 48000 double values.
// Run this WITHOUT calling vtlInitialize() !
// ****************************************************************************

int vtlApiTest(const char *speakerFileName, double *audio, int *numSamples)
{
  int failed = vtlInitialize(speakerFileName);
  if (failed != 0)
  {
    printf("Error in  in vtlApiTest(): vtlInitialize() failed.\n");
    return 1;
  }

  char version[100];
  vtlGetVersion(version);
  printf("Compile date of the library: %s\n", version);

  int audioSamplingRate = 0;
  int numTubeSections = 0;
  int numVocalTractParams = 0;
  int numGlottisParams = 0;

  vtlGetConstants(&audioSamplingRate, &numTubeSections, &numVocalTractParams, &numGlottisParams);

  printf("Audio sampling rate = %d\n", audioSamplingRate);
  printf("Num. of tube sections = %d\n", numTubeSections);
  printf("Num. of vocal tract parameters = %d\n", numVocalTractParams);
  printf("Num. of glottis parameters = %d\n", numGlottisParams);

  char tractParamNames[50 * 32];
  double tractParamMin[50];
  double tractParamMax[50];
  double tractParamNeutral[50];

  vtlGetTractParamInfo(tractParamNames, tractParamMin, tractParamMax, tractParamNeutral);

  char glottisParamNames[50 * 32];
  double glottisParamMin[50];
  double glottisParamMax[50];
  double glottisParamNeutral[50];

  vtlGetGlottisParamInfo(glottisParamNames, glottisParamMin, glottisParamMax, glottisParamNeutral);

  // ****************************************************************
  // Define two target tube shapes: one for /a/ and one for /i/.
  // ****************************************************************

  const int MAX_TUBES = 100;

  // These parameters are the same for both /i/ and /a/:
  double incisorPos_cm = 15.0;
  double velumOpening_cm2 = 0.0;
  double tongueTipSideElevation = 0.0;

  int i;

  // ****************************************************************
  // Define the tube for /i/.
  // ****************************************************************

  double tubeLength_cm_i[MAX_TUBES];
  double tubeArea_cm2_i[MAX_TUBES];
  int tubeArticulator_i[MAX_TUBES];

  for (i = 0; i < numTubeSections; i++)
  {
    // Full tube length is 16 cm.
    tubeLength_cm_i[i] = 16.0 / (double)numTubeSections;
    
    // Articulator is always the tongue (although not fully correct here)
    tubeArticulator_i[i] = 1;   // = tongue
    
    // Narrow mouth sections and wide pharynx sections
    if (i < numTubeSections / 2)
    {
      tubeArea_cm2_i[i] = 8.0;
    }
    else
    {
      tubeArea_cm2_i[i] = 2.0;
    }
  }

  // ****************************************************************
  // Define the tube for /a/.
  // ****************************************************************

  double tubeLength_cm_a[MAX_TUBES];
  double tubeArea_cm2_a[MAX_TUBES];
  int tubeArticulator_a[MAX_TUBES];

  for (i = 0; i < numTubeSections; i++)
  {
    // Full tube length is 16 cm.
    tubeLength_cm_a[i] = 16.0 / (double)numTubeSections;

    // Articulator is always the tongue (although not fully correct here)
    tubeArticulator_a[i] = 1;   // = tongue

    // Narrow mouth sections and wide pharynx sections
    if (i < numTubeSections / 2)
    {
      tubeArea_cm2_a[i] = 0.3;
    }
    else
    {
      tubeArea_cm2_a[i] = 8.0;
    }
  }

  // ****************************************************************
  // Set glottis parameters to default (neutral) values, which are
  // suitable for phonation.
  // ****************************************************************

  double glottisParams[Glottis::MAX_CONTROL_PARAMS];

  for (i = 0; i < numGlottisParams; i++)
  {
    glottisParams[i] = glottisParamNeutral[i];
  }

  // **************************************************************************
  // Synthesize a transition from /a/ to /i/ to /a/.
  // **************************************************************************

  int numTotalSamples = 0;
  int numNewSamples = 0;

  vtlResetTractSynthesis();

  // Initialize with /a/ at 120 Hz.

  glottisParams[0] = 120.0;   // 120 Hz F0
  glottisParams[1] = 0.0;     // P_sub = 0 dPa.
  vtlSynthesisAddTube(0, audio, tubeLength_cm_a, tubeArea_cm2_a, tubeArticulator_a,
    incisorPos_cm, velumOpening_cm2, tongueTipSideElevation, 
    glottisParams);

  // Make 0.2 s transition to /i/ at 100 Hz.

  glottisParams[0] = 100.0;   // 100 Hz F0
  glottisParams[1] = 8000.0;  // P_sub = 8000 dPa.
  numNewSamples = (int)(0.2*audioSamplingRate);
  printf("Adding %d samples...\n", numNewSamples);

  vtlSynthesisAddTube(numNewSamples, &audio[numTotalSamples], tubeLength_cm_i, tubeArea_cm2_i, tubeArticulator_i,
    incisorPos_cm, velumOpening_cm2, tongueTipSideElevation,
    glottisParams);
  numTotalSamples += numNewSamples;

  // Make 0.2 s transition to /a/ at 80 Hz.

  glottisParams[0] = 80.0;   // 80 Hz F0
  numNewSamples = (int)(0.2*audioSamplingRate);
  printf("Adding %d samples...\n", numNewSamples);

  vtlSynthesisAddTube(numNewSamples, &audio[numTotalSamples], tubeLength_cm_a, tubeArea_cm2_a, tubeArticulator_a,
    incisorPos_cm, velumOpening_cm2, tongueTipSideElevation, glottisParams);
  numTotalSamples += numNewSamples;

  printf("Done.\n");

  *numSamples = numTotalSamples;

  // **************************************************************************
  // Clean up and close the VTL synthesis.
  // **************************************************************************

  vtlClose();

  return 0;
}


// ****************************************************************************
// This function converts a segment sequence file (a TXT file containing the 
// sequence of speech segments in SAMPA and the associated durations) with the 
// name segFileName into a gestural score file (gesFileName).
// The f0 tier in the gestural score is set to a "standard" f0.
//
// Function return value:
// 0: success.
// 1: The API was not initialized.
// 2: Loading the segment sequence file failed.
// 3: Saving the gestural score file failed.
// ****************************************************************************

int vtlSegmentSequenceToGesturalScore(const char *segFileName, const char *gesFileName)
{
  if (!vtlApiInitialized)
  {
    printf("Error: The API has not been initialized.\n");
    return 1;
  }

  // Create and load the segment sequence file.
  
  SegmentSequence *segmentSequence = new SegmentSequence();
  if (segmentSequence->readFromFile(string(segFileName)) == false)
  {
    delete segmentSequence;
    printf("Error in vtlSegmentSequenceToGesturalScore(): Segment sequence file could not be loaded.\n");
    return 2;
  }

  // Create and save the gestural score.

  GesturalScore *gesturalScore = new GesturalScore(vocalTract, glottises[selectedGlottis]);
  gesturalScore->createFromSegmentSequence(segmentSequence);
  if (gesturalScore->saveGesturesXml(string(gesFileName)) == false)
  {
    delete segmentSequence;
    delete gesturalScore;
    printf("Error in vtlSegmentSequenceToGesturalScore(): Gestural score file could not be saved.\n");
    return 3;
  }

  delete segmentSequence;
  delete gesturalScore;

  return 0;
}


// ****************************************************************************
// This function directly converts a gestural score to a tube sequence file.
// The latter is a text file containing the vocal fold and tube model 
// parameters in steps of 2.5 ms.

// Parameters:
// o gesFileName (in): Name of the gestural score file to convert.
// o tubeSequenceFileName (in): Name of the tube sequence file.
//
// Function return value:
// 0: success.
// 1: The API was not initialized.
// 2: Loading the gestural score file failed.
// 3: The tube sequence file could not be saved.
// ****************************************************************************

int vtlGesturalScoreToTubeSequence(const char* gesFileName, const char* tubeSequenceFileName)
{
  if (!vtlApiInitialized)
  {
    printf("Error: The API has not been initialized.\n");
    return 1;
  }

  // ****************************************************************
  // Init and load the gestural score.
  // ****************************************************************

  GesturalScore* gesturalScore = new GesturalScore(vocalTract, glottises[selectedGlottis]);

  bool allValuesInRange = true;
  if (gesturalScore->loadGesturesXml(string(gesFileName), allValuesInRange) == false)
  {
    printf("Error in vtlGesturalScoreToTubeSequence(): Loading the gestural score file failed!\n");
    delete gesturalScore;
    return 2;
  }

  // Important !!!
  gesturalScore->calcCurves();

  // ****************************************************************
  // Do the actual conversion.
  // ****************************************************************

  bool ok = Synthesizer::gesturalScoreToTubeSequenceFile(gesturalScore, string(tubeSequenceFileName));

  if (ok == false)
  {
    printf("Error in vtlGesturalScoreToTubeSequence(): Saving the tube sequence file failed!\n");
    delete gesturalScore;
    return 3;
  }

  // ****************************************************************
  // Free the memory and return.
  // ****************************************************************

  delete gesturalScore;
  return 0;
}


// ****************************************************************************
// This function generates a sequence of text files each of which contains the
// 2D vocal tract contour lines in terms of (x, y) coordinates. The vocal tract
// shapes are obtained from the provided gestural score with 100 frames/s.
//
// Parameters:
// o gesFileName (in): Name of the gestural score file.
// o outputFolder (in): Name of the folder where the text files are saved.
//
// Function return value:
// 0: success.
// 1: The API was not initialized.
// 2: Loading the gestural score file failed.
// 3: Error saving the text files.
// ****************************************************************************

int vtlGesturalScoreToTractContourFiles(const char* gesFileName, const char* outputFolder)
{
  if (!vtlApiInitialized)
  {
    printf("Error: The API has not been initialized.\n");
    return 1;
  }

  // ****************************************************************
  // Init and load the gestural score.
  // ****************************************************************

  GesturalScore* gesturalScore = new GesturalScore(vocalTract, glottises[selectedGlottis]);

  bool allValuesInRange = true;
  if (gesturalScore->loadGesturesXml(string(gesFileName), allValuesInRange) == false)
  {
    printf("Error in vtlGesturalScoreToTractContourFiles(): Loading the gestural score file failed!\n");
    delete gesturalScore;
    return 2;
  }

  // Important !!!
  gesturalScore->calcCurves();

  // ****************************************************************
  // Do the actual conversion.
  // ****************************************************************

  bool ok = Synthesizer::gesturalScoreToTractContourFiles(gesturalScore, string(outputFolder));

  if (ok == false)
  {
    printf("Error in vtlGesturalScoreToTractContourFiles(): Saving the tract contour text files failed!\n");
    delete gesturalScore;
    return 3;
  }

  // ****************************************************************
  // Free the memory and return.
  // ****************************************************************

  delete gesturalScore;
  return 0;
}


// ****************************************************************************
// This function directly converts a gestural score to an audio signal or file.
// Parameters:
// o gesFileName (in): Name of the gestural score file to synthesize.
// o wavFileName (in): Name of the audio file with the resulting speech signal.
//     This can be the empty string "" if you do not want to save a WAV file.
// o miscFileName (in): Name of a text file to which additional data are 
//     written for each time step, e.g., the glottal flow and pressures.
//     This can be the empty string "" if you do not want to save these data.
// o audio (out): The resulting audio signal with sample values in the range 
//     [-1, +1] and with the sampling rate 48 kHz. Make sure that
//     this buffer is big enough for the synthesized signal. If you are not 
//     interested in the audio signal, set this pointer to NULL.
// o numSamples (out): The number of audio samples in the synthesized signal.
//     If you are not interested in this value, set this pointer to NULL.
// o normalizeAudio (in): Set to 1, if you want the audio data to be peak- 
//     normalized to -1 dB of the allowed value range. Otherwise, set it to 0.
// o enableConsoleOutput (in): Set to 1, if you want to allow output about the
//   synthesis progress in the console window. Otherwise, set it to 0.
//
// Function return value:
// 0: success.
// 1: The API was not initialized.
// 2: Loading the gestural score file failed.
// 3: The WAV file could not be saved.
// 4: The misc file could not be saved.
// ****************************************************************************

int vtlGesturalScoreToAudio(const char *gesFileName, const char *wavFileName,
  const char* miscFileName, double *audio, int *numSamples, 
  int normalizeAudio, int enableConsoleOutput)
{
  if (!vtlApiInitialized)
  {
    printf("Error: The API has not been initialized.\n");
    return 1;
  }

  int i;

  // ****************************************************************
  // Init and load the gestural score.
  // ****************************************************************

  GesturalScore *gesturalScore = new GesturalScore(vocalTract, glottises[selectedGlottis]);

  bool allValuesInRange = true;
  if (gesturalScore->loadGesturesXml(string(gesFileName), allValuesInRange) == false)
  {
    printf("Error in vtlGesturalScoreToAudio(): Loading the gestural score file failed!\n");
    delete gesturalScore;
    return 2;
  }

  // Important !!!
  gesturalScore->calcCurves();

  // ****************************************************************
  // Do the actual synthesis.
  // ****************************************************************

  vector<double> audioVector;
  bool ok = Synthesizer::synthesizeGesturalScore(gesturalScore, tdsModel, audioVector, 
    miscFileName, (bool)enableConsoleOutput);
  int numVectorSamples = (int)audioVector.size();

  if (!ok)
  {
    printf("Error in vtlGesturalScoreToAudio(): The misc file could not be saved!\n");
    delete gesturalScore;
    return 4;
  }

  // ****************************************************************
  // Potentially normalize the generated audio signal.
  // ****************************************************************

  if (normalizeAudio == 1)
  {
    double maxValue = 0.0;

    for (i = 0; i < numVectorSamples; i++)
    {
      if (fabs(audioVector[i]) > maxValue)
      {
        maxValue = fabs(audioVector[i]);
      }
    }

    if (maxValue >= 0.00000001)
    {
      // 0.89 corresponds to -1 dB.
      double factor = 0.89 / maxValue;
      for (i = 0; i < numVectorSamples; i++)
      {
        audioVector[i] *= factor;
      }
    }
  }

  // ****************************************************************
  // Copy the number of audio samples to the return value numSamples.
  // ****************************************************************

  if (numSamples != NULL)
  {
    *numSamples = numVectorSamples;
  }

  // ****************************************************************
  // Copy the synthesized signal into the return buffer audio.
  // ****************************************************************

  if (audio != NULL)
  {
    for (i = 0; i < numVectorSamples; i++)
    {
      audio[i] = audioVector[i];
    }
  }
   
  // ****************************************************************
  // Save the result as WAV file (if the name is not an empty string).
  // ****************************************************************

  if (wavFileName[0] != '\0')
  {
    AudioFile<double> audioFile;
    audioFile.setAudioBufferSize(1, numVectorSamples);
    audioFile.setBitDepth(16);
    audioFile.setSampleRate(AUDIO_SAMPLING_RATE_HZ);

    for (i = 0; i < numVectorSamples; i++)
    {
      audioFile.samples[0][i] = audioVector[i];
    }

    if (audioFile.save(string(wavFileName)) == false)
    {
      printf("Error in vtlGesturalScoreToAudio(): The WAV file could not be saved!\n");
      delete gesturalScore;
      return 3;
    }
  }

  // ****************************************************************
  // Free the memory and return.
  // ****************************************************************

  delete gesturalScore;
  return 0;
}


// ****************************************************************************
// This function converts a tube sequence file into an audio signal or file.
// Parameters:
// o tubeSequenceFileName (in): Name of the tube sequence file to synthesize.
// o wavFileName (in): Name of the audio file with the resulting speech signal.
//     This can be the empty string "" if you do not want to save a WAV file.
// o audio (out): The resulting audio signal with sample values in the range 
//     [-1, +1] and with the sampling rate 48 kHz. Make sure that
//     this buffer is big enough for the synthesized signal. If you are not 
//     interested in the audio signal, set this pointer to NULL.
// o numSamples (out): The number of audio samples in the synthesized signal.
//     If you are not interested in this value, set this pointer to NULL.
//
// Function return value:
// 0: success.
// 1: The API was not initialized.
// 2: Synthesis of the tube sequence file failed.
// 3: The WAV file could not be saved.
// ****************************************************************************

int vtlTubeSequenceToAudio(const char* tubeSequenceFileName,
  const char* wavFileName, double* audio, int* numSamples)
{
  if (!vtlApiInitialized)
  {
    printf("Error: The API has not been initialized.\n");
    return 1;
  }

  int i;
  vector<double> audioVector;

  bool ok = Synthesizer::synthesizeTubeSequence(string(tubeSequenceFileName), tdsModel, audioVector);

  if (ok == false)
  {
    printf("Error in vtlTubeSequenceToAudio(): Synthesis of the tube sequence file failed.\n");
    return 2;
  }

  int numVectorSamples = (int)audioVector.size();

  // ****************************************************************
  // Copy the number of audio samples to the return value numSamples.
  // ****************************************************************

  if (numSamples != NULL)
  {
    *numSamples = numVectorSamples;
  }

  // ****************************************************************
  // Copy the synthesized signal into the return buffer audio.
  // ****************************************************************

  if (audio != NULL)
  {
    for (i = 0; i < numVectorSamples; i++)
    {
      audio[i] = audioVector[i];
    }
  }

  // ****************************************************************
  // Save the result as WAV file (if the file name is not empty).
  // ****************************************************************

  if (wavFileName[0] != '\0')
  {
    AudioFile<double> audioFile;
    audioFile.setAudioBufferSize(1, numVectorSamples);
    audioFile.setBitDepth(16);
    audioFile.setSampleRate(AUDIO_SAMPLING_RATE_HZ);

    for (i = 0; i < numVectorSamples; i++)
    {
      audioFile.samples[0][i] = audioVector[i];
    }

    if (audioFile.save(string(wavFileName)) == false)
    {
      printf("Error in vtlTubeSequenceToAudio(): The WAV file could not be saved!\n");
      return 3;
    }
  }

  // ****************************************************************

  return 0;
}


// ****************************************************************************
// These are several functions to globally change a parameter in a gestural 
// score. The gestural score is read from the input file, manipulated with the
// given parameter, and then written into the output file.
// ****************************************************************************

int vtlGesScoreChangeF0Offset(const char* inputFileName, const char* outputFileName, double deltaF0_st)
{
  if (!vtlApiInitialized)
  {
    printf("Error: The API has not been initialized.\n");
    return 1;
  }

  // Load the gestural score.

  GesturalScore* gesturalScore = new GesturalScore(vocalTract, glottises[selectedGlottis]);
  bool allValuesInRange = true;
  if (gesturalScore->loadGesturesXml(string(inputFileName), allValuesInRange) == false)
  {
    printf("Error in vtlGesScoreChangeF0Offset(): Loading the gestural score file failed!\n");
    delete gesturalScore;
    return 1;
  }

  gesturalScore->calcCurves();    // Important !!!
  gesturalScore->changeF0Offset(deltaF0_st);

  // Save the modified gestural score.
  if (gesturalScore->saveGesturesXml(string(outputFileName)) == false)
  {
    printf("Error in vtlGesScoreChangeF0Offset(): Saving the gestural score file failed!\n");
    delete gesturalScore;
    return 1;
  }

  delete gesturalScore;
  return 0;
}

// ****************************************************************************
// ****************************************************************************

int vtlGesScoreChangeF0Range(const char* inputFileName, const char* outputFileName, double factor)
{
  if (!vtlApiInitialized)
  {
    printf("Error: The API has not been initialized.\n");
    return 1;
  }

  // Load the gestural score.

  GesturalScore* gesturalScore = new GesturalScore(vocalTract, glottises[selectedGlottis]);
  bool allValuesInRange = true;
  if (gesturalScore->loadGesturesXml(string(inputFileName), allValuesInRange) == false)
  {
    printf("Error in vtlGesScoreChangeF0Range(): Loading the gestural score file failed!\n");
    delete gesturalScore;
    return 1;
  }

  gesturalScore->calcCurves();    // Important !!!
  gesturalScore->changeF0Range(factor);

  // Save the modified gestural score.
  if (gesturalScore->saveGesturesXml(string(outputFileName)) == false)
  {
    printf("Error in vtlGesScoreChangeF0Range(): Saving the gestural score file failed!\n");
    delete gesturalScore;
    return 1;
  }

  delete gesturalScore;
  return 0;
}

// ****************************************************************************
// ****************************************************************************

int vtlGesScoreSubstituteGlottalShapes(const char* inputFileName, const char* outputFileName, const char* oldShapeName, const char* newShapeName)
{
  if (!vtlApiInitialized)
  {
    printf("Error: The API has not been initialized.\n");
    return 1;
  }

  // Load the gestural score.

  GesturalScore* gesturalScore = new GesturalScore(vocalTract, glottises[selectedGlottis]);
  bool allValuesInRange = true;
  if (gesturalScore->loadGesturesXml(string(inputFileName), allValuesInRange) == false)
  {
    printf("Error in vtlGesScoreSubstituteGlottalShapes(): Loading the gestural score file failed!\n");
    delete gesturalScore;
    return 1;
  }

  gesturalScore->calcCurves();    // Important !!!
  gesturalScore->substituteGlottalShapes(string(oldShapeName), string(newShapeName));

  // Save the modified gestural score.
  if (gesturalScore->saveGesturesXml(string(outputFileName)) == false)
  {
    printf("Error in vtlGesScoreSubstituteGlottalShapes(): Saving the gestural score file failed!\n");
    delete gesturalScore;
    return 1;
  }

  delete gesturalScore;
  return 0;
}

// ****************************************************************************
// ****************************************************************************

int vtlGesScoreChangeSubglottalPressure(const char* inputFileName, const char* outputFileName, double factor)
{
  if (!vtlApiInitialized)
  {
    printf("Error: The API has not been initialized.\n");
    return 1;
  }

  // Load the gestural score.

  GesturalScore* gesturalScore = new GesturalScore(vocalTract, glottises[selectedGlottis]);
  bool allValuesInRange = true;
  if (gesturalScore->loadGesturesXml(string(inputFileName), allValuesInRange) == false)
  {
    printf("Error in vtlGesScoreChangeSubglottalPressure(): Loading the gestural score file failed!\n");
    delete gesturalScore;
    return 1;
  }

  gesturalScore->calcCurves();    // Important !!!
  gesturalScore->changeSubglottalPressure(factor);

  // Save the modified gestural score.
  if (gesturalScore->saveGesturesXml(string(outputFileName)) == false)
  {
    printf("Error in vtlGesScoreChangeSubglottalPressure(): Saving the gestural score file failed!\n");
    delete gesturalScore;
    return 1;
  }

  delete gesturalScore;
  return 0;
}

// ****************************************************************************
// ****************************************************************************

int vtlGesScoreChangeDuration(const char* inputFileName, const char* outputFileName, double factor)
{
  if (!vtlApiInitialized)
  {
    printf("Error: The API has not been initialized.\n");
    return 1;
  }

  // Load the gestural score.

  GesturalScore* gesturalScore = new GesturalScore(vocalTract, glottises[selectedGlottis]);
  bool allValuesInRange = true;
  if (gesturalScore->loadGesturesXml(string(inputFileName), allValuesInRange) == false)
  {
    printf("Error in vtlGesScoreChangeDuration(): Loading the gestural score file failed!\n");
    delete gesturalScore;
    return 1;
  }

  gesturalScore->calcCurves();    // Important !!!
  gesturalScore->changeDuration(factor);

  // Save the modified gestural score.
  if (gesturalScore->saveGesturesXml(string(outputFileName)) == false)
  {
    printf("Error in vtlGesScoreChangeDuration(): Saving the gestural score file failed!\n");
    delete gesturalScore;
    return 1;
  }

  delete gesturalScore;
  return 0;
}

// ****************************************************************************
// ****************************************************************************

int vtlGesScoreChangeTimeConstants(const char* inputFileName, const char* outputFileName, double factor)
{
  if (!vtlApiInitialized)
  {
    printf("Error: The API has not been initialized.\n");
    return 1;
  }

  // Load the gestural score.

  GesturalScore* gesturalScore = new GesturalScore(vocalTract, glottises[selectedGlottis]);
  bool allValuesInRange = true;
  if (gesturalScore->loadGesturesXml(string(inputFileName), allValuesInRange) == false)
  {
    printf("Error in vtlGesScoreChangeTimeConstants(): Loading the gestural score file failed!\n");
    delete gesturalScore;
    return 1;
  }

  gesturalScore->calcCurves();    // Important !!!
  gesturalScore->changeTimeConstants(factor);

  // Save the modified gestural score.
  if (gesturalScore->saveGesturesXml(string(outputFileName)) == false)
  {
    printf("Error in vtlGesScoreChangeTimeConstants(): Saving the gestural score file failed!\n");
    delete gesturalScore;
    return 1;
  }

  delete gesturalScore;
  return 0;
}

// ****************************************************************************
