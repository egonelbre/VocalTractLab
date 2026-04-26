#include "ControlsPanel.h"

#include <algorithm>
#include <cstdio>
#include <mutex>

#include "AudioEngine.h"
#include "anatomy/VocalTract.h"
#include "glottis/Glottis.h"
#include "imgui.h"

namespace live {

FrameSnapshot readFrameSnapshot(AudioEngine& engine) {
  FrameSnapshot snap;
  snap.tractParams.resize(VocalTract::NUM_PARAMS);
  snap.glottisParams.resize(engine.numGlottisParams());
  std::lock_guard<std::mutex> lk(engine.control.mtx);
  snap.f0_Hz = engine.control.f0_Hz;
  snap.pressure_dPa = engine.control.pressure_dPa;
  snap.outputGain = engine.control.outputGain;
  for (int i = 0; i < VocalTract::NUM_PARAMS; ++i) {
    snap.tractParams[i] = engine.control.tractParams[i];
  }
  for (int i = 0; i < (int)snap.glottisParams.size(); ++i) {
    snap.glottisParams[i] = engine.control.glottisParams[i];
  }
  return snap;
}

void writeFrameSnapshot(AudioEngine& engine, const FrameSnapshot& snap) {
  std::lock_guard<std::mutex> lk(engine.control.mtx);
  engine.control.f0_Hz = snap.f0_Hz;
  engine.control.pressure_dPa = snap.pressure_dPa;
  engine.control.outputGain = snap.outputGain;
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
    for (int i = 0; i < VocalTract::NUM_PARAMS; ++i) {
      const auto& info = engine.tractParamInfo(i);
      float v = (float)snap.tractParams[i];
      char label[64];
      std::snprintf(label, sizeof(label), "%s##t%d", info.abbr.c_str(), i);
      if (ImGui::SliderFloat(label, &v, (float)info.min, (float)info.max,
                             "%.2f")) {
        snap.tractParams[i] = v;
      }
    }
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
