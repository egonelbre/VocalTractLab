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

#include "TdsOptionsDialog.h"

// IDs of the controls

static const int IDC_SOFT_WALLS           = 5001;
static const int IDC_SOFT_CONTACTS        = 5002;
static const int IDC_NOISE_SOURCES        = 5003;
static const int IDC_RADIATION_FROM_SKIN  = 5004;
static const int IDC_PIRIFORM_FOSSA       = 5005;
static const int IDC_INNER_LENGTH_CORRECTIONS = 5006;
static const int IDR_RADIATION_CHARACTERISTICS = 5011;
static const int IDB_ADAPT_FROM_FDS = 5013;


// The single instance of this class.

TdsOptionsDialog *TdsOptionsDialog::instance = NULL;

// ****************************************************************************
// The event table.
// ****************************************************************************

BEGIN_EVENT_TABLE(TdsOptionsDialog, wxDialog)
  EVT_CHECKBOX(IDC_SOFT_WALLS, TdsOptionsDialog::OnSoftWalls)
  EVT_CHECKBOX(IDC_SOFT_CONTACTS, TdsOptionsDialog::OnSoftContacts)
  EVT_CHECKBOX(IDC_NOISE_SOURCES, TdsOptionsDialog::OnNoiseSources)
  EVT_CHECKBOX(IDC_RADIATION_FROM_SKIN, TdsOptionsDialog::OnRadiationFromSkin)
  EVT_CHECKBOX(IDC_PIRIFORM_FOSSA, TdsOptionsDialog::OnPiriformFossa)
  EVT_CHECKBOX(IDC_INNER_LENGTH_CORRECTIONS, TdsOptionsDialog::OnInnerLengthCorrections)
  EVT_RADIOBOX(IDR_RADIATION_CHARACTERISTICS, TdsOptionsDialog::OnRadiationCharacteristics)
  EVT_BUTTON(IDB_ADAPT_FROM_FDS, TdsOptionsDialog::OnAdaptFromFds)
END_EVENT_TABLE()


// ****************************************************************************
/// Returns the single instance of this dialog.
// ****************************************************************************

TdsOptionsDialog *TdsOptionsDialog::getInstance()
{
  if (instance == NULL)
  {
    instance = new TdsOptionsDialog(NULL);
  }

  return instance;
}


// ****************************************************************************
/// Private constructor.
// ****************************************************************************

TdsOptionsDialog::TdsOptionsDialog(wxWindow *parent) : 
  wxDialog(parent, wxID_ANY, wxString("Options for time-domain synthesis"), 
           wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
{
//  this->Move(100, 50);

  // ****************************************************************
  // Init the variables first.
  // ****************************************************************

  model = Data::getInstance()->tdsModel;

  // ****************************************************************
  // Init and update the widgets.
  // ****************************************************************

  initWidgets();
  updateWidgets();
}


// ****************************************************************************
/// Init the widgets.
// ****************************************************************************

void TdsOptionsDialog::initWidgets()
{
  wxBoxSizer *baseSizer = new wxBoxSizer(wxVERTICAL);
  wxFlexGridSizer *gridSizer = new wxFlexGridSizer(3, 2, 0, 0);

  // Create an invisible dummy button with the ID wxID_CANCEL, so that
  // the user can close the dialog with ESC.
  wxButton *button = new wxButton(this, wxID_CANCEL, "", wxPoint(0,0), wxSize(0,0));

  // Add the check boxes for the options

  chkSoftWalls = new wxCheckBox(this, IDC_SOFT_WALLS, "Soft walls");
  gridSizer->Add(chkSoftWalls, 0, wxALL, 5);

  chkSoftContacts = new wxCheckBox(this, IDC_SOFT_CONTACTS, "Soft contacts");
  gridSizer->Add(chkSoftContacts, 0, wxALL, 5);

  chkNoiseSources = new wxCheckBox(this, IDC_NOISE_SOURCES, "Noise sources");
  gridSizer->Add(chkNoiseSources, 0, wxALL, 5);

  chkRadiationFromSkin = new wxCheckBox(this, IDC_RADIATION_FROM_SKIN, "Sound radiation from skin");
  gridSizer->Add(chkRadiationFromSkin, 0, wxALL, 5);

  chkPiriformFossa = new wxCheckBox(this, IDC_PIRIFORM_FOSSA, "Piriform fossa");
  gridSizer->Add(chkPiriformFossa, 0, wxALL, 5);

  chkInnerLengthCorrections = new wxCheckBox(this, IDC_INNER_LENGTH_CORRECTIONS, "Inner length corrections");
  gridSizer->Add(chkInnerLengthCorrections, 0, wxALL, 5);

  baseSizer->Add(gridSizer);

  // Add the choice boxes for the radiation characteristic.

  baseSizer->AddSpacer(8);

  const int NUM_RADIATION_CHARACTERISTICS = TdsModel::NUM_RADIATION_CHARACTERISTICS;
  const wxString RADIATION_CHOICES[NUM_RADIATION_CHARACTERISTICS] =
  {
    "Simple radiation (point source)",
    "Piston in a sphere",
    "Head and torso simulation"
  };

  radRadiationCharacteristics = new wxRadioBox(this, IDR_RADIATION_CHARACTERISTICS, "Radiation characteristic",
    wxDefaultPosition, wxDefaultSize, NUM_RADIATION_CHARACTERISTICS, RADIATION_CHOICES, NUM_RADIATION_CHARACTERISTICS, wxRA_SPECIFY_ROWS);
  baseSizer->Add(radRadiationCharacteristics, 0, wxALL | wxGROW, 3);

  // Add the button

  btnAdaptFromFds = new wxButton(this, IDB_ADAPT_FROM_FDS, "Adapt data from FDS");
  baseSizer->Add(btnAdaptFromFds, 0, wxALL, 5);

  // Set the sizer for this dialog

  this->SetSizer(baseSizer);
  baseSizer->Fit(this);
  baseSizer->SetSizeHints(this);
}


// ****************************************************************************
/// Update the widgets.
// ****************************************************************************

void TdsOptionsDialog::updateWidgets()
{
  chkSoftWalls->SetValue( model->options.softWalls );
  chkSoftContacts->SetValue(model->options.softContacts);
  chkNoiseSources->SetValue( model->options.generateNoiseSources );
  chkRadiationFromSkin->SetValue( model->options.radiationFromSkin );
  chkPiriformFossa->SetValue( model->options.piriformFossa );
  chkInnerLengthCorrections->SetValue( model->options.innerLengthCorrections );

  radRadiationCharacteristics->SetSelection(model->options.radiationCharacteristic);
}


// ****************************************************************************
// ****************************************************************************

void TdsOptionsDialog::OnSoftWalls(wxCommandEvent &event)
{
  model->options.softWalls = !model->options.softWalls;
  updateWidgets();
}


// ****************************************************************************
// ****************************************************************************

void TdsOptionsDialog::OnSoftContacts(wxCommandEvent& event)
{
  model->options.softContacts = !model->options.softContacts;
  updateWidgets();
}


// ****************************************************************************
// ****************************************************************************

void TdsOptionsDialog::OnNoiseSources(wxCommandEvent &event)
{
  model->options.generateNoiseSources = !model->options.generateNoiseSources;
  updateWidgets();
}


// ****************************************************************************
// ****************************************************************************

void TdsOptionsDialog::OnRadiationFromSkin(wxCommandEvent &event)
{
  model->options.radiationFromSkin = !model->options.radiationFromSkin;
  updateWidgets();
}


// ****************************************************************************
// ****************************************************************************

void TdsOptionsDialog::OnPiriformFossa(wxCommandEvent &event)
{
  model->options.piriformFossa = !model->options.piriformFossa;
  updateWidgets();
}


// ****************************************************************************
// ****************************************************************************

void TdsOptionsDialog::OnInnerLengthCorrections(wxCommandEvent &event)
{
  model->options.innerLengthCorrections = !model->options.innerLengthCorrections;
  updateWidgets();
}


// ****************************************************************************
// ****************************************************************************

void TdsOptionsDialog::OnRadiationCharacteristics(wxCommandEvent& event)
{
  int selection = event.GetSelection();
  model->options.radiationCharacteristic = (TdsModel::RadiationCharacteristic)selection;
  updateWidgets();
}


// ****************************************************************************
// ****************************************************************************

void TdsOptionsDialog::OnAdaptFromFds(wxCommandEvent &event)
{
  TlModel *tlModel = Data::getInstance()->tlModel;

  model->options.softWalls        = tlModel->options.softWalls;
  model->options.piriformFossa    = tlModel->options.piriformFossa;
  model->options.innerLengthCorrections = tlModel->options.innerLengthCorrections;

  updateWidgets();
}

// ****************************************************************************
