# Research notes

Reverse engineering log for Bully-DE. Addresses are absolute virtual addresses in
the unpacked image, which loads at `0x400000`. The retail `Bully.exe` is packed,
so nothing is readable until the packer finishes; see [Getting a readable
binary](#getting-a-readable-binary).

Every address below was checked byte for byte against the disassembly, and the
mod re-checks each one at runtime before writing to it.

## How Bully draws shadows

The game uses Gamebryo's shadow system, largely unmodified. The Bully-specific
parts are two subclasses:

| Class | Where | What it does |
|---|---|---|
| `MdBullyShadowRenderClick` | ctor `0x518E30`, vtable `0x917964` | Overrides two virtuals off `NiShadowRenderClick`. One returns a data pointer; the other sets a global flag around the base render call. No custom filtering. |
| `MdShadowRenderClickFactory` | `0x4035E0` | Registers the above with the render pipeline. |

`Md` is Mad Doc Software, who did the PC port.

`sub_4048E0` builds the frame graph: a "Bully Shadow Render Step", then a "Bully
Main Render Click", then a "Bully Main Render Step", wrapped in a "Bully Render
Frame".

### The system is spot-light only

This is the single most important fact about shadows in this game, and it is not
obvious from the engine side. Gamebryo registers directional, spot and point
shadow techniques, and `NiShadowManager` builds write-materials for all three
(`sub_758F50`). None of that matters, because no shipped shader can sample a
directional or point shadow map.

Counted across all 640 compiled shaders in `ShaderBinaries/High`:

| Shader constant | Shaders referencing it |
|---|---|
| `ShadowSpotLight0` .. `ShadowSpotLight3` | 640 / 640 |
| `ShadowDirLight`, `ShadowDirectionalLight` | 0 / 640 |
| `ShadowPointLight` | 0 / 640 |
| `ProjShadowMap`, `ProjectedShadow` | 0 / 640 |

Four spot slots, nothing else. Two things follow. The outdoor sun is a
wide-angle spot light pretending to be a sun, which is why one resolution fix
sharpened interiors and exteriors together. And raising the shadow generator
count above 8 is pointless, because a fifth simultaneous shadow light has no
shader slot to bind to.

### NiShadowGenerator fields

Offsets into the generator object, from `sub_40F7F0` and the clone at `0x7732D0`:

| Offset | Type | Meaning |
|---|---|---|
| `+0x08` | word | Flags. `0x10` selects a bias table row and drives `byte_BC74BC`; `0x20` forces an exact-size shadow map match instead of nearest; `0x08` marks the generator active. |
| `+0x0C` | ptr | `NiShadowTechnique` |
| `+0x4C` | ptr | Light |
| `+0x54` | float | Shadow bias |
| `+0x58` .. `+0x60` | float | Further bias parameters |
| `+0x64` | word | Shadow map size hint |

### How a shadow map is allocated

`sub_7CF290` handles 2D maps, which is the path every spot light takes:

```
v5 = *(WORD *)(generator + 100);            // the size hint
sub_757C10(v5, v5, fmt, &out, 1);           // try the reuse pool, exact match
sub_7575D0(v5, v5, fmt, 0, 3);              // else allocate a new one
```

`sub_7CF790` is the cube-map equivalent for point lights, and is dead in this
game for the reason above.

`sub_7575D0` clamps and then checks a budget:

```
0x7575D0: B8 00 08 00 00     mov eax, 800h      ; clamps BOTH width and height
0x75763A: cmp edx, [eax+0B8h]                   ; allocated + new > budget?
          -> purge callback, then fail and return null if still over
```

A null return means that light silently stops casting. There is no error, no
log, no fallback.

## The three things that gate shadow resolution

Patching any one or two of these does nothing visible. All three are required.

**1. The per-light size assignment at `0x40FCC9`.** This is the one that makes
everything else look broken. `sub_40FB30` runs whenever a shadow light is set up
and re-reads the size from the light's own data:

```
40FCC9: 66 8B 4A 7A   mov cx, [edx+7Ah]     ; lightDef+122
40FCCD: 66 89 48 64   mov [eax+64h], cx     ; generator+0x64
```

Whatever the constructor put in the size hint is overwritten here before a
single map is allocated. The mod replaces the load with `mov cx, imm16`
(`66 B9 iw`), which is also four bytes, so no padding is needed.

**2. The 2048 clamp.** Even with the hint forced, `sub_7575D0` truncates
anything larger:

| Address | Vanilla bytes | Site |
|---|---|---|
| `0x7575D1` | `00 08 00 00` | `mov eax, 800h`, clamps width and height in `sub_7575D0` |
| `0x757987` | `00 08 00 00` | `cmp ebx, 800h` in `sub_757980` (cube) |
| `0x75798F` | `00 08 00 00` | `mov ebx, 800h` in `sub_757980` (cube) |

**3. The memory budget at `0x758FEB`.** `NiShadowManager`'s constructor sets a
64 MB ceiling, sized for 8 generators at 1024x1024:

```
0x758FDB: C7 86 B4 00 00 00 08 00 00 00   mov [esi+0B4h], 8          ; generator count
0x758FE5: C7 86 B8 00 00 00 00 00 00 04   mov [esi+0B8h], 4000000h   ; 64 MB
```

Raise the resolution without raising this and allocations start failing partway
through a scene. The symptom is shadows disappearing at distance or when several
lights come into view, which reads like broken cascade logic and is not.

VRAM per map is `resolution^2 * 4`: 16 MB at 2048, 64 MB at 4096, 256 MB at 8192.

### Size hints in the constructors

Set once at creation and then overwritten by `0x40FCC9` for any light that goes
through `sub_40FB30`. Patched anyway, for generators that do not.

| Address | Vanilla bytes | Site |
|---|---|---|
| `0x40F9A3` | `00 04` | `mov word [ecx+64h], 400h` in `sub_40F7F0` |
| `0x7730DB` | `00 04` | `NiShadowGenerator` constructor |
| `0x773180` | `00 04` | `NiShadowGenerator(NiDynamicEffect*)` constructor |

## Shadow technique

`sub_40F7F0` pushes a technique name string at `0x40F9B4` and looks it up:

| String | Address |
|---|---|
| `NiPCFShadowTechnique` | `0x900460` (default) |
| `NiStandardShadowTechnique` | `0x9516C8` |
| `NiVSMShadowTechnique` | `0x951654` |

The pushed pointer is the immediate at `0x40F9B5`. Techniques are registered in
`sub_759570`.

## Shadow bias

`generator+0x54` is the depth bias, written from two places:

- `sub_40DDB0` reads it from the technique's own table at `technique + 32 + 4*i`,
  where `i` comes from the light type and the `0x10` flag.
- The inline path at `0x40FD15` reads a per-light float at `lightDef+128`.

Which one runs depends on a byte at `lightDef+126`. Neither is patched today.
Raising the resolution shrinks each texel, so a bias tuned for 1024 is larger
than it needs to be at 8192; if shadows detach from the base of objects, this is
where to look.

## Bloom

Bloom is a separate effect from the timecycle glow, and this caught me out once:
`GlowThresh` and `GlowStrength` in the timecycle feed `ScrFX_Glow` and
`ScrFX_Luminance`, not `ScrFX_Bloom`. Bloom parameters come from a **per-area**
table at `dword_CF0A68` (24-byte stride, area id 0-63), copied each frame into
`dword_AC7608` (enable), `AC760C` (threshold, 230), `AC7610` (strength, 80) and
`AC7614` (scale, 4). Patching those globals does not stick.

`sub_560480` runs the chain: threshold (pass 2), blur horizontal and vertical
(pass 7 twice), composite (pass 1).

### Sample count is not the knob

Both bloom pixel shaders are 58 instructions with **13 `texld`** and contain no
`LOOP` or `REP` instruction. The blur is fully unrolled, so `BLUR_SAMPLES` is a
compile-time constant read back from the effect only to size the kernel array the
C++ uploads. It cannot be raised at runtime, and 13 taps per axis is already
generous -- more taps on a wide kernel makes it smoother, not sharper.

### The radius is the knob

`sub_560480` builds each kernel entry as `offset = tapIndex / divisor`, where the
divisor is the screen dimension shifted right by two:

```
5605E7: call sub_405BA0     ; screen dims
5605EC: mov  eax, [eax]     ; width
5605EE: cdq
5605EF: and  edx, 3
5605F2: add  eax, edx
5605F4: C1 F8 02  sar eax, 2   ; divisor = width / 4     <- horizontal
...
56071B: C1 F8 02  sar eax, 2   ; divisor = height / 4    <- vertical
```

With 13 taps that is a +/-24 full-res pixel radius per axis. Lowering the shift
tightens it: `2 -> 1` halves the radius, `2 -> 0` quarters it. The shift
immediates are at `0x5605F6` and `0x56071D`.

Only the shift byte needs changing. The `and edx, 3` above it is the compiler's
signed-division rounding fixup; screen dimensions are always positive, so `edx`
is zero and the mask is inert regardless of the shift.


## Depth of field: investigated and abandoned

Bully SE ships `ScrFX_DepthOfField` as screen-effect pass 4, run by `sub_5601F0`.
Three separate things were wrong with it, all of them were fixed, and the effect
still never appeared on screen. The work is recorded here so nobody repeats it.

### What the effect looks like in the binary

| Thing | Address | State |
|---|---|---|
| Effect name table entry | `0xAC7644 + 4*4` | `"ScrFX_DepthOfField.fxl"`, pass index 4 |
| Runner | `sub_5601F0` | binds `gBlurTexture`, sets pixel sizes and `gBlurStrength`, runs pass 4 |
| Master enable | `dword_AC7624` | ships as **1** |
| Blur strength | `byte_AC7628` | ships as **120**, uploaded as `gBlurStrength = n / 255` |
| Blur source | `dword_CF12EC` | filled by `sub_55FDE0`, the gaussian pass (13 offsets, 13 weights, pass 6) |

Note `sub_55FDE0` and `sub_55DFE0` are different functions with confusingly
similar names. The first renders the blur texture; the second is the screen
effect defaults reset that writes `dword_AC7624 = 1` and `byte_AC7628 = 120`.
Patching those globals directly does not stick, because the reset runs on load.

### Problem 1: the area gate

The dispatcher only calls `sub_5601F0` when a per-area byte is set:

```
55E4E9: cmp  dword_AC7624, ebp   ; master enable, ships as 1
55E4EF: jz   skip
55E4F1: mov  ecx, flt_BD1008     ; current area id
55E4F8: call sub_430320          ; return byte_901DD0[areaId]
55E500: test al, al
55E502: jz   skip                ; 74 10
55E50C: call sub_5601F0
```

`sub_430320` is nothing but `return byte_901DD0[a1]`. That 64-entry table has
**six** entries set -- areas 0, 1, 22, 31, 42 and 43. Everywhere else the effect
is skipped.

NOPping the `74 10` at `0x55E502` removes the gate. Doing it there rather than
filling the table matters: `sub_40FB30` calls the same `sub_430320` predicate to
drive a lighting path, so rewriting the table changes area lighting too.

**Result: no visible change.**

### Problem 2: gBlurTexturePixelSize is never uploaded

`sub_5601F0` looks up both pixel-size constants and keeps only the first:

```
560275: push "gSceneTexturePixelSize"
56027D: call edx        ; -> eax
560284: mov  edi, eax   ; edi = SCENE handle, saved before the second lookup
56027F: push "gBlurTexturePixelSize"
56028E: call ecx        ; -> eax, immediately clobbered, never stored
5602F4: push edi        ; SetVector(scene, 0.25/dim)   <- meant for the blur texture
56031B: push edi        ; SetVector(scene, 1.0/dim)
```

`edi` carries the scene handle into both `SetVector` calls, so
`gSceneTexturePixelSize` is written twice and `gBlurTexturePixelSize` never
reaches the shader. The blur texture is quarter resolution, which is what the
discarded `0.25/dim` vector was for. This is a real shipped bug, confirmed in raw
disassembly rather than decompiler output.

It was fixed with two code caves: one storing the discarded handle at
`0x560290` (5 stolen bytes), one pushing it in place of `edi` at `0x5602EF`
(7 stolen bytes). `ebx` is unused across the whole function but is callee-saved,
so the handle went to a static rather than a register.

**Result: no visible change, at blur strength 120 and at 255.**

### Problem 3: ruled out by measurement

A read-only sampling thread confirmed that during normal gameplay the
dispatcher's two early-outs (`byte_BCBB53` at `0x55E2C3` and `byte_C1A998` at
`0x55E2D0`) are both clear, the area id is in range, the master enable reads 1,
the strength reads what was patched, and the blur texture source is non-null
with its ready flag set. Every gate is open and the effect still produces
nothing.

### Where it stands

The remaining candidate is the shader itself. `ScrFX_DepthOfField` contains two
pixel shaders: 4 instructions with 1 `texld`, and 12 instructions with 2 `texld`.
Two texture reads is too few for scene plus blur plus depth, so the
`DefaultTechnique` may have no depth term at all -- in which case this was never
distance depth of field, only a scripted full-screen blur, and the effect seen on
PS2 and Wii does not exist in this engine.

Settling that means disassembling the shader bytecode rather than reading its
constant names. Nobody has done it. The three fixes above are correct as far as
they go and are recorded here; the feature code was removed from the mod because
it changed nothing a player can see.


## Things that were tried and did not work

**The jitter texture.** `Graphics\NormalizedRandomDirections.tga` is 512x512, and
only its R and G channels carry data. Decoded as `c/255*2-1` the pairs are unit
2D vectors, one random direction per texel, which is the classic PCF kernel
rotation map. 620 of the 640 shaders name a `ShadowNoise_Map` sampler, so it
looked like the softness dial.

Variants were generated at 75%, 50%, 25% and 0% vector magnitude, verified by
round-tripping the original through the same encoder and confirming a
byte-identical file. Installed and compared against stock at the same location
and time of day, the 50% and 0% variants produced no visible difference at all.
Not softer, not sharper, no banding. The texture is not reaching the shadow path
that runs. Ruled out.

**Two patches from an early attempt that were wrong.** `0x7594B4` and `0x7594C3`
were believed to be a shadow map pool resolution. They are an `NiTArray` element
count: `sub_776B50` allocates `4 * n` bytes for a pointer array. Setting them to
4096 allocated a bigger pointer array and changed nothing about shadows.

**Attaching scene roots to the shadow generator.** An early version called
`sub_75DB00`, `sub_75E040` and `sub_75E240` on the globals at `0xBCBBBC`,
`0xBCBBC0` and `0xBCBBC8` to try to make characters cast mesh shadows. Those
three functions are renderer-side, dispatch through vtable slots 25, 28 and 29,
and dereference `this+0x18`. Calling them on scene graph nodes is undefined
behaviour and produced the crash dumps that started this investigation.
Characters already cast mesh shadows from the spot lights, so the feature was
not needed.

## Getting a readable binary

Retail `Bully.exe` is packed. Addresses cannot be found in the file on disk.

`UnpackHook` hooks `SystemParametersInfoA` through the import table, which the
packer calls after unpacking itself in memory. The mod checks for a known
unpacked byte pattern at `0x860C6B` first, in case it loaded late. Set
`DumpUnpackedBinary = 1` in the INI to write the decrypted image to
`Bully_unpacked.exe` next to the `.asi` on the next launch, and load that in IDA.
It is off by default because it writes 29 MB on every start.

The dump loads at `0x400000`, so addresses in it match the running process
directly with no rebasing.

## Diagnostics that helped

`sub_410670` builds two debug overlay strings, `DIFFUSE LIGHT COUNT: %d/%d` and
`SHADOW LIGHT COUNT: %d/%d`, and `sub_410320` formats `%s ON at distance
%.2f/%.2f - %.2f` per light. These are formatted and then discarded in the retail
build, but they name the concepts clearly and were useful for confirming that
lights are classified into shadow-casting and diffuse-only sets.

## Shader files

Shader paths are plain relative strings in the binary, so redirecting them to a
folder outside the game install is straightforward if it is ever needed:

```
ShaderBinaries\High     ShaderBinaries\Med     ShaderBinaries\Low
ShaderBinaries\Shaders  Shaders\Generated      %s\%s.fxb
```

The `.fxb` files are compiled Direct3D 9 bytecode from HLSL compiler 9.23 with no
source shipped. `Shaders\Data\Fragments\Text` holds NDL fragment sources, and
`Shaders\Generated` exists but is empty. Whether the engine can still compile
from those fragments at runtime has not been tested, and would be the cheaper
route to changing the PCF kernel.
