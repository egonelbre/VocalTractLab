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

#include "glottis/GeometricGlottis2025.h"
#include "core/Constants.h"
#include <cmath>
#include <iostream>

// ****************************************************************************
// Constructor.
// ****************************************************************************

GeometricGlottis2025::GeometricGlottis2025()
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
    { "Pulse shape", "pulse_shape", "", 1.0, "", -1.0, 1.0, 0.0, 0.0 },
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
    { "Rest thickness", "rest_thickness", "cm", 10, "mm", 0.2, 1.0, 0.45, 0.0 },
    { "Rest length", "rest_length", "cm", 10, "mm", 0.5, 2.0, 1.6, 0.0 },
    { "Rest f0", "rest_f0", "Hz", 1.0, "Hz", 50.0, 400.0, 120.0, 120.0 },
    { "Chink length", "chink_length", "cm", 10, "mm", 0.1, 0.5, 0.4, 0.0 },
    { "Jitter", "jitter", "%", 1, "%", 0.0, 100.0, 10.0, 10.0 }
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

string GeometricGlottis2025::getName()
{
  return string("Geometric glottis 2025");
}

// ****************************************************************************
// ****************************************************************************

void GeometricGlottis2025::resetMotion()
{
  supraglottalPressureFilter.createChebyshev(25.0/(double)AUDIO_SAMPLING_RATE_HZ, false, 4);
  supraglottalPressureFilter.resetBuffers();

  time_s = 0.0;
  phase = 0.0;
  supraglottalPressure_dPa = 0.0;
}


// ****************************************************************************
// This is the variant with EXPONENTIAL FUNCTIONS for collision handling.
// ****************************************************************************

void GeometricGlottis2025::calcGeometry()
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
  // Determin the phase values of the COS-function for the lower and 
  // upper edges modulo 2*pi.
  // ****************************************************************

  double phaseLag_rad = controlParams[PHASE_LAG].x;

  // Calc. the phases of the lower and upper vocal fold edges
  // modulo 2*pi, i.e., modulo 1 period.
  // Result: 0 <= phase <= 2*pi

  double edgePhase_rad[2];

  edgePhase_rad[0] = phase;
  // Important: Add 2*pi to definitely stay positive.
  edgePhase_rad[1] = phase + 2.0 * M_PI - phaseLag_rad;

  for (k = 0; k < 2; k++)
  {
    int N = (int)(edgePhase_rad[k] / (2.0 * M_PI));
    edgePhase_rad[k] -= (double)N * 2.0 * M_PI;
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
  // Calc. the displacement of the upper and lower edge. Consider 
  // here the contact between the vocal folds.
  // ****************************************************************

  double pulseShape = controlParams[PULSE_SHAPE].x;   // -1.0 ... +1.0
  double displacement[2];

  for (k = 0; k < 2; k++)
  {
    // Normalized time with 0 <= t <= 1 within the period.
    double t = edgePhase_rad[k] / (2.0 * M_PI);
    double A1_cm = amplitude;
    double abduction_cm = 0.0;
    if (k == 0)
    {
      abduction_cm = controlParams[LOWER_END_X].x;
    }
    else
    {
      abduction_cm = controlParams[UPPER_END_X].x;
    }

    // Important: Negative (=virtual) abduction values are not allowed
    // for the calculation of the displacement.
    if (abduction_cm < 0.0)
    {
      abduction_cm = 0.0;
    }

    double A2_cm = -abduction_cm / M_PI;    // Amp. where the VF start to touch.
    double A3_cm = A2_cm - 0.2 * amplitude; // 0.2 is an empirical constant.

    const double EPSILON = 0.000001;

    double t2 = 0.5 - EPSILON;
    double t3 = 0.5 + EPSILON;
    if (A2_cm > -A1_cm)
    {
      t2 = acos(A2_cm / A1_cm) / (2.0 * M_PI);      // Normalize to range [0, 1].
      t3 = 1.0 - t2;
    }

    double t1 = t2;
    double t4 = t3;
    if (pulseShape < 0.0)
    {
      t1 = t2 + 0.10 * pulseShape;
    }
    else
    {
      t4 = t3 + 0.10 * pulseShape;
    }

    double val_t1 = A1_cm * cos(t1 * 2.0 * M_PI);
    double grad_t1 = -A1_cm * 2 * M_PI * sin(2 * M_PI * t1);
    double tau_t1 = -(val_t1 - A3_cm) / grad_t1;

    double val_t4 = A1_cm * cos(t4 * 2.0 * M_PI);
    double grad_t4 = -A1_cm * 2 * M_PI * sin(2 * M_PI * t4);
    double tau_t4 = (val_t4 - A3_cm) / grad_t4;

    double value = A1_cm * cos(t * 2.0 * M_PI);
    double y = 0.0;

    if ((t >= t1) && (t < t4))
    {
      y = A3_cm + (val_t1 - A3_cm) * exp(-(t - t1) / tau_t1);
      if (y > value) { value = y; }

      y = A3_cm + (val_t4 - A3_cm) * exp(-(t4 - t) / tau_t4);
      if (y > value) { value = y; }
    }

    displacement[k] = value;
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
  const double DELTA_LENGTH = length / (double)(NUM_SAMPLES - 1);
  double endX[2];        // Displacement at the vocal processes
  double prevX, x;
  double area[2];
  double t;

  endX[0] = controlParams[LOWER_END_X].x;
  endX[1] = controlParams[UPPER_END_X].x;

  for (i = 0; i < 2; i++)
  {
    x = 0.0;
    prevX = 0.0;
    area[i] = chinkArea;

    for (k = 1; k <= NUM_SAMPLES; k++)
    {
      t = (double)k / (double)NUM_SAMPLES;    // 0 <= t <= 1
      x = endX[i] * t + displacement[i] * sin(t * M_PI);
      if (x < 0.0) { x = 0.0; }
      area[i] += DELTA_LENGTH * (prevX + x);
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

  derivedParams[LENGTH].x = length;
  derivedParams[THICKNESS].x = thickness;
  derivedParams[AMPLITUDE].x = amplitude;
  derivedParams[LOWER_CORD_X].x = displacement[0];
  derivedParams[UPPER_CORD_X].x = displacement[1];
  derivedParams[LOWER_AREA].x = area[0];
  derivedParams[UPPER_AREA].x = area[1];
  derivedParams[CHINK_WIDTH].x = chinkArea / chinkLength;
}


// ****************************************************************************
// This is the variant with PARABOLA SEGMENTS for collision handling.
// ****************************************************************************

void GeometricGlottis2025::calcGeometry_alternative()
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
  // Determin the phase values of the COS-function for the lower and 
  // upper edges modulo 2*pi.
  // ****************************************************************

  double phaseLag_rad = controlParams[PHASE_LAG].x;

  // Calc. the phases of the lower and upper vocal fold edges
  // modulo 2*pi, i.e., modulo 1 period.
  // Result: 0 <= phase <= 2*pi

  double edgePhase_rad[2];

  edgePhase_rad[0] = phase;
  // Important: Add 2*pi to definitely stay positive.
  edgePhase_rad[1] = phase + 2.0 * M_PI - phaseLag_rad;

  for (k = 0; k < 2; k++)
  {
    int N = (int)(edgePhase_rad[k] / (2.0 * M_PI));
    edgePhase_rad[k] -= (double)N * 2.0 * M_PI;
  }

  // ****************************************************************
  // Calculate the amplitude of vibration.
  // The amplitude calculation is based on the paper by Titze (1989):
  // "On the relation between subglottal pressure and fundamental 
  // frequency in phonation", but simplified, and extended by the  
  // factor RELATIVE_AMPLITUDE (0 .. 1).
  // ****************************************************************

  double transPressure_dPa = controlParams[PRESSURE].x - supraglottalPressure_dPa;
  if (transPressure_dPa < 0.0)          // Must always be >= 0 !!
  {
    transPressure_dPa = 0.0;
  }

  // When the glottis length equals the rest length, the amplitude varies
  // with the sqrt of the subglottal pressure and should be 0.1 cm at 8000 dPa.

  double amplitude = 0.0011 * sqrt(transPressure_dPa);

  // Model that the vibration amplitude quickly drops to zero below
  // 2000 dPa.

  if (transPressure_dPa < 1000.0)
  {
    amplitude = 0.0;
  }
  else if (transPressure_dPa < 2000.0)
  {
    // Drop to zero between 2000 and 1000 dPa.
    amplitude *= (transPressure_dPa - 1000.0) / 1000.0;
  }

  // Also model the effect that the amplitude decreases when the vocal
  // folds are elongated and vice versa. The approximation here
  // probably underestimates the true effect a bit
  // (see Titze, 1989).

  amplitude *= (staticParams[REST_LENGTH].x / length);

  // The "relative amplitude" simulates a decrease in amplitude due to 
  // stiffening or an increse in friction. The relative amplitude can 
  // have a virtual target and is skipped at zero here.

  double relAmp = controlParams[RELATIVE_AMPLITUDE].x;

  if (relAmp < 0.0)
  {
    relAmp = 0.0;
  }
  amplitude *= relAmp;

  // Omit diplophonic double pulsing here ...

  // ****************************************************************
  // Calc. the displacement of the upper and lower edge. Consider 
  // here the contact between the vocal folds.
  // ****************************************************************

  const double EPSILON = 0.000001;
  const double DELTA_A_CM = 0.01;    // Tissue compression, empirically determined.
  double pulseShape = controlParams[PULSE_SHAPE].x;   // 0.0 ... 1.0
  double displacement[2];

  for (k = 0; k < 2; k++)
  {
    // Normalized time with 0 <= t <= 1 within the period.
    double t = edgePhase_rad[k] / (2.0 * M_PI);
    double A1_cm = amplitude;
    double abduction_cm = 0.0;
    if (k == 0)
    {
      abduction_cm = controlParams[LOWER_END_X].x;
    }
    else
    {
      abduction_cm = controlParams[UPPER_END_X].x;
    }

    double A2_cm = -abduction_cm / M_PI;    // Amp. where the VF start to touch.
    // A2 must remain slightly higher than the lowest point of f1(t).
    if (A2_cm < -A1_cm + EPSILON)
    {
      A2_cm = -A1_cm + EPSILON;
    }
    double A3_cm = A2_cm - DELTA_A_CM;
    double t1 = acos(A2_cm / A1_cm) / (2.0 * M_PI);    // Normalize to 0 < t0 < 1.

    // Determine the values t2 and a for the falling edge.
    
    double grad_t1 = -A1_cm * 2 * M_PI * sin(2 * M_PI * t1);
    double a = grad_t1 * grad_t1 / (4.0 * DELTA_A_CM);
    double t2 = t1 - 2.0 * DELTA_A_CM / grad_t1;

    // Determine the values t3, b, and t4 for the rising edge.

    double boundedT2 = t2;
    if (boundedT2 > 0.5)
    {
      boundedT2 = 0.5;
    }
    double t3 = pulseShape * boundedT2 + (1.0 - pulseShape) * (1.0 - boundedT2);
    double minT4 = 1.0 - boundedT2;
    double maxT4 = 1.0;
    double t4Step = (maxT4 - minT4) / 100.0;
    double t4 = minT4;
    double bestT4 = minT4;
    double bestError = 1000000000;
    double b = 0.0;
    double E = 0.0;

    for (i = 0; i < 100; i++)
    {
      b = (A1_cm * cos(2.0 * M_PI * t4) - A3_cm) / ((t4 - t3)* (t4 - t3));
      E = fabs(2.0 * b * (t4 - t3) + 2.0 * M_PI * A1_cm * sin(2.0 * M_PI * t4));
      if (E < bestError)
      {
        bestError = E;
        bestT4 = t4;
      }
      t4 = t4 + t4Step;
    }

    t4 = bestT4;
    b = (A1_cm * cos(2.0 * M_PI * t4) - A3_cm) / ((t4 - t3) * (t4 - t3));

    // Calculate the actual displacement.

    double value = A1_cm * cos(t * 2.0 * M_PI);
    double f2 = a * (t - t2) * (t - t2) + A3_cm;
    double f3 = b * (t - t3) * (t - t3) + A3_cm;

    if ((t >= t1) && (t <= t2) && (f2 > value))
    {
      value = f2;
    }
    if ((t >= t3) && (t <= t4) && (f3 > value))
    {
      value = f3;
    }
    if ((t >= t2) && (t <= t3))
    {
      value = A3_cm;
    }

    displacement[k] = value;
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
  const double DELTA_LENGTH = length / (double)(NUM_SAMPLES - 1);
  double endX[2];        // Displacement at the vocal processes
  double prevX, x;
  double area[2];
  double t;

  endX[0] = controlParams[LOWER_END_X].x;
  endX[1] = controlParams[UPPER_END_X].x;

  for (i = 0; i < 2; i++)
  {
    x = 0.0;
    prevX = 0.0;
    area[i] = chinkArea;

    for (k = 1; k <= NUM_SAMPLES; k++)
    {
      t = (double)k / (double)NUM_SAMPLES;    // 0 <= t <= 1
      x = endX[i] * t + displacement[i] * sin(t * M_PI);
      if (x < 0.0) { x = 0.0; }
      area[i] += DELTA_LENGTH * (prevX + x);
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

  derivedParams[LENGTH].x = length;
  derivedParams[THICKNESS].x = thickness;
  derivedParams[AMPLITUDE].x = amplitude;
  derivedParams[LOWER_CORD_X].x = displacement[0];
  derivedParams[UPPER_CORD_X].x = displacement[1];
  derivedParams[LOWER_AREA].x = area[0];
  derivedParams[UPPER_AREA].x = area[1];
  derivedParams[CHINK_WIDTH].x = chinkArea / chinkLength;
}


// ****************************************************************************
/// Performs a time step of the simulation.
/// Requires four pressure values: subglottal, lower glottis, upper glottis, 
/// supraglottal.
// ****************************************************************************

void GeometricGlottis2025::incTime(const double timeIncrement_s, const double pressure_dPa[])
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

  // Add "flutter" according to Klatt and Klatt (1990)
  
  double flutter_percent = controlParams[FLUTTER].x;

  F0 += (flutter_percent / 50.0) * (F0 / 100.0) * 
    (sin(2.0*M_PI*12.7*time_s) + sin(2.0*M_PI*7.1*time_s) + sin(2.0*M_PI*4.7*time_s));

  phase += F0*timeIncrement_s*2.0*M_PI;

  // Add "jitter" according to Model I from Schoentgen (2001):
  // "Stochastic models of jitter", JASA.
  // The maximum value for b is from the paper by Fray et al. (2012):
  // "Development and perceptual assessment...", JASA

  const double MAX_B = 0.28;        // 0.28 is very rough.
  double jitter_percent = staticParams[JITTER].x;
  double b = MAX_B * jitter_percent / 100.0;
  double xi = -1.0;
  if (rand() >= RAND_MAX / 2)
  {
    xi = +1.0;
  }
  xi *= sqrt(timeIncrement_s);
  phase += 2 * M_PI * b * xi;

  // ****************************************************************

  time_s+= timeIncrement_s;
}


// ****************************************************************************
// Returns the lengths and the areas of the two glottal tube sections.
// ****************************************************************************

void GeometricGlottis2025::getTubeData(double *length_cm, double *area_cm2)
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

int GeometricGlottis2025::getApertureParamIndex()
{
  return UPPER_END_X;
}


// ****************************************************************************
