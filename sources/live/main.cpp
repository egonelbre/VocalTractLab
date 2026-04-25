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
//   SpectrumPanel        — FFT-based primary spectrum
//
// AudioEngine wraps the synthesizer + OpenAL queue and runs either on a
// background thread (native) or pumped from this main loop (Emscripten).
// ****************************************************************************

#include <cstdio>
#include <filesystem>

#include "AssetPaths.h"
#include "AudioEngine.h"
#include "ControlsPanel.h"
#include "SpectrumPanel.h"
#include "VocalTract2DPanel.h"
#include "VocalTract3DPanel.h"
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
};
LabApp g_app;

void glfwErrorCallback(int code, const char* msg) {
  std::fprintf(stderr, "glfw error %d: %s\n", code, msg);
}

// Builds a default Controls / Vocal Tract / Vocal Tract 3D / Primary Spectrum
// layout the first time the dockspace exists. ImGui persists user-dragged
// arrangements to imgui.ini so this only fires on a fresh install.
void buildDefaultDockLayout(ImGuiID dockspace_id) {
  ImGui::DockBuilderRemoveNode(dockspace_id);
  ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);
  ImGuiID leftId, rightId;
  ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.32f, &leftId,
                              &rightId);
  ImGuiID rightTopId, rightBottomId;
  ImGui::DockBuilderSplitNode(rightId, ImGuiDir_Up, 0.65f, &rightTopId,
                              &rightBottomId);
  ImGui::DockBuilderDockWindow("Controls", leftId);
  ImGui::DockBuilderDockWindow("Vocal Tract", rightTopId);
  ImGui::DockBuilderDockWindow("Vocal Tract 3D", rightTopId);
  ImGui::DockBuilderDockWindow("Primary Spectrum", rightBottomId);
  ImGui::DockBuilderFinish(dockspace_id);
}

void frameTick() {
  GLFWwindow* window = g_app.window;
  live::AudioEngine& engine = *g_app.engine;
  ComplexSignal& fftBuf = *g_app.fftBuf;

#if defined(__EMSCRIPTEN__)
  // No background audio thread on the web; keep the OpenAL queue topped up
  // here. 8 chunks × 10 ms = 80 ms of headroom, plenty for a 60 fps main
  // loop even when the browser stalls a frame.
  engine.pumpMainThread(8);
#endif

  glfwPollEvents();
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(
      0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
  static bool dockBuilt = false;
  if (!dockBuilt) {
    dockBuilt = true;
    buildDefaultDockLayout(dockspace_id);
  }

  // Snapshot control state once per frame so the panels work against a
  // single consistent copy. The 2D panel updates uiTract -> calculateAll()
  // before the 3D panel reads from it, so they always agree.
  live::FrameSnapshot snap = live::readFrameSnapshot(engine);
  live::renderControlsPanel(engine, snap);
  live::renderVocalTract2DPanel(engine.uiTract(), snap.tractParams.data());
  live::renderVocalTract3DPanel(engine.uiTract());
  live::renderSpectrumPanel(engine.history, fftBuf);
  live::writeFrameSnapshot(engine, snap);

  ImGui::Render();
  int displayW, displayH;
  glfwGetFramebufferSize(window, &displayW, &displayH);
  glViewport(0, 0, displayW, displayH);
  glClearColor(0.93f, 0.93f, 0.95f, 1.0f);
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
  glfwSwapInterval(1);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  ImGui::StyleColorsLight();
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

#if defined(__EMSCRIPTEN__)
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
