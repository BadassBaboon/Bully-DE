<p align="center">
  <img src="assets/bully-de.png" alt="Bully Definitive Edition" width="640">
</p>

# Bully: Definitive Edition

An ASI plugin for **Bully: Scholarship Edition** on PC. It pushes draw distance
out, sharpens shadows, tightens bloom, and fixes the white halos MSAA leaves
around foliage.

Every patch checks the bytes it is about to overwrite against the vanilla
encoding first, so a wrong address produces a line in the log instead of a crash.
Nothing is applied blindly.

---

## What it does

**Draw distance.** Fourteen LOD object pools, the camera far clip, sector
traversal, distance culling and the corona light table all move together.
Raising one without the others either does nothing or overflows something, which
is why this is a set of coordinated patches rather than a single number. Ships
at 4x, which puts the far clip at 1200 m against the game's 300.

**Pedestrian population.** `PedPopScale` scales two separate things in memory
after `PedPop.dat` is parsed: how far pedestrians spawn, and how many of them
each area gets. Your own data file is still read normally and scaled on top, so a
custom `PedPop.dat` keeps working. The pool sizes are a third thing again and
only raise the ceiling.

**Shadow resolution.** From the game's 1024 up to 8192. Three separate things in
the engine cap it: a per-light size assignment that overwrites the value every
frame, a hard 2048 clamp in both allocators, and a 64 MB budget sized for eight
1024x1024 maps. Change fewer than all three and you get nothing at all, or
shadows that vanish at distance.

Outdoors the sun is a wide-angle spot light stretched across a neighbourhood, so
those 1024 texels are spread thin and edges turn to mush. Indoors the same map
covers one room and looks sharp. One resolution fix sharpens both.

**Bloom.** The blur smears because of its radius, not its sample count. Both
bloom shaders already do 13 taps per axis, fully unrolled. `BloomRadiusPercent`
narrows the kernel, which concentrates the same glow into a smaller area rather
than dimming it.

**Anti-aliasing.** Hardware alpha-to-coverage never activates for foliage and
wire fences under MSAA, which is what produces the white outlines around leaves
and chain-link. This fixes the check that gates it.

**Fog and motion blur toggles.** Read the note below on what "fog" actually is in
this game, because it is not what most people expect.

## Install

1. Install an ASI loader if you do not have one. Ultimate ASI Loader works.
2. Copy `Bully-DE.asi` and `Bully-DE.ini` into the game's `plugins` folder.
3. Launch, then read `plugins/Bully-DE.log`.

This repository holds the source. The built `.asi` is not committed, so cloning
will not give you one; build it with the instructions below.

## Settings

Every setting is documented in `Bully-DE.ini` itself, with the reasoning behind
it. The tables below are the summary.

### Draw distance

| Setting | Default | Notes |
|---|---|---|
| `LodMultiplier` | `4.0` | Scales all fourteen LOD object pools, and the far clip when `FarClipOverride` is `0`. |
| `FarClipOverride` | `0.0` | `0` auto-computes `LodMultiplier x 300 m`, so 1200 m at the shipped multiplier. |
| `NearPlane` | `1.0` | The engine ships 0.25 m. What matters is the far/near **ratio**: the timecycle's own `NearFarRatio` column says 2500, and past that distant coplanar surfaces Z-fight. The log warns if your combination exceeds it. |
| `PedPopScale` | `2.0` | Scales pedestrian spawn ranges **and** per-area population counts. Minimum spawn radii are deliberately left alone, otherwise NPCs appear on top of the camera. This is the one setting that changes gameplay rather than rendering: it raises populations in interiors and scripted areas too. |
| `PedPoolSize` | `2048` | Vanilla 490. The loop bound follows at 2x automatically. |
| `VehiclePoolSize` | `2048` | Vanilla 250. |
| `ForceHighDetailModels` | `0` | Never drops to low-detail meshes. This bypasses the distance comparison entirely, so `LodMultiplier` stops affecting model switching while it is on. |
| `BypassDistanceCulling`, `ExtendCoronaBuffer`, `EnableSectorOverflowGuard`, `ExtendTerrainDrawDistance` | `1` | Parts of the same coordinated set. |
| `FilterRadarPedBlips` | `0` | Stops distant ambient NPCs cluttering the minimap. |

### Shadows

| Setting | Default | Notes |
|---|---|---|
| `ShadowMapResolution` | `8192` | VRAM per map is `resolution^2 x 4`: 16 MB at 2048, 64 MB at 4096, 256 MB at 8192. |
| `ShadowBudgetMB` | `0` (auto) | This is the setting that decides whether shadows survive at distance, not the resolution. |
| `ShadowTechnique` | `1` (PCF) | `0` is hard-edged with no filtering. |
| `ShadowGeneratorCount` | `8` | Do not raise. Every shipped shader binds exactly four spot-shadow slots, so a fifth light has nowhere to go. |
| `DisableBlobShadows` | `0` | Removes the flat oval decals under characters. They are separate from the mesh shadows the spot lights cast, so this takes away ground contact where those lights do not reach. |

### Bloom, AA and post-processing

| Setting | Default | Notes |
|---|---|---|
| `BloomRadiusPercent` | `50` | `100`, `50` or `25`. Only powers of two are reachable, because the radius comes from a bit shift. |
| `BloomMode` | `0` | `1` forces bloom on everywhere, `2` off entirely. |
| `BloomThreshold` / `BloomStrength` / `BloomScale` | `-1` | `-1` keeps the game's own per-area values (230 / 80 / 4). |
| `EnableAAFixes` | `1` | Only does anything with MSAA actually enabled. |
| `DisableDistanceFog` / `DisableMotionBlur` | `1` | |
| `LogPostFXState` | `0` | Read-only diagnostic. Samples the screen-effect gate state once a second and logs it when it changes. Patches nothing. |

## What "fog" means in this game

Two different things get called fog, and only one of them is fog.

What most players see as fog is **buildings fading into existence**. Object
definitions carry their own `FadeDistance` alongside `MaxRadius`, so geometry
ramps in from transparent as it streams. That reads as fog lifting, but it is an
alpha ramp on the objects themselves.

The actual shader fog is a subtle haze that adds depth, most visible on distant
mountains and cliffs. That is what `DisableDistanceFog` switches off, by blanking
the uniform names the engine looks the constants up by.

So the two settings do different jobs. If you want buildings to stop
materialising in front of you, that is the **draw distance** settings, not the fog
toggle. Turning the fog off as well removes the haze, which flattens distant
terrain. Vanilla sits between the two.

## Build

Requires CMake 3.20 or newer and a Visual Studio toolchain with the Windows SDK.
The game is 32-bit, so the build has to be too.

```cmd
cmake -B build -A Win32
cmake --build build --config Release
```

Output is `build/Release/Bully-DE.asi`, with `Bully-DE.ini` copied next to it.
Nothing is needed beyond the Win32 API. The CRT is linked statically, so the
built `.asi` imports only `kernel32.dll` and does not need the VC++
redistributable on the machine that runs it.

## If something looks wrong

Read `plugins/Bully-DE.log`. It records every patch, its address, and the value
it replaced.

**No log at all** means the `.asi` was never loaded, which is a loader or install
problem rather than a mod one. Antivirus quarantining the file is the usual
cause, and it does it silently.

**A signature mismatch** means the game build is not the one this was developed
against. That patch was skipped rather than applied blindly, and the log prints
both the expected and the actual bytes.

**Shadows disappearing at distance** means the shadow map budget or your VRAM ran
out. Set `ShadowBudgetMB` to something your card can hold, or drop
`ShadowMapResolution` to 4096.

**Shimmering on distant road markings or building faces** is Z-fighting from the
far/near ratio. Raise `NearPlane` or lower `FarClipOverride`. The log warns when
the ratio exceeds 2500:1.

## Known limitations

**Only spot lights cast shadows.** The engine supports directional and point
shadow maps and sets up materials for both, but none of the 640 compiled shaders
can sample them. Changing that would mean authoring new shaders.

**Depth of field does not work and is not included.** It exists in the engine,
three separate faults were found and fixed, and it still never rendered. The
investigation is written up in `docs/RESEARCH.md`.

**VSM shadows are not offered.** The engine registers the technique, but variance
shadow maps need a two-channel format holding depth and depth squared, and every
shipped shader samples the map the PCF way. Selecting it makes shadows shift and
bend.

**MSAA flickering is not fully fixable from an ASI.** It comes from the
alpha-tested textures themselves, not only from render state.

**Shadow bias is untouched.** Smaller texels need less bias than the stock value
assumes, so at 8192 shadows may sit slightly away from the base of the object
casting them. Both write sites are identified in the research notes.

Tested on the Steam release on Windows. The signature checks mean other builds
refuse to patch rather than corrupt themselves, but nothing else has been tried.

## Documentation

[`docs/RESEARCH.md`](docs/RESEARCH.md) is the reverse engineering log: verified
addresses, struct layouts, how each system actually works, and the things that
were tried and did not work. That last part is most of its value. It includes a
section on patches that applied cleanly, matched their expected bytes, and did
something other than what they were believed to do.

[`CHANGELOG.md`](CHANGELOG.md) covers what changed between releases.

## Credits

Silent, for [SilentPatchBully](https://github.com/CookiePLMonster/SilentPatchBully),
which is where the process-attach and patching approach came from.

ThirteenAG, for Ultimate ASI Loader.
