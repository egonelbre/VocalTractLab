#include "VowelChartPanel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

#include "AudioEngine.h"
#include "ControlsPanel.h"
#include "acoustics/TlModel.h"
#include "acoustics/Tube.h"
#include "anatomy/VocalTract.h"
#include "imgui.h"

namespace live {

namespace {

struct VowelMarker {
  const char* ipa;       // UTF-8 label drawn in the marker
  double f1_Hz;
  double f2_Hz;
  const char* shapeName; // JD2 SAMPA name; closest match when no exact equivalent exists
};

// Cardinal English/IPA vowels at typical adult-male formant positions,
// each linked to the closest vowel shape in the JD2 (German) speaker
// file. The chart layout matches the reference mockup: F1 increases
// upward (open vowels at the top), F2 increases left-to-right (front
// vowels on the right). This is the inverse of the traditional IPA
// chart but reads more naturally as a plain x/y plot.
constexpr VowelMarker kVowels[] = {
    {"i",      280.0, 2250.0, "i"},
    {"\xc9\xaa", 400.0, 2100.0, "I"},   // ɪ  (U+026A)
    {"e",      400.0, 2050.0, "e"},
    {"\xc9\x9b", 550.0, 1850.0, "E"},   // ɛ  (U+025B)
    {"\xc3\xa6", 700.0, 1750.0, "E"},   // æ  (U+00E6) — closest JD2 match: E
    {"\xc9\x91", 730.0, 1100.0, "a"},   // ɑ  (U+0251)
    {"\xc9\x94", 600.0, 900.0,  "O"},   // ɔ  (U+0254)
    {"\xca\x8c", 650.0, 1200.0, "6_low"},// ʌ (U+028C) — central open ≈ German [ɐ]
    {"\xc9\x9c", 500.0, 1500.0, "@"},   // ɜ  (U+025C) — schwa-adjacent
    {"o",      450.0, 850.0,  "o"},
    {"\xca\x8a", 450.0, 1050.0, "U"},   // ʊ  (U+028A)
    {"u",      300.0, 750.0,  "u"},
};

constexpr int kNumVowels = (int)(sizeof(kVowels) / sizeof(kVowels[0]));

// IDW exponent — higher = sharper snap to the nearest vowel. 4 makes a
// direct click feel like a snap while still blending smoothly between
// neighbours when the cursor sits between markers.
constexpr double kIdwExponent = 4.0;
constexpr double kIdwFloor_Hz = 1.0;

const VocalTract::Shape* findShape(
    const std::vector<VocalTract::Shape>& shapes, const char* name) {
  if (!name) return nullptr;
  for (const auto& s : shapes) {
    if (s.name == name) return &s;
  }
  return nullptr;
}

// Inverse-distance blend over the vowel markers in (F1, F2) space.
// F1 and F2 share the same Hz scale so unweighted Euclidean distance
// works without per-axis rescaling. Vowels whose mapped JD2 shape is
// missing from the speaker file are silently dropped.
void blendShapes(const std::vector<VocalTract::Shape>& shapes,
                 double f1_Hz, double f2_Hz, FrameSnapshot& snap) {
  std::array<double, kNumVowels> w{};
  std::array<const VocalTract::Shape*, kNumVowels> shapePtr{};
  double wSum = 0.0;
  for (int i = 0; i < kNumVowels; ++i) {
    shapePtr[i] = findShape(shapes, kVowels[i].shapeName);
    if (!shapePtr[i]) continue;
    double df1 = f1_Hz - kVowels[i].f1_Hz;
    double df2 = f2_Hz - kVowels[i].f2_Hz;
    double d = std::sqrt(df1 * df1 + df2 * df2);
    if (d < kIdwFloor_Hz) d = kIdwFloor_Hz;
    double wi = 1.0 / std::pow(d, kIdwExponent);
    w[i] = wi;
    wSum += wi;
  }
  if (wSum <= 0.0) return;
  for (int p = 0; p < VocalTract::NUM_PARAMS; ++p) {
    double v = 0.0;
    for (int i = 0; i < kNumVowels; ++i) {
      if (w[i] == 0.0 || !shapePtr[i]) continue;
      v += (w[i] / wSum) * shapePtr[i]->param[p];
    }
    snap.tractParams[p] = v;
  }
}

// Pulls the lowest two formants out of the UI tract via the same
// transmission-line model the spectrum panel uses. Returns false on a
// complete closure (formant peaks are not meaningful then) or when
// fewer than two formants were extracted.
bool computeCurrentFormants(VocalTract* uiTract,
                            double& f1_Hz, double& f2_Hz) {
  f1_Hz = 0.0;
  f2_Hz = 0.0;
  if (!uiTract) return false;
  static TlModel s_tl;
  uiTract->getTube(&s_tl.tube);
  s_tl.tube.resetGlottisSections(0.0);
  constexpr int MAX_F = 4;
  double freq[MAX_F]{};
  double bw[MAX_F]{};
  int n = 0;
  bool fric = false, closure = false, nasal = false;
  s_tl.getFormants(freq, bw, n, MAX_F, fric, closure, nasal);
  if (closure || n < 2) return false;
  f1_Hz = freq[0];
  f2_Hz = freq[1];
  return true;
}

}  // namespace

void renderVowelChartPanel(AudioEngine& engine, FrameSnapshot& snap) {
  ImGui::Begin("Vowel Chart");
  ImGui::TextUnformatted("Click or drag in the chart to morph the mouth shape.");

  ImVec2 avail = ImGui::GetContentRegionAvail();
  float side = std::min(avail.x, avail.y);
  if (side < 200.0f) side = 200.0f;
  ImVec2 origin = ImGui::GetCursorScreenPos();

  // InvisibleButton claims the layout area and serves as the hit target
  // for click/drag interaction. Drawing happens via the window draw list
  // so the underlying button is purely a transparent input surface.
  ImGui::InvisibleButton("##vowel_chart", ImVec2(side, side));
  bool active = ImGui::IsItemActive();

  // Inset the plot rect from the bounding box to leave room for axis labels.
  const float padL = 36.0f;
  const float padR = 8.0f;
  const float padT = 18.0f;
  const float padB = 24.0f;
  ImVec2 plotMin(origin.x + padL, origin.y + padT);
  ImVec2 plotMax(origin.x + side - padR, origin.y + side - padB);
  if (plotMax.x <= plotMin.x + 10.0f || plotMax.y <= plotMin.y + 10.0f) {
    ImGui::End();
    return;
  }

  // Axis ranges chosen to fit the cardinal vowel cluster comfortably.
  // Both axes use linear Hz; F1 ≈ tongue height, F2 ≈ tongue front/back.
  const double f1Lo_Hz = 200.0, f1Hi_Hz = 1000.0;
  const double f2Lo_Hz = 600.0, f2Hi_Hz = 3000.0;

  auto x_for_f2 = [&](double f2) {
    double t = (f2 - f2Lo_Hz) / (f2Hi_Hz - f2Lo_Hz);
    return plotMin.x + (float)(t * (plotMax.x - plotMin.x));
  };
  auto y_for_f1 = [&](double f1) {
    // F1 increases upward, so invert the linear mapping.
    double t = (f1 - f1Lo_Hz) / (f1Hi_Hz - f1Lo_Hz);
    return plotMax.y - (float)(t * (plotMax.y - plotMin.y));
  };
  auto f2_for_x = [&](float x) {
    double t = (double)(x - plotMin.x) / (double)(plotMax.x - plotMin.x);
    return f2Lo_Hz + t * (f2Hi_Hz - f2Lo_Hz);
  };
  auto f1_for_y = [&](float y) {
    double t = (double)(plotMax.y - y) / (double)(plotMax.y - plotMin.y);
    return f1Lo_Hz + t * (f1Hi_Hz - f1Lo_Hz);
  };

  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImU32 colBg = ImGui::GetColorU32(ImGuiCol_FrameBg);
  const ImU32 colGrid = ImGui::GetColorU32(ImGuiCol_Text, 0.18f);
  const ImU32 colText = ImGui::GetColorU32(ImGuiCol_Text);
  const ImU32 colTextDim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
  const ImU32 colBorder = ImGui::GetColorU32(ImGuiCol_Border);
  const ImU32 colMarkerEdge = ImGui::GetColorU32(ImGuiCol_Text, 0.45f);
  const ImU32 colMarkerFill = ImGui::GetColorU32(ImGuiCol_FrameBgHovered);
  const ImU32 colMarkerMissing = ImGui::GetColorU32(ImGuiCol_FrameBg);
  const ImU32 colDot = ImGui::GetColorU32(ImGuiCol_CheckMark);
  const ImU32 colDotEdge = ImGui::GetColorU32(ImGuiCol_Text);

  dl->AddRectFilled(plotMin, plotMax, colBg);

  // Frequency gridlines + tick labels.
  static const double f1Grid[] = {300.0, 500.0, 700.0, 900.0};
  for (double g : f1Grid) {
    if (g < f1Lo_Hz || g > f1Hi_Hz) continue;
    float y = y_for_f1(g);
    dl->AddLine(ImVec2(plotMin.x, y), ImVec2(plotMax.x, y), colGrid, 1.0f);
    char lbl[16];
    std::snprintf(lbl, sizeof(lbl), "%d", (int)g);
    dl->AddText(ImVec2(plotMin.x - 30.0f, y - 7.0f), colTextDim, lbl);
  }
  static const double f2Grid[] = {1000.0, 1500.0, 2000.0, 2500.0};
  for (double g : f2Grid) {
    if (g < f2Lo_Hz || g > f2Hi_Hz) continue;
    float x = x_for_f2(g);
    dl->AddLine(ImVec2(x, plotMin.y), ImVec2(x, plotMax.y), colGrid, 1.0f);
    char lbl[16];
    std::snprintf(lbl, sizeof(lbl), "%d", (int)g);
    dl->AddText(ImVec2(x - 12.0f, plotMax.y + 4.0f), colTextDim, lbl);
  }
  dl->AddText(ImVec2(plotMin.x - 30.0f, plotMin.y - 14.0f), colTextDim,
              "F1 Hz");
  dl->AddText(ImVec2(plotMax.x - 36.0f, plotMax.y + 4.0f), colTextDim,
              "F2 Hz");

  // Vowel markers. Markers whose mapped JD2 shape is missing render in a
  // dimmed style so it is obvious they will not contribute to the blend.
  const float markerR = 14.0f;
  const auto& shapes = engine.tractShapes();
  for (int i = 0; i < kNumVowels; ++i) {
    float x = x_for_f2(kVowels[i].f2_Hz);
    float y = y_for_f1(kVowels[i].f1_Hz);
    bool hasShape = (findShape(shapes, kVowels[i].shapeName) != nullptr);
    ImU32 fill = hasShape ? colMarkerFill : colMarkerMissing;
    dl->AddCircleFilled(ImVec2(x, y), markerR, fill);
    dl->AddCircle(ImVec2(x, y), markerR, colMarkerEdge, 0, 1.5f);
    ImVec2 ts = ImGui::CalcTextSize(kVowels[i].ipa);
    ImU32 lblColor = hasShape ? colText : colTextDim;
    dl->AddText(ImVec2(x - ts.x * 0.5f, y - ts.y * 0.5f), lblColor,
                kVowels[i].ipa);
  }

  // Drag handler: while the invisible button is active (mouse held inside
  // the chart area), morph the tract toward the cursor's vowel position.
  if (active) {
    ImVec2 mp = ImGui::GetIO().MousePos;
    if (mp.x < plotMin.x) mp.x = plotMin.x;
    if (mp.x > plotMax.x) mp.x = plotMax.x;
    if (mp.y < plotMin.y) mp.y = plotMin.y;
    if (mp.y > plotMax.y) mp.y = plotMax.y;
    double f2 = f2_for_x(mp.x);
    double f1 = f1_for_y(mp.y);
    blendShapes(shapes, f1, f2, snap);
    dl->AddCircle(ImVec2(mp.x, mp.y), 8.0f, colDot, 0, 2.0f);
  }

  // Live formant dot — the actual F1/F2 of the current tract config, so
  // the user can see how their chosen position compares with what the
  // synthesizer is producing. Skipped on closure / fewer than two formants.
  double cf1 = 0.0, cf2 = 0.0;
  if (computeCurrentFormants(engine.uiTract(), cf1, cf2)) {
    float x = x_for_f2(cf2);
    float y = y_for_f1(cf1);
    if (x >= plotMin.x && x <= plotMax.x && y >= plotMin.y && y <= plotMax.y) {
      dl->AddCircleFilled(ImVec2(x, y), 5.0f, colDot);
      dl->AddCircle(ImVec2(x, y), 6.0f, colDotEdge, 0, 1.2f);
    }
  }

  dl->AddRect(plotMin, plotMax, colBorder);

  ImGui::End();
}

}  // namespace live
