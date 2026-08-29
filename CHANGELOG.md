# Changelog

## 1.1.2

### Fixed

**Lampposts and other 2DFX coronas disappearing with `ExtendCoronaBuffer`.**
Relocating the corona table only works if every instruction referencing it is
repointed. One was missed: `fld dword ptr [esi+0C660BCh]` at `0x00511774` kept
reading the old table, which nothing writes once the table has moved, so it read
zero and the comparison below it rejected the corona.

The same field is correctly relocated at three other sites, including one 42
bytes earlier in the same function, so this was an omission rather than a
misunderstanding. Nothing crashed and no signature check failed, because the
instruction was left exactly as the game shipped it.

`tools/verify_corona_coverage.py` now checks the patch list against every
reference in the game image, so a missed one is a build-time failure instead of
a bug report. It also records the one reference that must *not* be relocated:
`0x00510E90` uses the table's start address as the end sentinel of a different
array.

## 1.1.1

### Fixed

**Camera clipping through characters in cutscenes.** `NearPlane` shipped at 1.0 m
in 1.1.0, which clips geometry closer than a metre to the camera. Cutscene
cameras sit well inside that, so the front of a character's head was clipped away
and you could see through into the hollow interior.

It now defaults to the engine's own 0.25 m, so vanilla behaviour is unchanged.
The precision the higher value bought was not needed at the shipped 600 m far
clip, where 0.25 m already gives 2400:1 against the 2500:1 the timecycle's
`NearFarRatio` column assumes.

The setting remains for anyone pushing `FarClipOverride` out far enough to
matter, and the log now warns whenever it is raised above 0.25.

## 1.1.0

Draw distance, LOD, population, anti-aliasing and post-processing, on top of the
shadow and bloom work from 1.0.0.

### Added

**Draw distance and LOD.** Fourteen LOD object pools scale with `LodMultiplier`.
The capacity immediate drives both of the pool's allocations, so raising it
raises the buffers with it; there is no separate size to keep in step.

**Population.** Pedestrian and vehicle pools are configurable (`PedPoolSize`,
`VehiclePoolSize`), with the ped loop bound derived at 2x the size because that
relationship is vanilla and breaking it makes the game iterate past its own
allocation.

**`PedPopScale`.** Scales two separate things in memory after `PedPop.dat` is
parsed: the spawn and cull ranges, and the per-area population counts. Ranges and
counts live in different parts of the file and are read by different functions,
so scaling only the ranges makes pedestrians visible further away without there
being any more of them.

Your own data file is read normally and scaled on top, so a custom `PedPop.dat`
keeps working. Minimum spawn radii are deliberately left alone: they control how
close an NPC may appear to the camera, and scaling them makes people pop in on
top of the player. Counts are scaled per category and the row total recomputed
from them, so a row stays self-consistent.

This is the one setting in the mod that changes gameplay rather than rendering.
It raises populations in interiors and scripted areas too, not just the street.

**`NearPlane`.** Exposes the camera near plane, defaulting to the engine's own
0.25 m so vanilla behaviour is unchanged. Raising it buys depth precision, since
Z-fighting on distant coplanar surfaces is driven by the far/near ratio and the
timecycle's `NearFarRatio` column says 2500. It is only worth raising alongside a
much larger `FarClipOverride`, and not past 0.5 without checking a cutscene:
the near plane is where geometry starts being clipped, and cutscene cameras sit
within a metre of characters' faces. The log reports the ratio and warns above
2500:1.

**Camera far clip.** `FarClipOverride`, applied to both the sector raymarcher and
the `NiCamera` view frustum so the two cannot disagree.

**Distance culling bypass, sector traversal, corona table.** Traversal expands to
1296 sectors with five bounds guards on the insertion sites. The corona table
moves to unused space at the tail of the image and expands from 56 to 1024 slots.

**Anti-aliasing.** Hardware alpha-to-coverage is activated for foliage and wire
fences under MSAA, removing the white halos.

**Fog and motion blur toggles.**

**Diagnostics.** An opt-in read-only sampler that logs the screen-effect gate
state once a second. It patches nothing and hooks nothing.

### Changed

- Ships at 2x out of the box: `LodMultiplier` 2.0, far clip auto-computing to
  600 m, `PedPopScale` 2.0, pools at 2048. Higher multipliers work and 4x is
  comfortable on modern hardware, but draw distance is the most expensive setting
  in the mod and the sector traversal caps insertions at 1999 per frame, past
  which distant geometry silently stops drawing.
- Verified patch helpers moved to `Patch.h`/`Patch.cpp` and shared by every
  feature. A signature mismatch now logs both byte sequences.
- CRT linked statically. The `.asi` previously imported `MSVCP140.dll` and
  `VCRUNTIME140.dll`; anyone without the VC++ redistributable got a silent load
  failure with no log to explain it. It now imports only `kernel32.dll`.

### Removed

**`ShadowTechnique = 2` (VSM).** The engine registers `NiVSMShadowTechnique`, but
variance shadow maps need a two-channel format holding depth and depth squared,
and every shipped shader samples the shadow map the PCF way. Selecting it makes
shadows shift, bend and render incorrectly.

**`FixFlickeringTextures`.** The byte it patched is not an opcode; it is the
displacement of an existing `jz`, and every reachable target lands inside the
wrong function or past its end. Flickering under MSAA also comes from the
alpha-tested textures themselves, so it is not something an ASI can fix alone.

**`ExtendLightShadowRange`.** The site it patched is three field initialisations
in a constructor, not a radius.

**Depth of field.** Investigated at length and abandoned. Three separate faults
were found and fixed -- an area gate that limited it to 6 of 64 areas, a shipped
bug where `gBlurTexturePixelSize` is looked up and discarded, and a set of
dispatcher gates that were ruled out by measurement -- and the effect still never
appeared. The findings are recorded in `docs/RESEARCH.md`.

### Fixed

- A jump displacement rewrite that landed mid-instruction inside a different
  function, running its body without a prologue against the wrong stack frame.
- Two shader uniform name blanks that overran by four bytes each, one of them
  into `BoneIdx`, the skinning bone index.
- A constructor patch that left three fields uninitialised.
- `std::stof` on an INI value with no exception handling, called from `DllMain`.
  A malformed `FarClipOverride` killed the process before the game started.

## 1.0.0

Shadow map resolution and bloom.

Raising the shadow resolution requires three separate changes; fewer than all
three does nothing visible or makes shadows vanish at distance. Bloom smears
because of its blur radius, not its sample count. Both are documented in
`docs/RESEARCH.md`.
