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

#include "acoustics/TdsModel.h"
#include <fstream>
#include <iomanip>
#include <cstdlib>
#include <limits>
#include <iostream>
#include <random>

const double TdsModel::THETA = 0.5;   // 0.505;
const double TdsModel::THETA1 = 1.0 - TdsModel::THETA;

// For MIN_AREA_CM2, 0.01 mm^2 seems to be a very good value.
const double TdsModel::MIN_AREA_CM2 = 0.0001; // = 0.01 mm^2;

// Cutoff-freq. of the low-pass filter for the flow that induces friction
const double TdsModel::NOISE_CUTOFF_FREQ = 500.0;     

// ****************************************************************************
/// Constructor.
// ****************************************************************************

TdsModel::TdsModel()
{
  // ****************************************************************
  // Acoustic options
  // ****************************************************************

  options.softWalls = true;
  options.softContacts = true;
  options.generateNoiseSources = true;
  options.radiationFromSkin = true;
  options.piriformFossa = true;
  options.innerLengthCorrections = false;
  options.radiationCharacteristic = HEAD_TORSO_RADIATION_CHARACTERISTIC;

  // ****************************************************************

  tubeSection = new TubeSection[Tube::NUM_SECTIONS];
  branchCurrent = new BranchCurrent[NUM_BRANCH_CURRENTS];

  Tube tube;
  setTube(&tube);

  initModel();
  resetMotion();
}

// ****************************************************************************
/// Destructor.
// ****************************************************************************

TdsModel::~TdsModel()
{
  delete[] tubeSection;
  delete[] branchCurrent;
}


// ****************************************************************************
// ****************************************************************************

void TdsModel::initModel()
{
  TubeSection *ts;
  BranchCurrent *bc;
  int i, j, k;

  timeStep = 1.0 / (double)AUDIO_SAMPLING_RATE_HZ;

  // ****************************************************************
  // Tube section for a (static) pressure or volume velocity source.
  // ****************************************************************

  flowSourceSection      = -1;
  pressureSourceSection  = -1;

  // ****************************************************************
  // Assign the source and target sections for the branch currents.
  // ****************************************************************

  // Every tube section has one in-flowing current

  for (i=0; i < Tube::NUM_SECTIONS; i++)
  {
    bc = &branchCurrent[i];
    bc->sourceSection = i - 1;
    bc->targetSection = i;
  }

  // Branch currents with special source sections.

  branchCurrent[Tube::FIRST_TRACHEA_SECTION].sourceSection = -1;
  branchCurrent[Tube::FIRST_NOSE_SECTION].sourceSection = Tube::LAST_PHARYNX_SECTION;
  branchCurrent[Tube::FIRST_FOSSA_SECTION].sourceSection = 
    Tube::FIRST_PHARYNX_SECTION + Tube::FOSSA_COUPLING_SECTION;
  
  for (i=0; i < Tube::NUM_SINUS_SECTIONS; i++)
  {
    branchCurrent[Tube::FIRST_SINUS_SECTION + i].sourceSection = 
      Tube::FIRST_NOSE_SECTION + Tube::SINUS_COUPLING_SECTION[i];
  }

  // The two currents from the mouth opening in to free space.

  bc = &branchCurrent[Tube::NUM_SECTIONS];
  bc->sourceSection = Tube::LAST_MOUTH_SECTION;
  bc->targetSection = -1;

  bc = &branchCurrent[Tube::NUM_SECTIONS + 1];
  bc->sourceSection = Tube::LAST_MOUTH_SECTION;
  bc->targetSection = -1;

  // The two currents from the nostrils into free space.

  bc = &branchCurrent[Tube::NUM_SECTIONS + 2];
  bc->sourceSection = Tube::LAST_NOSE_SECTION;
  bc->targetSection = -1;

  bc = &branchCurrent[Tube::NUM_SECTIONS + 3];
  bc->sourceSection = Tube::LAST_NOSE_SECTION;
  bc->targetSection = -1;


  // ****************************************************************
  // Assign each tube section the currents that flow into and out of
  // the section.
  // ****************************************************************

  for (i=0; i < Tube::NUM_SECTIONS; i++)
  {
    ts = &tubeSection[i];

    ts->currentIn     = -1;
    ts->currentOut[0] = -1;
    ts->currentOut[1] = -1;
  }

  for (i=0; i < NUM_BRANCH_CURRENTS; i++)
  {
    bc = &branchCurrent[i];

    // Where does it come from ?
    if (bc->sourceSection != -1)
    {
      ts = &tubeSection[bc->sourceSection];
      if (ts->currentOut[0] == -1) 
        { ts->currentOut[0] = i; } 
      else 
        { ts->currentOut[1] = i; }
    }

    // Where does it flow to ?
    if (bc->targetSection != -1)
    {
      ts = &tubeSection[bc->targetSection];
      ts->currentIn = i;
    }
  }


  // ****************************************************************
  // Init the matrix and the help structures for the Cholesky 
  // factorization.
  // ****************************************************************

  for (i = 0; i < NUM_BRANCH_CURRENTS; i++)
  {
    numFilledRowValuesSymmetricEnvelope[i] = 0;
    for (k = 0; k < MAX_CONCERNED_MATRIX_COLUMNS_SYMMETRIC_ENVELOPE; k++)
    {
      filledRowIndexSymmetricEnvelope[i][k] = 0;
    }
  }

  for (i = 0; i < NUM_BRANCH_CURRENTS; i++)
  {
    numFilledColumnValuesSymmetricEnvelope[i] = 0;
    for (k = 0; k < MAX_CONCERNED_MATRIX_ROWS_SYMMETRIC_ENVELOPE; k++)
    {
      filledColumnIndexSymmetricEnvelope[i][k] = 0;
    }
  }

  // Fill the whole matrix with zeros.

  for (i = 0; i < NUM_BRANCH_CURRENTS; i++)
  {
    for (j = 0; j < NUM_BRANCH_CURRENTS; j++)
    {
      matrix[i][j] = 0.0;
    }
  }

  // Fill the whole factorizationMatrix with zeros.

  for (i = 0; i < NUM_BRANCH_CURRENTS; i++)
  {
    for (j = 0; j < NUM_BRANCH_CURRENTS; j++)
    {
      factorizationMatrix[i][j] = 0.0;
    }
  }

  // Set a 1 to all non-zero places in the matrix.

  for (i = 0; i < NUM_BRANCH_CURRENTS; i++)
  {
    if (branchCurrent[i].sourceSection != -1)
    {
      ts = &tubeSection[branchCurrent[i].sourceSection];
      if (ts->currentIn != -1) { matrix[i][ts->currentIn] = 1.0; }
      if (ts->currentOut[0] != -1) { matrix[i][ts->currentOut[0]] = 1.0; }
      if (ts->currentOut[1] != -1) { matrix[i][ts->currentOut[1]] = 1.0; }
    }

    if (branchCurrent[i].targetSection != -1)
    {
      ts = &tubeSection[branchCurrent[i].targetSection];
      if (ts->currentIn != -1) { matrix[i][ts->currentIn] = 1.0; }
      if (ts->currentOut[0] != -1) { matrix[i][ts->currentOut[0]] = 1.0; }
      if (ts->currentOut[1] != -1) { matrix[i][ts->currentOut[1]] = 1.0; }
    }

    // Keep in mind the columns elements with a number different from
    // zero in one row in the lower left triangular matrix and the 
    // columns elements between the first non zeros in one row and 
    // the main diagonal (symmetric envelope format).
    // The indices on the main diagonal are not kept in mind explicitly.

    numFilledRowValuesSymmetricEnvelope[i] = 0;
    int betweenNonZeros = 0;

    for (j = 0; j < i; j++)
    {
      if ((matrix[i][j] != 0.0) || betweenNonZeros != 0)
      {
        betweenNonZeros = 1;
        matrix[i][j] = 1;  // for the calculation of the envelpe column indices

        if (numFilledRowValuesSymmetricEnvelope[i] < MAX_CONCERNED_MATRIX_COLUMNS_SYMMETRIC_ENVELOPE)
        {
          filledRowIndexSymmetricEnvelope[i][numFilledRowValuesSymmetricEnvelope[i]] = j;
          numFilledRowValuesSymmetricEnvelope[i]++;
        }
        else
        {
          printf("Error: Attention: The max. number of used rows and columns has been "
            "exceeded in prepareCholeskyFactorization().\n");
        }
      }
    }
  }

  // Keep in mind the rows elements with a number different from zero
  // in one column in the lower left triangular matrix.
  // (symmetric envelope format)
  // The indices on the main diagonal are not kept in mind explicitly.

  for (j = 0; j < NUM_BRANCH_CURRENTS; j++)
  {
    numFilledColumnValuesSymmetricEnvelope[j] = 0;

    for (i = j + 1; i < NUM_BRANCH_CURRENTS; i++)
    {
      if (matrix[i][j] != 0.0)
      {

        if (numFilledColumnValuesSymmetricEnvelope[j] < MAX_CONCERNED_MATRIX_ROWS_SYMMETRIC_ENVELOPE)
        {
          filledColumnIndexSymmetricEnvelope[j][numFilledColumnValuesSymmetricEnvelope[j]] = i;
          numFilledColumnValuesSymmetricEnvelope[j]++;
        }
        else
        {
          printf("Error: Attention: The max. number of used rows and columns has been "
            "exceeded in prepareCholeskyFactorization().\n");
        }
      }
    }
  }

  // ****************************************************************
  // Init the matrix.
  // ****************************************************************

  // Fill the whole matrix with zeros.

  for (i=0; i < NUM_BRANCH_CURRENTS; i++)
  {
    for (j=0; j < NUM_BRANCH_CURRENTS; j++) 
    { 
      matrix[i][j] = 0.0; 
    }
  }

  // Set a 1 to all non-zero places in the matrix.

  for (i=0; i < NUM_BRANCH_CURRENTS; i++)
  {
    if (branchCurrent[i].sourceSection != -1)
    {
      ts = &tubeSection[ branchCurrent[i].sourceSection ];
      if (ts->currentIn     != -1) { matrix[i][ ts->currentIn ] = 1.0; }
      if (ts->currentOut[0] != -1) { matrix[i][ ts->currentOut[0] ] = 1.0; }
      if (ts->currentOut[1] != -1) { matrix[i][ ts->currentOut[1] ] = 1.0; }
    }

    if (branchCurrent[i].targetSection != -1)
    {
      ts = &tubeSection[ branchCurrent[i].targetSection ];
      if (ts->currentIn     != -1) { matrix[i][ ts->currentIn ] = 1.0; }
      if (ts->currentOut[0] != -1) { matrix[i][ ts->currentOut[0] ] = 1.0; }
      if (ts->currentOut[1] != -1) { matrix[i][ ts->currentOut[1] ] = 1.0; }
    }

  }

}


// ****************************************************************************
/// Sets all flows, pressures, etc. into the equilibrium state.
// ****************************************************************************

void TdsModel::resetMotion()
{
  const double Q = 0.5;
  TubeSection *ts;
  BranchCurrent *bc;
  int i, k;

  // ****************************************************************
  // Reinitialize the random number generator with a constant seed
  // so that the noise in every synthesis is exactly the same.
  // ****************************************************************

  randomNumberGenerator.seed(10);

  // ****************************************************************
  // The tube sections.
  // ****************************************************************

  for (i=0; i < Tube::NUM_SECTIONS; i++)
  {
    ts = &tubeSection[i];

    ts->pressure         = 0.0;
    ts->pressureRate     = 0.0;
    ts->wallCurrent      = 0.0;
    ts->wallCurrentRate  = 0.0;
    ts->wallCurrentRate2 = 0.0;
    
    // Intermediate values ********************************
    ts->L                = 0.0;
    ts->C                = 0.0;
    ts->R[0] = ts->R[1]  = 0.0;
    ts->S                = 0.0;
    ts->alpha            = 0.0;
    ts->beta             = 0.0;
    ts->D                = 0.0;
    ts->E                = 0.0;

    // Set the noise sources to 0 ***********************************

    ts->monopoleSource.targetAmp1kHz = 0.0;
    ts->monopoleSource.currentAmp1kHz = 0.0;
    ts->monopoleSource.isFirstOrder = false;
    ts->monopoleSource.cutoffFreq = 1.0;
    ts->monopoleSource.sample = 0.0;
    for (k = 0; k < NUM_NOISE_BUFFER_SAMPLES; k++)
    {
      ts->monopoleSource.inputBuffer[k] = 0.0;
      ts->monopoleSource.outputBuffer[k] = 0.0;
    }

    ts->dipoleSource.targetAmp1kHz = 0.0;
    ts->dipoleSource.currentAmp1kHz = 0.0;
    ts->dipoleSource.isFirstOrder = false;
    ts->dipoleSource.cutoffFreq = 1.0;
    ts->dipoleSource.sample = 0.0;
    
    for (k = 0; k < NUM_NOISE_BUFFER_SAMPLES; k++)
    {
      ts->dipoleSource.inputBuffer[k] = 0.0;
      ts->dipoleSource.outputBuffer[k] = 0.0;
    }
  }

  // Extra dipole source at the lips ********************************

  lipsDipoleSource.targetAmp1kHz = 0.0;
  lipsDipoleSource.currentAmp1kHz = 0.0;
  lipsDipoleSource.isFirstOrder = false;
  lipsDipoleSource.cutoffFreq = 1.0;
  lipsDipoleSource.sample = 0.0;

  for (k = 0; k < NUM_NOISE_BUFFER_SAMPLES; k++)
  {
    lipsDipoleSource.inputBuffer[k] = 0.0;
    lipsDipoleSource.outputBuffer[k] = 0.0;
  }

  // ****************************************************************
  // The branch currents.
  // ****************************************************************

  for (i=0; i < NUM_BRANCH_CURRENTS; i++)
  {
    bc = &branchCurrent[i];

    bc->magnitude = 0.0;
    bc->magnitudeRate = 0.0;
    bc->noiseMagnitude = 0.0;
  }

  flowSourceAmp = 0.0;
  pressureSourceAmp = 0.0;
  position = 0;             // The internal position counter

  numConstrictions = 0;

  // The volume velocity vector is 0. *******************************
  
  for (i=0; i < NUM_BRANCH_CURRENTS; i++) 
  { 
    flowVector[i] = 0.0; 
  }

  transglottalPressureFilter.createChebyshev(50.0 / (double)AUDIO_SAMPLING_RATE_HZ, false, 4);
  transglottalPressureFilter.resetBuffers();
}

// ****************************************************************************
// If filtering is true, the rate of tube area changes is limited here.
// ****************************************************************************

void TdsModel::setTube(Tube* tube, bool filtering)
{
  // The CLOSING_CURVE_FACTOR was precisely truned to avoid chirp 
  // artifacts at closures, but without to smooth too strongly.
  const double CLOSING_CURVE_FACTOR = 40e6;   // For closing movements

  // The OPENING_CURVE_FACTOR was precisely tuned to avoid click 
  // artifacts, but also allow sufficiently rapid opening for, e.g.,
  // /ibi/ and /imi/ (which may otherwise sound like /idi/ or /iNi/).
  const double OPENING_CURVE_FACTOR = 480e6;   // For opening movements

  const double MAX_AREA_CHANGE_RATE_CM2_S = 400.0;
  const double CONSTANT_DELTA_AREA_LIMIT_CM2 = MAX_AREA_CHANGE_RATE_CM2_S / (double)AUDIO_SAMPLING_RATE_HZ;

  int i;
  TubeSection* target = NULL;
  Tube::Section* source = NULL;
  double oldArea_cm2 = 0.0;
  double newArea_cm2 = 0.0;
  double deltaArea_cm2 = 0.0;

  for (i = 0; i < Tube::NUM_SECTIONS; i++)
  {
    source = tube->sections[i];
    target = &tubeSection[i];

    // If the new tube comes from a continuous sequence (filtering = true)
    // and the tube section belongs to the pharyngeal or oral cavity
    // (NOT the glottis sections!), limit the rate of tube area changes here.

    if ((options.softContacts) && (filtering) && 
        (((i >= Tube::FIRST_PHARYNX_SECTION) && (i <= Tube::LAST_MOUTH_SECTION)) ||
          ((i >= Tube::FIRST_NOSE_SECTION) && (i <= Tube::LAST_NOSE_SECTION))))
    {
      oldArea_cm2 = target->area;
      newArea_cm2 = source->area_cm2;
      deltaArea_cm2 = newArea_cm2 - oldArea_cm2;

      // Static limitation.
      
      if (deltaArea_cm2 > CONSTANT_DELTA_AREA_LIMIT_CM2) { deltaArea_cm2 = CONSTANT_DELTA_AREA_LIMIT_CM2; }
      if (deltaArea_cm2 < -CONSTANT_DELTA_AREA_LIMIT_CM2) { deltaArea_cm2 = -CONSTANT_DELTA_AREA_LIMIT_CM2; }

      // Dynamic limitation (depending on the current/old area).
      
      double curveFactor = CLOSING_CURVE_FACTOR;
      if (newArea_cm2 > oldArea_cm2)
      {
        curveFactor = OPENING_CURVE_FACTOR;
      }
      double t = sqrt(sqrt(oldArea_cm2 / curveFactor)) + timeStep;
      double deltaAreaLimit_cm2 = curveFactor * t * t * t * t - oldArea_cm2;

      if (deltaArea_cm2 > deltaAreaLimit_cm2) { deltaArea_cm2 = deltaAreaLimit_cm2; }
      if (deltaArea_cm2 < -deltaAreaLimit_cm2) { deltaArea_cm2 = -deltaAreaLimit_cm2; }

      newArea_cm2 = oldArea_cm2 + deltaArea_cm2;

      target->pos = source->pos_cm;
      target->area = newArea_cm2;
      target->length = source->length_cm;
      target->volume = source->length_cm * newArea_cm2;
    }
    else
    {
      target->pos = source->pos_cm;
      target->area = source->area_cm2;
      target->length = source->length_cm;
      target->volume = source->volume_cm3;
    }

    target->articulator = source->articulator;
  }

  double dummy;
  tube->getAuxParams(dummy, this->teethPosition, this->tongueTipSideElevation);
}


// ****************************************************************************
// ****************************************************************************

void TdsModel::getTube(Tube *tube)
{
  int i;
  TubeSection *source = NULL;
  Tube::Section *target = NULL;

  for (i=0; i < Tube::NUM_SECTIONS; i++)
  {
    source = &tubeSection[i];
    target = tube->sections[i];

    target->pos_cm     = source->pos;
    target->area_cm2   = source->area;
    target->length_cm  = source->length;
    target->volume_cm3 = source->volume;
    target->articulator = source->articulator;
  }

  double nasalPortArea_cm2 = tubeSection[Tube::FIRST_NOSE_SECTION].area;
  tube->setAuxParams(nasalPortArea_cm2, teethPosition, tongueTipSideElevation);
}


// ****************************************************************************
/// Sets the flow source into the given tube section and to the given strength.
/// \param flow_cm3_s Strength of the source in cm3/s.
/// \param section Section of the source. Set section to -1 to disable the 
/// flow source.
// ****************************************************************************

void TdsModel::setFlowSource(double flow_cm3_s, int section)
{
  flowSourceSection = section;
  flowSourceAmp     = flow_cm3_s;
}


// ****************************************************************************
/// Sets the pressure source into the given tube section and to the given 
/// strength.
/// \param pressure_dPa Strength of the source in deci-Pascal.
/// \param section Section of the source. Set section to -1 to disable the 
/// pressure source.
// ****************************************************************************

void TdsModel::setPressureSource(double pressure_dPa, int section)
{
  pressureSourceSection = section;
  pressureSourceAmp = pressure_dPa;
}


// ****************************************************************************
/// Calculate a new time step in the simulation.
/// Returns the sum flow radiated from the mouth, the nostrils and the vocal
/// tract walls.
/// \param mouthFlow_cm3_s Flow through the mouth opening.
/// \param nostrilFlow_cm3_s Flow through the nostrils.
/// \param skinFlow_cm3_s Flow due to the outer skin surface vibration.
// ****************************************************************************

double TdsModel::proceedTimeStep(double& mouthFlow_cm3_s, double& nostrilFlow_cm3_s,
  double& skinFlow_cm3_s)
{
  TubeSection *ts = NULL;

  // Calculation of resistors and other values.
  prepareTimeStep();
  
  // Calculation of the matrix coefficients.
  calcMatrix();

  // Solve the system of eqs. with cholesky factorization
  solveEquationsCholesky();

  // Recalculate all currents, pressures and their derivatives.
  updateVariables();

  // ****************************************************************
  // Get the radiated flow.
  // ****************************************************************

  mouthFlow_cm3_s = 0.0;

  ts = &tubeSection[Tube::LAST_MOUTH_SECTION];
  if (ts->currentOut[0] != -1) { mouthFlow_cm3_s += branchCurrent[ ts->currentOut[0] ].magnitude; }
  if (ts->currentOut[1] != -1) { mouthFlow_cm3_s += branchCurrent[ ts->currentOut[1] ].magnitude; }

  nostrilFlow_cm3_s = 0.0;

  ts = &tubeSection[Tube::LAST_NOSE_SECTION];
  if (ts->currentOut[0] != -1) { nostrilFlow_cm3_s += branchCurrent[ ts->currentOut[0] ].magnitude; }
  if (ts->currentOut[1] != -1) { nostrilFlow_cm3_s += branchCurrent[ ts->currentOut[1] ].magnitude; }

  // ****************************************************************
  // Consider the sound radiation from the skin.
  // ****************************************************************

  skinFlow_cm3_s = 0.0;

  if (options.radiationFromSkin)
  {
    // Sum up the wall flow of all supraglottal tube sections.
    for (int i = 0; i < Tube::NUM_PHARYNX_MOUTH_SECTIONS; i++)
    {
      skinFlow_cm3_s+= tubeSection[Tube::FIRST_PHARYNX_SECTION + i].wallCurrent;
    }
    
    for (int i = 0; i < Tube::NUM_NOSE_SECTIONS; i++)
    {
      skinFlow_cm3_s += tubeSection[Tube::FIRST_NOSE_SECTION + i].wallCurrent;
    }
  }
  
  // Increase the internal position counter to the next sample.
  position++;     

  double radiatedFlow = mouthFlow_cm3_s + nostrilFlow_cm3_s + skinFlow_cm3_s;

  return radiatedFlow;
}


// ****************************************************************************
/// Calculate the network components.
// ****************************************************************************

void TdsModel::prepareTimeStep()
{
  TubeSection *ts = NULL;
  int i;
  double d;
  double Lw, Rw, Cw;
  double surface;
  double circ;

  // Calculate the values D and E and the components L, R, W, C for
  // all tube sections.

  for (i=0; i < Tube::NUM_SECTIONS; i++)
  {
    ts = &tubeSection[i];
    if (ts->area < MIN_AREA_CM2) 
    { 
      ts->area = MIN_AREA_CM2; 
    }

    ts->S = 0.0;      // No pressure source at the inlet of the section
    circ = 2.0*sqrt(ts->area*M_PI);

    // **************************************************************
    // Recalculate the components of the dynamic tube sections.
    // **************************************************************

    // The Helmholtz-Resonators are special.

    if ((i >= Tube::FIRST_SINUS_SECTION) && (i <= Tube::LAST_SINUS_SECTION))
    {
      ts->L    = AMBIENT_DENSITY_CGS*(ts->length / ts->area);
      ts->C    = ts->volume / (AMBIENT_DENSITY_CGS*SOUND_VELOCITY_CGS*SOUND_VELOCITY_CGS);

      // The Hagen-Poiseuille flow resistance strongly underestimates
      // the damping in the resonator neck, especially for higher
      // frequencies. Hence we use an empirical scaling factor.
      // Without the factor, the losses would be too low, which
      // can cause a long lasting Helmholtz resonance in rare cases.
      const double EMPIRICAL_FACTOR = 15.0;
      ts->R[0] = EMPIRICAL_FACTOR * (8.0*AIR_VISCOSITY_CGS * M_PI * ts->length) / (ts->area * ts->area);
      ts->R[1] = ts->R[0];
    }
    else

    // Normal tube segments.
    {
      ts->L = (AMBIENT_DENSITY_CGS * 0.5 * ts->length) / ts->area;
      ts->C = ts->volume / (AMBIENT_DENSITY_CGS * SOUND_VELOCITY_CGS * SOUND_VELOCITY_CGS);

      // Here we use the new model for viscous losses.
      double dcResistance = DC_R0 * pow(REF_AREA_CM2 / ts->area, DC_EXPONENT);
      double acResistance = AC_R0 * pow(REF_AREA_CM2 / ts->area, AC_EXPONENT) * sqrt(AC_FREQUENCY_HZ);

      if (dcResistance > acResistance)
      {
        ts->R[0] = 0.5 * ts->length * dcResistance;
      }
      else
      {
        ts->R[0] = 0.5 * ts->length * acResistance;
      }
      ts->R[1] = ts->R[0];
    }

    // **************************************************************
    // The alpha and beta values for the incorporation of wall 
    // vibration.
    // **************************************************************

    ts->alpha = 0.0;
    ts->beta = 0.0;

  	if ((options.softWalls) && (i != Tube::LOWER_GLOTTIS_SECTION) && (i != Tube::UPPER_GLOTTIS_SECTION))
  	{
      // What is the area of the wall ?
      if ((i >= Tube::FIRST_SINUS_SECTION) && (i <= Tube::LAST_SINUS_SECTION))
      {
        // Surface of a sphere
        surface = 4.0*M_PI*pow((3.0*ts->volume)/(4.0*M_PI), 2.0/3.0); 
      }
      else
      {
        // Surface of a cylinder barrel
        surface = circ*ts->length;  
      }

      if (surface < MIN_AREA_CM2)
      {
        surface = MIN_AREA_CM2;
      }

      Rw = WALL_DAMPING_PER_UNIT_AREA_CGS / surface;
      Lw = WALL_MASS_PER_UNIT_AREA_CGS / surface;
      Cw = surface / WALL_STIFFNESS_PER_UNIT_AREA_CGS;

      ts->alpha = 1.0 / (Lw / (timeStep*timeStep*THETA*THETA) + Rw / (timeStep*THETA) + 1.0/Cw);
      ts->beta = ts->alpha*(
        ts->wallCurrent*(Lw/(timeStep*timeStep*THETA*THETA) + Rw/(timeStep*THETA)) +
        ts->wallCurrentRate*(Lw*(THETA1/THETA + 1.0)/(timeStep*THETA) + Rw*(THETA1/THETA)) +
        ts->wallCurrentRate2*Lw*(THETA1/THETA)
        );
  	}

  }

  // ****************************************************************
  // End of the tube section loop.
  // Below, add the kinetic pressure changes/losses.
  // ****************************************************************

  double A1, A2, A3;
  double u1, u2, u3;
  double k;
  int branchSection;

  for (i = 0; i < Tube::NUM_SECTIONS; i++)
  {
    if (((i >= Tube::FIRST_TRACHEA_SECTION) && (i < Tube::LAST_MOUTH_SECTION)) ||
      ((i >= Tube::FIRST_NOSE_SECTION) && (i < Tube::LAST_NOSE_SECTION)) ||
      ((i >= Tube::FIRST_FOSSA_SECTION) && (i < Tube::LAST_FOSSA_SECTION)))
    {
      // Add a kinetic resistance between sections i and i+1, and
      // possibly beteen i and a side branch section.

      A1 = tubeSection[i].area;
      u1 = getCurrentOut(i);

      A2 = tubeSection[i + 1].area;
      u2 = getCurrentIn(i + 1);

      A3 = MIN_AREA_CM2;
      u3 = 0.0;

      // Is there potentially a branch current? This is the case for
      // the branches to the piriform fossa, the nasal cavity, and
      // to the four paranasal cavities.

      branchSection = -1;
      if (tubeSection[i].currentOut[1] != -1)
      {
        branchSection = branchCurrent[tubeSection[i].currentOut[1]].targetSection;
        A3 = tubeSection[branchSection].area;
        u3 = getCurrentIn(branchSection);
      }

      // If there is no branch (in the majority of cases), the (recovery) 
      // coefficient may be < 1.0, when the flow is in the direction of 
      // a tube expansion.

      k = 1.0;
      if ((branchSection == -1) && (((A1 > A2) && (u1 < 0.0)) || ((A1 < A2) && (u1 > 0.0))))
      {
        if ((i == Tube::LOWER_GLOTTIS_SECTION) || (i == Tube::UPPER_GLOTTIS_SECTION))
        {
          k = GLOTTIS_RECOVERY_COEFF;
        }
        else
        {
          k = TRACT_RECOVERY_COEFF;
        }
      }

      tubeSection[i].R[1] -= k * 0.5 * u1 * AMBIENT_DENSITY_CGS / (A1 * A1);
      tubeSection[i + 1].R[0] += k * 0.5 * u2 * AMBIENT_DENSITY_CGS / (A2 * A2);
      if (branchSection != -1)
      {
        tubeSection[branchSection].R[0] += k * 0.5 * u3 * AMBIENT_DENSITY_CGS / (A3 * A3);
      }
    }
  }

  // ****************************************************************
  // Possibly decouple the sinus piriformis.
  // ****************************************************************

  if (options.piriformFossa == false)
  {
    // Set entrance of piriform fossae to the (very high) flow resistance
    // of the smallest possible area.
    ts = &tubeSection[Tube::FIRST_FOSSA_SECTION];
    ts->R[0] = 8.0 * AIR_VISCOSITY_CGS * ts->length * M_PI / (MIN_AREA_CM2 * MIN_AREA_CM2);
  }

  // ****************************************************************
  // Calculate the position and strength of the noise sources.
  // ****************************************************************
 
  if (options.generateNoiseSources) 
  { 
    calcNoiseSources(); 
  }

  // ****************************************************************
  // Calculate D and E for each tube section.
  // ****************************************************************

  double sourceAmp;

  for (i=0; i < Tube::NUM_SECTIONS; i++)
  {
    ts = &tubeSection[i];

    sourceAmp = ts->monopoleSource.sample;

    if (i == flowSourceSection) 
    { 
      sourceAmp += flowSourceAmp; 
    }

    d = timeStep*THETA / (ts->C + ts->alpha);
    ts->E = d;
    ts->D = ts->pressure + timeStep*THETA1*ts->pressureRate - 
            d*(ts->beta - sourceAmp);
  }

}


// ****************************************************************************
/// Resets the member variables of a constriction.
// ****************************************************************************

void TdsModel::resetConstriction(Constriction *c)
{
  c->firstSection = -1;
  c->lastSection = -1;
  c->narrowestSection = -1;
  c->obstaclePos = 0.0;
  c->obstacleSection = -1;
  c->area = 0.0;
  c->flow = 0.0;
  c->velocity = 0.0;
  c->cutoffFreq = 0.0;
  c->gain = 0.0;
  c->fullAmp = 0.0;
  c->articulator = Tube::OTHER_ARTICULATOR;
}


// ****************************************************************************
/// Calculate the positions and amplitudes of the noise sources.
// ****************************************************************************

void TdsModel::calcNoiseSources()
{
  const double MAX_CONSTRICTION_AREA = 1.0;   // 1.0 cm^2
  const double MAX_DELTA_AREA = 0.2;          // 0.2 cm^2
  const double MAX_TEETH_DISTANCE = 2.5;      // = 2.5 cm

  Constriction *cons = NULL;
  int i, k;
  TubeSection *ts = NULL;
  NoiseSource *s = NULL;

  // ****************************************************************
  // Reset the target amplitude of all noise sources.
  // ****************************************************************

  for (i=0; i < Tube::NUM_SECTIONS; i++)
  {
    tubeSection[i].monopoleSource.targetAmp1kHz = 0.0;
    tubeSection[i].dipoleSource.targetAmp1kHz = 0.0;
  }
  lipsDipoleSource.targetAmp1kHz = 0.0;

  // ****************************************************************
  // The noise source right above the glottis is always there.
  // ****************************************************************

  numConstrictions = 0;
  cons = &constriction[numConstrictions];
  numConstrictions++;

  resetConstriction(cons);
  
  cons->firstSection = Tube::LOWER_GLOTTIS_SECTION;
  cons->lastSection = Tube::UPPER_GLOTTIS_SECTION;
  // Always take the upper glottis section as the "narrowest" section
  // to avoid flipping of the two sections all the time.
  cons->narrowestSection = Tube::UPPER_GLOTTIS_SECTION;
  cons->obstaclePos = 1.5;   // 1.5 cm above the glottis.
  cons->articulator = Tube::VOCAL_FOLDS;

  // ****************************************************************
  // The noise source at the nasal cavity entrance is always there.
  // ****************************************************************

  cons = &constriction[numConstrictions];
  numConstrictions++;

  resetConstriction(cons);

  cons->firstSection = Tube::FIRST_NOSE_SECTION;
  cons->lastSection = Tube::FIRST_NOSE_SECTION;
  cons->narrowestSection = Tube::FIRST_NOSE_SECTION;
  // Obstacle pos. is not used. A fixed obstacle section is used further below.
  cons->obstaclePos = tubeSection[Tube::FIRST_NOSE_SECTION].pos + 1.5;
  cons->articulator = Tube::OTHER_ARTICULATOR;

  // ****************************************************************
  // Determine the most constricted parts made with the tongue.
  // ****************************************************************

  double minTongueAreaForTeethSource = 1000000.0;   // Extremely high area.
  double minTongueArea = 1000000.0;   // Extremely high area.
  int minTongueSection = -1;

  for (i = Tube::FIRST_PHARYNX_SECTION; i <= Tube::LAST_MOUTH_SECTION; i++)
  {
    if ((tubeSection[i].articulator == Tube::TONGUE) && 
        (tubeSection[i].area < minTongueArea))
    {
      minTongueArea = tubeSection[i].area;
      minTongueSection = i;
    }
  }

  if (minTongueArea < MAX_CONSTRICTION_AREA)
  {
    cons = &constriction[numConstrictions];
    numConstrictions++;

    resetConstriction(cons);
    
    cons->firstSection = minTongueSection;
    cons->lastSection = minTongueSection;
    cons->narrowestSection = minTongueSection;
    cons->articulator = Tube::TONGUE;

    // Find the first and last section of this constricted region.
    
    double maxArea = minTongueArea + MAX_DELTA_AREA; 

    while ((tubeSection[ cons->firstSection ].area < maxArea) &&
           (tubeSection[ cons->firstSection ].articulator == Tube::TONGUE) &&
           (cons->firstSection > Tube::FIRST_PHARYNX_SECTION)) { cons->firstSection--; }
  
    while ((tubeSection[ cons->lastSection ].area < maxArea) &&
           (tubeSection[ cons->lastSection ].articulator == Tube::TONGUE) &&
           (cons->lastSection < Tube::LAST_MOUTH_SECTION)) { cons->lastSection++; }

    cons->firstSection++;
    cons->lastSection--;

    // Position and distance to an obstacle.

    double jetPos = tubeSection[ cons->lastSection ].pos + tubeSection[ cons->lastSection ].length;

    // The case for /s, S/
    if (teethPosition - jetPos < MAX_TEETH_DISTANCE)
    {
      cons->obstaclePos = teethPosition;
      minTongueAreaForTeethSource = tubeSection[ cons->narrowestSection ].area;
    }
    else
    // The case for /x, ch/
    {
      // The obstacle is in the middle of the section immediately
      // downstream from the jet section. This way, we get 2 pressure
      // sources at each end of the section to approximate the
      // source distribution.
      cons->obstaclePos = tubeSection[ cons->lastSection + 1 ].pos + 
        0.5*tubeSection[ cons->lastSection + 1 ].length;
    }
  }


  // ****************************************************************
  // Is there potentially a second constriction in the tongue region?
  // ****************************************************************

  if ((numConstrictions > 0) && (constriction[numConstrictions-1].articulator == Tube::TONGUE))
  {
    Constriction *prevCons = &constriction[numConstrictions-1];
    minTongueArea = 1000000.0;   // Extremely high area.
    minTongueSection = -1;

    for (i = Tube::FIRST_PHARYNX_SECTION; i <= Tube::LAST_MOUTH_SECTION; i++)
    {
      if ((tubeSection[i].articulator == Tube::TONGUE) && 
          (tubeSection[i].area < minTongueArea) && 
          ((i < prevCons->firstSection) || (i > prevCons->lastSection)))
      {
        minTongueArea = tubeSection[i].area;
        minTongueSection = i;
      }
    }

    if (minTongueArea < MAX_CONSTRICTION_AREA)
    {
      cons = &constriction[numConstrictions];
      numConstrictions++;

      resetConstriction(cons);

      cons->narrowestSection = minTongueSection;
      cons->firstSection = minTongueSection;
      cons->lastSection = minTongueSection;
      cons->articulator = Tube::TONGUE;

      // Find the first and last section of this constricted region.
    
      double maxArea = minTongueArea + MAX_DELTA_AREA; 

      while ((tubeSection[ cons->firstSection ].area < maxArea) &&
             (tubeSection[ cons->firstSection ].articulator == Tube::TONGUE) &&
             (cons->firstSection > Tube::FIRST_PHARYNX_SECTION)) { cons->firstSection--; }
  
      while ((tubeSection[ cons->lastSection ].area < maxArea) &&
             (tubeSection[ cons->lastSection ].articulator == Tube::TONGUE) &&
             (cons->lastSection < Tube::LAST_MOUTH_SECTION)) { cons->lastSection++; }

      cons->firstSection++;
      cons->lastSection--;

      if ((cons->firstSection > prevCons->lastSection + 1) || (cons->lastSection < prevCons->firstSection - 1))
      {
        // Position and distance to an obstacle.

        double jetPos = tubeSection[ cons->lastSection ].pos + tubeSection[ cons->lastSection ].length;

        // The case for /s,S/
        if (teethPosition - jetPos < MAX_TEETH_DISTANCE)
        {
          cons->obstaclePos = teethPosition;
          minTongueAreaForTeethSource = tubeSection[ cons->narrowestSection ].area;
        }
        else
        // The case for /x,ch/
        {
          // The obstacle is in the middle of the section immediately
          // downstream from the jet section. This way, we get 2 pressure
          // sources at each end of the section to approximate the
          // source distribution.
          cons->obstaclePos = tubeSection[ cons->lastSection + 1 ].pos + 
            0.5*tubeSection[ cons->lastSection + 1 ].length;
        }
      }
      else
      {
        // Remove this constriction again.
        numConstrictions--;
      }

    }
  }


  // ****************************************************************
  // Determine the most constricted part in the region of the lower 
  // lip.
  // ****************************************************************

  double maxArea = 0.0;
  double minLipArea = 1000000.0;    // Very big area.
  int minLipSection = -1;

  for (i = Tube::FIRST_MOUTH_SECTION; i <= Tube::LAST_MOUTH_SECTION; i++)
  {
    if ((tubeSection[i].articulator == Tube::LOWER_LIP) && 
        (tubeSection[i].area < minLipArea))
    {
      minLipArea = tubeSection[i].area;
      minLipSection = i;
    }
  }

  // The constriction area has to be smaller than a potential tongue
  // constriction with a source at the upper incisors.

  if ((minLipArea < MAX_CONSTRICTION_AREA) && (minLipArea < minTongueAreaForTeethSource))
  {
    cons = &constriction[numConstrictions];
    numConstrictions++;

    resetConstriction(cons);

    cons->firstSection = minLipSection;
    cons->lastSection = minLipSection;
    cons->narrowestSection = minLipSection;
    cons->articulator = Tube::LOWER_LIP;

    // Find the first and last section of this constricted region.
    
    maxArea = minLipArea + MAX_DELTA_AREA; 

    while ((tubeSection[ cons->firstSection ].area < maxArea) &&
           (tubeSection[ cons->firstSection ].articulator == Tube::LOWER_LIP) &&
           (cons->firstSection > Tube::FIRST_MOUTH_SECTION)) { cons->firstSection--; }
  
    while ((tubeSection[ cons->lastSection ].area < maxArea) &&
           (tubeSection[ cons->lastSection ].articulator == Tube::LOWER_LIP) &&
           (cons->lastSection < Tube::LAST_MOUTH_SECTION)) { cons->lastSection++; }

    cons->firstSection++;
    if (tubeSection[cons->lastSection].area >= maxArea)
    {
      cons->lastSection--;
    }

    // Put the noise source in the middle of the constriction.
    // This sounds better compared to a source at the end of the constriction.
    cons->obstaclePos = 0.5 * (tubeSection[cons->firstSection].pos + tubeSection[cons->lastSection].pos + tubeSection[cons->lastSection].length);
  }

    
  // ****************************************************************
  // Determine the parameters of the dipole noise source(s) for all
  // individual constrictions.
  // Always use two dipole sources: One at the upstream end of the
  // tube section with the constriction, and one at the downstream 
  // end. The amplitude of both sources is scaled in relation to the
  // distances of both tube ends to the constriction location.
  // ****************************************************************

  for (k=0; k < numConstrictions; k++)
  {
    cons = &constriction[k];

    // ************************************************************
    // Determine and restrict the cross-sectional area and
    // the diameter of the constriction.
    // ************************************************************

    ts = &tubeSection[cons->narrowestSection];
    cons->area = ts->area;

    if (cons->area < MIN_AREA_CM2)
    {
      cons->area = MIN_AREA_CM2;
    }

    double diameter_cm = sqrt(4.0 * cons->area / M_PI);

    // ************************************************************
    // Determine the flow and particle velocity in the constriction.
    // This may be needed for the calc. of the cutoff frequency.
    // ************************************************************

    cons->flow = 0.0;

    if (ts->currentOut[0] != -1)
    {
      cons->flow += branchCurrent[ts->currentOut[0]].noiseMagnitude;
    }
    if (ts->currentOut[1] != -1)
    {
      cons->flow += branchCurrent[ts->currentOut[1]].noiseMagnitude;
    }

    // Generate noise sources only for outgoing flow.
    // Otherwise we might get click artifacts.

    if (cons->flow < 0.0)
    {
      cons->flow = 0.0;
    }

    cons->velocity = cons->flow / cons->area;

    // ************************************************************
    // Determine the cutoff frequency of the noise shaping filter
    // and the gain, depending on the place of articulation.
    // ************************************************************

    cons->cutoffFreq = 1000.0;    // Default value.
    cons->gain = 0.0;

    if (cons->articulator == Tube::LOWER_LIP)
    {
      // /f/ has a flat source spectrum (see Badin 1989).
      double gain_f = 0.7e-6;
      double cutoffFreq_f = 8000.0;

      // The burst of /p,b/ has essentially low-frequency noise.
      double gain_p = 0.2e-6;
      double cutoffFreq_p = 500.0;

      // Determine whether the lower-lip constriction is with the
      // upper incisors (d=1) or rather with the upper lip (d=0).

      double areaAtTeeth_cm2 = 1000000.0;
      for (i = Tube::FIRST_MOUTH_SECTION; i <= Tube::LAST_MOUTH_SECTION; i++)
      {
        if ((teethPosition >= tubeSection[i].pos) &&
          (teethPosition <= tubeSection[i].pos + tubeSection[i].length))
        {
          areaAtTeeth_cm2 = tubeSection[i].area;
        }
      }

      const double CRITICAL_CONSTRICTION_AREA_CM2 = 0.15;
      double d = exp(-(areaAtTeeth_cm2 - CRITICAL_CONSTRICTION_AREA_CM2) / 0.10);

      if (d > 1.0) { d = 1.0; }

      cons->gain = d * gain_f + (1.0 - d) * gain_p;
      cons->cutoffFreq = d * cutoffFreq_f + (1.0 - d) * cutoffFreq_p;
    }
    else

    if (cons->articulator == Tube::VOCAL_FOLDS)
    {
      // According to Badin et al. (1994), the aspiration noise source
      // spectrum is essentially flat -> high f_c, but not too high.
      // f_c > 6 kHz sounds increasingly worse.
      cons->gain = 0.2e-6;
      cons->cutoffFreq = 1000.0;
    }
    else

    // The noise source in the nasal cavity.
    if (cons->firstSection == Tube::FIRST_NOSE_SECTION)
    {
      // A gain higher than 0.3e-6 might lead to click artifacts in some cases.
      cons->gain = 0.3e-6;
      cons->cutoffFreq = 500;
    }
    else

    // The primary articulator is the TONGUE here.
    {
      // Fricatives and plosives with the constriction at the incisors: /s, sh, c, d, t/
      if (fabs(cons->obstaclePos - teethPosition) < 0.0001)
      {
        // Parameters for fricatives with high-frequency noise.
        // f_c should be around 4-6 kHz.
        double jetPos = tubeSection[cons->lastSection].pos + tubeSection[cons->lastSection].length;
        double obstacleDistance_cm = fabs(cons->obstaclePos - jetPos);
        const double REF_DISTANCE_CM = 3.0;   // Where the values drop to 37 %

        // Gain and cutoff freq. for /s, S, C/.
        // Reduce both gain and cutoff frequency with increasing distance to obstacle.
        double gain_fricatives = 1.0e-6 * exp(-obstacleDistance_cm / REF_DISTANCE_CM);
        double cutoffFreq_fricatives = 6000.0 * exp(-obstacleDistance_cm / REF_DISTANCE_CM);

        // The bursts of /d,t/ has a clearly lower cutoff frequency.
        double gain_plosives = 0.6e-6 * exp(-obstacleDistance_cm / REF_DISTANCE_CM);
        double cutoffFreq_plosives = 1500.0 * exp(-obstacleDistance_cm / REF_DISTANCE_CM);

        // Interploate between the settings for plosives and fricatives,
        // depending on the elevation of the tongue tip sides.

        double d = tongueTipSideElevation;  // = 1 for fricatives
        
        if (d < 0.0) { d = 0.0; }   // d < 0 means lateral passages.
        if (d > 1.0) { d = 1.0; }

        cons->gain = d * gain_fricatives + (1.0 - d) * gain_plosives;
        cons->cutoffFreq = d * cutoffFreq_fricatives + (1.0 - d) * cutoffFreq_plosives;

        // Prevent a noise source for lateral passages.
        // The noise source for /s, sh/ does simply not fit for laterals,
        // even when the amplitude is strongly turned down.

        if (tongueTipSideElevation < 0.0)
        {
          // Ramp down the gain to zero for TS3 going from 0 ... REF_ELEVATION.
          const double REF_ELEVATION = -0.2;
          double factor = (tongueTipSideElevation - REF_ELEVATION) / -REF_ELEVATION;
          if (factor < 0.0) { factor = 0.0; }
          if (factor > 1.0) { factor = 1.0; }
          cons->gain *= factor;
        }
      }
      else
      // The case for a wall source for /x, k/.
      {
        // The gain should be about 6 dB (factor 1/2) smaller than the
        // gain for fricative noise sources at the incisors, and about
        // the same as the gain for the /d,t/-noise source. For higher
        // gains, /ata/ might sound like /aka/, because this wall noise
        // source (as in /k/) dominates the perception.

        cons->gain = 0.5e-6;
        cons->cutoffFreq = 1500.0;
      }
    }

    // ************************************************************
    // Smoothly reduce the gain to zero for areas between 0.5 cm^2 
    // and 1.0 cm^2 in order to prevent a potential click artifact
    // when the noise source is suddenly turned off at 1 cm^2.
    // ************************************************************

    const double TAPER_START_AREA = 0.5;   // cm^2
    if (cons->area >= TAPER_START_AREA)
    {
      // d in [0, 1]
      double d = (cons->area - TAPER_START_AREA) / (MAX_CONSTRICTION_AREA - TAPER_START_AREA);
      cons->gain *= (1.0 - d);
    }

    // ************************************************************
    // Calc. the level (RMS value) of the noise source.
    // ************************************************************

    cons->fullAmp = cons->gain*fabs(cons->velocity)*
      cons->velocity*cons->velocity*sqrt(cons->area);   // Stevens' book

    // Safety check for the cutoff frequency.

    if (cons->cutoffFreq > 10000.0)
    {
      cons->cutoffFreq = 10000.0;
    }
    if (cons->cutoffFreq < 1.0)
    {
      cons->cutoffFreq = 1.0;
    }

    // **************************************************************
    // Determine the tube section with the obstacle and put the
    // noise source there (splitted in two sources).
    // **************************************************************

    // This is a noise source in nasal cavity.

    if (cons->firstSection == Tube::FIRST_NOSE_SECTION)
    {
      // Take a fixed obstable section directly downstream from the 
      // velic port and set the parameters of the (single) noise source.
      cons->obstacleSection = Tube::FIRST_NOSE_SECTION + 1;
      NoiseSource* source = &tubeSection[cons->obstacleSection].dipoleSource;
      source->targetAmp1kHz = cons->fullAmp;
      source->isFirstOrder = false;
      source->cutoffFreq = cons->cutoffFreq;
    }

    else
    // This is a noise source in the pharynx or mouth.
    {
      // If the obstaclePos is in front of the mouth of the vocal tract
      // then move it at the very lip end.

      if (cons->obstaclePos >= tubeSection[Tube::LAST_MOUTH_SECTION].pos + tubeSection[Tube::LAST_MOUTH_SECTION].length)
      {
        cons->obstaclePos = tubeSection[Tube::LAST_MOUTH_SECTION].pos + 0.99 * tubeSection[Tube::LAST_MOUTH_SECTION].length;
      }

      // Determine the obstacle section.

      cons->obstacleSection = -1;

      for (i = Tube::FIRST_PHARYNX_SECTION; (i <= Tube::LAST_MOUTH_SECTION) && (cons->obstacleSection == -1); i++)
      {
        if ((tubeSection[i].pos <= cons->obstaclePos) &&
          (tubeSection[i].pos + tubeSection[i].length >= cons->obstaclePos))
        {
          cons->obstacleSection = i;
        }
      }

      if (cons->obstacleSection != -1)
      {
        // Set the parameters of the two noise sources.

        NoiseSource* upstreamSource = NULL;
        NoiseSource* downstreamSource = NULL;

        if (cons->obstacleSection < Tube::LAST_MOUTH_SECTION)
        {
          upstreamSource = &tubeSection[cons->obstacleSection].dipoleSource;
          downstreamSource = &tubeSection[cons->obstacleSection + 1].dipoleSource;
        }
        else
        {
          upstreamSource = &tubeSection[cons->obstacleSection].dipoleSource;
          downstreamSource = &lipsDipoleSource;
        }

        // Factors between 0 and 1 for the contributions of the two sources.
        double downstreamFactor = (cons->obstaclePos - tubeSection[cons->obstacleSection].pos) /
          tubeSection[cons->obstacleSection].length;
        double upstreamFactor = 1.0 - downstreamFactor;

        upstreamSource->targetAmp1kHz = upstreamFactor * cons->fullAmp;
        upstreamSource->isFirstOrder = false;
        upstreamSource->cutoffFreq = cons->cutoffFreq;

        downstreamSource->targetAmp1kHz = downstreamFactor * cons->fullAmp;
        downstreamSource->isFirstOrder = false;
        downstreamSource->cutoffFreq = cons->cutoffFreq;
      }
    }

  }


  // ****************************************************************
  // Calculate the new noise samples at the positions of the sources.
  // Iterate through the tube sections of pharynx, mouth, and nose.
  // ****************************************************************

  const double MIN_MONOPOLE_AMP = 0.001;          // cm^3/s
  const double MIN_DIPOLE_AMP = 0.001;          // deci-Pascal

  // Run through all tube sections.

  for (i = Tube::FIRST_PHARYNX_SECTION; i <= Tube::LAST_NOSE_SECTION; i++)
  {
    ts = &tubeSection[i];
    calcNoiseSample(&ts->monopoleSource, MIN_MONOPOLE_AMP);
    calcNoiseSample(&ts->dipoleSource, MIN_DIPOLE_AMP);
  }
  calcNoiseSample(&lipsDipoleSource, MIN_DIPOLE_AMP);
}


// ****************************************************************************
/// Calculates a new noise sample for the given noise source and the given
/// amplitude threshold.
// ****************************************************************************

void TdsModel::calcNoiseSample(NoiseSource *s, double ampThreshold)
{
  double delta = 0.0;

  // ****************************************************************
  // The noise amplitude may not increase too fast. However,
  // a decrease can happen instantaneously (nonlinear filter).
  // This is important for the last milliseconds before the implosion
  // of plosives. When A_c is 15 mm^2, the target amplitude gets very
  // high (as for fricatives), but with the filter below, the rise 
  // towards this high value is sufficiently delayed to prevent 
  // strong frication noise during the implosion.
  // ****************************************************************

  if (s->targetAmp1kHz > s->currentAmp1kHz)
  {
    // First-order filter goes up to 63 % of the target value within 
    // the time constant.
    const double TIME_CONSTANT_SAMPLES = 0.003 * (double)AUDIO_SAMPLING_RATE_HZ;   // 3 ms
    const double F = exp(-1.0 / TIME_CONSTANT_SAMPLES);

    // Filter sqrt(amp) instead of amp, which delays even better.
    double sqrtTargetAmp = sqrt(s->targetAmp1kHz);
    double sqrtCurrentAmp = sqrt(s->currentAmp1kHz);
    sqrtCurrentAmp = (1.0 - F) * sqrtTargetAmp + F * sqrtCurrentAmp;
    s->currentAmp1kHz = sqrtCurrentAmp * sqrtCurrentAmp;
  }
  else
  {
    s->currentAmp1kHz = s->targetAmp1kHz;
  }

  // ****************************************************************
  // Turn the source off.
  // ****************************************************************

  if (s->currentAmp1kHz < ampThreshold)
  {
    s->currentAmp1kHz = 0.0;

    // Clear the noise sample buffers.
    int k;
    for (k = 0; k < NUM_NOISE_BUFFER_SAMPLES; k++)
    {
      s->inputBuffer[k] = 0.0;
      s->outputBuffer[k] = 0.0;
    }

    // Return with a sample value of zero.
    s->sample = 0.0;
    return;
  }
   
  // ****************************************************************
  // The source is active.
  // ****************************************************************
  
  // Setup the IIR-filter to get the filter coefficients.
  IirFilter filter;

  if (s->isFirstOrder)
  {
    filter.createSinglePoleLowpass(s->cutoffFreq*timeStep);
  }
  else
  {
    // Always assume a critically damped 2nd order low-pass filter.
    double Q = 1.0 / sqrt(2.0);
    filter.createSecondOrderLowpass(s->cutoffFreq * timeStep, Q);
  }

  // ****************************************************************
  // Adjust the gain of the filter such that the RMS of the radiated
  // noise keeps constant, independently from the cutoff frequency.
  // Therefore, increase the gain when f_c < 1 kHz, and decrease the
  // gain, when f_c > 1 kHz, i.e., we aim for a kind of constant
  // gain-bandwidth product.
  // ****************************************************************

  double filterGain = s->currentAmp1kHz;
  if (s->cutoffFreq < 1.0)
  {
    s->cutoffFreq = 1.0;
  }
  double factor = 1000.0 / s->cutoffFreq;
  // Multiplication with factor is necessary due to the radiation
  // characteristics.
  filterGain *= factor * sqrt(factor);

  // Constrol the OVERALL noise gain with an additional factor.
  filterGain *= 0.6;
   
  // ****************************************************************
  // Insert a new random number into the input buffer and
  // do the recursive filtering.
  // ****************************************************************

  // inputSample is a random number with the standard deviation
  // 1/sqrt(12) and range limited to [-1.0, 1.0]
  normal_distribution<double> normalDistribution(0.0, 1.0 / sqrt(12.0));
  double inputSample = 0.0;
  do 
  {
    inputSample = normalDistribution(randomNumberGenerator);
  } 
  while ((inputSample < -1.0) || (inputSample > 1.0));
  
  s->inputBuffer[position & NOISE_BUFFER_MASK] = inputSample;
  double sum = filter.a[0] * inputSample;
  int k;
  for (k = 1; k <= filter.order; k++)
  {
    sum += filter.a[k] * s->inputBuffer[(position - k) & NOISE_BUFFER_MASK];
    sum += filter.b[k] * s->outputBuffer[(position - k) & NOISE_BUFFER_MASK];
  }
  s->outputBuffer[position & NOISE_BUFFER_MASK] = sum;
  s->sample = sum * filterGain;      // Resulting magnitude
}


// ****************************************************************************
/// Returns the current flow in the center of the given section.
// ****************************************************************************

void TdsModel::getSectionFlow(int sectionIndex, double &inflow, double &outflow)
{
  inflow = getCurrentIn(sectionIndex);
  outflow = getCurrentOut(sectionIndex);
}

// ****************************************************************************
/// Returns the current pressure in the center of the given section.
// ****************************************************************************

double TdsModel::getSectionPressure(int sectionIndex)
{
  if ((sectionIndex < 0) ||(sectionIndex >= Tube::NUM_SECTIONS))
  {
    return 0.0;
  }

  TubeSection *ts = &tubeSection[sectionIndex];
  return ts->pressure;
}


// ****************************************************************************
/// Calculates the junction inductance between two adjacent tube sections with
/// the given areas according to SONDHI (1983).
/// This additional inductance is a kind of "inner length correction" that
/// affects the formant frequencies when there are abrupt changes in the area
/// function.
// ****************************************************************************

double TdsModel::getJunctionInductance(double A1_cm2, double A2_cm2)
{
  double a, b;    // Radii of the bigger and smaller tube section

  // Safety checks.

  if (A1_cm2 < MIN_AREA_CM2)
  {
    A1_cm2 = MIN_AREA_CM2;
  }
  
  if (A2_cm2 < MIN_AREA_CM2)
  {
    A2_cm2 = MIN_AREA_CM2;
  }

  // ****************************************************************

  if (A1_cm2 > A2_cm2)
  {
    a = sqrt(A1_cm2 / M_PI);
    b = sqrt(A2_cm2 / M_PI);
  }
  else
  {
    a = sqrt(A2_cm2 / M_PI);
    b = sqrt(A1_cm2 / M_PI);
  }

  double H = 1.0 - b/a;
  double L = 8.0*AMBIENT_DENSITY_CGS*H / (3.0*M_PI*M_PI*b);

  return L;
}


// ****************************************************************************
/// Calculate the coefficients of the matrix for the system of eqs.
// ****************************************************************************

void TdsModel::calcMatrix()
{
  int i;

  // Clear the solution vector. *************************************

  for (i=0; i < NUM_BRANCH_CURRENTS; i++) 
  { 
    solutionVector[i] = 0.0; 
  }

  // ****************************************************************
  // Fill one row of the matrix for each branch current.
  // ****************************************************************

  double F, G, H;
  BranchCurrent *bc       = NULL;
  TubeSection   *sourceTs = NULL;
  TubeSection   *targetTs = NULL;

  for (i=0; i < NUM_BRANCH_CURRENTS; i++)
  {
    bc = &branchCurrent[i];
    if (bc->sourceSection != -1) 
    { 
      sourceTs = &tubeSection[bc->sourceSection]; 
    } 
    else 
    { 
      sourceTs = NULL; 
    }
    
    if (bc->targetSection != -1) 
    { 
      targetTs = &tubeSection[bc->targetSection]; 
    } 
    else 
    { 
      targetTs = NULL; 
    }

    // **************************************************************
    // Both the source and the target section are INvalid.
    // **************************************************************

    if ((!sourceTs) && (!targetTs))
    {
      printf("Error in calcMatrix(): Both the source and target section of the "
        "branch current are invalid!\n");
      return;
    }

    // **************************************************************
    // The branch current flows into free space.
    // **************************************************************

    if (targetTs == NULL)
    {
      // ************************************************************
      // There is no radiation impedance.
      // ************************************************************

      if ((sourceTs->currentOut[0] == -1) || (sourceTs->currentOut[1] == -1))
      {
        printf("Error in calcMatrix(): There are no 2 parallel currents for "
          "the radiation impedance.\n");
        return;
      }

      int resistanceCurrent  = sourceTs->currentOut[0];
      int inductivityCurrent = sourceTs->currentOut[1];

      double uR = branchCurrent[resistanceCurrent].magnitude;
      double uL = branchCurrent[inductivityCurrent].magnitude;
      double uR_rate = branchCurrent[resistanceCurrent].magnitudeRate;
      double uL_rate = branchCurrent[inductivityCurrent].magnitudeRate;

      double L_A = sourceTs->L;
      double R_A = sourceTs->R[1];
      double S   = -lipsDipoleSource.sample;    // Spannungsquelle am Ende des Rohrabschnitts

      double radiationArea = sourceTs->area;

      // ************************************************************
      // Current through the radiation resistor.
      // ************************************************************

      if (i == resistanceCurrent)
      {
        double R_rad = (128 * AMBIENT_DENSITY_CGS * SOUND_VELOCITY_CGS) / 
          (9.0 * M_PI * M_PI * radiationArea);

        F = L_A / (timeStep*THETA) + R_A + R_rad;
        G = L_A / (timeStep*THETA) + R_A;
        H = -(L_A / (timeStep*THETA)) * (uR+uL) - (L_A*(THETA1/THETA))*(uR_rate + uL_rate) + S;
      }
      else

      // ************************************************************
      // Current through the radiation inductivity.
      // ************************************************************

      if (i == inductivityCurrent)
      {
        double L_rad = (8.0*AMBIENT_DENSITY_CGS) / (3.0*M_PI*sqrt(radiationArea*M_PI));
        double L_AB = L_A + L_rad;

        F = L_A/(timeStep*THETA) + R_A;
        G = L_AB/(timeStep*THETA) + R_A;
        H = -(1.0/(timeStep*THETA))*(L_A*uR + L_AB*uL) - (THETA1/THETA)*(L_A*uR_rate + L_AB*uL_rate) + S;
      }
      else
      {
        printf("Error in calcMatrix(): The branch current into the free field has "
          "not a valid type.\n");
        return;
      }

      // Inflowing currents
      if (sourceTs->currentIn != -1) { matrix[i][sourceTs->currentIn] = sourceTs->E; }
        
      // Branch currents through the inductivity and the resistance
      matrix[i][resistanceCurrent]  = -sourceTs->E - F;
      matrix[i][inductivityCurrent] = -sourceTs->E - G;

      solutionVector[i] = H - sourceTs->D;
    }

    else

    // **************************************************************
    // The target tube section is valid in any case.
    // **************************************************************
    {
      // Inductivities, resistances, ...

      double L_B = targetTs->L;
      double R_B = targetTs->R[0];

      double L_A = 0.0;
      double R_A = 0.0;
      if (sourceTs != NULL)
      {
        L_A = sourceTs->L;
        R_A = sourceTs->R[1];
      }

      double L_AB = L_A + L_B;
      double R_AB = R_A + R_B;

      // Is there a birfurcation between the sections A and B ?

      int branchingOffCurrent = -1;

      if (sourceTs != NULL)
      {
        if (sourceTs->currentOut[0] == i) 
          { branchingOffCurrent = sourceTs->currentOut[1]; }
        else
          { branchingOffCurrent = sourceTs->currentOut[0]; }
      }

      // Strength of an additional pressure source between both sections
      
      double S = targetTs->S;
      S-= targetTs->dipoleSource.sample;

      if (bc->targetSection == pressureSourceSection) 
      { 
        S-= pressureSourceAmp; 
      }

      // ************************************************************
      // There is a parallel outflowing current.
      // ************************************************************

      if (branchingOffCurrent != -1)
      {
        double uB      = bc->magnitude;
        double uB_rate = bc->magnitudeRate;
        double uD;
        double uD_rate;

        uD      = branchCurrent[branchingOffCurrent].magnitude;
        uD_rate = branchCurrent[branchingOffCurrent].magnitudeRate;

        F = L_AB/(timeStep*THETA) + R_AB;
        G = L_A/(timeStep*THETA)  + R_A;
        H = - (1.0/(timeStep*THETA))*(L_AB*uB + L_A*uD)
            - (THETA1/THETA)*(L_AB*uB_rate + L_A*uD_rate) + S;

        matrix[i][branchingOffCurrent] = -sourceTs->E - G;    // the parallel current.

        // In A inflowing currents
        if (sourceTs != NULL)
        {
          if (sourceTs->currentIn != -1) { matrix[i][sourceTs->currentIn] = sourceTs->E; }
        }

        // This current
        matrix[i][i] = -targetTs->E - sourceTs->E - F;

        // From B outflowing currents
        if (targetTs->currentOut[0] != -1) { matrix[i][targetTs->currentOut[0]] = targetTs->E; }
        if (targetTs->currentOut[1] != -1) { matrix[i][targetTs->currentOut[1]] = targetTs->E; }

        // Solution value
        solutionVector[i] = H + targetTs->D - sourceTs->D;
      }
      else

      // ************************************************************
      // Simple current from section A to B.
      // ************************************************************

      {
        double u      = bc->magnitude;
        double u_rate = bc->magnitudeRate;

        // Apply the "inner tube length correction" to the junction 
        // between these two sections in terms of an additional
        // inductivity (SONDHI, 1983).

        if ((options.innerLengthCorrections) && (bc->sourceSection >= Tube::FIRST_PHARYNX_SECTION) &&
          (bc->targetSection <= Tube::LAST_MOUTH_SECTION))
        {
          L_AB+= getJunctionInductance(tubeSection[bc->sourceSection].area, tubeSection[bc->targetSection].area);
        }

        G = L_AB / (timeStep*THETA) + R_AB;
        H = - u_rate*L_AB*(THETA1/THETA) - (L_AB*u) / (timeStep*THETA) + S;

        // In A inflowing currents
        if (sourceTs != NULL)
        {
          if (sourceTs->currentIn != -1) { matrix[i][sourceTs->currentIn] = sourceTs->E; }
        }

        // This current
        matrix[i][i] = -targetTs->E - G;
        if (sourceTs != NULL) { matrix[i][i]-= sourceTs->E; }

        // From B outflowing currents
        if (targetTs->currentOut[0] != -1) { matrix[i][targetTs->currentOut[0]] = targetTs->E; }
        if (targetTs->currentOut[1] != -1) { matrix[i][targetTs->currentOut[1]] = targetTs->E; }

        // Solution value
        solutionVector[i] = H + targetTs->D;
        if (sourceTs != NULL) { solutionVector[i]-= sourceTs->D; }
      }

    }
  }   // All branch currents

}


// ****************************************************************************
/// Recalculate all currents, pressures and their temporal derivatives.
// ****************************************************************************

void TdsModel::updateVariables()
{
  int i;
  TubeSection *ts = NULL;
  BranchCurrent *bc = NULL;

  double oldPressure;
  double oldCurrent;
  double oldCurrentRate;
  double netFlow;

  // ****************************************************************
  // The new currents and their derivatives
  // ****************************************************************

  double noiseFilterCoeff = exp(-2.0*M_PI*NOISE_CUTOFF_FREQ*timeStep);

  for (i=0; i < NUM_BRANCH_CURRENTS; i++)
  {
    bc = &branchCurrent[i];
    oldCurrent = bc->magnitude;
    oldCurrentRate = bc->magnitudeRate;

    bc->magnitude = flowVector[i];
    bc->magnitudeRate = (bc->magnitude - oldCurrent)/(timeStep*THETA) - (THETA1/THETA)*bc->magnitudeRate;
    bc->noiseMagnitude = (1.0-noiseFilterCoeff)*bc->magnitude + noiseFilterCoeff*bc->noiseMagnitude;
  }

  // ****************************************************************
  // The new pressures and their derivatives
  // ****************************************************************

  for (i=0; i < Tube::NUM_SECTIONS; i++)
  {
    ts = &tubeSection[i];

    netFlow = getCurrentIn(ts) - getCurrentOut(ts);

    oldPressure = ts->pressure;
    ts->pressure = ts->D + ts->E*netFlow;
    ts->pressureRate = (ts->pressure - oldPressure)/(timeStep*THETA) - ts->pressureRate*(THETA1/THETA);

    // The current "into the wall".
    oldCurrent = ts->wallCurrent;
    oldCurrentRate = ts->wallCurrentRate;
    
    ts->wallCurrent = ts->pressureRate*ts->alpha + ts->beta;
    ts->wallCurrentRate  = (ts->wallCurrent - oldCurrent)/(timeStep*THETA) - oldCurrentRate*(THETA1/THETA);
    ts->wallCurrentRate2 = (ts->wallCurrentRate - oldCurrentRate)/(timeStep*THETA) - ts->wallCurrentRate2*(THETA1/THETA);
  }

}


// ****************************************************************************
/// Solve the linear system of equations using the Cholesky factorization
// ****************************************************************************

void TdsModel::solveEquationsCholesky()
{
  int k, i, j, u, v;

  // ****************************************************************
  // Negate the whole matrix and the right-hand side vector.
  // ****************************************************************

  for (i = 0; i < NUM_BRANCH_CURRENTS; i++)
  {
    factorizationMatrix[i][i] = -matrix[i][i];
    // calculate row i without main diagonal elements
    for (v = 0; v < numFilledRowValuesSymmetricEnvelope[i]; v++)
    {
      j = filledRowIndexSymmetricEnvelope[i][v];
      factorizationMatrix[i][j] = -matrix[i][j];
    }
  }

  for (i = 0; i < NUM_BRANCH_CURRENTS; i++)
  {
    solutionVector[i] = -solutionVector[i];
  }

  // ****************************************************************
  // Cholesky factorization
  // ****************************************************************

  for (k = 0; k < NUM_BRANCH_CURRENTS; k++)
  {
    for (v = 0; v < numFilledRowValuesSymmetricEnvelope[k]; v++)
    {
      j = filledRowIndexSymmetricEnvelope[k][v];
      factorizationMatrix[k][k] -= factorizationMatrix[k][j] * factorizationMatrix[k][j];
    }

    if (factorizationMatrix[k][k] < 0.0)
    {
      factorizationMatrix[k][k] = 0.0;
      printf("Error: Cholesky factorization: Matrix is not positive definite!\n");
    }
    factorizationMatrix[k][k] = sqrt(factorizationMatrix[k][k]);

    // calculate column k without main diagonal elements
    for (u = 0; u < numFilledColumnValuesSymmetricEnvelope[k]; u++)
    {
      i = filledColumnIndexSymmetricEnvelope[k][u];

      // calculate row k without main diagonal elements
      for (v = 0; v < numFilledRowValuesSymmetricEnvelope[k]; v++)
      {
        j = filledRowIndexSymmetricEnvelope[k][v];
        factorizationMatrix[i][k] -= factorizationMatrix[i][j] * factorizationMatrix[k][j];
      }

      factorizationMatrix[i][k] = factorizationMatrix[i][k] / factorizationMatrix[k][k];
    }

  }

  // ****************************************************************
  // forward substitution
  // ****************************************************************

  for (k = 0; k < NUM_BRANCH_CURRENTS; k++)
  {
    for (v = 0; v < numFilledRowValuesSymmetricEnvelope[k]; v++)
    {
      j = filledRowIndexSymmetricEnvelope[k][v];
      solutionVector[k] -= factorizationMatrix[k][j] * solutionVector[j];
    }
    solutionVector[k] /= factorizationMatrix[k][k];
  }

  // ****************************************************************
  // backward substitution
  // ****************************************************************

  for (k = NUM_BRANCH_CURRENTS - 1; k >= 0; --k)
  {
    for (u = 0; u < numFilledColumnValuesSymmetricEnvelope[k]; u++)
    {
      i = filledColumnIndexSymmetricEnvelope[k][u];
      solutionVector[k] -= factorizationMatrix[i][k] * flowVector[i];
    }
    flowVector[k] = solutionVector[k] / factorizationMatrix[k][k];
  }
}


// ****************************************************************************
/// Returns the volume velocity into the given tube section.
/// \param section The tube section
// ****************************************************************************

double TdsModel::getCurrentIn(const int section)
{
  double flow = 0.0;
  if ((section >= 0) && (section < Tube::NUM_SECTIONS))
  {
    TubeSection *ts = &tubeSection[section];
    if (ts->currentIn != -1) 
    { 
      flow+= branchCurrent[ts->currentIn].magnitude; 
    }
  }
  return flow;
}

// ****************************************************************************
/// Returns the volume velocity out of the given tube section.
/// \param section The tube section
// ****************************************************************************
    
double TdsModel::getCurrentOut(const int section)
{
  double flow = 0.0;
  if ((section >= 0) && (section < Tube::NUM_SECTIONS))
  {
    TubeSection *ts = &tubeSection[section];
    if (ts->currentOut[0] != -1) { flow+= branchCurrent[ts->currentOut[0]].magnitude; }
    if (ts->currentOut[1] != -1) { flow+= branchCurrent[ts->currentOut[1]].magnitude; }
  }
  return flow;
}

// ****************************************************************************
/// Returns the volume velocity into the given tube section.
/// \param ts Pointer to the tube section
// ****************************************************************************

double TdsModel::getCurrentIn(const TubeSection *ts)
{
  double flow = 0.0;
  if (ts != NULL)
  {
    if (ts->currentIn != -1) 
    { 
      flow+= branchCurrent[ts->currentIn].magnitude; 
    }
  }
  return flow;
}

// ****************************************************************************
/// Returns the volume velocity out of the given tube section.
/// \param ts Pointer to the tube section
// ****************************************************************************
    
double TdsModel::getCurrentOut(const TubeSection *ts)
{
  double flow = 0.0;
  if (ts != NULL)
  {
    if (ts->currentOut[0] != -1) { flow+= branchCurrent[ts->currentOut[0]].magnitude; }
    if (ts->currentOut[1] != -1) { flow+= branchCurrent[ts->currentOut[1]].magnitude; }
  }
  return flow;
}


// ****************************************************************************
