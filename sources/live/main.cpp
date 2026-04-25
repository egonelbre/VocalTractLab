// ****************************************************************************
// VocalTractLab realtime experimentation lab.
//
// ImGui-based companion app to the wxWidgets GUI. Streams audio through the
// vtl synthesizer in real time and lets the user move the fundamental, the
// subglottal pressure, and the articulation parameters around with sliders
// while watching the vocal tract outline and the radiated spectrum update.
// ****************************************************************************

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "AudioEngine.h"
#include "anatomy/Surface.h"
#include "anatomy/VocalTract.h"
#include "core/Constants.h"
#include "core/Geometry.h"
#include "dsp/Dsp.h"
#include "dsp/Signal.h"
#include "glottis/Glottis.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

namespace {

// ---- Asset discovery ------------------------------------------------------
//
// We try the executable's directory first (Linux/Windows install), then the
// macOS bundle's Resources/, and finally the source tree (so a binary built
// in build/<preset>/ during development can still find the speaker file).

std::filesystem::path findSpeakerFile(const char* argv0) {
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::path exe(argv0);
  fs::path exeDir = fs::absolute(exe, ec).parent_path();

  std::vector<fs::path> candidates;
  candidates.push_back(exeDir / "JD2.speaker");
#if defined(__APPLE__)
  // For .app bundles: argv0 is .../Contents/MacOS/vtl_live.
  candidates.push_back(exeDir.parent_path() / "Resources" / "JD2.speaker");
#endif
  candidates.push_back(exeDir / ".." / "JD2.speaker");
  candidates.push_back(exeDir / ".." / "data" / "speakers" / "JD2.speaker");
  candidates.push_back(exeDir / ".." / ".." / "data" / "speakers" /
                       "JD2.speaker");
  candidates.push_back(exeDir / ".." / ".." / ".." / "data" / "speakers" /
                       "JD2.speaker");

  for (const auto& p : candidates) {
    if (fs::exists(p, ec)) return fs::canonical(p, ec);
  }
  return fs::path();
}

// ---- Vocal tract drawing --------------------------------------------------
//
// Pulls the same line strips that VocalTractPicture::render2D draws on the
// wxGLCanvas — upper and lower covers (with uvula / epiglottis insertions),
// the tongue mediosagittal contour, the dashed tongue circle, the teeth and
// lips. Coordinates from the surfaces are in cm; the helper maps them into
// the supplied screen rectangle preserving aspect ratio.

struct TractView {
  ImVec2 origin;     // screen pixel that maps to (0, 0) in cm
  float scale_px_per_cm;
  ImVec2 toScreen(double x_cm, double y_cm) const {
    return ImVec2(origin.x + (float)x_cm * scale_px_per_cm,
                  origin.y - (float)y_cm * scale_px_per_cm);
  }
};

void appendVertex(std::vector<ImVec2>& out, const TractView& view, double x,
                  double y) {
  out.push_back(view.toScreen(x, y));
}

void drawStrip(ImDrawList* dl, const std::vector<ImVec2>& pts, ImU32 color,
               float thickness) {
  if (pts.size() >= 2) {
    dl->AddPolyline(pts.data(), (int)pts.size(), color, ImDrawFlags_None,
                    thickness);
  }
}

void drawVocalTract(ImDrawList* dl, VocalTract* tract, ImVec2 canvasMin,
                    ImVec2 canvasMax) {
  // The model lives roughly in x ∈ [-12, 8] cm, y ∈ [-12, 4] cm in the
  // mediosagittal plane (head facing right). Pick a scale that fits this
  // bounding box, then center.
  const double modelW = 22.0;  // cm
  const double modelH = 16.0;  // cm
  const double modelCx = -2.0;
  const double modelCy = -4.0;

  float canvasW = canvasMax.x - canvasMin.x;
  float canvasH = canvasMax.y - canvasMin.y;
  float scale = std::min(canvasW / (float)modelW, canvasH / (float)modelH);
  TractView view;
  view.scale_px_per_cm = scale;
  view.origin = ImVec2(
      canvasMin.x + canvasW * 0.5f - (float)modelCx * scale,
      canvasMin.y + canvasH * 0.5f + (float)modelCy * scale);

  const ImU32 colBlack = IM_COL32(20, 20, 20, 255);
  const ImU32 colTongue = IM_COL32(220, 80, 60, 255);
  const ImU32 colTongueSide = IM_COL32(200, 200, 200, 255);
  const ImU32 colDashed = IM_COL32(120, 120, 120, 255);
  const ImU32 colLip = IM_COL32(220, 80, 60, 255);
  const float thickThin = 1.0f;
  const float thickMain = 1.6f;

  std::vector<ImVec2> pts;

  // ---- Upper cover (with uvula inserted at its centerline) ----
  pts.clear();
  Surface* s = &tract->surfaces[VocalTract::UPPER_COVER];
  int ribPoint = s->numRibPoints - 1;
  int boundary =
      VocalTract::NUM_LARYNX_RIBS + VocalTract::NUM_PHARYNX_RIBS;
  for (int i = 0; i <= boundary; ++i) {
    Point3D q = s->getVertex(i, ribPoint);
    appendVertex(pts, view, q.x, q.y);
  }
  s = &tract->surfaces[VocalTract::UVULA];
  ribPoint = s->numRibPoints - 1;
  for (int i = 0; i < s->numRibs; ++i) {
    Point3D q = s->getVertex(i, ribPoint);
    appendVertex(pts, view, q.x, q.y);
  }
  ribPoint = 0;
  for (int i = s->numRibs - 1; i >= 0; --i) {
    Point3D q = s->getVertex(i, ribPoint);
    appendVertex(pts, view, q.x, q.y);
  }
  s = &tract->surfaces[VocalTract::UPPER_COVER];
  ribPoint = s->numRibPoints - 1;
  for (int i = boundary + 1; i < s->numRibs; ++i) {
    Point3D q = s->getVertex(i, ribPoint);
    appendVertex(pts, view, q.x, q.y);
  }
  drawStrip(dl, pts, colBlack, thickMain);

  // ---- Lower cover with epiglottis inserted ----
  pts.clear();
  s = &tract->surfaces[VocalTract::LOWER_COVER];
  ribPoint = s->numRibPoints - 1;
  for (int i = 0; i < VocalTract::NUM_LARYNX_RIBS - 1; ++i) {
    Point3D q = s->getVertex(i, ribPoint);
    appendVertex(pts, view, q.x, q.y);
  }
  s = &tract->surfaces[VocalTract::EPIGLOTTIS];
  ribPoint = s->numRibPoints - 1;
  for (int i = 0; i < s->numRibs; ++i) {
    Point3D q = s->getVertex(i, ribPoint);
    appendVertex(pts, view, q.x, q.y);
  }
  ribPoint = 0;
  for (int i = s->numRibs - 1; i >= 0; --i) {
    Point3D q = s->getVertex(i, ribPoint);
    appendVertex(pts, view, q.x, q.y);
  }
  s = &tract->surfaces[VocalTract::LOWER_COVER];
  ribPoint = s->numRibPoints - 1;
  for (int i = VocalTract::NUM_LARYNX_RIBS - 1; i < s->numRibs; ++i) {
    Point3D q = s->getVertex(i, ribPoint);
    appendVertex(pts, view, q.x, q.y);
  }
  drawStrip(dl, pts, colBlack, thickMain);

  // ---- Tongue mediosagittal contour ----
  pts.clear();
  s = &tract->surfaces[VocalTract::TONGUE];
  ribPoint = s->numRibPoints / 2;
  for (int i = 0; i < s->numRibs; ++i) {
    Point3D q = s->getVertex(i, ribPoint);
    appendVertex(pts, view, q.x, q.y);
  }
  drawStrip(dl, pts, colTongue, thickMain);

  // ---- Tongue 1 cm side line (intersects z = -1 cm with each tongue rib) ----
  pts.clear();
  const double EPS = 1e-6;
  int midRibPoint = s->numRibPoints / 2;
  for (int i = 0; i < s->numRibs; ++i) {
    bool ok = false;
    Point3D found;
    for (int k = 0; k < midRibPoint; ++k) {
      Point3D Q = s->getVertex(i, k);
      Point3D R = s->getVertex(i, k + 1);
      if (Q.z <= -1.0 && R.z >= -1.0) {
        double d = R.z - Q.z;
        if (d < EPS) d = EPS;
        double t = (-1.0 - Q.z) / d;
        found = Q + (R - Q) * t;
        ok = true;
      }
    }
    if (!ok) found = s->getVertex(i, 0);
    appendVertex(pts, view, found.x, found.y);
  }
  drawStrip(dl, pts, colTongueSide, thickThin);

  // ---- Dashed tongue body ellipse ----
  {
    double mx = tract->params[VocalTract::TCX].limitedX;
    double my = tract->params[VocalTract::TCY].limitedX;
    double rx = tract->anatomy.tongueCenterRadiusX_cm;
    double ry = tract->anatomy.tongueCenterRadiusY_cm;
    constexpr int N = 64;
    for (int i = 0; i < N; ++i) {
      // Gap every other segment to fake a dashed line.
      if ((i & 1) == 0) continue;
      double a0 = 2.0 * M_PI * (double)i / (double)N;
      double a1 = 2.0 * M_PI * (double)(i + 1) / (double)N;
      ImVec2 p0 = view.toScreen(mx + rx * std::cos(a0),
                                my + ry * std::sin(a0));
      ImVec2 p1 = view.toScreen(mx + rx * std::cos(a1),
                                my + ry * std::sin(a1));
      dl->AddLine(p0, p1, colDashed, thickThin);
    }
  }

  // ---- Upper teeth: inner edge + cropped outer edge + first/last full rib ----
  s = &tract->surfaces[VocalTract::UPPER_TEETH];
  pts.clear();
  ribPoint = 0;
  for (int i = 0; i < s->numRibs; ++i) {
    Point3D q = s->getVertex(i, ribPoint);
    appendVertex(pts, view, q.x, q.y);
  }
  drawStrip(dl, pts, colBlack, thickThin);

  pts.clear();
  ribPoint = 2;
  for (int i = 0; i < s->numRibs - 4; ++i) {
    Point3D q = s->getVertex(i, ribPoint);
    appendVertex(pts, view, q.x, q.y);
  }
  drawStrip(dl, pts, colBlack, thickThin);

  pts.clear();
  for (int i = 0; i < 2; ++i) {
    Point3D q = s->getVertex(0, i);
    appendVertex(pts, view, q.x, q.y);
  }
  drawStrip(dl, pts, colBlack, thickThin);

  pts.clear();
  for (int i = 0; i < s->numRibPoints; ++i) {
    Point3D q = s->getVertex(s->numRibs - 2, i);
    appendVertex(pts, view, q.x, q.y);
  }
  drawStrip(dl, pts, colBlack, thickThin);

  // ---- Lower teeth ----
  s = &tract->surfaces[VocalTract::LOWER_TEETH];
  pts.clear();
  ribPoint = 0;
  for (int i = 0; i < s->numRibs; ++i) {
    Point3D q = s->getVertex(i, ribPoint);
    appendVertex(pts, view, q.x, q.y);
  }
  drawStrip(dl, pts, colBlack, thickThin);

  pts.clear();
  ribPoint = 2;
  for (int i = 0; i < s->numRibs - 7; ++i) {
    Point3D q = s->getVertex(i, ribPoint);
    appendVertex(pts, view, q.x, q.y);
  }
  drawStrip(dl, pts, colBlack, thickThin);

  pts.clear();
  for (int i = 0; i < 2; ++i) {
    Point3D q = s->getVertex(0, i);
    appendVertex(pts, view, q.x, q.y);
  }
  drawStrip(dl, pts, colBlack, thickThin);

  pts.clear();
  for (int i = 0; i < s->numRibPoints; ++i) {
    Point3D q = s->getVertex(s->numRibs - 2, i);
    appendVertex(pts, view, q.x, q.y);
  }
  drawStrip(dl, pts, colBlack, thickThin);

  // ---- Lips ----
  s = &tract->surfaces[VocalTract::UPPER_LIP];
  pts.clear();
  for (int i = 0; i < s->numRibPoints; ++i) {
    Point3D q = s->getVertex(s->numRibs - 1, i);
    appendVertex(pts, view, q.x, q.y);
  }
  drawStrip(dl, pts, colLip, thickMain);

  s = &tract->surfaces[VocalTract::LOWER_LIP];
  pts.clear();
  for (int i = 0; i < s->numRibPoints; ++i) {
    Point3D q = s->getVertex(s->numRibs - 1, i);
    appendVertex(pts, view, q.x, q.y);
  }
  drawStrip(dl, pts, colLip, thickMain);
}

// ---- Spectrum drawing -----------------------------------------------------

constexpr int FFT_LEN_EXPONENT = 10;
constexpr int FFT_LEN = 1 << FFT_LEN_EXPONENT;  // 1024 samples ≈ 21 ms

void drawSpectrum(ImDrawList* dl, live::AudioHistory& history,
                  ComplexSignal& fftBuf, ImVec2 canvasMin, ImVec2 canvasMax) {
  float canvasW = canvasMax.x - canvasMin.x;
  float canvasH = canvasMax.y - canvasMin.y;
  if (canvasW <= 1.0f || canvasH <= 1.0f) return;

  // Background and frame.
  const ImU32 colBg = IM_COL32(245, 245, 245, 255);
  const ImU32 colGrid = IM_COL32(200, 200, 200, 255);
  const ImU32 colCurve = IM_COL32(40, 80, 200, 255);
  const ImU32 colText = IM_COL32(80, 80, 80, 255);
  dl->AddRectFilled(canvasMin, canvasMax, colBg);

  // Pull latest samples and apply Hann window.
  static std::array<float, FFT_LEN> samples;
  history.copyLatest(samples.data(), FFT_LEN);

  fftBuf.reset(FFT_LEN);
  for (int i = 0; i < FFT_LEN; ++i) {
    double w = 0.5 - 0.5 * std::cos(2.0 * M_PI * i / (double)(FFT_LEN - 1));
    fftBuf.re[i] = (double)samples[i] * w;
    fftBuf.im[i] = 0.0;
  }
  complexFFT(fftBuf, FFT_LEN_EXPONENT, true);

  // Frequency / dB axes.
  const double fMin_Hz = 0.0;
  const double fMax_Hz = 8000.0;
  const double dbMin = -90.0;
  const double dbMax = 0.0;
  const double freqStep_Hz = (double)AUDIO_SAMPLING_RATE_HZ / (double)FFT_LEN;

  auto xForFreq = [&](double f) {
    return canvasMin.x +
           canvasW * (float)((f - fMin_Hz) / (fMax_Hz - fMin_Hz));
  };
  auto yForDb = [&](double db) {
    if (db < dbMin) db = dbMin;
    if (db > dbMax) db = dbMax;
    return canvasMax.y -
           canvasH * (float)((db - dbMin) / (dbMax - dbMin));
  };

  // Vertical grid every 1 kHz with labels.
  for (int khz = 1; khz < (int)(fMax_Hz / 1000.0); ++khz) {
    float x = xForFreq((double)khz * 1000.0);
    dl->AddLine(ImVec2(x, canvasMin.y), ImVec2(x, canvasMax.y), colGrid, 1.0f);
    char label[16];
    std::snprintf(label, sizeof(label), "%dk", khz);
    dl->AddText(ImVec2(x + 2.0f, canvasMax.y - 14.0f), colText, label);
  }
  // Horizontal grid every 20 dB with labels.
  for (double db = dbMin + 20.0; db < dbMax; db += 20.0) {
    float y = yForDb(db);
    dl->AddLine(ImVec2(canvasMin.x, y), ImVec2(canvasMax.x, y), colGrid, 1.0f);
    char label[16];
    std::snprintf(label, sizeof(label), "%d dB", (int)db);
    dl->AddText(ImVec2(canvasMin.x + 2.0f, y - 14.0f), colText, label);
  }

  // Spectrum curve: one polyline along the visible canvas, sampling the FFT
  // bins at each pixel. Magnitudes get a small floor before the log to keep
  // the curve numerically sane near silence.
  std::vector<ImVec2> pts;
  pts.reserve((int)canvasW + 1);
  const double mag0 = 1e-6;
  for (int px = 0; px <= (int)canvasW; ++px) {
    double f = fMin_Hz + (fMax_Hz - fMin_Hz) * (double)px / (double)canvasW;
    double bin = f / freqStep_Hz;
    int i0 = (int)bin;
    if (i0 < 0) i0 = 0;
    if (i0 >= FFT_LEN - 1) i0 = FFT_LEN - 2;
    double t = bin - (double)i0;
    double m = (1.0 - t) * fftBuf.getMagnitude(i0) +
               t * fftBuf.getMagnitude(i0 + 1);
    if (m < mag0) m = mag0;
    double db = 20.0 * std::log10(m);
    pts.push_back(
        ImVec2(canvasMin.x + (float)px, yForDb(db)));
  }
  if (pts.size() >= 2) {
    dl->AddPolyline(pts.data(), (int)pts.size(), colCurve, ImDrawFlags_None,
                    1.5f);
  }
  dl->AddRect(canvasMin, canvasMax, IM_COL32(150, 150, 150, 255));
}

// ---- GLFW glue ------------------------------------------------------------

void glfwErrorCallback(int code, const char* msg) {
  std::fprintf(stderr, "glfw error %d: %s\n", code, msg);
}

}  // namespace

int main(int argc, char** argv) {
  std::filesystem::path speakerPath =
      findSpeakerFile(argc > 0 ? argv[0] : "vtl_live");
  if (speakerPath.empty()) {
    std::fprintf(stderr,
                 "live: could not locate JD2.speaker next to the binary\n");
    return 1;
  }

  glfwSetErrorCallback(glfwErrorCallback);
  if (!glfwInit()) return 1;

#if defined(__APPLE__)
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  const char* glslVersion = "#version 150";
#else
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
  const char* glslVersion = "#version 130";
#endif

  GLFWwindow* window =
      glfwCreateWindow(1280, 800, "VocalTractLab Live", nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    return 1;
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  ImGui::StyleColorsLight();
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init(glslVersion);

  live::AudioEngine engine;
  if (!engine.start(speakerPath.string())) {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 1;
  }

  // FFT scratch space, reused each frame.
  ComplexSignal fftBuf(FFT_LEN);

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Snapshot of control state for this frame's UI; written-back at the end.
    double f0_Hz, pressure_dPa;
    float gain;
    std::vector<double> tractParamsLocal(VocalTract::NUM_PARAMS);
    std::vector<double> glottisParamsLocal(engine.numGlottisParams());
    {
      std::lock_guard<std::mutex> lk(engine.control.mtx);
      f0_Hz = engine.control.f0_Hz;
      pressure_dPa = engine.control.pressure_dPa;
      gain = engine.control.outputGain;
      for (int i = 0; i < VocalTract::NUM_PARAMS; ++i) {
        tractParamsLocal[i] = engine.control.tractParams[i];
      }
      for (int i = 0; i < (int)glottisParamsLocal.size(); ++i) {
        glottisParamsLocal[i] = engine.control.glottisParams[i];
      }
    }

    // -------- Controls --------
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(420, 780), ImGuiCond_FirstUseEver);
    ImGui::Begin("Controls");

    {
      float f0_f = (float)f0_Hz;
      float p_f = (float)pressure_dPa;
      ImGui::TextUnformatted("Source");
      ImGui::Separator();
      if (ImGui::SliderFloat("F0 (Hz)", &f0_f, 50.0f, 500.0f, "%.1f")) {
        f0_Hz = f0_f;
      }
      if (ImGui::SliderFloat("Subglottal pressure (dPa)", &p_f, 0.0f, 16000.0f,
                             "%.0f")) {
        pressure_dPa = p_f;
      }
      ImGui::SliderFloat("Output gain", &gain, 0.0f, 1.0f, "%.2f");

      ImGui::Spacing();
      ImGui::TextUnformatted("Vowel preset");
      const auto& shapes = engine.tractShapes();
      static int comboIndex = -1;
      const char* preview =
          (comboIndex >= 0 && comboIndex < (int)shapes.size())
              ? shapes[comboIndex].name.c_str()
              : "(custom)";
      if (ImGui::BeginCombo("Tract shape", preview)) {
        for (int i = 0; i < (int)shapes.size(); ++i) {
          bool selected = (i == comboIndex);
          if (ImGui::Selectable(shapes[i].name.c_str(), selected)) {
            comboIndex = i;
            for (int p = 0; p < VocalTract::NUM_PARAMS; ++p) {
              tractParamsLocal[p] = shapes[i].param[p];
            }
          }
          if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }
    }

    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Articulation",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
      for (int i = 0; i < VocalTract::NUM_PARAMS; ++i) {
        const auto& info = engine.tractParamInfo(i);
        float v = (float)tractParamsLocal[i];
        char label[64];
        std::snprintf(label, sizeof(label), "%s##t%d", info.abbr.c_str(), i);
        if (ImGui::SliderFloat(label, &v, (float)info.min, (float)info.max,
                               "%.2f")) {
          tractParamsLocal[i] = v;
        }
      }
    }

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
                             (int)glottisParamsLocal.size());
            for (int p = 0; p < n; ++p) {
              if (p == Glottis::FREQUENCY || p == Glottis::PRESSURE) continue;
              glottisParamsLocal[p] = gShapes[i].controlParam[p];
            }
          }
          if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }
      for (int i = 0; i < (int)glottisParamsLocal.size(); ++i) {
        if (i == Glottis::FREQUENCY || i == Glottis::PRESSURE) continue;
        const auto& info = engine.glottisParamInfo(i);
        float v = (float)glottisParamsLocal[i];
        char label[80];
        std::snprintf(label, sizeof(label), "%s##g%d", info.abbr.c_str(), i);
        if (ImGui::SliderFloat(label, &v, (float)info.min, (float)info.max,
                               "%.3f")) {
          glottisParamsLocal[i] = v;
        }
      }
    }

    ImGui::End();

    // -------- Vocal tract --------
    ImGui::SetNextWindowPos(ImVec2(440, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(820, 500), ImGuiCond_FirstUseEver);
    ImGui::Begin("Vocal Tract");
    {
      // Apply current articulation to the visualization tract and rebuild.
      VocalTract* tract = engine.uiTract();
      for (int i = 0; i < VocalTract::NUM_PARAMS; ++i) {
        tract->params[i].x = tractParamsLocal[i];
      }
      tract->calculateAll();

      ImVec2 avail = ImGui::GetContentRegionAvail();
      ImVec2 pos = ImGui::GetCursorScreenPos();
      ImVec2 canvasMin = pos;
      ImVec2 canvasMax = ImVec2(pos.x + avail.x, pos.y + avail.y);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(canvasMin, canvasMax, IM_COL32(255, 255, 255, 255));
      drawVocalTract(dl, tract, canvasMin, canvasMax);
      dl->AddRect(canvasMin, canvasMax, IM_COL32(150, 150, 150, 255));
      ImGui::Dummy(avail);
    }
    ImGui::End();

    // -------- Spectrum --------
    ImGui::SetNextWindowPos(ImVec2(440, 520), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(820, 270), ImGuiCond_FirstUseEver);
    ImGui::Begin("Primary Spectrum");
    {
      ImVec2 avail = ImGui::GetContentRegionAvail();
      ImVec2 pos = ImGui::GetCursorScreenPos();
      ImVec2 canvasMin = pos;
      ImVec2 canvasMax = ImVec2(pos.x + avail.x, pos.y + avail.y);
      drawSpectrum(ImGui::GetWindowDrawList(), engine.history, fftBuf,
                   canvasMin, canvasMax);
      ImGui::Dummy(avail);
    }
    ImGui::End();

    // Push UI state back to the audio thread.
    {
      std::lock_guard<std::mutex> lk(engine.control.mtx);
      engine.control.f0_Hz = f0_Hz;
      engine.control.pressure_dPa = pressure_dPa;
      engine.control.outputGain = gain;
      for (int i = 0; i < VocalTract::NUM_PARAMS; ++i) {
        engine.control.tractParams[i] = tractParamsLocal[i];
      }
      for (int i = 0; i < (int)glottisParamsLocal.size(); ++i) {
        engine.control.glottisParams[i] = glottisParamsLocal[i];
      }
    }

    ImGui::Render();
    int displayW, displayH;
    glfwGetFramebufferSize(window, &displayW, &displayH);
    glViewport(0, 0, displayW, displayH);
    glClearColor(0.93f, 0.93f, 0.95f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
  }

  engine.stop();

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
