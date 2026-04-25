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

#ifndef __GESTURAL_SCORE_H__
#define __GESTURAL_SCORE_H__

#include <ostream>
#include <string>
#include <vector>

#include "TubeSequence.h"
#include "VocalTract.h"
#include "Glottis.h"
#include "XmlNode.h"
#include "SegmentSequence.h"


using namespace std;

const int MIN_GESTURE_DURATION_MS = 1;
const int MAX_GESTURE_DURATION_MS = 3600000;

// ****************************************************************************
// ****************************************************************************

struct Gesture
{
  double duration_s;
  double dVal;
  double slope;
  string sVal;
  double tau_s;
  bool neutral;
};


// ****************************************************************************
// For the used dynamical system for target approximation, the  TIME CONSTANT 
// tau of a transition is 1/10 of the transition duration after a step input.
// After 10*tau, a movement reached over 97% of the movement range. 50% of the 
// displacement is reached at about half (47%) the transition duration.
// ****************************************************************************

struct Target
{
  double duration;
  double value;
  double slope;
  double tau_s;
};


// ****************************************************************************
/// A sequence of gestures of the same type.
// ****************************************************************************

class GestureSequence
{
  // **************************************************************************
  // Public data.
  // **************************************************************************

public:
  // Properties of the gestures in this sequence.
  string name;
  string abbr;
  string unit;
  double minValue;
  double maxValue;
  double minSlope;
  double maxSlope;
  double minTau_s;
  double maxTau_s;
  bool nominalValues;

  // **************************************************************************
  // Public functions.
  // **************************************************************************

public:
  GestureSequence();
  void init(const string &name, const string &abbr, const string &unit, 
    double minValue, double maxValue, double minSlope, double maxSlope, 
    double minTau_s, double maxTau_s, bool nominalValues);
  void clear();
  int numGestures();
  Gesture *getGesture(int index);
  bool isValidIndex(int index);

  double getGestureBegin_s(int index);
  double getGestureEnd_s(int index);
  int getIndexAt(double pos_s);
  double getDuration_s();
  
  void appendGesture(Gesture &g);
  void insertGesture(Gesture &g, int index);
  void deleteGesture(int index);
  int putGesture(Gesture &g, double startPos_s);

  bool writeToXml(ostream &os, int indent);
  bool readFromXml(XmlNode &node, bool &allValuesInRange);

  void initGestureParams(Gesture &g);
  void limitGestureParams(Gesture &g);

  // **************************************************************************
  // Private data.
  // **************************************************************************

private:
  vector<Gesture> gestures;
};


// ****************************************************************************
// ****************************************************************************

class GesturalScore : public TubeSequence
{
  // **************************************************************************
  // Public data.
  // **************************************************************************

public:
  // Reference frequency for the conversion between Hz and st.
  static const double REFERENCE_FREQUENCY;
  static const double DEFAULT_TIME_CONSTANT_S;
  static const double VELIC_OPENING_TIME_CONSTANT_S;
  static const double VELIC_CLOSING_TIME_CONSTANT_S;
  static const int CURVE_SAMPLING_RATE = 400;   // 400 Hz = 2.5 ms point spacing
  static const int MAX_CURVE_SAMPLES = CURVE_SAMPLING_RATE * 60;
  static const int MAX_PARAM_TARGETS = 512;

  enum GestureType
  {
    VOWEL_GESTURE,
    LIP_GESTURE,
    TONGUE_TIP_GESTURE,
    TONGUE_BODY_GESTURE,
    VELIC_GESTURE,
    GLOTTAL_SHAPE_GESTURE,
    F0_GESTURE,
    PRESSURE_GESTURE,
    NUM_GESTURE_TYPES
  };

  GestureSequence gestures[NUM_GESTURE_TYPES];
  vector<Target> tractParamTargets[VocalTract::NUM_PARAMS];
  vector<Target> glottisParamTargets[Glottis::MAX_CONTROL_PARAMS];
  vector<double> tractParamCurve[VocalTract::NUM_PARAMS];
  vector<double> glottisParamCurve[Glottis::MAX_CONTROL_PARAMS];

  // The objects associated with these pointers are not administrated
  // by this class.
  VocalTract *vocalTract;
  Glottis *glottis;

  // **************************************************************************
  // Public functions.
  // **************************************************************************

public:
  GesturalScore(VocalTract *vocalTract, Glottis *glottis);
  ~GesturalScore();
  
  void clear();
  void initTestScore();
  void createFromSegmentSequence(SegmentSequence *origSegmentSequence);
  void createPitchGestures(SegmentSequence* origSegmentSequence);

  void addClosingGesture(GestureType gestureType, string gestureName,
    double closureBegin_s, double closureEnd_s, 
    double onsetTau_s, double offsetTau_s, 
    bool connectToPrevGesture);
  
  bool hasVocalTactClosure(GestureType gestureType, string gestureName,
    double gestureBegin_s, double gestureEnd_s, 
    double onsetTau_s, double offsetTau_s,
    double testTime_s);

  void putClosingGesture(GestureType gestureType, string gestureName,
    double gestureBegin_s, double gestureEnd_s, double onsetTau_s, double offsetTau_s);

  bool hasVocalTactConstriction(GestureType gestureType, string gestureName,
    double gestureBegin_s, double gestureEnd_s,
    double onsetTau_s, double offsetTau_s,
    double testTime_s);

  void addVelicOpeningGesture(double openingBegin_s, double openingEnd_s, 
    double targetVelicOpening_cm2);
  bool hasVelicOpening(double gestureBegin_s, double gestureEnd_s, 
    double targetVelicOpening_cm2, double testTime_s);

  bool loadGesturesXml(const string &fileName, bool &allValuesInRange);
  bool saveGesturesXml(const string &fileName);

  // MUST be called after any change to the score.
  void calcCurves();
  void getParams(double pos_s, double *vocalTractParams, double *glottisParams);
  double getScoreDuration_s();

  // Manipulation functions
  void changeF0Offset(double deltaF0_st);
  void changeF0Range(double factor);
  void changeF0TargetSlope(double deltaSlope_st_s);
  void substituteGlottalShapes(const string &oldShapeName, const string &newShapeName);
  void changeSubglottalPressure(double factor);
  void getF0Statistic(double &f0Mean_st, double &f0Sd_st, double &f0Mean_Hz, double &f0Sd_Hz);

  void changeDuration(double factor);
  void changeTimeConstants(double factor);

  static void getPseudoInverseNx2(double M[][2], int numRows, double *invM);
  static void mapToVowelSubspace(VocalTract *vt, double *vocalTractParams, double &alphaTongue, 
    double &betaTongue, double &alphaLips, double &betaLips);

  static void limitVowelSubspaceCoord(double &alphaTongue, double &betaTongue, double &alphaLips, double &betaLips);

  static bool getContextDependentConsonant(VocalTract *vt, const char *name, double alphaTongue, 
    double betaTongue, double alphaLips, double betaLips, double *consonantParams);

  static bool getContextDependentConsonant(VocalTract *vt, 
    const char *consonantName, const char *contextVowelName, double *consonantParams);

  static double getConstrictionArea_cm2(VocalTract *vt, 
    double *vocalTractParams, GestureType gestureType);

  static double getF0_st(double freq_Hz);
  static double getF0_Hz(double freq_st);

  // **************************************************************************
  // Overwritten functions of the interface class.
  // **************************************************************************

  void getTube(int sampleIndex, Tube& tube);
  void getGlottisParams(int sampleIndex, double* params);
  int getDuration_pt();

  // **************************************************************************
  // Private data.
  // **************************************************************************

private:
  Tube *leftTube;
  Tube *rightTube;
  int leftTubeIndex;
  vector<double> inputSignal;
  vector<double> tauSignal;

  // **************************************************************************
  // Private functions.
  // **************************************************************************

private:
  void calcTractParamTargets();
  void calcGlottisParamTargets();
  void calcParamCurve(vector<Target> &paramTargets, vector<double> &paramCurve);
  
  double getTimeToStepResponseValue_s(double normValue, double tau_s);
  void setLowerLimit(double& x, const double minValue);
  void setUpperLimit(double& x, const double maxValue);
  void setLimits(double& x, const double minValue, const double maxValue);
};


#endif