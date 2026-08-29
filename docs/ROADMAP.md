# OpenDF Roadmap

OpenDF is an open source reimplementation of the engine behind Bethesda's
*The Elder Scrolls II: Daggerfall*. It ships no game assets -- you need your
own copy of the original game data.

This document tracks where the project is going. It is a living document;
nothing here is a promise or a schedule.

## Prior art

[Daggerfall Unity](https://github.com/Interkarma/daggerfall-unity) is the
reference point for what a complete Daggerfall reimplementation looks like. It
is a finished, playable engine, and its issue tracker, feature list and notes
on the original game's file formats and quirks are the best available map of
what actually has to be built -- much of Daggerfall's behaviour is only
documented by someone having reimplemented it once already.

OpenDF is not a port of it and shares no code: Daggerfall Unity is C# on Unity,
OpenDF is C++ on OpenSceneGraph. It is worth reading before starting any of the
work below, both for the format details and for the order it tackled things in.

## Where we are

The engine boots, loads the game's BSA archives through a virtual filesystem,
renders dungeon and exterior blocks through a deferred OSG pipeline, and puts
a MyGUI console and status overlay on top. Privateer's Hold -- the starting
dungeon -- loads and renders, and you can walk around it.

That makes OpenDF a working renderer and world loader, not yet a game: there
is no character, no combat, no inventory, no save/load, and no quest system.

## Milestone 1: clear the existing TODOs

Before adding anything new, deal with what the code already admits is
unfinished. These are all marked in-tree, and each is small and self-contained
-- the point of doing them first is that several of them quietly limit what
can be built on top.

**Texture loading -- RLE compression is unimplemented.**
`src/components/dfosg/texloader.cpp` handles uncompressed textures only. Three
compression types (`RleCompressed`, `ImageRle`, `RecordRle`) print
`Unhandled ... compression type` to stderr and fall back to a dummy image, in
both the single-frame and multi-frame paths. Any art stored in those formats
is simply missing today. This is the highest-value item on the list: it is a
straight decoder implementation with a well-documented format, and it makes
otherwise-invisible content appear.

**Dungeon actions -- unhandled action types.**
`src/opendf/world/dblocks.cpp` logs `Unhandled action type: 0x..` for action
records it does not recognise, and routes them to `actions/unknown.cpp`, which
logs the raw payload and does nothing. This fires during normal play in
Privateer's Hold. Each action type is a distinct behaviour (doors, teleports,
triggers, traps), so working through them is what turns a walkable dungeon
into an interactive one.

**Sky rendering.**
`src/opendf/render/pipeline.cpp` clears the G-buffer every frame with a note
that this should stop once sky rendering exists. A skybox loader was started
(see commit `f67ebee`) but is not wired into the pipeline. Finishing it also
removes a per-frame full-screen clear.

**VFS archives are not Archives.**
`src/components/vfs/manager.cpp` keeps loose root paths, a list of archives,
and two special-cased BSA archives (`ARCH3D.BSA`, `DAGGER.SND`) addressed by
numeric ID rather than name, with a `FIXME` saying these should all be
`Archive` implementations behind one interface. Unifying them makes lookup
rules consistent and gets rid of the special cases.

**MyGUI backend gaps.**
`src/components/mygui_osg/texture.cpp` has two: render-to-texture is not
implemented (`getRenderTarget()` returns `nullptr`), which blocks any GUI
feature needing an offscreen surface; and `GL_ALPHA` images are reinterpreted
as luminance, which is a guess that should be confirmed against what MyGUI
actually wants.

**Windowing odds and ends.**
`setScreenSettings()` in `src/components/sdlutil/graphicswindow.cpp` returns
`false` -- resolution changes have to go through an appropriately-sized
fullscreen window rather than a raw mode set. `src/opendf/engine.cpp` has a
`SDL_WINDOWEVENT_CLOSE` handler that does nothing on the assumption that
`SDL_QUIT` follows anyway, and logs unhandled window events to stderr. Minor,
but they are the difference between "works on my machine" and "behaves".

**Terrain tile 0xff.**
`src/components/resource/texturemanager.cpp` maps texture ID `0xff` to 0 with a
`TODO` asking whether it actually means "procedurally texture this tile".
Worth answering before terrain work starts in earnest.

## Beyond that

Rough order of interest, deliberately vague -- these are directions, not
commitments:

* **A player.** Collision, gravity, and a controllable camera with real
  movement rules, rather than a free-flying viewpoint.
* **The rest of the world.** Exteriors, towns and the overworld travel map;
  the terrain tileset path already exists but is only exercised outdoors.
* **Game systems.** Character creation, stats, inventory, combat, magic --
  the actual game on top of the engine.
* **Save/load**, ideally compatible with the original save format.
* **Sound and music.** `DAGGER.SND` is already opened by the VFS but nothing
  plays it yet.
* **Packaging.** Getting a build into people's hands on all three platforms
  without them having to compile it.

## Non-goals

* Shipping game assets. OpenDF requires an existing Daggerfall installation.
  Daggerfall is available for free from Bethesda, and the launcher points you
  at the download and installs the game data from the archive you get.
* Being a Daggerfall *mod loader* or a remaster. The aim is to run the
  original game faithfully first.
