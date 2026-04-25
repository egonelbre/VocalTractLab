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

#include "synthesis/StaticPhone.h"


// ****************************************************************************
// Sets up the tube sequence with the given tube and control parameters of the
// given glottis. The duration of the static phone is given by duration_samples.
// ****************************************************************************

void StaticPhone::setup(const Tube &tube, Glottis *glottis, const int duration_samples)
{
  this->tube = tube;
  this->duration_samples = duration_samples;

  if (this->duration_samples < 0.4*AUDIO_SAMPLING_RATE_HZ)
  {
    this->duration_samples = (int)(0.4*AUDIO_SAMPLING_RATE_HZ);
  }

  // Copy the control parameters of the glottis.
  numGlottisParams = (int)glottis->controlParams.size();
  for (int i = 0; i < numGlottisParams; i++)
  {
    glottisParams[i] = glottis->controlParams[i].x;
  }

  // In the case the user wants a constant F0.
  constantF0 = glottis->controlParams[Glottis::FREQUENCY].x;

  double duration_s = this->duration_samples / (double)AUDIO_SAMPLING_RATE_HZ;

  // The time function for pulmonary pressure.

  const int NUM_PRESSURE_NODES = 4;
  TimeFunction::Node p[NUM_PRESSURE_NODES] =
  {
    {0,              0.0},
    {0.04,           glottis->controlParams[Glottis::PRESSURE].x},
    {duration_s-0.2, glottis->controlParams[Glottis::PRESSURE].x},
    {duration_s,     0.0}
  };
  pressureTimeFunction.setNodes(p, NUM_PRESSURE_NODES);

  // The time function for f0; should be very close to the given
  // target f0 in the middle of the phone!

  const int NUM_F0_NODES = 4;
  TimeFunction::Node f0[NUM_F0_NODES] =
  {
    {0.0,             0.9*glottis->controlParams[Glottis::FREQUENCY].x},
    {0.5*duration_s,  1.00*glottis->controlParams[Glottis::FREQUENCY].x},
    {0.75*duration_s, 0.8*glottis->controlParams[Glottis::FREQUENCY].x},
    {1.0*duration_s,  0.7*glottis->controlParams[Glottis::FREQUENCY].x}
  };
  f0TimeFunction.setNodes(f0, NUM_F0_NODES);
}


// ****************************************************************************
// ****************************************************************************

void StaticPhone::getTube(int sampleIndex, Tube& tube)
{
  tube = this->tube;
}


// ****************************************************************************
// ****************************************************************************

void StaticPhone::getGlottisParams(int sampleIndex, double* params)
{
  for (int i = 0; i < numGlottisParams; i++)
  {
    params[i] = glottisParams[i];
  }
  
  // Overwrite the time-variant values for pressure and f0.
  
  double t_s = (double)sampleIndex / (double)AUDIO_SAMPLING_RATE_HZ;
  params[Glottis::PRESSURE] = pressureTimeFunction.getValue(t_s);
  params[Glottis::FREQUENCY] = f0TimeFunction.getValue(t_s);

  if (useConstantF0)
  {
    params[Glottis::FREQUENCY] = constantF0;
  }
}


// ****************************************************************************
// ****************************************************************************

int StaticPhone::getDuration_pt()
{
  return duration_samples;
}

// ****************************************************************************
