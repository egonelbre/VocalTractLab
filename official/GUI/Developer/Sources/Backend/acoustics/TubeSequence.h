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

#ifndef __TUBE_SEQUENCE_H__
#define __TUBE_SEQUENCE_H__

#include "acoustics/Tube.h"

// ****************************************************************************
// This virtual base class provides an INTERFACE to classes that provide both
// an area function (tube) and the glottis parameters at any sampling point
// in time.
// This interface is implemented, for example, by classes that generate tube
// sequences for gestural scores or for static speech sounds.
// ****************************************************************************

class TubeSequence
{
public:
  virtual ~TubeSequence() {}
  virtual void getTube(int sampleIndex, Tube &tube) = 0;
  virtual void getGlottisParams(int sampleIndex, double* params) = 0;
  virtual int getDuration_pt() = 0;
};

#endif
