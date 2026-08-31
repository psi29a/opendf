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

Two notes on macOS. Homebrew has no `mygui` formula, and none is needed:
when MyGUI isn't found, the build fetches and builds MyGUI 3.4.4 in-tree
automatically -- engine only, configured to match what opendf expects. See
`OPENDF_FETCH_MYGUI` below to control that.

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

    -DBUILD_LAUNCHER=AUTO|ON|OFF           build the Qt6 launcher (AUTO)
    -DOPENDF_FETCH_MYGUI=AUTO|ON|OFF       build MyGUI in-tree (AUTO)
    -DMYGUI_DONT_USE_OBSOLETE=AUTO|ON|OFF  match how MyGUI was built (AUTO)

`BUILD_LAUNCHER=AUTO` builds the launcher if Qt6 6.2+ is installed and skips it
otherwise, so an engine-only build needs no Qt6. Pass `ON` to require it (the
build then fails if Qt6 is missing) or `OFF` to never build it.

`OPENDF_FETCH_MYGUI=AUTO` uses a system MyGUI when `find_package` locates one
and otherwise fetches and builds MyGUI 3.4.4 in-tree, which is what happens on
macOS. `ON` always builds in-tree without looking for a system one; `OFF`
requires a system MyGUI and fails if it is absent.

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

    -data <path>       add a data path (may be given more than once)
    -log <file>        write the log to <file> instead of the default
    -devparm           enable debug-level logging
    -set <cvar> <val>  override a config variable for this run only

`-set` wins over the config file and is never written back to it, so it is
safe for one-off and scripted runs.

The log goes next to the config files -- `$XDG_CONFIG_HOME/opendf/opendf.log`
(`%AppData%\opendf\opendf.log` on Windows), falling back to
`~/.config/opendf/` -- rather than into whatever directory you started the
engine from. `-log` overrides it and is taken exactly as given, so a relative
path there is still relative to the working directory.

### Lighting

Dungeon lighting follows Daggerfall Unity's values; `docs/ROADMAP.md` records
which numbers come from where. Three cvars tune it, as percentages:

    r_ambientscale     100   ambient light
    r_torchscale       100   the player's torch
    r_lightintensity    80   the lights placed in the dungeon

Change them from the console and type `relight` to apply without reloading, or
pass them with `-set` for a single run.

### Screenshots

Press **F12**, or type `screenshot [path]` in the console.

For non-interactive use -- checking what the renderer actually produced
without a window to look at -- `r_shotframe` renders that many frames, writes
one PNG, and quits:

    ./build/opendf -set r_shotframe 4 -set r_shotfile /tmp/shot

That writes `/tmp/shot_0_0.png` and exits. `r_shotframe = 0` (the default)
disables it entirely.

## License

GPLv3 -- see [LICENSE.txt](LICENSE.txt).
