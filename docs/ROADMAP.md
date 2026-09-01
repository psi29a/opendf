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

## Lighting, measured against Daggerfall Unity

Dungeon lighting is calibrated against DFU rather than eyeballed, so it is
worth recording which numbers are theirs and which are ours.

The single most important finding: **DFU renders in linear colour space**
(`ProjectSettings.asset`, `m_ActiveColorSpace: 1`). It decodes sRGB textures to
linear, lights in linear, and gamma-encodes on output. We were doing none of
that -- multiplying gamma-encoded palette colours by linear light and writing
the result straight out, which crushes midtones badly. Every DFU constant
looked wrong here as a result, and the old `(1 - d^2/r^2)^2` falloff only
looked acceptable because its excessive brightness cancelled the missing
gamma. `data/shaders/object.frag`, `data/shaders/sprite.frag` and
`data/shaders/terrain.frag` now decode albedo with `pow(c, 2.2)` into the
RGBA16F G-buffer, and `data/shaders/combiner.frag` encodes the lit result
back; with that in place DFU's own numbers produce a sensible image.

Matching DFU:

* **Dungeon ambient** `(0.12, 0.12, 0.12)`, and **castle ambient** `0.58`
  inside castle blocks -- `PlayerAmbientLight`. The castle flag comes from the
  magnitude byte of a block's start marker (TEXTURE.199 record 10), the same
  place `DaggerfallBillboard.cs` reads it; `World::updateCurrentBlock` tracks
  which block the camera stands in so it changes as you walk.
* **Light range** `radius * 3` -- `RDBLayout.AddLight`.
* **Light intensity** 0.8 -- the `m_Intensity` on DFU's dungeon light prefab,
  applied through `r_lightintensity`.
* **Falloff**, Unity's built-in point light shape. `1/(1 + 25*(d/r)^2)` is
  `1/26` at `d == r`, not zero, so it is rescaled to
  `(1/(1 + 25*(d/r)^2) - 1/26) * 26/25` -- exactly 1 at the source and exactly
  0 at the radius, so a light neither leaks past the volume it claims nor pops
  when it ends. A radius of zero is rejected before the division; the RDB
  permits one, and it would otherwise put a NaN into an additively blended
  light buffer.
* **Player torch** -- a white point light on the camera, radius 240. DFU's
  `EnablePlayerTorch` uses range 6 Unity units, which is 240 Daggerfall units
  at `MeshReader.GlobalScale` (0.025).
* **Light flicker** (`FlickerCallback`) -- DFU animates dungeon and city lights
  but not interior ones. It varies *radius*, never intensity: 14 ticks a
  second, each cycle picking a random target in `[base - Variance, base]` and
  creeping toward it, so a light only ever dips below base and drifts back.
  Only RDB lights flicker; the torch is steady, matching DFU, whose torch
  gutters only when the light item is nearly spent.

Ours, not DFU's:

* **Exterior ambient** `(0.255, 0.267, 0.358)` is invented. It is the older
  eyeball-tuned `(0.537, 0.549, 0.627)` converted to linear (`c^2.2`) when
  lighting moved to linear space, so it looks as it did before rather than
  being washed out by the encode. DFU instead lerps
  `ExteriorNightAmbientLight (0.25)` to `ExteriorNoonAmbientLight (0.9)` by
  daylight scale; ours is fixed and cannot track time of day because there is
  no time of day yet.
* **Ambient is a floor, not a base.** `dir_light.frag` computes
  `max(ambient, diffuse * N.L)` where Unity adds ambient to everything, so ours
  has more contrast between lit and unlit surfaces.

Tuning knobs, as percentages because the cvar system has no float type. The
`relight` console command re-applies all three without reloading:

    r_ambientscale     100   scales whichever ambient is active
    r_torchscale       100   the player torch
    r_lightintensity    80   RDB point lights; DFU uses 0.8

Still missing:

* **Interior (building) ambient.** DFU has `InteriorAmbientLight` and a night
  variant; we only have sun-on and sun-off.
* **Special-area ambient.** DFU has `SpecialAreaLight` (also 0.58) for e.g. the
  Daggerfall castle treasure room. It is tracked separately from the castle
  flag and we do not read it.
* **Night ambient scaling.** `NightAmbientLightScale` has no meaning without a
  day/night cycle, so there is no cvar for it.

Performance: each light is a *fullscreen* quad, so an off-screen light would
otherwise cost a full screen of fragments (the shader's `discard` only saves
work after three G-buffer fetches). The light pass runs under an ortho
projection with `NO_CULLING`, so OSG cannot cull them for us;
`LightCullCallback` tests each light's sphere against the *main* camera's
frustum instead. At the Privateer's Hold spawn that skips 43 of 72 lights.
DFU does the equivalent in `DungeonLightHandler`. Replacing the quads with real
light volumes would go further, and has not been needed yet.

## Curated tables carried over from Daggerfall Unity

Some facts about the data simply aren't *in* the data, so Daggerfall Unity
keeps hand-maintained lookup tables for them. Where we need the same
behaviour we have to carry the same tables over; there is no deriving them.

Ported so far:

* **Emissive flats** (`MeshManager::loadFlat`, from DFU
  `TextureReader.emissiveTextures`) -- fires, torches, braziers, glowing
  creatures: the flats that light themselves rather than waiting to be lit.
  185 archive/record pairs.

  Worth recording *why* this is a list. Dungeon lights are separate RDB
  objects, so "a light sits on this flat" looks like it should identify
  emitters. Measured across all 1295 blocks, the correlation is strong but
  wrong at the edges: archive 210 record 12 co-locates with a light 80% of
  the time and is *not* emissive, record 24 never co-locates and *is*, and
  roughly half the list is creatures and interior props with no RDB light to
  correlate against at all. The heuristic also can't work where we need it,
  since `loadFlat` sees a texture id and never a placement.

Not ported, because nothing consumes them yet -- a table with no caller is
just somewhere for errors to hide:

* **`DungeonTextureTable`** (`{119, 120, 122, 123, 124, 168}`) -- the dungeon
  wall texture set, needed once per-dungeon texture swapping lands. Closest
  to being needed.
* **Window textures** and the spectral/ghost eye special cases, both in
  DFU's `TextureReader` -- needed when emissive gets subtler than
  all-or-nothing, since only the glass or the eyes glow.
* **Per-race NPC archives** (`MobilePersonBillboard`, 381-398 and 451-456) --
  needed when townspeople exist.

## Non-goals

* Shipping game assets. OpenDF requires an existing Daggerfall installation.
  Daggerfall is available for free from Bethesda, and the launcher points you
  at the download and installs the game data from the archive you get.
* Being a Daggerfall *mod loader* or a remaster. The aim is to run the
  original game faithfully first.
