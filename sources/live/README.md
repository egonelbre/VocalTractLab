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
emrun --no-browser --port 8000 vtl_live.html  # then visit http://localhost:8000/vtl_live.html
```

`emrun` (bundled with emsdk) serves the page with the `Cross-Origin-Opener-
Policy: same-origin` and `Cross-Origin-Embedder-Policy: require-corp`
headers, which the browser requires to enable `SharedArrayBuffer` — without
those headers the audio worklet thread cannot start. The plain
`python3 -m http.server` does not set the headers; if you prefer a custom
server, configure it to send both COOP and COEP on the HTML / JS / WASM
responses.

Browsers gate audio playback on a user gesture, so click once on the
"Click anywhere to start audio" overlay (or any control) to create the
AudioContext and start the worklet.

> **Pitfall:** opening Chrome DevTools while audio is playing makes the
> worklet thread miss its 2.6 ms-per-quantum deadline (DevTools instruments
> every wasm call). Chrome silently drops the processor after a few missed
> quanta and the audio cuts to silence. Listen with DevTools closed; reopen
> it to inspect the UI / sliders only when you don't need to hear the
> output. The same effect cascades when `-sASSERTIONS=1` /
> `-sSTACK_OVERFLOW_CHECK=2` are also enabled — those are off by default
> on the wasm live target for that reason; flip them on with
> `-DVTL_LIVE_WASM_ASSERTIONS=ON` only when you're chasing a crash.

### What changes for the browser build

- Synthesis runs on a Web Audio AudioWorklet thread (a browser-managed
  realtime audio thread), not on the UI thread. A long-running ImGui
  frame, GC pause, or browser stall no longer drops audio.
- The OpenAL backend is bypassed on the live build; the worklet's
  process callback writes float samples straight into the Web Audio
  graph. (The vtl synthesis code itself still pulls in OpenAL through
  the upstream `io/SoundLib.cpp` for compile-time compatibility.)
- The main loop is driven by `emscripten_set_main_loop()` instead of a
  blocking `while (!glfwWindowShouldClose(...))`.
- The speaker files are preloaded into Emscripten's MEMFS at
  `/JD2.speaker`, `/M01.speaker`, `/W02.speaker` via `--preload-file`.
- GLSL preamble switches to `#version 300 es` to match WebGL2.
- Build flags include `-pthread`, `-sAUDIO_WORKLET=1`, and
  `-sWASM_WORKERS=1` — together they give us atomics, shared memory,
  and emscripten's wasm audio worklet runtime.
