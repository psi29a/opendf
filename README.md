# OpenDF

An open-source engine for Bethesda's *The Elder Scrolls II: Daggerfall*.

OpenDF ships no game assets. You need your own copy of the original game data
-- Daggerfall is available for free from Bethesda, and the launcher will point
you at it and install it for you.

![Image of OpenDF in Buccaneer's Den](https://forum.openmw.org/download/file.php?id=628&mode=view)

See [docs/ROADMAP.md](docs/ROADMAP.md) for where the project is going.

## Building

OpenDF is built with CMake and a C++17 toolchain.

### Dependencies

Build tools:

* CMake >= 3.16
* A C++17 compiler (GCC 7+, Clang 5+, or MSVC 2017+)
* Ninja (the generator used throughout this README; any CMake generator works)

Libraries:

* OpenSceneGraph -- rendering (osgDB, osgViewer, osgGA, osgUtil)
* MyGUI          -- in-game GUI and console
* SDL2           -- windowing and input
* OpenGL
* Qt 6 >= 6.2    -- the launcher only; skipped automatically when absent

### Installing dependencies

Linux (Debian/Ubuntu):

    sudo apt-get install -y cmake ninja-build build-essential \
        libopenscenegraph-dev libmygui-dev libsdl2-dev \
        libgl1-mesa-dev qt6-base-dev

macOS (Homebrew):

    brew install cmake ninja open-scene-graph sdl2-compat qt freetype

Two caveats on macOS. Homebrew has no `mygui` formula, so MyGUI has to be
built from source and pointed at with `MYGUI_HOME`; opendf supplies its own
OSG-backed render manager, so the engine alone is enough:

    git clone --depth 1 --branch MyGUI3.4.4 https://github.com/MyGUI/mygui.git
    cmake -S mygui -B mygui/build -DCMAKE_INSTALL_PREFIX=$HOME/mygui-install \
        -DMYGUI_DONT_USE_OBSOLETE=ON \
        -DMYGUI_RENDERSYSTEM=1 -DMYGUI_BUILD_DEMOS=OFF -DMYGUI_BUILD_TOOLS=OFF \
        -DMYGUI_BUILD_PLUGINS=OFF -DMYGUI_BUILD_DOCS=OFF
    cmake --build mygui/build --target install
    export MYGUI_HOME=$HOME/mygui-install

`MYGUI_DONT_USE_OBSOLETE=ON` there is required, not tidiness: 3.4.4 defaults it
to `FALSE` and installs no `MyGUI_Config.h`, so opendf's `AUTO` detection treats
it as a pre-3.5 MyGUI and defines the macro. Building the library without it
would leave the two disagreeing about every widget's layout.

And there is no `sdl2` formula any more: `sdl2-compat` replaces it, providing
the same `sdl2.pc` and SDL2 headers on top of SDL3.

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

This produces the engine (`opendf`) and a BSA archive tool (`bsatool`) in
`build/`, plus the launcher (`opendf-launcher`) when Qt6 was found -- see
`BUILD_LAUNCHER` below.

Useful CMake options (defaults in parentheses):

    -DBUILD_LAUNCHER=AUTO|ON|OFF build the Qt6 launcher (AUTO)
    -DMYGUI_DONT_USE_OBSOLETE=AUTO|ON|OFF  match how MyGUI was built (AUTO)

`BUILD_LAUNCHER=AUTO` builds the launcher if Qt6 6.2+ is installed and skips it
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

Building the launcher also builds `innoextract`, which it uses to unpack
DaggerfallSetup for "install from archive". This is not optional: every
released innoextract (1.9, 2020) stops at Inno Setup 6.0.5, while current
DaggerfallSetup builds are made with 6.6.x, so a system innoextract simply
refuses the file. It is fetched from a fork carrying 6.4-6.6 support, pinned
to a reviewed commit, and pulls in Boost, liblzma and iconv. Once that support
is released upstream and distros carry it, this goes away.

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
