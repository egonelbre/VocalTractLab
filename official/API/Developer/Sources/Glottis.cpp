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

#include "Glottis.h"
#include "TdsModel.h"

#include <iomanip>
#include <cstdio>

// This constant should have the same value as the same constant in Tube.h/cpp
const double Glottis::DEFAULT_ASPIRATION_STRENGTH_DB = -40.0;

// ****************************************************************************
/// Default implementation.
// ****************************************************************************

double Glottis::getAspirationStrength_dB()
{
  // When this function was not overwritten by a derived class,
  // just return the default value.
  return DEFAULT_ASPIRATION_STRENGTH_DB;
}


// ****************************************************************************
/// Returns the shape with the given name, or NULL, if a shape with that name
/// is not in the list.
// ****************************************************************************

Glottis::Shape *Glottis::getShape(const string &name)
{
  int i;
  Shape *s = NULL;
  int numShapes = (int)shapes.size();

  for (i=0; (i < numShapes) && (s == NULL); i++)
  {
    if (shapes[i].name == name)
    {
      s = &shapes[i];
    }
  }

  return s;
}


// ****************************************************************************
/// Writes the glottis data to an XML string.
// ****************************************************************************

bool Glottis::writeToXml(ostream &os, int initialIndent, bool isSelected)
{
  int i, k;
  char st[1024];
  int indent = initialIndent;

  // ****************************************************************
  // Open the glottis element.
  // ****************************************************************

  os << string(indent, ' ') << "<glottis_model type=\"" << getName() << "\" selected=\"" << isSelected << "\">" << endl;
  indent+= 2;

  // ****************************************************************
  // Write the static parameters.
  // ****************************************************************

  os << string(indent, ' ') << "<static_params>" << endl;
  indent+= 2;

  for (i=0; i < (int)staticParams.size(); i++)
  {
    sprintf(st, "<param index=\"%d\" name=\"%s\" abbr=\"%s\" unit=\"%s\" min=\"%f\" max=\"%f\" default=\"%f\" value=\"%f\"/>",
      i,
      staticParams[i].name.c_str(),
      staticParams[i].abbr.c_str(),
      staticParams[i].cgsUnit.c_str(),
      staticParams[i].min,
      staticParams[i].max,
      staticParams[i].neutral,
      staticParams[i].x);

    os << string(indent, ' ') << st << endl;
  }

  indent-= 2;
  os << string(indent, ' ') << "</static_params>" << endl;

  // ****************************************************************
  // Write the control parameters.
  // ****************************************************************

  os << string(indent, ' ') << "<control_params>" << endl;
  indent+= 2;

  for (i=0; i < (int)controlParams.size(); i++)
  {
    sprintf(st, "<param index=\"%d\" name=\"%s\" abbr=\"%s\" unit=\"%s\" min=\"%f\" max=\"%f\" default=\"%f\" value=\"%f\"/>",
      i,
      controlParams[i].name.c_str(),
      controlParams[i].abbr.c_str(),
      controlParams[i].cgsUnit.c_str(),
      controlParams[i].min,
      controlParams[i].max,
      controlParams[i].neutral,
      controlParams[i].x);

    os << string(indent, ' ') << st << endl;
  }

  indent-= 2;
  os << string(indent, ' ') << "</control_params>" << endl;

  // ****************************************************************
  // Write the shapes.
  // ****************************************************************

  os << string(indent, ' ') << "<shapes>" << endl;
  indent+= 2;

  for (k=0; k < (int)shapes.size(); k++)
  {
    os << string(indent, ' ') << "<shape name=\"" << shapes[k].name << "\">" << endl;
    indent+= 2;

    for (i=0; i < (int)controlParams.size(); i++)
    {
      sprintf(st, "<control_param index=\"%d\" value=\"%f\"/>", i, shapes[k].controlParam[i]);
      os << string(indent, ' ') << st << endl;
    }

    indent-= 2;
    os << string(indent, ' ') << "</shape>" << endl;
  }

  indent-= 2;
  os << string(indent, ' ') << "</shapes>" << endl;

  // ****************************************************************
  // Close the glottis element.
  // ****************************************************************

  indent-= 2;
  os << string(indent, ' ') << "</glottis_model>" << endl;

  return true;
}


// ****************************************************************************
/// Reads the glottis data and shapes from the given XML-structure.
// ****************************************************************************

bool Glottis::readFromXml(XmlNode &rootNode)
{
  int i, k;
  XmlNode *node;
  
  int index;
  string name;
  double value;

  // ****************************************************************
  // Read the static parameter values.
  // ****************************************************************

  XmlNode *staticParamNode = rootNode.getChildElement("static_params");
  
  if (staticParamNode != NULL)
  {
    for (i=0; i < (int)staticParamNode->childElement.size(); i++)
    {
      node = staticParamNode->childElement[i];
      index = node->getAttributeInt("index");
      name  = node->getAttributeString("name");
      value = node->getAttributeDouble("value");

      if ((index < 0) || (index >= (int)staticParams.size()))
      {
        printf("Error: Static parameter index out of range for parameter '%s'!\n", name.c_str());
        return false;
      }

      if (name != staticParams[index].name)
      {
        printf("Error: The name of the static parameter %d is '%s' but should be '%s'!\n", 
          index, name.c_str(), staticParams[index].name.c_str());
        return false;
      }

      staticParams[index].x = value;
    }
  }

  // ****************************************************************
  // Read the control parameter values.
  // ****************************************************************

  XmlNode *controlParamNode = rootNode.getChildElement("control_params");

  if (controlParamNode != NULL)
  {
    for (i=0; i < (int)controlParamNode->childElement.size(); i++)
    {
      node = controlParamNode->childElement[i];
      index = node->getAttributeInt("index");
      name  = node->getAttributeString("name");
      value = node->getAttributeDouble("value");

      if ((index < 0) || (index >= (int)controlParams.size()))
      {
        printf("Error: Control parameter index out of range for parameter '%s'!\n", name.c_str());
        return false;
      }

      if (name != controlParams[index].name)
      {
        printf("Error: The name of the control parameter %d is '%s' but should be '%s'!\n", 
          index, name.c_str(), controlParams[index].name.c_str());
        return false;
      }

      // Don't overwrite with the values from the xml-file. Keep the
      // default values at the start of the program.
//      controlParam[index].x = value;
    }
  }

  // ****************************************************************
  // Read the shapes.
  // ****************************************************************

  XmlNode *shapesNode = rootNode.getChildElement("shapes");

  if (shapesNode != NULL)
  {
    // Clear all current shapes
    shapes.clear();

    // Run through all shapes
    int numshapes = shapesNode->numChildElements("shape");
    for (i=0; i < numshapes; i++)
    {
      XmlNode *shapeNode = shapesNode->getChildElement("shape", i);
      Shape s;
      s.name = shapeNode->getAttributeString("name");

      // Init default values for the shape to read in.
      s.controlParam.resize( controlParams.size() );
      for (k=0; k < (int)controlParams.size(); k++)
      {
        s.controlParam[k] = controlParams[k].neutral;
      }

      if (s.name.empty())
      {
        printf("Error: Invalid glottis shape element. The shape name is missing!\n");
        return false;
      }

      // Run through all control parameter values
      int numParams = shapeNode->numChildElements("control_param");

      for (k=0; k < numParams; k++)
      {
        node = shapeNode->getChildElement("control_param", k);
        index = node->getAttributeInt("index");
        value = node->getAttributeDouble("value");

        if ((index < 0) || (index >= (int)controlParams.size()))
        {
          printf("Error: Shape parameter index out of range!\n");
          return false;
        }

        s.controlParam[index] = value;
      }

      // Add the read shape to the shapes list.
      shapes.push_back(s);
    }
  }

  // ****************************************************************
  // Calculate the geometry with the loaded data.
  // ****************************************************************

  resetMotion();
  calcGeometry();

  return true;
}

// ****************************************************************************
/// Print the names and units of the control and derived parameters in a row.
// ****************************************************************************

void Glottis::printParamNames(ostream &os)
{
  int i;

  if (saveReducedParamsSet == false)
  {
    for (i = 0; i < (int)controlParams.size(); i++)
    {
      os << controlParams[i].abbr << "[" << controlParams[i].cgsUnit << "] ";
    }

    for (i = 0; i < (int)derivedParams.size(); i++)
    {
      os << derivedParams[i].abbr << "[" << derivedParams[i].cgsUnit << "] ";
    }
  }

  // This is the reduced parameter set.

  os << "glottal_flow[cm^3/s] ";
  os << "mouth_flow[cm^3/s] ";
  os << "nostril_flow[cm^3/s] ";
  os << "skin_flow[cm^3/s] ";
  os << "radiated_pressure[dPa] ";

  if (saveReducedParamsSet == false)
  {
    // Add pressure values in some tube sections.

    os << "P_subglottal[dPa] ";
    os << "P_intraglottal1[dPa] ";
    os << "P_intraglottal2[dPa] ";

    // Add the 40 supraglottal tube pressures.
    for (i = 0; i < Tube::NUM_PHARYNX_MOUTH_SECTIONS; i++)
    {
      os << "section_pressure" << (i + 1) << "[dPa] ";
    }

    // Add the 40 supraglottal tube lengths.
    for (i = 0; i < Tube::NUM_PHARYNX_MOUTH_SECTIONS; i++)
    {
      os << "section_length" << (i + 1) << "[cm] ";
    }

    // Add the 40 supraglottal tube areas.
    for (i = 0; i < Tube::NUM_PHARYNX_MOUTH_SECTIONS; i++)
    {
      os << "section_area" << (i + 1) << "[cm^2] ";
    }
  }

  os << endl;
}


// ****************************************************************************
/// Print the values of the control and derived parameters in a row.
// ****************************************************************************

void Glottis::printParamValues(ostream &os, double glottalFlow_cm3_s, 
  double mouthFlow_cm3_s, double nostrilFlow_cm3_s, double skinFlow_cm3_s, 
  double radiatedPressure_dPa, double subglottalPressure_dPa, 
  double intraglottalPressure1_dPa, double intraglottalPressure2_dPa,
  double sectionPressures_dPa[], double sectionLengths_cm[], double sectionAreas_cm2[])
{
  int i;

  if (saveReducedParamsSet == false)
  {
    // Specify how many digits to display after the decimal point.
    os << fixed << setprecision(8);

    for (i = 0; i < (int)controlParams.size(); i++)
    {
      os << controlParams[i].x << " ";
    }

    for (i = 0; i < (int)derivedParams.size(); i++)
    {
      os << derivedParams[i].x << " ";
    }
  }

  // Add some flow values and the radiated pressure.
  os << glottalFlow_cm3_s << " ";
  os << mouthFlow_cm3_s << " ";
  os << nostrilFlow_cm3_s << " ";
  os << skinFlow_cm3_s << " ";
  os << radiatedPressure_dPa << " ";

  if (saveReducedParamsSet == false)
  {
    os << subglottalPressure_dPa << " ";
    os << intraglottalPressure1_dPa << " ";
    os << intraglottalPressure2_dPa << " ";

    // Add for all supraglottal tube sections the inner pressure, the 
    // section length and area.

    for (i = 0; i < Tube::NUM_PHARYNX_MOUTH_SECTIONS; i++)
    {
      os << sectionPressures_dPa[i] << " ";
    }

    for (i = 0; i < Tube::NUM_PHARYNX_MOUTH_SECTIONS; i++)
    {
      os << sectionLengths_cm[i] << " ";
    }

    for (i = 0; i < Tube::NUM_PHARYNX_MOUTH_SECTIONS; i++)
    {
      os << sectionAreas_cm2[i] << " ";
    }
  }

  os << endl;
}

// ****************************************************************************
/// Restricts the values of all parameters in the given vector.
// ****************************************************************************

void Glottis::restrictParams(vector<Parameter> &p)
{
  int i;

  for (i=0; i < (int)p.size(); i++)
  {
    if (p[i].x < p[i].min)
    {
      p[i].x = p[i].min;
    }

    if (p[i].x > p[i].max)
    {
      p[i].x = p[i].max;
    }
  }
}


// ****************************************************************************
/// Temporarily store (cache) the control parameter values, so that they can 
/// restored later.
// ****************************************************************************

void Glottis::storeControlParams()
{
  hasStoredControlParams = true;
  int i;
  for (i = 0; i < (int)controlParams.size(); i++)
  {
    storedControlParams[i] = controlParams[i].x;
  }
}


// ****************************************************************************
/// Restore the temporarily stored (cached) control parameter values.
// ****************************************************************************

void Glottis::restoreControlParams()
{
  if (hasStoredControlParams == false)
  {
    return;
  }

  int i;
  for (i = 0; i < (int)controlParams.size(); i++)
  {
    controlParams[i].x = storedControlParams[i];
  }
  hasStoredControlParams = false;

  calcGeometry();
}


// ****************************************************************************
