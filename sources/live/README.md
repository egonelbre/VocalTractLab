# vtl_live

ImGui-based realtime experimentation app for the VocalTractLab synthesizer.
Independent of the wxWidgets GUI; runs the synthesizer at 48 kHz with the
articulation, glottis, and F0 driven from sliders and from drag handles on a
2D mediosagittal view.

## Native build (macOS / Linux)

The live build is built as part of the regular CMake configure step:

```sh
cmake --preset mac-debug
cmake --build --preset mac-debug --target vtl_live
```

Artifact: `build/mac-debug/vtl_live.app` (macOS bundle) or `build/<preset>/vtl_live`.

## WebAssembly build (Emscripten)

The same source tree compiles for the browser through Emscripten. There is no
vcpkg involvement on this path: GLFW and OpenAL come from Emscripten's
built-in ports and ImGui (docking branch) is fetched at configure time via
`FetchContent`. The native-only targets (wxWidgets GUI, benchmarks, regression
tests, the C API shared library) are skipped automatically.

```sh
# One-time: install / activate the Emscripten SDK.
git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
~/emsdk/emsdk install latest
~/emsdk/emsdk activate latest
source ~/emsdk/emsdk_env.sh   # exports EMSDK + puts emcc on PATH

cmake --preset wasm
cmake --build --preset wasm
```

Artifacts land in `build/wasm/`:

- `vtl_live.html` — the page Emscripten generates as a launcher
- `vtl_live.js` — JS glue
- `vtl_live.wasm` — the compiled module
- `vtl_live.data` — the preloaded virtual filesystem (contains `JD2.speaker`)

To run:

```sh
cd build/wasm
python3 -m http.server 8000
# open http://localhost:8000/vtl_live.html
```

Browsers gate audio playback on a user gesture, so press a slider or click
once to unmute the OpenAL output.

### What changes for the browser build

- The audio thread is replaced by `AudioEngine::pumpMainThread()`, which the
  main loop calls every frame. OpenAL still queues + plays the same chunks;
  it just runs cooperatively now.
- The main loop is driven by `emscripten_set_main_loop()` instead of a
  blocking `while (!glfwWindowShouldClose(...))`.
- The speaker file is preloaded into Emscripten's MEMFS at `/JD2.speaker`
  via `--preload-file`.
- GLSL preamble switches to `#version 300 es` to match WebGL2.
