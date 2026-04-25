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

#include "acoustics/Tube.h"
#include <iostream>
#include <cstdio>
#include <cmath>


// ****************************************************************************
// Constructor. Initializes the area function.
// ****************************************************************************

Tube::Tube()
{
  // Create the pointer list to the sections before doing anything else.
  createSectionList();

  initStaticSections();
  resetPharynxMouthSections();
  resetAuxParams();
  resetGlottisSections(0.0);
}

// ****************************************************************************
// Init the static parts: the trachea, the nose, and the piriform fossa.
// piriformFossaLength_cm : The effective acousic length (incl. end correction).
// piriformFossaVolume_cm3 : The total volume of both left and the right sinus.
// ****************************************************************************

void Tube::initStaticSections(
  const double tracheaLength_cm, 
  const double noseLength_cm,
  const double piriformFossaLength_cm, 
  const double piriformFossaVolume_cm3)
{
  int i;
  Section* ts = NULL;

  // Keep the static tube parameters in mind.

  this->tracheaLength_cm = tracheaLength_cm;
  this->noseLength_cm = noseLength_cm;
  this->piriformFossaLength_cm = piriformFossaLength_cm;
  this->piriformFossaVolume_cm3 = piriformFossaVolume_cm3;

  // ****************************************************************
  // Trachea.
  // ****************************************************************

  for (i = 0; i < NUM_TRACHEA_SECTIONS; i++)
  {
    ts = &tracheaSections[i];

    ts->pos_cm = 0.0;
    ts->area_cm2 = 2.5;     // 2.5 cm^2 trachea area
    ts->length_cm = tracheaLength_cm / (double)NUM_TRACHEA_SECTIONS;
    ts->volume_cm3 = ts->area_cm2 * ts->length_cm;
    ts->articulator = OTHER_ARTICULATOR;
  }

  ts = &tracheaSections[0];
  ts->area_cm2 = 4.0;     // 4 cm^2 trachea area
  ts->length_cm = tracheaLength_cm / (double)NUM_TRACHEA_SECTIONS;
  ts->volume_cm3 = ts->area_cm2 * ts->length_cm;

  ts = &tracheaSections[1];
  ts->area_cm2 = 3.0;     // 3 cm^2 trachea area
  ts->length_cm = tracheaLength_cm / (double)NUM_TRACHEA_SECTIONS;
  ts->volume_cm3 = ts->area_cm2 * ts->length_cm;

  // ****************************************************************
  // Nose.
  // Measurements by Dang & Honda (1994).
  // For the paranasal sinuses, see also Dang & Honda (1996), Table V.
  // The values were taken from the center of the tube sections.
  // ****************************************************************
/*
  // Subject 1.
  const double NOSE_LENGTH_CM[NUM_NOSE_SECTIONS] =
  { 0.6, 0.6, 0.6, 0.6, 0.6, 0.6, 0.6, 0.6, 0.6, 0.6,
    0.6, 0.6, 0.6, 0.6, 0.6, 0.6, 0.6, 0.6, 0.6 };
  const double NOSE_AREA_CM2[NUM_NOSE_SECTIONS] =
  { 1.63, 2.07, 2.72, 3.59, 4.24, 3.26, 3.04, 3.04, 2.72, 2.5,
    2.39, 2.39, 1.85, 0.76, 1.41, 1.74, 1.30, 1.74, 0.76 };
*/

/*
  // Subject 2.
  const double NOSE_LENGTH_CM[NUM_NOSE_SECTIONS] =
  { 0.57, 0.57, 0.57, 0.57, 0.57, 0.57, 0.57, 0.57, 0.57, 0.57,
    0.57, 0.57, 0.57, 0.57, 0.57, 0.57, 0.57, 0.57, 0.57 };
  const double NOSE_AREA_CM2[NUM_NOSE_SECTIONS] =
  { 1.30, 1.20, 1.30, 1.20, 1.60, 2.50, 3.60, 2.25, 2.50, 1.75,
    2.60, 2.00, 1.50, 1.50, 0.75, 1.30, 1.60, 1.65, 1.40 };
*/

/*
  // Subject 3.
  const double NOSE_LENGTH_CM[NUM_NOSE_SECTIONS] =
  { 0.6, 0.6, 0.6, 0.6, 0.6, 0.6, 0.6, 0.6, 0.6, 0.6,
    0.6, 0.6, 0.6, 0.6, 0.6, 0.6, 0.6, 0.6, 0.6 };
  const double NOSE_AREA_CM2[NUM_NOSE_SECTIONS] =
  { 0.70, 0.85, 1.20, 1.90, 3.50, 4.70, 4.10, 3.50, 3.70, 3.50,
    3.70, 3.60, 3.00, 2.80, 2.30, 1.80, 1.40, 1.40, 1.60 };
*/

  // Subject 4: Has the strongest spectral tilt among all subjects,
  // which is good for the acoustics of nasals.
  const double NOSE_LENGTH_CM[NUM_NOSE_SECTIONS] =
  { 0.57, 0.57, 0.57, 0.57, 0.57, 0.57, 0.57, 0.57, 0.57, 0.57,
    0.57, 0.57, 0.57, 0.57, 0.57, 0.57, 0.57, 0.57, 0.57 };
  const double NOSE_AREA_CM2[NUM_NOSE_SECTIONS] =
  { 1.30, 1.25, 1.30, 1.20, 1.60, 2.50, 3.60, 2.25, 2.50, 1.75,
    2.50, 2.10, 1.50, 1.50, 0.75, 1.25, 1.60, 1.65, 1.40 };


  const double SINUS_VOLUME_CM3[NUM_SINUS_SECTIONS] = { 11.3, 6.8, 33.0, 6.2 };
  const double NECK_LENGTH_CM[NUM_SINUS_SECTIONS] = { 0.3,   0.3,   0.45,  1.0 };
  const double NECK_AREA_CM2[NUM_SINUS_SECTIONS] = { 0.185, 0.185, 0.145, 0.11 };

  double noseLengthFactor = noseLength_cm / DEFAULT_NASAL_CAVITY_LENGTH_CM;

  for (i = 0; i < NUM_NOSE_SECTIONS; i++)
  {
    ts = &noseSections[i];

    ts->pos_cm = 0.0;
    ts->area_cm2 = NOSE_AREA_CM2[i];
    ts->length_cm = noseLengthFactor * NOSE_LENGTH_CM[i];
    ts->volume_cm3 = ts->area_cm2 * ts->length_cm;
    ts->articulator = OTHER_ARTICULATOR;
  }

  // The paranasal sinuses (they never change).

  for (i = 0; i < NUM_SINUS_SECTIONS; i++)
  {
    ts = &sinusSections[i];

    ts->pos_cm = 0.0;
    ts->area_cm2 = NECK_AREA_CM2[i];
    ts->length_cm = NECK_LENGTH_CM[i];
    ts->volume_cm3 = SINUS_VOLUME_CM3[i];
    ts->articulator = OTHER_ARTICULATOR;
  }

  // ****************************************************************
  // Piriform fossae.
  // ****************************************************************

  double maxArea_cm2 = 2.0 * piriformFossaVolume_cm3 / piriformFossaLength_cm;
  double sectionLength_cm = piriformFossaLength_cm / (double)NUM_FOSSA_SECTIONS;

  for (i = 0; i < NUM_FOSSA_SECTIONS; i++)
  {
    ts = &fossaSections[i];

    ts->pos_cm = 0.0;
    ts->area_cm2 = maxArea_cm2 * (1.0 - (i + 0.5) / (double)NUM_FOSSA_SECTIONS);
    ts->length_cm = sectionLength_cm;
    ts->volume_cm3 = ts->area_cm2 * ts->length_cm;
    ts->articulator = OTHER_ARTICULATOR;
  }

  calcPositions();
}


// ****************************************************************************
// Returns the parameters that were used to initialize the static tube sections.
// ****************************************************************************

void Tube::getStaticParams(double& tracheaLength_cm, double& noseLength_cm,
  double& piriformFossaLength_cm, double& piriformFossaVolume_cm3)
{
  tracheaLength_cm = this->tracheaLength_cm;
  noseLength_cm = this->noseLength_cm;
  piriformFossaLength_cm = this->piriformFossaLength_cm;
  piriformFossaVolume_cm3 = this->piriformFossaVolume_cm3;
}


// ****************************************************************************
// Resets the tube to have a uniform cross-sectional area.
// ****************************************************************************

void Tube::resetPharynxMouthSections()
{
  double sectionLengths_cm[NUM_PHARYNX_MOUTH_SECTIONS];
  double sectionAreas_cm2[NUM_PHARYNX_MOUTH_SECTIONS];
  Articulator sectionArticulators[NUM_PHARYNX_MOUTH_SECTIONS];

  for (int i = 0; i < NUM_PHARYNX_MOUTH_SECTIONS; i++)
  {
    // Full tube length is 16 cm.
    sectionLengths_cm[i] = 16.0 / (double)NUM_PHARYNX_MOUTH_SECTIONS;
    sectionAreas_cm2[i] = 4.0;
    sectionArticulators[i] = OTHER_ARTICULATOR;
  }

  setPharynxMouthSections(sectionLengths_cm, sectionAreas_cm2, sectionArticulators);
}


// ****************************************************************************
// Sets the geometry and articulators of the pharynx and mouth sections.
// ****************************************************************************

void Tube::setPharynxMouthSections(
  const double* sectionLengths_cm,
  const double* sectionAreas_cm2,
  const Articulator* sectionArticulators)
{
  int i;
  Section* ts = NULL;

  for (i = 0; i < NUM_PHARYNX_MOUTH_SECTIONS; i++)
  {
    ts = &pharynxMouthSections[i];

    ts->pos_cm = 0.0;   // Will be set later.
    ts->length_cm = sectionLengths_cm[i];
    ts->area_cm2 = sectionAreas_cm2[i];
    if (ts->area_cm2 < MIN_AREA_CM2) { ts->area_cm2 = MIN_AREA_CM2; }
    ts->volume_cm3 = ts->area_cm2 * ts->length_cm;
    ts->articulator = sectionArticulators[i];
  }

  calcPositions();
}


// ****************************************************************************
// Convenience function to set both sections of the glottis to the same area.
// ****************************************************************************

void Tube::resetGlottisSections(const double area_cm2)
{
  double l_cm[NUM_GLOTTIS_SECTIONS] = { 0.3, 0.3 };
  double a_cm2[NUM_GLOTTIS_SECTIONS] = { area_cm2, area_cm2 };

  setGlottisSections(l_cm, a_cm2);
}


// ****************************************************************************
// Sets the geometry of the lower and upper glottis sections.
// lengths_cm : Lengths of the NUM_GLOTTIS_SECTIONS in cm.
// areas_cm2 : Areas of the NUM_GLOTTIS_SECTIONS in cm^2.
// ****************************************************************************

void Tube::setGlottisSections(const double *lengths_cm, const double *areas_cm2)
{
  int i;
  Section *ts = NULL;

  for (i=0; i < NUM_GLOTTIS_SECTIONS; i++)
  {
    ts = &glottisSections[i];

    ts->pos_cm     = 0.0;     // Will be calculated later.
    ts->length_cm  = lengths_cm[i];
    ts->area_cm2   = areas_cm2[i];
    if (ts->area_cm2 < MIN_AREA_CM2) { ts->area_cm2 = MIN_AREA_CM2; }
    ts->volume_cm3 = ts->area_cm2 * ts->length_cm;
    ts->articulator = VOCAL_FOLDS;
  }

  calcPositions();
}


// ****************************************************************************
// Sets the nasal port area, the incisior position, and the tongue tip side
// elevation to default values.
// ****************************************************************************

void Tube::resetAuxParams()
{
  setAuxParams(0.0, 15.0, 0.0);
}


// ****************************************************************************
// Sets the nasal port area, the incisior position, and the tongue tip side
// elevation.
// ****************************************************************************

void Tube::setAuxParams(const double nasalPortArea_cm2, 
  const double incisorPos_cm, const double tongueTipSideElevation)
{
  // Modify the first few areas of the nose for a naso-pharyngeal 
  // port of the given size.

  int i;
  Section* ts = NULL;
  const int N = 4;  // Only the first N sections of the nose change their shape.
  double targetArea_cm2 = noseSections[N].area_cm2;

  for (i = 0; i < N; i++)
  {
    ts = &noseSections[i];
    ts->area_cm2 = nasalPortArea_cm2 + ((double)(i * i) * (targetArea_cm2 - nasalPortArea_cm2)) / (double)(N * N);
    if (ts->area_cm2 < MIN_AREA_CM2) { ts->area_cm2 = MIN_AREA_CM2; }
    ts->volume_cm3 = ts->area_cm2 * ts->length_cm;
  }

  // Set the aux. parameters.
  this->incisorPos_cm = incisorPos_cm;
  this->tongueTipSideElevation = tongueTipSideElevation;
}


// ****************************************************************************
// Returns the nasal port area, the incisior position, and the tongue tip side
// elevation.
// ****************************************************************************

void Tube::getAuxParams(double &nasalPortArea_cm2, double &incisorPos_cm, double &tongueTipSideElevation)
{
  nasalPortArea_cm2 = noseSections[0].area_cm2;
  incisorPos_cm = this->incisorPos_cm;
  tongueTipSideElevation = this->tongueTipSideElevation;
}


// ****************************************************************************
// Interpolate linearly between leftTube and rightTube to configure this 
// current tube.
// The parameter ratio = [0, 1] determines the contribution of rightTube,
// i.e., thisTube = (1.0-ratio)*leftTube + ratio*rightTube.
// ****************************************************************************

void Tube::interpolate(const Tube* leftTube, const Tube* rightTube, const double ratio)
{
  int i;
  Section* ts = NULL;

  double ratio1 = 1.0 - ratio;

  for (i = 0; i < NUM_SECTIONS; i++)
  {
    ts = sections[i];
    ts->pos_cm = ratio1 * leftTube->sections[i]->pos_cm + ratio * rightTube->sections[i]->pos_cm;
    ts->area_cm2 = ratio1 * leftTube->sections[i]->area_cm2 + ratio * rightTube->sections[i]->area_cm2;
    ts->length_cm = ratio1 * leftTube->sections[i]->length_cm + ratio * rightTube->sections[i]->length_cm;
    ts->volume_cm3 = ratio1 * leftTube->sections[i]->volume_cm3 + ratio * rightTube->sections[i]->volume_cm3;

    if (ratio < 0.5)
    {
      ts->articulator = leftTube->sections[i]->articulator;
    }
    else
    {
      ts->articulator = rightTube->sections[i]->articulator;
    }
  }

  // Interpolate the teeth position and the tongue tip side elevation.

  this->incisorPos_cm = ratio1 * leftTube->incisorPos_cm + ratio * rightTube->incisorPos_cm;
  this->tongueTipSideElevation = ratio1 * leftTube->tongueTipSideElevation + ratio * rightTube->tongueTipSideElevation;
}


// ****************************************************************************
/// Calculate the absolute positions of the tube sections.
// ****************************************************************************

void Tube::calcPositions()
{
  int i;
  double pos;
  const double REFERENCE_POS = 0.0;

  pos = REFERENCE_POS;

  // ****************************************************************
  // The two glottal tube sections.
  // ****************************************************************

  pos-= glottisSections[1].length_cm;
  glottisSections[1].pos_cm = pos;

  pos-= glottisSections[0].length_cm;
  glottisSections[0].pos_cm = pos;

  // ****************************************************************
  // Trachea sections.
  // ****************************************************************

  for (i = NUM_TRACHEA_SECTIONS-1; i >= 0; i--)
  {
    pos-= tracheaSections[i].length_cm;
    tracheaSections[i].pos_cm = pos;
  }

  // ****************************************************************
  // Pharynx and mouth segments.
  // ****************************************************************

  double fossaPos = 0.0;
  double nosePos = 0.0;
  pos = REFERENCE_POS;
  
  for (i=0; i < NUM_PHARYNX_MOUTH_SECTIONS; i++)
  {
    pharynxMouthSections[i].pos_cm = pos;
    pos+= pharynxMouthSections[i].length_cm;

    if (i == FOSSA_COUPLING_SECTION) { fossaPos = pos; }
    if (i == NUM_PHARYNX_SECTIONS - 1) { nosePos = pos; }
  }

  // ****************************************************************
  // Nose and piriform fossa segments.
  // ****************************************************************

  pos = nosePos;
  for (i=0; i < NUM_NOSE_SECTIONS; i++)
  {
    noseSections[i].pos_cm = pos;
    pos+= noseSections[i].length_cm;
  }

  pos = fossaPos;
  for (i=0; i < NUM_FOSSA_SECTIONS; i++)
  {
    fossaSections[i].pos_cm = pos;
    pos+= fossaSections[i].length_cm;
  }

  // ****************************************************************
  // Position of the paranasal sinuses.
  // ****************************************************************

  int couplingSection;

  for (i=0; i < NUM_SINUS_SECTIONS; i++)
  {
    couplingSection = SINUS_COUPLING_SECTION[i];
    sinusSections[i].pos_cm = noseSections[couplingSection].pos_cm + noseSections[couplingSection].length_cm;
  }

}

// ****************************************************************************
// ****************************************************************************

void Tube::print()
{
  int i;

  for (i=0; i < NUM_SECTIONS; i++)
  {
    if (i == FIRST_TRACHEA_SECTION)
    {
      printf("\n# Trachea sections\n");
    }
    else
    if (i == FIRST_GLOTTIS_SECTION)
    {
      printf("\n# Glottis sections\n");
    }
    else
    if (i == FIRST_PHARYNX_SECTION)
    {
      printf("\n# Pharynx-mouth sections\n");
    }
    else
    if (i == FIRST_NOSE_SECTION)
    {
      printf("\n# Nose sections\n");
    }
    else
    if (i == FIRST_FOSSA_SECTION)
    {
      printf("\n# Piriform fossa sections\n");
    }
    else
    if (i == FIRST_SINUS_SECTION)
    {
      printf("\n# Paranasal sinus sections\n");
    }

    printf("#%2d: x=%6.2f cm l=%6.2f cm A=%6.2f cm^2 V=%6.2f cm^3\n", 
      i, 
      sections[i]->pos_cm,
      sections[i]->length_cm,
      sections[i]->area_cm2,
      sections[i]->volume_cm3
    );
  }
}

// ****************************************************************************
/// Overwrite the assignment operator!
// ****************************************************************************

void Tube::operator=(const Tube &t)
{
  int i;

  for (i=0; i < NUM_SECTIONS; i++)
  {
    *this->sections[i] = *t.sections[i];
  }

  this->incisorPos_cm = t.incisorPos_cm;
  this->tongueTipSideElevation = t.tongueTipSideElevation;
}

// ****************************************************************************
/// Are the tubes identical ?
// ****************************************************************************

bool Tube::operator==(const Tube &t)
{
  if ((this->incisorPos_cm != t.incisorPos_cm) ||
    (this->tongueTipSideElevation != t.tongueTipSideElevation))
  {
    return false;
  }

  bool equal = true;
  int i;
  Section *a, *b;

  for (i=0; i < NUM_SECTIONS; i++)
  {
    a = this->sections[i];
    b = t.sections[i];

    if ((a->area_cm2    != b->area_cm2) ||
        (a->length_cm   != b->length_cm) ||
        (a->pos_cm      != b->pos_cm) ||
        (a->volume_cm3  != b->volume_cm3) ||
        (a->articulator != b->articulator))
    {
      equal = false;
      break;
    }
  }

  return equal;
}

// ****************************************************************************
/// Are the tubes not identical ?
// ****************************************************************************

bool Tube::operator!=(const Tube &t)
{
  return !(*this == t);
}


// ****************************************************************************
// Creates the list with pointers to ALL tube sections of the model for
// convenient iterations of all sections.
// ****************************************************************************

void Tube::createSectionList()
{
  int i;

  for (i = 0; i < NUM_TRACHEA_SECTIONS; i++)
  {
    sections[FIRST_TRACHEA_SECTION + i] = &tracheaSections[i];
  }

  for (i = 0; i < NUM_GLOTTIS_SECTIONS; i++)
  {
    sections[FIRST_GLOTTIS_SECTION + i] = &glottisSections[i];
  }

  for (i = 0; i < NUM_PHARYNX_MOUTH_SECTIONS; i++)
  {
    sections[FIRST_PHARYNX_SECTION + i] = &pharynxMouthSections[i];
  }

  for (i = 0; i < NUM_NOSE_SECTIONS; i++)
  {
    sections[FIRST_NOSE_SECTION + i] = &noseSections[i];
  }

  for (i = 0; i < NUM_FOSSA_SECTIONS; i++)
  {
    sections[FIRST_FOSSA_SECTION + i] = &fossaSections[i];
  }

  for (i = 0; i < NUM_SINUS_SECTIONS; i++)
  {
    sections[FIRST_SINUS_SECTION + i] = &sinusSections[i];
  }
}

// ****************************************************************************
