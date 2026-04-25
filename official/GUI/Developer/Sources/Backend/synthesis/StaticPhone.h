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

#ifndef __STATIC_PHONE_H__
#define __STATIC_PHONE_H__

#include "acoustics/TdsModel.h"
#include "acoustics/TubeSequence.h"
#include "acoustics/Tube.h"
#include "glottis/Glottis.h"
#include "glottis/GeometricGlottis2019.h"
#include "glottis/GeometricGlottis2025.h"
#include "core/TimeFunction.h"

// ****************************************************************************
/// This class provides the tube sequence for the time-domain synthesis of
/// a static phone.
// ****************************************************************************

class StaticPhone : public TubeSequence
{
  // **************************************************************************
  // Public variables.
  // **************************************************************************

public:
  bool useConstantF0{ true };

  // **************************************************************************
  // Public functions.
  // **************************************************************************

public:
  StaticPhone() {};
  ~StaticPhone() {};
  void setup(const Tube &tube, Glottis *glottis, const int duration_samples);

  // Overwritten functions of the interface class
  void getTube(int sampleIndex, Tube& tube);
  void getGlottisParams(int sampleIndex, double* params);
  int getDuration_pt();

  // **************************************************************************
  // Private data.
  // **************************************************************************

private:
  double constantF0{ 120.0 };
  int duration_samples{ AUDIO_SAMPLING_RATE_HZ };
  TimeFunction f0TimeFunction;
  TimeFunction pressureTimeFunction;
  Tube tube;
  int numGlottisParams{ 0 };
  double glottisParams[Glottis::MAX_CONTROL_PARAMS];
};

#endif
