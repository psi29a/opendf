# OpenDF

An open source engine for Bethesda's *The Elder Scrolls II: Daggerfall*.

OpenDF ships no game assets. You need your own copy of the original game data
-- Daggerfall is available for free from Bethesda, and the launcher will point
you at it and install it for you.

![Image of OpenDF in Buccaneer's Den](https://forum.openmw.org/download/file.php?id=628&mode=view)

See [docs/ROADMAP.md](docs/ROADMAP.md) for where the project is going.

## Building

OpenDF is built with CMake and a C++17 toolchain.

### Dependencies

Build tools:

* CMake >= 3.10
* A C++17 compiler (GCC 7+, Clang 5+, or MSVC 2017+)
* Ninja (the generator used throughout this README; any CMake generator works)

Libraries:

* OpenSceneGraph -- rendering (osgDB, osgViewer, osgGA, osgUtil)
* MyGUI          -- in-game GUI and console
* SDL2           -- windowing and input
* OpenGL
* Qt 6 >= 6.5    -- the launcher only; skipped automatically when absent

### Installing dependencies

Linux (Debian/Ubuntu):

    sudo apt-get install -y cmake ninja-build build-essential \
        libopenscenegraph-dev libmygui-dev libsdl2-dev \
        libgl1-mesa-dev qt6-base-dev

macOS (Homebrew):

    brew install cmake ninja open-scene-graph mygui sdl2 qt6

Windows (vcpkg):

    git clone https://github.com/microsoft/vcpkg.git
    .\vcpkg\bootstrap-vcpkg.bat
    .\vcpkg\vcpkg.exe install osg mygui sdl2 qtbase --triplet x64-windows

### Configure and build

Linux / macOS:

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build

Windows (from a Developer Command Prompt, using the vcpkg toolchain):

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
        -DCMAKE_TOOLCHAIN_FILE=<path-to-vcpkg>\scripts\buildsystems\vcpkg.cmake
    cmake --build build

This produces three binaries in `build/`: the engine (`opendf`), the launcher
(`opendf-launcher`) and a BSA archive tool (`bsatool`).

Useful CMake options (defaults in parentheses):

    -DBUILD_LAUNCHER=AUTO|ON|OFF build the Qt6 launcher (AUTO)
    -DBUILD_INNOEXTRACT=ON|OFF   build a bundled innoextract (OFF)
    -DMYGUI_DONT_USE_OBSOLETE=AUTO|ON|OFF  match how MyGUI was built (AUTO)

`BUILD_LAUNCHER=AUTO` builds the launcher if Qt6 6.5 is installed and skips it
otherwise, so an engine-only build needs no Qt6. Pass `ON` to require it (the
build then fails if Qt6 is missing) or `OFF` to never build it.

`MYGUI_DONT_USE_OBSOLETE` has to agree with the `MYGUI_DONT_USE_OBSOLETE` the
MyGUI you link against was built with -- it changes the layout of every widget,
and a mismatch corrupts memory rather than failing to build.

`AUTO` handles both eras of MyGUI. From 3.5 on, MyGUI installs a generated
`MyGUI_Config.h` recording how it was built and includes it from
`MyGUI_Prerequest.h`, so the headers already agree with the library and we
define nothing -- passing `-DMYGUI_DONT_USE_OBSOLETE` there would *override*
that header and cause the very mismatch it prevents. Older MyGUI records
nothing, so `AUTO` falls back to defining it, which matches the Debian/Ubuntu
packages. Force `ON`/`OFF` only for a pre-3.5 MyGUI built some other way.

`BUILD_INNOEXTRACT` is for the launcher's "install from archive" support. The
launcher shells out to `innoextract` to unpack DaggerfallSetup, but every
released innoextract (1.9, 2020) is too old to read the Inno Setup version it
now ships with; turning this on builds a patched fork alongside the launcher.
It is off by default because it pulls in Boost, liblzma and iconv. Without it
the launcher still uses an `innoextract` on `PATH`, and falls back to Wine.

Tip: to flip an option on an existing build directory, re-run cmake on it
(e.g. `cmake build -DBUILD_LAUNCHER=OFF`) or delete `build/CMakeCache.txt`.

## Running

The easiest path is the launcher:

    ./build/opendf-launcher

It points you at where to get Daggerfall, installs the game data from the
archive you downloaded, lets you pick the data folder and a few options, and
writes the config files for you before starting the engine.

To run the engine directly, it needs to know where Daggerfall's `ARENA2`
folder is. Put that in `settings.cfg`:

    data-root = /path/to/daggerfall/ARENA2

The engine looks for `settings.cfg` in the usual per-platform config
directories and in the working directory; it will tell you where to create one
if it cannot find a `data-root`. Then:

    ./build/opendf

Command line options:

    -data <path>    add a data path (may be given more than once)
    -log <file>     write the log to <file>
    -devparm        enable debug-level logging

## License

GPLv3 -- see [LICENSE.txt](LICENSE.txt).
