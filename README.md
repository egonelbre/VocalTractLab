# VocalTractLab (unofficial fork)

This is an **unofficial** fork of [VocalTractLab](https://www.vocaltractlab.de/),
an articulatory speech synthesizer developed by **Peter Birkholz** (TU Dresden).
Starting from the VTL 2.4 release, the source tree has been reorganized into
layered modules and a CMake/vcpkg build that targets macOS, Linux, and Windows
from a single description.

For documentation, papers, and the official Windows binaries, please refer to
the upstream site: <https://www.vocaltractlab.de/>.

## Layout

- `sources/` — C++ source tree.
  - `sources/vtl/` — the synthesizer library (upstream "Backend"), grouped
    into `api/`, `core/`, `dsp/`, `io/`, `phonetics/`, `anatomy/`, `glottis/`,
    `acoustics/`, `synthesis/`, and `analysis/`.
  - `sources/gui/` — the wxWidgets app (upstream "Frontend"), grouped into
    `app/`, `pages/`, `dialogs/`, `pictures/`, `graphing/`, `util/`, and
    `windows/` (Windows resource compilation inputs).
- `data/` — runtime data the GUI loads at startup: `speakers/`, `config.ini`,
  the `example01.*` set, vowel outline GIFs, and a `batch/` directory of
  segment files for the batch tools.
- `examples/` — API client demos. `matlab/` and `python/` show how to call
  the `VocalTractLabApi` shared library.
- `docs/` — manual and other documentation.
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
- Linux / Windows: `VocalTractLab2` executable. Copy the contents of
  `data/` (speakers, `config.ini`, example files, and the `batch/`
  directory renamed to `Examples/`) next to the binary so it can find
  `JD2.speaker`, `config.ini`, etc. at runtime.

## License

VocalTractLab is licensed under the GNU General Public License v3.0 — see
[`LICENSE`](LICENSE). All credit for the underlying
synthesizer goes to Peter Birkholz and the contributors listed in the source
file headers.
