<p align="center">
  <img src="assets/bully-de.png" alt="Bully Definitive Edition" width="640">
</p>

# Bully: Definitive Edition

An ASI plugin for **Bully: Scholarship Edition** (PC) that raises the shadow map
resolution from 1024 to whatever you ask for.

Bully's shadows are soft and blocky outdoors and sharp indoors, and both come
from the same 1024x1024 shadow map. Indoors a spot light covers one room, so
those texels land close together. Outdoors the sun is a spot light stretched
across a whole neighbourhood, so the same 1024 texels are spread thin and the
edges turn to mush. Raising the resolution fixes both at once.

The setting looks like a one-line patch and is not. Three separate things in the
engine cap it, and changing fewer than all three does nothing at all or makes
shadows vanish at distance. Details are in [`docs/RESEARCH.md`](docs/RESEARCH.md).

---

## What it does

**Shadow map resolution.** Ships at 8192, up from the game's 1024. 4096 is a
reasonable choice if you would rather not spend the memory; the difference
between the two is clear indoors and slight outdoors.

**Shadow filtering.** The game uses PCF. Standard (hard edges) and VSM are
selectable, both off by default. Standard is sharper and shows shadow acne on
shallow angles, because the depth bias was tuned for PCF.

**Shadow map memory budget.** `NiShadowManager` ships with a 64 MB ceiling sized
for eight 1024x1024 maps. The mod scales it with the resolution you pick, and
lets you cap it by hand. This is the setting that decides whether shadows survive
at distance, not the resolution.

Every patch checks the bytes it is about to overwrite against the vanilla
encoding and skips with a log line if they do not match, so a wrong address
produces a log entry rather than a crash.

## Install

1. Install an ASI loader if you do not have one. Ultimate ASI Loader works.
2. Copy `Bully-DE.asi` and `Bully-DE.ini` into the game's `plugins` folder.
3. Launch the game and check `plugins/Bully-DE.log`.

This repository holds the source. The built `.asi` is not committed, so cloning
will not give you one; build it with the instructions below.

## Settings

All in `Bully-DE.ini`, which documents each one in place.

| Setting | Default | Notes |
|---|---|---|
| `ShadowMapResolution` | `8192` | VRAM per shadow map is `resolution^2 * 4`: 16 MB at 2048, 64 MB at 4096, 256 MB at 8192. |
| `ShadowTechnique` | `1` (PCF) | `0` is hard-edged, `2` is VSM. |
| `ShadowGeneratorCount` | `8` | Do not raise this. Every compiled shader binds exactly four spot-shadow slots, so a fifth light has nowhere to go. |
| `ShadowBudgetMB` | `0` (auto) | Auto is `resolution^2 * 4 * 10`, which is 2560 MB at 8192. Set a number here if you are short on VRAM. |
| `DisableBlobShadows` | `0` | Turns off the flat oval decals under characters and cars. They are separate from the mesh shadows the spot lights cast, so this removes ground contact where those lights do not reach. |
| `DumpUnpackedBinary` | `0` | Writes the decrypted `Bully.exe` image to disk for use in IDA. 29 MB per launch. |

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
cause, and it tends to do it silently.

**A signature mismatch line** means the game build is not the one this was
developed against, and that patch was skipped rather than applied blindly.

**Shadows disappearing at distance or when several lights are in view** means the
shadow map budget or your VRAM ran out. Set `ShadowBudgetMB` to something your
card can hold, or drop `ShadowMapResolution` to 4096.

## Known limitations

Only spot lights cast shadows in this game. The engine supports directional and
point shadow maps and `NiShadowManager` sets up materials for both, but none of
the 640 compiled shaders can sample them. Nothing here changes that, and doing so
would mean authoring new shaders.

Shadow bias is untouched. Smaller texels need less bias than the stock value was
tuned for, so shadows may sit very slightly away from the base of the object
casting them at 8192. Both places the engine writes the bias are identified in
the research notes if this turns out to matter.

Tested on the Steam release on Windows. The signature checks mean other builds
refuse to patch rather than corrupt themselves, but nothing else has been tried.

## Credits

Silent, for [SilentPatchBully](https://github.com/CookiePLMonster/SilentPatchBully),
which is where the process-attach and patching approach came from.

ThirteenAG, for Ultimate ASI Loader.
