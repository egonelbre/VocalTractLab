#include "ControlsPanel.h"

#include <algorithm>
#include <cstdio>

#include "AudioEngine.h"
#include "anatomy/VocalTract.h"
#include "glottis/Glottis.h"
#include "imgui.h"

namespace live {

FrameSnapshot readFrameSnapshot(AudioEngine& engine) {
  FrameSnapshot snap;
  snap.tractParams.resize(VocalTract::NUM_PARAMS);
  snap.glottisParams.resize(engine.numGlottisParams());
  // The audio thread is the only other reader, and it uses
  // ControlState::snapshot(). Going through the same seqlock here keeps
  // the UI's per-frame snapshot consistent with what the audio thread
  // sees — important because writeFrameSnapshot is the only path that
  // pushes UI edits back to the synth.
  ControlSnapshot s = engine.control.snapshot();
  snap.f0_Hz = s.f0_Hz;
  snap.pressure_dPa = s.pressure_dPa;
  snap.outputGain = s.outputGain;
  snap.autoTongueRoot = s.autoTongueRoot;
  for (int i = 0; i < VocalTract::NUM_PARAMS; ++i) {
    snap.tractParams[i] = s.tractParams[i];
  }
  for (int i = 0; i < (int)snap.glottisParams.size(); ++i) {
    snap.glottisParams[i] = s.glottisParams[i];
  }
  return snap;
}

void writeFrameSnapshot(AudioEngine& engine, const FrameSnapshot& snap) {
  ControlState::Writer w(engine.control);
  engine.control.f0_Hz = snap.f0_Hz;
  engine.control.pressure_dPa = snap.pressure_dPa;
  engine.control.outputGain = snap.outputGain;
  engine.control.autoTongueRoot = snap.autoTongueRoot;
  for (int i = 0; i < VocalTract::NUM_PARAMS; ++i) {
    engine.control.tractParams[i] = snap.tractParams[i];
  }
  for (int i = 0; i < (int)snap.glottisParams.size(); ++i) {
    engine.control.glottisParams[i] = snap.glottisParams[i];
  }
}

void renderControlsPanel(AudioEngine& engine, FrameSnapshot& snap) {
  ImGui::Begin("Controls");

  // ---- Source --------------------------------------------------------------
  {
    float f0_f = (float)snap.f0_Hz;
    float p_f = (float)snap.pressure_dPa;
    ImGui::TextUnformatted("Source");
    ImGui::Separator();
    if (ImGui::SliderFloat("F0 (Hz)", &f0_f, 50.0f, 500.0f, "%.1f")) {
      snap.f0_Hz = f0_f;
    }
    if (ImGui::SliderFloat("Subglottal pressure (dPa)", &p_f, 0.0f, 16000.0f,
                           "%.0f")) {
      snap.pressure_dPa = p_f;
    }
    ImGui::SliderFloat("Output gain", &snap.outputGain, 0.0f, 1.0f, "%.2f");
    // Tract shape presets live in the dedicated "Tract Shapes" panel.
  }

  ImGui::Spacing();
  // ---- Articulation --------------------------------------------------------
  if (ImGui::CollapsingHeader("Articulation",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    // Halve the slider bar so the label that follows it has room to read
    // even on narrow docks. Without this the slider eats most of the row
    // and the label gets clipped.
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);

    auto tractSlider = [&](int idx, const char* shortLabel) {
      const auto& info = engine.tractParamInfo(idx);
      float v = (float)snap.tractParams[idx];
      char label[64];
      std::snprintf(label, sizeof(label), "%s##t%d", shortLabel, idx);
      if (ImGui::SliderFloat(label, &v, (float)info.min, (float)info.max,
                             "%.2f")) {
        snap.tractParams[idx] = v;
      }
    };

    // Group the 19 tract params by articulator. With each section
    // labelled in a SeparatorText heading, the per-slider label can
    // collapse to "X" / "Y" / "Angle" etc. instead of repeating the
    // articulator name on every row.
    ImGui::SeparatorText("Hyoid");
    tractSlider(VocalTract::HX, "X");
    tractSlider(VocalTract::HY, "Y");

    ImGui::SeparatorText("Jaw");
    tractSlider(VocalTract::JX, "X");
    tractSlider(VocalTract::JA, "Angle (deg.)");

    ImGui::SeparatorText("Lip");
    tractSlider(VocalTract::LP, "Protrusion");
    tractSlider(VocalTract::LD, "Distance");

    ImGui::SeparatorText("Velum");
    tractSlider(VocalTract::VS, "Shape");
    tractSlider(VocalTract::VO, "Opening (cm^2)");

    ImGui::SeparatorText("Tongue body");
    tractSlider(VocalTract::TCX, "X");
    tractSlider(VocalTract::TCY, "Y");

    ImGui::SeparatorText("Tongue tip");
    tractSlider(VocalTract::TTX, "X");
    tractSlider(VocalTract::TTY, "Y");

    ImGui::SeparatorText("Tongue blade");
    tractSlider(VocalTract::TBX, "X");
    tractSlider(VocalTract::TBY, "Y");

    ImGui::SeparatorText("Tongue root");
    {
      bool prev = snap.autoTongueRoot;
      ImGui::Checkbox("Auto (from tongue body)##trauto", &snap.autoTongueRoot);
      if (prev && !snap.autoTongueRoot) {
        // Just turned auto OFF — seed the slider value from whatever the
        // synth was computing automatically, so editing starts at the
        // current visible position instead of the stale slider value.
        snap.tractParams[VocalTract::TRX] =
            engine.uiTract()->params[VocalTract::TRX].x;
        snap.tractParams[VocalTract::TRY] =
            engine.uiTract()->params[VocalTract::TRY].x;
      }
      ImGui::BeginDisabled(snap.autoTongueRoot);
      tractSlider(VocalTract::TRX, "X");
      tractSlider(VocalTract::TRY, "Y");
      ImGui::EndDisabled();
    }

    ImGui::SeparatorText("Tongue side elevation");
    tractSlider(VocalTract::TS1, "1");
    tractSlider(VocalTract::TS2, "2");
    tractSlider(VocalTract::TS3, "3");

    // AES and PW share a signed convention: negative = contraction,
    // positive = expansion. AES < 0 is the aryepiglottic-sphincter
    // contraction that defines twang. PW < 0 is constrictor
    // narrowing (Edge / Kulning), PW > 0 is stylopharyngeus dilation
    // ("open throat", Operatic squillo). The two are independent
    // muscle actions.
    ImGui::SeparatorText("Voice quality (twang & pharynx)");
    tractSlider(VocalTract::AES, "AES epilarynx (-narrow / +wide)");
    tractSlider(VocalTract::PW,  "Pharyngeal width (-narrow / +wide)");

    // Thyroid forward tilt — cricothyroid action. Tilts the upper
    // epiglottis backward toward the arytenoids. Independent of AES;
    // the Belt vs Squillo / Edge vs Opera distinction in pedagogy
    // turns largely on this gesture.
    ImGui::SeparatorText("Larynx posture");
    tractSlider(VocalTract::TT, "Thyroid tilt");

    ImGui::PopItemWidth();
  }

  // ---- Glottis -------------------------------------------------------------
  if (ImGui::CollapsingHeader("Glottis", ImGuiTreeNodeFlags_DefaultOpen)) {
    const auto& gShapes = engine.glottisShapes();
    static int gComboIndex = -1;
    const char* gPreview =
        (gComboIndex >= 0 && gComboIndex < (int)gShapes.size())
            ? gShapes[gComboIndex].name.c_str()
            : "(custom)";
    if (ImGui::BeginCombo("Glottis shape", gPreview)) {
      for (int i = 0; i < (int)gShapes.size(); ++i) {
        bool selected = (i == gComboIndex);
        if (ImGui::Selectable(gShapes[i].name.c_str(), selected)) {
          gComboIndex = i;
          int n = std::min((int)gShapes[i].controlParam.size(),
                           (int)snap.glottisParams.size());
          for (int p = 0; p < n; ++p) {
            // f0 and pressure stay under the dedicated sliders above.
            if (p == Glottis::FREQUENCY || p == Glottis::PRESSURE) continue;
            snap.glottisParams[p] = gShapes[i].controlParam[p];
          }
        }
        if (selected) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
    for (int i = 0; i < (int)snap.glottisParams.size(); ++i) {
      if (i == Glottis::FREQUENCY || i == Glottis::PRESSURE) continue;
      const auto& info = engine.glottisParamInfo(i);
      float v = (float)snap.glottisParams[i];
      char label[80];
      std::snprintf(label, sizeof(label), "%s##g%d", info.abbr.c_str(), i);
      if (ImGui::SliderFloat(label, &v, (float)info.min, (float)info.max,
                             "%.3f")) {
        snap.glottisParams[i] = v;
      }
    }
  }

  ImGui::End();
}

}  // namespace live
