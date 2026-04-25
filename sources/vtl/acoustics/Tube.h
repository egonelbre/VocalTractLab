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

#ifndef __TUBE_H__
#define __TUBE_H__

// ****************************************************************************
// This class represents a discrete vocal tract area function or tube 
// including all side branches and the glottis.
// Only the pharynx and mouth sections and the glottal sections are meant to be 
// changed during speaking. All other sections should be left in their 
// original state. 
// The following functions are needed to fully specify a tube:
//
// initStaticSections() -> Is needed only once.
// setPharynxMouthSections()
// setGlottisSections()
// setAuxParams() -> Sets the nasal port area and other aux params.
// ****************************************************************************

class Tube
{
  // **************************************************************************
  // Public data.
  // **************************************************************************

public:
  // Each tube section is in the region of one of these articulators.
  // This information is needed for the synthesis of fricatives.
  enum Articulator
  {
    VOCAL_FOLDS,
    TONGUE,
    LOWER_INCISORS,
    LOWER_LIP,
    OTHER_ARTICULATOR,
    NUM_ARTICULATORS
  };

  struct Section
  {
    double pos_cm;
    double area_cm2;
    double length_cm;
    double volume_cm3;
    Articulator articulator;
  };

  static constexpr int NUM_TRACHEA_SECTIONS  = 23;    // 1 cm per section = 23 cm in total
  static constexpr int NUM_GLOTTIS_SECTIONS  = 2;
  static constexpr int NUM_PHARYNX_SECTIONS = 18;
  static constexpr int NUM_MOUTH_SECTIONS = 32;
  static constexpr int NUM_PHARYNX_MOUTH_SECTIONS = NUM_PHARYNX_SECTIONS + NUM_MOUTH_SECTIONS;
  static constexpr int NUM_NOSE_SECTIONS  = 19;
  static constexpr int NUM_SINUS_SECTIONS = 4;
  static constexpr int NUM_FOSSA_SECTIONS = 5;
  static constexpr int NUM_SECTIONS       = NUM_TRACHEA_SECTIONS + NUM_GLOTTIS_SECTIONS +
    NUM_PHARYNX_MOUTH_SECTIONS + NUM_NOSE_SECTIONS + NUM_SINUS_SECTIONS + NUM_FOSSA_SECTIONS;

  static constexpr int FIRST_TRACHEA_SECTION = 0;
  static constexpr int FIRST_GLOTTIS_SECTION = FIRST_TRACHEA_SECTION + NUM_TRACHEA_SECTIONS;
  static constexpr int LOWER_GLOTTIS_SECTION = FIRST_GLOTTIS_SECTION;
  static constexpr int UPPER_GLOTTIS_SECTION = FIRST_GLOTTIS_SECTION + 1;
  static constexpr int FIRST_PHARYNX_SECTION = FIRST_GLOTTIS_SECTION + NUM_GLOTTIS_SECTIONS;
  static constexpr int FIRST_MOUTH_SECTION   = FIRST_PHARYNX_SECTION + NUM_PHARYNX_SECTIONS;
  static constexpr int FIRST_NOSE_SECTION    = FIRST_PHARYNX_SECTION + NUM_PHARYNX_MOUTH_SECTIONS;
  static constexpr int FIRST_FOSSA_SECTION   = FIRST_NOSE_SECTION + NUM_NOSE_SECTIONS;
  static constexpr int FIRST_SINUS_SECTION   = FIRST_FOSSA_SECTION + NUM_FOSSA_SECTIONS;

  static constexpr int LAST_TRACHEA_SECTION  = FIRST_TRACHEA_SECTION + NUM_TRACHEA_SECTIONS - 1;
  static constexpr int LAST_PHARYNX_SECTION  = FIRST_PHARYNX_SECTION + NUM_PHARYNX_SECTIONS - 1;
  static constexpr int LAST_MOUTH_SECTION    = FIRST_MOUTH_SECTION + NUM_MOUTH_SECTIONS - 1;
  static constexpr int LAST_NOSE_SECTION     = FIRST_NOSE_SECTION + NUM_NOSE_SECTIONS - 1;
  static constexpr int LAST_FOSSA_SECTION    = FIRST_FOSSA_SECTION + NUM_FOSSA_SECTIONS - 1;
  static constexpr int LAST_SINUS_SECTION    = FIRST_SINUS_SECTION + NUM_SINUS_SECTIONS - 1;

  static const int FOSSA_COUPLING_SECTION = 4;    // Piriform fossa coupling section
  static constexpr int SINUS_COUPLING_SECTION[NUM_SINUS_SECTIONS] = { 8, 9, 11, 12 };

  static constexpr double MIN_AREA_CM2 = 0.01e-2;  // 0.01 mm^2

  // We take a tube of 23 cm for the subglottal system, which has
  // a rather constant cross section along this part according to 
  // Weibels data (1963), and which gives us F1_sub = 550 Hz and
  // F2_sub = 1270 Hz.
  static constexpr double DEFAULT_SUBGLOTTAL_CAVITY_LENGTH_CM = 23.0;
  static constexpr double DEFAULT_NASAL_CAVITY_LENGTH_CM = 11.4;
  static constexpr double DEFAULT_PIRIFORM_FOSSA_LENGTH_CM = 3.0;
  static constexpr double DEFAULT_PIRIFORM_FOSSA_VOLUME_CM3 = 2.0;

  static constexpr char ARTICULATOR_LETTER[NUM_ARTICULATORS] = { 'V', 'T', 'I', 'L', 'N' };

  // ****************************************************************

  Section tracheaSections[NUM_TRACHEA_SECTIONS];
  Section glottisSections[NUM_GLOTTIS_SECTIONS];
  Section pharynxMouthSections[NUM_PHARYNX_MOUTH_SECTIONS];
  Section noseSections[NUM_NOSE_SECTIONS];
  Section sinusSections[NUM_SINUS_SECTIONS];
  Section fossaSections[NUM_FOSSA_SECTIONS];

  // List of pointers to ALL tube sections of the model
  Section *sections[NUM_SECTIONS];

  // **************************************************************************
  // Public functions.
  // **************************************************************************

public:
  Tube();

  void initStaticSections(
    const double tracheaLength_cm = DEFAULT_SUBGLOTTAL_CAVITY_LENGTH_CM,
    const double noseLength_cm = DEFAULT_NASAL_CAVITY_LENGTH_CM,
    const double piriformFossaLength_cm = DEFAULT_PIRIFORM_FOSSA_LENGTH_CM,
    const double piriformFossaVolume_cm3 = DEFAULT_PIRIFORM_FOSSA_VOLUME_CM3
  );
  void getStaticParams(double &tracheaLength_cm, double &noseLength_cm,
    double &piriformFossaLength_cm, double &piriformFossaVolume_cm3);

  void resetPharynxMouthSections();
  void setPharynxMouthSections(
    const double* sectionLengths_cm, 
    const double* sectionAreas_cm2,
    const Articulator* sectionArticulators
  );

  void resetGlottisSections(const double area_cm2);
  void setGlottisSections(const double *lengths_cm, const double *areas_cm2);

  void resetAuxParams();
  void setAuxParams(const double nasalPortArea_cm2, const double incisorPos_cm, const double tongueTipSideElevation);
  void getAuxParams(double &nasalPortArea_cm2, double &incisorPos_cm, double &tongueTipSideElevation);

  void interpolate(const Tube* leftTube, const Tube* rightTube, const double ratio);
  void calcPositions();
  void print();

  void operator=(const Tube &t);
  bool operator==(const Tube &t);
  bool operator!=(const Tube &t);

  // **************************************************************************
  // Private data.
  // **************************************************************************

private:
  // Position of the incisors.
  double incisorPos_cm{ 0.0 };

  // Elevation of the tongue tip side (corresponding to the TS3 
  // parameter of the vocal tract model).
  double tongueTipSideElevation{ 0.0 };

  // The static tube parameters.
  double tracheaLength_cm{ 0.0 };
  double noseLength_cm{ 0.0 };
  double piriformFossaLength_cm{ 0.0 };
  double piriformFossaVolume_cm3{ 0.0 };

  // **************************************************************************
  // Private functions.
  // **************************************************************************

private:
  void createSectionList();
};

#endif

