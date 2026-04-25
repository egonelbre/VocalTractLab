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

#include "GeometricGlottis2019.h"
#include "Constants.h"
#include <cmath>
#include <iostream>

// ****************************************************************************
// Constructor.
// ****************************************************************************

GeometricGlottis2019::GeometricGlottis2019()
{
  int i;

  // ****************************************************************
  // Control parameters.
  // ****************************************************************

  Parameter cp[NUM_CONTROL_PARAMS] =
  {
    { "f0", "f0", "Hz", 1, "Hz", 40.0, 600.0, 120.0, 0.0 },
    { "Subglottal pressure", "pressure", "dPa", 1.0, "dPa", 0.0, 16000.0, 8000.0, 0.0 },
    { "Lower displacement", "x_bottom", "cm", 10.0, "mm", -0.05, 0.3, 0.02, 0.0 },
    { "Upper displacement", "x_top", "cm", 10.0, "mm", -0.05, 0.3, 0.02, 0.0 },
    { "Chink area", "chink_area", "cm^2", 100.0, "mm^2", -0.25, 0.25, 0.03, 0.0 },
    { "Phase lag", "lag", "rad", 57.296, "deg", 0.0, 3.1415, 1.22, 0.0 },
    { "Relative amplitude", "rel_amp", "", 1.0, "", -1.0, 1.0, 1.0, 1.0 },
    { "Double pulsing", "double_pulsing", "", 1.0, "", 0.0, 1.0, 0.05, 0.0 },
    { "Pulse skewness", "pulse_skewness", "", 1.0, "", -0.5, 0.5, 0.0, 0.0 },
    { "Flutter", "flutter", "%", 1.0, "%", 0.0, 100.0, 25.0, 25.0 }
  };

  controlParams.clear();
  for (i=0; i < NUM_CONTROL_PARAMS; i++)
  {
    controlParams.push_back( cp[i] );
    controlParams[i].x = controlParams[i].neutral;
  }

  // ****************************************************************
  // Static parameters.
  // ****************************************************************

  Parameter sp[NUM_STATIC_PARAMS] =
  {
    { "Rest thickness", "rest_thickness", "cm", 10, "mm", 0.3, 1.0, 0.45, 0.0 },
    { "Rest length", "rest_length", "cm", 10, "mm", 0.5, 2.0, 1.6, 0.0 },
    { "Rest f0", "rest_f0", "Hz", 1.0, "Hz", 50.0, 400.0, 120.0, 120.0 },
    { "Chink length", "chink_length", "cm", 10, "mm", 0.1, 0.5, 0.4, 0.0 }
  };

  staticParams.clear();
  for (i=0; i < NUM_STATIC_PARAMS; i++)
  {
    staticParams.push_back( sp[i] );
    staticParams[i].x = staticParams[i].neutral;
  }

  // ****************************************************************
  // Derived parameters.
  // ****************************************************************

  Parameter dp[NUM_DERIVED_PARAMS] =
  {
    { "Length", "length", "cm", 10, "mm", 0.0, 3.0, 0.0, 0.0 },
    { "Thickness", "thickness", "cm", 10, "mm", 0.0, 2.0, 0.0, 0.0 },
    { "Vibration amplitude", "amplitude", "cm", 10, "mm", 0.0, 1.0, 0.0, 0.0 },
    { "Lower cord x", "lower_cord_x", "cm", 10, "mm", 0.0, 1.0, 0.0, 0.0 },
    { "Upper cord x", "upper_cord_x", "cm", 10, "mm", 0.0, 1.0, 0.0, 0.0 },
    { "Lower glottal area", "lower_area", "cm^2", 100, "mm^2", 0.0, 4.0, 0.0, 0.0 },
    { "Upper glottal area", "upper_area", "cm^2", 100, "mm^2", 0.0, 4.0, 0.0, 0.0 },
    { "Chink width", "chink_width", "cm", 10, "mm", 0.0, 1.0, 0.0, 0.0 }
  };

  derivedParams.clear();
  for (i=0; i < NUM_DERIVED_PARAMS; i++)
  {
    derivedParams.push_back( dp[i] );
    derivedParams[i].x = derivedParams[i].neutral;
  }

  // ****************************************************************
  // Create a shape for the default parameter values.
  // ****************************************************************

  Shape s;
  s.name = "default";
  s.controlParam.resize(NUM_CONTROL_PARAMS);
  for (i=0; i < NUM_CONTROL_PARAMS; i++)
  {
    s.controlParam[i] = controlParams[i].neutral;
  }

  shapes.push_back(s);


  // ****************************************************************
  // Stop the motion and calculate the geometry.
  // ****************************************************************
  
  resetMotion();
  calcGeometry();
}


// ****************************************************************************
/// Returns a descriptive name for this glottis type.
// ****************************************************************************

string GeometricGlottis2019::getName()
{
  return string("Geometric glottis 2019");
}

// ****************************************************************************
// ****************************************************************************

void GeometricGlottis2019::resetMotion()
{
  supraglottalPressureFilter.createChebyshev(25.0/(double)AUDIO_SAMPLING_RATE_HZ, false, 4);
  supraglottalPressureFilter.resetBuffers();

  time_s = 0.0;
  phase = 0.0;
  supraglottalPressure_dPa = 0.0;
}


// ****************************************************************************
// ****************************************************************************

void GeometricGlottis2019::calcGeometry()
{
  int i, k;

  // ****************************************************************
  // Limit the range of the parameter values
  // ****************************************************************

  restrictParams(controlParams);
  restrictParams(staticParams);

  // ****************************************************************
  // Calculate the current length, thickness and swinging amplitude.
  // The length calculation is based on a sqrt-relation with f0
  // based on the Four-Parameter-Model-paper by Titze (1989):
  // "A four-parameter model of the glottis ..."
  // ****************************************************************

  double length = staticParams[REST_LENGTH].x * sqrt(controlParams[FREQUENCY].x / staticParams[REST_F0].x);
  double thickness = (staticParams[REST_THICKNESS].x * staticParams[REST_LENGTH].x) / length;

  // ****************************************************************
  // Skew the pulses by warping the phase, i.e., the argument of the
  // sin()-function.
  // ****************************************************************

  double skewness = controlParams[PULSE_SKEWNESS].x;
  double phaseLag_rad = controlParams[PHASE_LAG].x;

  // Calc. the phases of the lower and upper vocal fold edges
  // modulo 4*pi, i.e., modulo 2 periods.
  // Result: 0 <= phase <= 4*pi

  double origPhase[2];

  origPhase[0] = phase;
  // Important: Add 4*pi (= 2 periods) to definitely stay positive.
  origPhase[1] = phase + 4.0*M_PI - phaseLag_rad;

  for (k = 0; k < 2; k++)
  {
    int N = (int)(origPhase[k] / (4.0 * M_PI));
    origPhase[k] -= (double)N * 4.0 * M_PI;
  }

  // The values for alpha (= phase) and beta (= warped phase) are
  // supporting points for the piecewise linear warping function.

  const int NUM_SUPPORT_POINTS = 6;
  double alpha[NUM_SUPPORT_POINTS];
  double beta[NUM_SUPPORT_POINTS];
  double denominator;
  const double EPSILON = 0.000000001;

  alpha[0] = 0.0;
  alpha[1] = (1.0 + skewness) * M_PI / 2.0;
  alpha[2] = M_PI + (1.0 - skewness) * M_PI / 2.0;
  alpha[3] = 2.0 * M_PI + (1.0 + skewness) * M_PI / 2.0;
  alpha[4] = 3.0 * M_PI + (1.0 - skewness) * M_PI / 2.0;
  alpha[5] = 4.0 * M_PI;

  beta[0] = 0.0;
  beta[1] = 0.5 * M_PI;
  beta[2] = 1.5 * M_PI;
  beta[3] = 2.5 * M_PI;
  beta[4] = 3.5 * M_PI;
  beta[5] = 4.0 * M_PI;

  double warpedPhase[2];

  warpedPhase[0] = origPhase[0];
  warpedPhase[1] = origPhase[1];

  for (k = 0; k < 2; k++)     // Lower and upper vocal fold edge
  {
    for (i = 0; i < NUM_SUPPORT_POINTS - 1; i++)
    {
      if ((origPhase[k] >= alpha[i]) && (origPhase[k] <= alpha[i + 1]))
      {
        denominator = alpha[i + 1] - alpha[i];
        if (fabs(denominator) < EPSILON)
        {
          denominator = EPSILON;
        }
        warpedPhase[k] = beta[i] + (beta[i+1] - beta[i]) * (origPhase[k] - alpha[i]) / denominator;
        break;
      }
    }
  }

  // ****************************************************************
  // Calculate the amplitude of vibration.
  // The dependence on the sqrt(P_sub) is based on the paper by Titze 
  // (1989): "On the relation between subglottal pressure and fundamental 
  // frequency in phonation".
  // The proportionality to vocal fold length is based on perceptual
  // experiments with male and female voices, and on Zhang (2021): 
  // "Contribution of laryngeal size to differences between ..."
  // The scaling constant achieves an amplitude of 0.63 mm for 800 Pa 
  // and a 1.6 cm length.
  // ****************************************************************

  double transPressure_dPa = controlParams[PRESSURE].x - supraglottalPressure_dPa;
  if (transPressure_dPa < 0.0)          // Must always be >= 0 !!
  {
    transPressure_dPa = 0.0;
  }

  double amplitude = 0.0007 * (staticParams[REST_LENGTH].x / 1.6) * sqrt(transPressure_dPa);

  // Also model the effect that the amplitude decreases when the vocal
  // folds are elongated and vice versa. The approximation here
  // probably underestimates the true effect a bit (see Titze, 1989).

//  amplitude *= (staticParams[REST_LENGTH].x / length);

  // Model that the vibration amplitude quickly drops to zero below 2000 dPa.

  if (transPressure_dPa < 1000.0)
  {
    amplitude = 0.0;
  }
  else if (transPressure_dPa < 2000.0)
  {
    // Drop to zero between 2000 and 1000 dPa.
    amplitude *= (transPressure_dPa - 1000.0) / 1000.0;
  }

  // The "relative amplitude" simulates a decrease in amplitude due to 
  // stiffening or an increse in friction. The relative amplitude can 
  // have a virtual target and is skipped at zero here.

  double relAmp = controlParams[RELATIVE_AMPLITUDE].x;

  if (relAmp < 0.0)
  {
    relAmp = 0.0;
  }
  amplitude *= relAmp;


  // ****************************************************************
  // In the case of diplophonic double pulsing, reduce the amplitude
  // of every 2nd pulse, according to Klatt and Klatt (1990).
  // ****************************************************************

  double doublePulsing = controlParams[DOUBLE_PULSING].x;

  double displacement[2];

  for (i = 0; i < 2; i++)
  {
    if ((warpedPhase[i] <= 1.5 * M_PI) || (warpedPhase[i] >= 3.5 * M_PI))
    {
      displacement[i] = amplitude * sin(warpedPhase[i]);
    }
    else
    // Potentially reduced elongation of every 2nd pulse.
    {
      displacement[i] = -amplitude + amplitude * (sin(warpedPhase[i]) + 1.0) * (1.0 - 0.5*doublePulsing);
    }
  }

  // ****************************************************************
  // Glottal area at the upper and lower edge.
  // ****************************************************************

  // Area of the chink between the arytenoids
  double chinkArea = controlParams[CHINK_AREA].x;
  // The negative values for chink areas are only for virtual targets.
  // So constrain the values to >= 0 here.
  if (chinkArea < 0.0)
  {
    chinkArea = 0.0;
  }

  const int NUM_SAMPLES = 32;
  const double DELTA_LENGTH = length / (double)(NUM_SAMPLES-1);
  double endX[2];        // Displacement at the vocal processes
  double x, prevX;
  double area[2];
  double t;

  endX[0] = controlParams[LOWER_END_X].x;
  endX[1] = controlParams[UPPER_END_X].x;

  for (i=0; i < 2; i++)
  {
    x = 0.0;
    prevX = 0.0;
    area[i] = chinkArea;

    for (k=1; k <= NUM_SAMPLES; k++)
    {
      t = (double)k / (double)NUM_SAMPLES;    // 0 <= t <= 1
      x = endX[i] * t + displacement[i] * sin(t*M_PI);
      if (x < 0.0) { x = 0.0; }
      area[i]+= DELTA_LENGTH*(prevX + x);
      prevX = x;
    }
  }

  // ****************************************************************
  // Set the derived values.
  // ****************************************************************

  double chinkLength = staticParams[CHINK_LENGTH].x;
  if (chinkLength < 0.000001)
  {
    chinkLength = 0.000001;
  }

  derivedParams[LENGTH].x       = length;
  derivedParams[THICKNESS].x    = thickness;
  derivedParams[AMPLITUDE].x    = amplitude;
  derivedParams[LOWER_CORD_X].x = displacement[0];
  derivedParams[UPPER_CORD_X].x = displacement[1];
  derivedParams[LOWER_AREA].x   = area[0];
  derivedParams[UPPER_AREA].x   = area[1];
  derivedParams[CHINK_WIDTH].x  = chinkArea / chinkLength;
}


// ****************************************************************************
/// Performs a time step of the simulation.
/// Requires four pressure values: subglottal, lower glottis, upper glottis, 
/// supraglottal.
// ****************************************************************************

void GeometricGlottis2019::incTime(const double timeIncrement_s, const double pressure_dPa[])
{
  double subglottalPressure_dPa   = pressure_dPa[0];
  double lowerGlottisPressure_dPa = pressure_dPa[1];
  double upperGlottisPressure_dPa = pressure_dPa[2];

  // Low-pass filter the pressure right above the glottis for the 
  // calculation of the vocal fold's vibration amplitude.
  double supraglottalPressure_dPa = 
    supraglottalPressureFilter.getOutputSample(pressure_dPa[3]);

  double F0 = controlParams[FREQUENCY].x;

  // Restrict and set the supraglottal pressure

  if (supraglottalPressure_dPa > 40000.0) 
  { 
    supraglottalPressure_dPa = 40000.0; 
  }
  
  if (supraglottalPressure_dPa < -40000.0) 
  { 
    supraglottalPressure_dPa = -40000.0; 
  }
  this->supraglottalPressure_dPa = supraglottalPressure_dPa;

  // Add jitter ("flutter") according to Klatt and Klatt (1990)
  
  double flutter_percent = controlParams[FLUTTER].x;

  F0 += (flutter_percent / 50.0) * (F0 / 100.0) * 
    (sin(2.0*M_PI*12.7*time_s) + sin(2.0*M_PI*7.1*time_s) + sin(2.0*M_PI*4.7*time_s));

  phase += F0*timeIncrement_s*2.0*M_PI;
  time_s+= timeIncrement_s;
}


// ****************************************************************************
// Returns the lengths and the areas of the two glottal tube sections.
// ****************************************************************************

void GeometricGlottis2019::getTubeData(double *length_cm, double *area_cm2)
{
  length_cm[0] = derivedParams[THICKNESS].x*0.5;
  length_cm[1] = derivedParams[THICKNESS].x*0.5;
  area_cm2[0]  = derivedParams[LOWER_AREA].x;
  area_cm2[1]  = derivedParams[UPPER_AREA].x;
}


// ****************************************************************************
/// Returns the index of the parameter that best represents the glottal
/// aperture.
// ****************************************************************************

int GeometricGlottis2019::getApertureParamIndex()
{
  return UPPER_END_X;
}


// ****************************************************************************
