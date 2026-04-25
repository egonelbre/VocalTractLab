#include "core/Constants.h"

// ****************************************************************************
// Define the wall parameters.
// The wall stiffness mainly controls how long a voicebar lasts in the closure 
// phase of a plosive. It has little effect on the F1 damping/shift.
// ****************************************************************************

double WALL_MASS_PER_UNIT_AREA_CGS = 2.0;
double WALL_DAMPING_PER_UNIT_AREA_CGS = 1400.0;
double WALL_STIFFNESS_PER_UNIT_AREA_CGS = 50000.0;

