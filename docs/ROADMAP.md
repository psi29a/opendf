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

## Under consideration: moving the renderer to VSG

Not scheduled, and deliberately so -- but worth writing down, because the
question keeps coming up and the answer has changed.

OpenSceneGraph is effectively unmaintained. That is not an abstract worry:
two of the bugs fixed in this tree are OSG's, not ours -- it sizes a texture's
mipmap chain from power-of-two-rounded dimensions while allocating storage at
the true size (every non-power-of-two texture rendered black), and its
windowing lookup returns the first registered interface rather than the one
matching the traits (we opened a second, empty window). Both needed
workarounds rather than fixes. There will be more.

[VulkanSceneGraph](https://github.com/vsg-dev/VulkanSceneGraph) is the
successor in spirit, by the same author, Vulkan-native and at a stable
released API (1.1.x).

**Supporting both is not on the table.** OSG is OpenGL and VSG is Vulkan;
they are not two backends behind one interface. Doing both means two
renderers, two shader sets and two GUI backends, for a project that is not yet
a game. If the renderer moves, it moves.

What a migration would actually cost, in this tree:

* *Cheap, mechanical.* `osg::Vec*`/`Matrix`/`Quat` (~194 uses) and
  `osg::ref_ptr` (~139) have direct VSG equivalents with the same
  intrusive-refcount idiom.
* *Cheap, mostly deletion.* The GUI backend. MyGUI 3.5 ships a Vulkan
  platform (`MYGUI_RENDERSYSTEM=10`) that initialises from raw handles --
  `VkInstance`, `VkPhysicalDevice`, `VkDevice`, queue family, `VkQueue` --
  and VSG exposes exactly those via `Instance::vk()`, `PhysicalDevice::vk()`,
  `Device::vk()` and `Queue::vk()`. Our own `src/components/mygui_osg/`
  (~990 lines) would largely be deleted rather than ported. Note it is a
  plain Vulkan platform, not a VSG-specific one, so it is not a bet on VSG.
* *Cheap.* Windowing. SDL already creates Vulkan surfaces
  (`SDL_Vulkan_CreateSurface`), in SDL2 as well as SDL3, so much of
  `graphicswindow.cpp` (318 lines) stops being needed. Moving to SDL3 is a
  separate decision on its own merits -- Vulkan support is not a reason for
  it.
* *Medium.* Scene assembly in `world/`, `class/` and `actions/`:
  Group/Transform/Geometry map over reasonably.
* **Expensive, and the real cost.** `src/opendf/render/pipeline.cpp` (364
  lines) hand-builds a deferred G-buffer out of render-to-texture cameras
  with explicit GL formats and render ordering; under VSG that becomes
  explicit Vulkan render passes and descriptor sets.
* **Expensive.** The 14 shaders in `data/shaders/` (347 lines) are
  `#version 130` desktop GLSL using OSG's `osg_ModelViewMatrix`/`osg_Vertex`
  built-ins. Vulkan GLSL needs explicit layouts, descriptor sets and push
  constants. VSG bundles glslang so they can still be compiled from source
  rather than shipped as SPIR-V, but they have to be rewritten.

So the GUI and windowing arguments against moving have largely gone away; the
pipeline and the shaders are what is left, and they are a rewrite of the
rendering half of the project.

macOS is a further nudge in the same direction. Apple deprecated OpenGL in
10.14 and caps the legacy context at 2.1, so opendf's shaders are pinned at
`#version 120` there. VSG on macOS runs Vulkan through
[MoltenVK](https://github.com/KhronosGroup/MoltenVK), which sits on Metal --
the API Apple is actually maintaining -- so a VSG migration would replace
the 2.1 pin with a modern backend on every platform at once.

We tried a 3.3 Core context on macOS first (SDL context request + OSG traits
+ Program aliasing overrides + explicit attribute binding + `#version 330
core` shaders and MRT `layout(location = N)`) and every screen-facing draw
came out black. Debugging it produced the `OPENDF_GL_DIAG` harness -- a
self-contained `osg::Drawable` that bypassed OSG entirely, managing its own
program, VAO and VBO through raw `SDL_GL_GetProcAddress` entry points and
logging `glGetError` at every step. It rendered a magenta fullscreen
triangle on macOS 26 / Apple Silicon (M5 Pro, GL 4.1 Core) with zero GL
errors, which proved the Core context, the shader work and Cocoa surface
presentation were all fine; the failure was inside OSG 3.6.5's
`Geometry::drawImplementation` under Core, which silently drops draws for
its own `osg::Geometry` objects. Anything we drove manually paints; anything
OSG orchestrates (deferred pipeline screen quads, the MyGUI backend, scene
meshes) came out empty. OpenMW hits the same wall and short-circuits it in
`components/shader/shadermanager.cpp` with `templateName = "compatibility/"
+ templateName;` and the comment "until core support is supported".

So opendf on macOS is now on legacy 2.1 with `#version 120` shaders and
OSG-orchestrated draws, matching OpenMW. The harness has been deleted now
that it has delivered its verdict -- it was dead code in `pipeline.cpp`
behind an off-by-default `#define`, and on legacy 2.1 it only reconfirms
what already renders. Resurrect it from git history if a Core context or
the VSG port is ever retried; it was last seen working (GLSL 120 and 330
core, VAO optional, centre-pixel readback) just before its removal. It also
transposes cleanly into a `GL_KHR_debug` callback on drivers that expose it
(macOS Core does not) -- rename `drawImplementation` into a
`glDebugMessageCallback` handler and log the message text instead of
`glGetError`.

Two things make this cheaper whenever it happens, and are worth doing anyway:
keep rendering behind the `render/` seam so `world/` and `class/` do not reach
for `osg::` directly, and keep shaders as external data files (they already
are). A sensible trigger to revisit: once the game is actually playable, or
when working around an OSG bug costs more than building the seam would.

Separately and independently of any of this, MyGUI 3.5 is where MyGUI
development now is -- 3.4.3 is what produced the ABI mismatch this tree works
around -- so moving to it has value on its own.

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
