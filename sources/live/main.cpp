// ****************************************************************************
// VocalTractLab realtime experimentation lab.
//
// ImGui-based companion app to the wxWidgets GUI. Streams audio through the
// vtl synthesizer in real time and lets the user move the fundamental, the
// subglottal pressure, and the articulation parameters around with sliders
// while watching the vocal tract outline (2D + 3D) and the radiated spectrum
// update.
//
// This file owns only the GLFW + ImGui plumbing and the per-frame
// orchestration. The actual UI lives in the panel modules:
//
//   ControlsPanel        — sliders, vowel/glottis presets
//   VocalTract2DPanel    — mediosagittal outline + drag handles
//   VocalTract3DPanel    — software-projected wireframe
//   VowelChartPanel      — F1/F2 vowel chart, click/drag to morph tract
//   SpectrumPanel        — FFT-based primary spectrum + VTTF + formants
//   LfPulsePanel         — reference LF glottal pulse + derivative
//
// AudioEngine wraps the synthesizer + OpenAL queue and runs either on a
// background thread (native) or pumped from this main loop (Emscripten).
// ****************************************************************************

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "AssetPaths.h"
#include "AudioEngine.h"
#include "ControlsPanel.h"
#include "LfPulsePanel.h"
#include "PresetsPanel.h"
#include "SpectrumPanel.h"
#include "VocalTract2DPanel.h"
#include "VocalTract3DPanel.h"
#include "VowelChartPanel.h"
#include "dsp/Signal.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"

#include <GLFW/glfw3.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

namespace {

// File-scope app state so emscripten_set_main_loop() can find it from a
// plain function pointer.
struct LabApp {
  GLFWwindow* window = nullptr;
  live::AudioEngine* engine = nullptr;
  ComplexSignal* fftBuf = nullptr;
  std::vector<live::SpeakerOption> speakers;
  // Bigger Noto Sans, re-rasterized at a larger pixel size, used by
  // the Tract Shapes panel to render the per-shape buttons. nullptr
  // when the font file couldn't be loaded — the panel falls back to
  // the default UI font.
  ImFont* tractShapeFont = nullptr;
};
LabApp g_app;

// Resolve the full speaker option list for the segmented switcher.
// Native: probe each known basename via findRuntimeAsset and include
// only the ones that actually landed next to the binary. WASM: the
// CMake --preload-file flag mounts all three at the FS root, so the
// list is fixed.
std::vector<live::SpeakerOption> resolveSpeakerOptions(const char* argv0) {
  struct Known {
    const char* file;
    const char* display;
  };
  static const Known known[] = {
      {"JD2.speaker", "JD2"},
      {"M01.speaker", "M01"},
      {"W02.speaker", "W02"},
  };
  std::vector<live::SpeakerOption> out;
#if defined(__EMSCRIPTEN__)
  (void)argv0;
  for (const Known& k : known) {
    out.push_back({k.display, std::string("/") + k.file});
  }
#else
  for (const Known& k : known) {
    std::filesystem::path p = live::findRuntimeAsset(argv0, k.file);
    if (!p.empty()) {
      out.push_back({k.display, p.string()});
    }
  }
#endif
  return out;
}

void glfwErrorCallback(int code, const char* msg) {
  std::fprintf(stderr, "glfw error %d: %s\n", code, msg);
}

#if defined(__EMSCRIPTEN__)
// Resize the canvas to match the given window dimensions and tell GLFW
// about it so ImGui's IO.DisplaySize follows. The shell.html sizes the
// canvas to 100vw/100vh via CSS, but the canvas's internal pixel
// dimensions (and the WebGL framebuffer) are independent and only change
// when we set them. We pull the CSS size in via EM_ASM rather than
// emscripten_get_element_css_size: with -pthread + -sAUDIO_WORKLET the
// generated wrapper for that API hits a "querySelector empty" runtime
// error inside its event-handler dispatch path on emscripten 5.x, so
// going through a small EM_ASM block sidesteps the whole proxy code.
void syncCanvasToViewport(GLFWwindow* window) {
  int w = 0, h = 0;
  EM_ASM({
    var c = Module.canvas || document.getElementById('canvas');
    if (!c) return;
    var r = c.getBoundingClientRect();
    HEAP32[$0 >> 2] = r.width  | 0;
    HEAP32[$1 >> 2] = r.height | 0;
  }, &w, &h);
  if (w <= 0 || h <= 0) return;
  emscripten_set_canvas_element_size("#canvas", w, h);
  glfwSetWindowSize(window, w, h);
}

EM_BOOL onBrowserResize(int /*eventType*/, const EmscriptenUiEvent* /*e*/,
                        void* userData) {
  syncCanvasToViewport(static_cast<GLFWwindow*>(userData));
  return EM_FALSE;
}
#endif

// Builds a default Controls / Vocal Tract / Vocal Tract 3D / Primary Spectrum
// layout the first time the dockspace exists. ImGui persists user-dragged
// arrangements to imgui.ini so this only fires on a fresh install.
void buildDefaultDockLayout(ImGuiID dockspace_id) {
  ImGui::DockBuilderRemoveNode(dockspace_id);
  ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);
  ImGuiID leftId, rightId;
  ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.22f, &leftId,
                              &rightId);
  ImGuiID rightTopId, rightBottomId;
  ImGui::DockBuilderSplitNode(rightId, ImGuiDir_Up, 0.55f, &rightTopId,
                              &rightBottomId);
  // Slice the top-right strip into three side-by-side cells so the 2D
  // tract, 3D tract, and vowel chart are all visible at once instead of
  // sharing a single tabbed dock.
  ImGuiID rtTract2dId, rtRest;
  ImGui::DockBuilderSplitNode(rightTopId, ImGuiDir_Left, 1.0f / 3.0f,
                              &rtTract2dId, &rtRest);
  ImGuiID rtTract3dId, rtVowelId;
  ImGui::DockBuilderSplitNode(rtRest, ImGuiDir_Left, 0.5f, &rtTract3dId,
                              &rtVowelId);
  // Split the left strip into Tract Shapes (top) and Controls (bottom).
  // Tract Shapes is button-dense and reads well in a constrained box;
  // the Controls sliders need the full panel width regardless, so the
  // bottom half gives them enough vertical room for the Articulation
  // and Glottis sections.
  ImGuiID leftTopId, leftBottomId;
  ImGui::DockBuilderSplitNode(leftId, ImGuiDir_Up, 0.5f, &leftTopId,
                              &leftBottomId);
  ImGui::DockBuilderDockWindow("Tract Shapes", leftTopId);
  ImGui::DockBuilderDockWindow("Controls", leftBottomId);
  ImGui::DockBuilderDockWindow("Vocal Tract", rtTract2dId);
  ImGui::DockBuilderDockWindow("Vocal Tract 3D", rtTract3dId);
  ImGui::DockBuilderDockWindow("Vowel Chart", rtVowelId);
  ImGui::DockBuilderDockWindow("Primary Spectrum", rightBottomId);
  ImGui::DockBuilderDockWindow("Glottal Pulse", rightBottomId);
  ImGui::DockBuilderFinish(dockspace_id);
}

void frameTick() {
  GLFWwindow* window = g_app.window;
  live::AudioEngine& engine = *g_app.engine;
  ComplexSignal& fftBuf = *g_app.fftBuf;

  glfwPollEvents();
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(
      0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
  static bool dockBuilt = false;
  static bool focusPrimary = false;
  if (!dockBuilt) {
    dockBuilt = true;
    buildDefaultDockLayout(dockspace_id);
    // Defer the focus call to after the panels have been Begin'd this
    // frame — SetWindowFocus only finds windows that already exist.
    focusPrimary = true;
  }

#if defined(__EMSCRIPTEN__)
  // Browsers refuse to start AudioContext output without a user gesture.
  // Watch for the first click or key press anywhere in the app and
  // forward it to the engine, which creates the worklet and resumes the
  // context. The call happens inside the same rAF tick as the
  // GLFW-forwarded JS event, well inside the browser's transient
  // activation window.
  if (!engine.audioContextRunning()) {
    ImGuiIO& io = ImGui::GetIO();
    bool gesture = false;
    for (int b = 0; b < IM_ARRAYSIZE(io.MouseClicked); ++b) {
      if (io.MouseClicked[b]) { gesture = true; break; }
    }
    if (!gesture && ImGui::IsKeyPressed(ImGuiKey_Space, false)) gesture = true;
    if (gesture) engine.requestAudioStart();
  }
#endif

  // Snapshot control state once per frame so the panels work against a
  // single consistent copy. The 2D panel updates uiTract -> calculateAll()
  // before the 3D panel reads from it, so they always agree.
  live::FrameSnapshot snap = live::readFrameSnapshot(engine);
  live::renderControlsPanel(engine, snap);
  // Tract shapes / speaker switcher panel. May call AudioEngine::restart
  // and re-read snap in place when the user picks a different speaker.
  live::renderTractShapesPanel(engine, snap, g_app.speakers,
                               g_app.tractShapeFont);
  // Vowel chart runs before the 2D panel so a click/drag in F1/F2 space
  // mutates snap.tractParams before the tract is recalculated and drawn.
  live::renderVowelChartPanel(engine, snap);
  live::renderVocalTract2DPanel(engine.uiTract(), snap.tractParams.data());
  live::renderVocalTract3DPanel(engine.uiTract());
  live::renderSpectrumPanel(engine.history, fftBuf, engine.uiTract(),
                           snap.f0_Hz);
  live::renderLfPulsePanel(snap.f0_Hz);
  live::writeFrameSnapshot(engine, snap);

  // Make Primary Spectrum the active tab in its dock on a fresh layout.
  // Runs after all panels' Begin so SetWindowFocus can find the window.
  if (focusPrimary) {
    focusPrimary = false;
    ImGui::SetWindowFocus("Primary Spectrum");
  }

#if defined(__EMSCRIPTEN__)
  // Until the user has clicked once, the AudioContext is suspended and
  // the worklet has not yet been created. Show a centered banner so it's
  // obvious why the app is silent — clicking anywhere dismisses it.
  if (!engine.audioContextRunning()) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImVec2 center(vp->Pos.x + vp->Size.x * 0.5f,
                  vp->Pos.y + vp->Size.y * 0.5f);
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.85f);
    ImGuiWindowFlags overlayFlags = ImGuiWindowFlags_NoDecoration |
                                    ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_NoSavedSettings |
                                    ImGuiWindowFlags_NoFocusOnAppearing |
                                    ImGuiWindowFlags_NoNav |
                                    ImGuiWindowFlags_AlwaysAutoResize;
    if (ImGui::Begin("##audio_start_overlay", nullptr, overlayFlags)) {
      ImGui::TextUnformatted("Click anywhere to start audio");
    }
    ImGui::End();
  }
#endif

  ImGui::Render();
  int displayW, displayH;
  glfwGetFramebufferSize(window, &displayW, &displayH);
  glViewport(0, 0, displayW, displayH);
  // Clear to the active ImGui WindowBg so the area behind the dockspace
  // (visible briefly while panels are dragged) blends with the theme.
  ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
  glClearColor(bg.x, bg.y, bg.z, bg.w);
  glClear(GL_COLOR_BUFFER_BIT);
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  glfwSwapBuffers(window);
}

}  // namespace

int main(int argc, char** argv) {
#if defined(__EMSCRIPTEN__)
  // Emscripten preloads files into a virtual filesystem at the path passed
  // through --preload-file; the CMake target maps the speaker file to "/".
  std::filesystem::path speakerPath = "/JD2.speaker";
#else
  std::filesystem::path speakerPath =
      live::findSpeakerFile(argc > 0 ? argv[0] : "vtl_live");
  if (speakerPath.empty()) {
    std::fprintf(stderr,
                 "live: could not locate JD2.speaker next to the binary\n");
    return 1;
  }
#endif

  glfwSetErrorCallback(glfwErrorCallback);
  if (!glfwInit()) return 1;

#if defined(__EMSCRIPTEN__)
  // WebGL2 maps to GLES 3.0 in browsers; ImGui's opengl3 backend supports
  // both desktop GL and GLES, the GLSL preamble just has to match.
  glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
  const char* glslVersion = "#version 300 es";
#elif defined(__APPLE__)
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
#if !defined(__EMSCRIPTEN__)
  // GLFW's emscripten port routes glfwSwapInterval to
  // emscripten_set_main_loop_timing, which we haven't set up yet (that
  // happens later via emscripten_set_main_loop). It logs a noisy "Cannot
  // set timing mode for main loop since a main loop does not exist!"
  // warning and is otherwise a no-op — the browser drives rendering via
  // requestAnimationFrame either way.
  glfwSwapInterval(1);
#endif

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  // Dark by default; the panels read colors back through GetColorU32 so
  // switching to StyleColorsLight()/StyleColorsClassic() works too.
  ImGui::StyleColorsDark();

  // Replace the default Proggy Clean (ASCII-only) with Noto Sans so the
  // IPA glyphs in the vowel chart actually render. JetBrains Mono and
  // most other popular monospace fonts only cover ASCII + Latin
  // Extended; Noto Sans is the only widely-available font we found with
  // full 96/96 IPA Extensions coverage at a reasonable size. The build
  // copies the .ttf next to the binary on native and preloads it at
  // "/NotoSans-Regular.ttf" on the web, so a single hardcoded name
  // covers both.
  {
#if defined(__EMSCRIPTEN__)
    std::filesystem::path fontPath = "/NotoSans-Regular.ttf";
#else
    std::filesystem::path fontPath = live::findRuntimeAsset(
        argc > 0 ? argv[0] : "vtl_live", "NotoSans-Regular.ttf");
#endif
    // Basic Latin + Latin-1 (covers æ U+00E6) + Latin Extended-A/B + IPA
    // Extensions (ɪ ɛ ɑ ɔ ʌ ɜ ʊ in VowelChartPanel) + Combining
    // Diacritical Marks (raised/lowered marks under ɐ for the
    // 6_mid / 6_low IPA renders in the Tract Shapes panel).
    static const ImWchar ranges[] = {
        0x0020, 0x00FF, 0x0100, 0x024F, 0x0250, 0x02AF, 0x0300, 0x036F, 0,
    };
    if (!fontPath.empty()) {
      io.Fonts->AddFontFromFileTTF(fontPath.string().c_str(), 16.0f, nullptr,
                                   ranges);
      // Larger re-rasterization of the same font for the Tract Shapes
      // buttons. Re-rasterizing (rather than ImGui::SetWindowFontScale)
      // keeps the IPA glyphs crisp at the bigger size.
      g_app.tractShapeFont = io.Fonts->AddFontFromFileTTF(
          fontPath.string().c_str(), 22.0f, nullptr, ranges);
    }
    if (io.Fonts->Fonts.empty()) {
      // Font missing on disk — keep the default so the app still launches.
      io.Fonts->AddFontDefault();
    }
  }

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init(glslVersion);

  static live::AudioEngine engine;
  if (!engine.start(speakerPath.string())) {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 1;
  }

  static ComplexSignal fftBuf(live::FFT_LEN);

  g_app.window = window;
  g_app.engine = &engine;
  g_app.fftBuf = &fftBuf;
  g_app.speakers = resolveSpeakerOptions(argc > 0 ? argv[0] : "vtl_live");

#if defined(__EMSCRIPTEN__)
  // The shell stretches the canvas across the viewport via CSS, but glfwCreateWindow
  // hard-set the backing pixel dimensions to 1280x800. Resize once now and
  // again on every browser resize so ImGui follows the viewport.
  syncCanvasToViewport(window);
  emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, window,
                                 EM_FALSE, onBrowserResize);
  // emscripten owns the event loop in the browser; this returns and the
  // page keeps spinning frameTick at the browser's rAF cadence.
  emscripten_set_main_loop(frameTick, 0, 1);
  return 0;
#else
  while (!glfwWindowShouldClose(window)) {
    frameTick();
  }

  engine.stop();

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
#endif
}
