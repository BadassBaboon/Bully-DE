# Changelog

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

**`NearPlane`.** The engine ships a 0.25 m near plane, far closer than anything
is drawn, which costs depth precision for nothing. What matters is the ratio to
the far plane; the timecycle's own `NearFarRatio` column says 2500, and past that
distant coplanar surfaces Z-fight. Defaults to 1.0 m, and the log warns when your
far/near combination exceeds 2500:1.

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

- Ships at 4x draw distance out of the box: `LodMultiplier` 4.0, far clip
  auto-computing to 1200 m, pools at 2048. `PedPopScale` ships at 2.0, kept
  separate because population is a different axis from geometry.
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
