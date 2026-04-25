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

#include <fstream>
#include "SynthesisThread.h"
#include "Backend/synthesis/Synthesizer.h"
#include <wx/stopwatch.h>


// ****************************************************************************
// ****************************************************************************

SynthesisThread::SynthesisThread(wxWindow *window, TubeSequence *tubeSequence) : wxThread()
{
  this->window = window;
  this->tubeSequence = tubeSequence;
}


// ****************************************************************************
// ****************************************************************************

void* SynthesisThread::Entry()
{
  Data* data = Data::getInstance();

  // ****************************************************************
  // Restrict the synthesis speed.
  // ****************************************************************

  int speed_percent = data->synthesisSpeed_percent;
  if (speed_percent < 1)
  {
    speed_percent = 1;
  }
  if (speed_percent > 100)
  {
    speed_percent = 100;
  }

  // ****************************************************************
  // Shall we write out the glottis data ?
  // ****************************************************************

  ofstream miscFileStream;
  if (data->saveGlottisSignals)
  {
    miscFileStream.open(data->glottisSignalsFileName.ToStdString());
    if (!miscFileStream)
    {
      wxPrintf("Error: Failed to open the file %s to write out the glottis signals!\n",
        data->glottisSignalsFileName.c_str());
    }
    else
    {
      data->getSelectedGlottis()->printParamNames(miscFileStream);
    }
  }

  // ****************************************************************
  // Generate the audio signal sample by sample.
  // ****************************************************************

  wxStopWatch stopWatch;    // This starts the stop watch.
  int numSequenceSamples = tubeSequence->getDuration_pt();
  Tube tube;
  double glottisParams[Glottis::MAX_CONTROL_PARAMS];
  double x{ 0.0 };

  while ((data->nextTdsSampleIndex < numSequenceSamples) && (wasCanceled() == false))
  {
    // Check if we were asked to exit
    if (TestDestroy())
    {
      break;
    }

    // **************************************************************
    // Create any type of command event here to send the position
    // in percent in a thread-safe way.
    // If no animation is shown, send an event every 200 samples to
    // stay reactive (react on Cancel-button).
    // **************************************************************

    if (((data->showAnimation) && ((data->nextTdsSampleIndex % speed_percent) == 0)) ||
      ((data->showAnimation == false) && ((data->nextTdsSampleIndex % 200) == 0)))
    {
      // Set guiUpdateFinished to false BEFORE the command event is
      // sent to the GUI
      guiUpdateFinished = false;

      int pos_percent = 100 * data->nextTdsSampleIndex / numSequenceSamples;
      wxCommandEvent event(wxEVT_COMMAND_MENU_SELECTED, SYNTHESIS_THREAD_EVENT);
      event.SetInt(pos_percent);
      wxPostEvent(window, event);

      // Wait until the GUI update in response to our event finished.
      while (guiUpdateFinished == false)
      {
        // Give the rest of the thread time slice to the system allowing the other threads to run.
        this->Yield();
      }
    }

    tubeSequence->getTube(data->nextTdsSampleIndex, tube);
    tubeSequence->getGlottisParams(data->nextTdsSampleIndex, glottisParams);
    x = data->synthesizer->addSample(glottisParams, &tube, &miscFileStream);
    data->track[Data::MAIN_TRACK]->setValue(data->nextTdsSampleIndex, (signed short)(x * 32767.0));

    data->nextTdsSampleIndex++;
  }

  stopWatch.Pause();
  wxPrintf("The synthesis took %ld ms.\n", stopWatch.Time());

  // ****************************************************************
  // Close the file with the glottis signals.
  // ****************************************************************

  if ((data->saveGlottisSignals) && (miscFileStream))
  {
    miscFileStream.close();
  }

  // ****************************************************************
  // Send an event to the GUI thread that tells it that this thread
  // has finished.
  // ****************************************************************

  wxCommandEvent event(wxEVT_COMMAND_MENU_SELECTED, SYNTHESIS_THREAD_EVENT);
  event.SetInt(-1); // that's all
  wxPostEvent(window, event);

  return NULL;
}

// ****************************************************************************
