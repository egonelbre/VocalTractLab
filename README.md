# VocalTractLab (unofficial fork)

This is an **unofficial** fork of [VocalTractLab](https://www.vocaltractlab.de/),
an articulatory speech synthesizer developed by **Peter Birkholz** (TU Dresden).
The upstream VTL 2.4 release is preserved under [`official/`](official/);
fixes and a CMake/vcpkg build live at the repo root so the GUI can be built on
macOS and Linux alongside the existing Windows Visual Studio project.

For documentation, papers, and the official Windows binaries, please refer to
the upstream site: <https://www.vocaltractlab.de/>.

## Layout

- `sources/` — C++ source tree (reorganized from the upstream `Sources/` flat
  layout). Two top-level modules:
  - `sources/vtl/` — the synthesizer library (the upstream "Backend"), grouped
    into `api/`, `core/`, `dsp/`, `io/`, `phonetics/`, `anatomy/`, `glottis/`,
    `acoustics/`, `synthesis/`, and `analysis/` layers.
  - `sources/gui/` — the wxWidgets app (the upstream "Frontend"), grouped into
    `app/`, `pages/`, `dialogs/`, `pictures/`, `graphing/`, and `util/`.
- `official/` — upstream VTL 2.4 non-source assets: speakers, examples,
  license, manual, and the legacy Visual Studio `.sln`/`.vcxproj` project
  files (now pointing at the moved `sources/`).
- `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json` — cross-platform build
  for the `VocalTractLab2` GUI executable and the `VocalTractLabApi` shared
  library.
- `third_party/vcpkg` — vendored vcpkg submodule that supplies wxWidgets and
  (on non-Windows) openal-soft.

## Fixes on top of VTL 2.4

The fork applies a small set of compatibility patches needed to build the GUI
with a modern C++17 toolchain and wxWidgets 3.3.

See `git log` for individual commits and rationale.

## Building

### Prerequisites

- CMake ≥ 3.25 and Ninja (or Visual Studio 2022 on Windows).
- A C++17 compiler (clang on macOS, GCC on Linux, MSVC on Windows).
- macOS / Linux: standard development tools (Xcode command line tools or
  build-essential). vcpkg will fetch wxWidgets and openal-soft.
- Linux: the system packages vcpkg's wxWidgets port needs (X11, GTK, GL, etc.).

### One-time setup

```sh
git clone --recursive https://github.com/egonelbre/VocalTractLab.git
cd VocalTractLab
./third_party/vcpkg/bootstrap-vcpkg.sh   # Windows: bootstrap-vcpkg.bat
```

If you already cloned without `--recursive`:

```sh
git submodule update --init --recursive
```

### Configure and build

Pick the preset that matches your platform:

```sh
cmake --preset mac-debug          # or mac-release / linux-debug / win-debug / win-release
cmake --build --preset mac-debug
```

The first configure step will build vcpkg's dependencies (wxWidgets, and
openal-soft on non-Windows); subsequent runs reuse the cached install tree
under `vcpkg_installed/`.

The build output lands in `build/<preset>/`:

- macOS: `VocalTractLab2.app` with speaker files, `config.ini`,
  `example01.*`, vowel outline GIFs, and `Examples/` bundled into
  `Contents/Resources/`.
- Linux / Windows: `VocalTractLab2` executable. Run it from
  `official/GUI/` (or copy the speaker/config files next to the binary) so
  it can find `JD2.speaker`, `config.ini`, etc.

### Windows

The original `GUI/Developer/VocalTractLab2.sln` still works for Windows
developers and remains the upstream-sanctioned build path. The CMake build
is provided as a portable alternative.

## License

VocalTractLab is licensed under the GNU General Public License v3.0 — see
[`official/license.txt`](official/license.txt). All credit for the underlying
synthesizer goes to Peter Birkholz and the contributors listed in the source
file headers.
