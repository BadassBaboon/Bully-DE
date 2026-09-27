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

Which one runs depends on a byte at `lightDef+126`. Both converge at
`0x0040FD1E`, so a single hook covers them.

### Tried, and it does nothing

A multiplier on `generator+0x54` was implemented at that convergence point and
measured in game at 0.1x. No visible change at all.

`sub_759570` shows why. It builds each technique and fills the table in place;
for `NiPCFShadowTechnique`, which is what this game uses:

```
technique+0x20 = 0.0        technique+0x2C = 0.0001
technique+0x24 = 0.98       technique+0x30 = 0.96
technique+0x28 = 0.0        technique+0x34 = 0.0001
```

Six floats, matching `sub_40DDB0`'s index range of 0-5. **Several entries are
exactly 0.0**, and multiplying zero gives zero, so for any light type selecting
those entries the knob could not do anything at any value. A multiplier was the
wrong operator for this field.

The magnitudes also argue against the "depth bias" label this section inherited:
0.98 and 0.96 look like depth-comparison thresholds, and only the 0.0001 pair
looks bias-like. `sub_772480` compares `generator+0x54` against the table entry
it should hold, which reads as a "still current?" check rather than a tunable.

Worth knowing before trying again: shadows sit correctly against their casters
at 8192 in normal play, so there is no observed problem here to fix. Anyone
picking this up should first identify which of the six entries a Bully spot
light actually selects, and find the instruction that consumes the value, before
assuming it is a bias at all.

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


## Draw distance, LOD and population

Every address here was found in `Bully.exe` and every patch was checked the same
way: disassemble the instruction at the site, work out what the replacement
actually does, and only then trust it. The cases where that check was skipped are
recorded in
[Patches that looked right and were not](#patches-that-looked-right-and-were-not).

### LOD object pools

`sub_44D320` builds fourteen pool descriptors. Each is a 28-byte object from
`sub_5EEAA0(0x1Ch)`; the capacity is stored at `[esi+8]` and then `sub_44A760`
allocates from it:

```c
this[2] = capacity;              // the immediate the mod patches
this[0] = alloc(4 * this[2]);    // pointer array
this[1] = alloc(this[2]);        // per-slot status bytes
```

Both allocations derive from the same field, so raising the capacity immediate
raises the buffers with it. There is no separate allocation size to keep in step.

| Pool | Immediate | Vanilla |
|---|---|---|
| 1 Buildings / world | `0x44D34F` | 15000 |
| 2 Static props | `0x44D38D` | 2000 |
| 3 Render nodes | `0x44D3CB` | 15 |
| 4 Dynamic props | `0x44D693` | 57 |
| 5 Small objects | `0x44D6D1` | 8 |
| 6 Light references | `0x44D70F` | 300 |
| 7 LOD meshes | `0x44D74D` | 220 |
| 8 World geometry | `0x44D78B` | 2250 |
| 9 Root | `0x44D7C9` | 1 |
| 10 Render instances | `0x44D841` | 4150 |
| 11 Effects / decals | `0x44D87F` | 87 |
| 12 Shadow / light casters | `0x44D8BD` | 275 |
| 13 Close occluders | `0x44D8FB` | 35 |
| 14 Animated geometry | `0x44D939` | 30 |

Pool 7 is the exception: it holds LOD mesh slots and is the first to run out as
draw distance grows, because every distant building needs one whether or not it
is on screen. Scaling 220 linearly still starves it, so it gets a flat 2048 floor.

### Population pools and PedPop ranges

The pool capacities and the spawn ranges are two different things, and raising
only the capacities does nothing visible.

| Site | Vanilla | What it is |
|---|---|---|
| `0x6D3F0A` | 490 | Ped pool size |
| `0x6D3F4E` | 980 | Ped loop bound -- exactly 2x the size, must move with it |
| `0x6D40BF` | 490 | Ped allocation size |
| `0x6D426C` | 250 | Vehicle pool size |

The ranges live in `Config\Dat\PedPop.dat`, parsed by `sub_49C3D0` into the
object at `dword_C2C108`. The first data line is scanned as
`"%f %f %f %f %f %f %f %i %i"` into consecutive float slots, and the file's own
header names them:

| Index | Field |
|---|---|
| `base[7690]` | `m_fOnScreenCullRange` |
| `base[7691]` | `m_fOnScreenMinRadius` |
| `base[7692]` | `m_fOnScreenMaxRadius` |
| `base[7693]` | `m_fOffScreenCullRange` |
| `base[7694]` | `m_fOffScreenMinRadius` |
| `base[7695]` | `m_fOffScreenMaxRadius` |
| `base[7696..7701]` | parser's own copy of 7690..7695 |
| `base[7702..7707]` | second data line |
| `base[7708]` | `m_fOffScreenHeightCull` |
| `base[7709]` | `m_nChancePerFrame` (int) |
| `base[7710]` | `m_nNumAttempts` (int) |

The mod hooks the `call sub_49C3D0` at `0x49F1A0` and scales these values after
parsing, rather than shipping an edited data file. That covers spawn *distance*
only; the counts are handled separately, below. The player's own file is read
normally and scaled on top, so a custom `PedPop.dat` keeps working.

Two details matter. The parser copies 7690..7695 into 7696..7701, so both sets
have to be updated or the game reverts to the unscaled copy. And the minimum
radii (7691, 7694) are deliberately left alone -- they set how close a ped may
spawn to the camera, and scaling them makes NPCs appear on top of the player.

### Per-area population counts

Spawn ranges and spawn counts are separate, and scaling only the ranges makes
pedestrians visible further away without there being any more of them. The pool
capacities above are a third separate thing again: they are only a ceiling.

The counts are the per-area, per-time-period rows further down the same file,
parsed by `sub_4645F0`. In `sub_49C3D0` the record is addressed as:

```
49C566: imul ecx, 1D8h            ; areaId * 0x1D8
49C56D: lea  eax, [edi+edi*8]     ; period * 9
49C570: add  ecx, esi             ; + table base
49C572: lea  ecx, [ecx+eax*4+4]   ; + period*36 + 4
49C576: call sub_4645F0
```

So every area holds four 36-byte rows, one per time period, and `edi` runs 0..3.
Inside a row:

| Offset | Type | Meaning |
|---|---|---|
| `+0x04` | int | total |
| `+0x08` | bytes | per-category counts, one per column in the file |
| `+0x14` | int | write cursor; ends up holding how many were written |

The cursor at `+0x14` is what tells the scaler how many categories the row
actually has, rather than assuming a fixed width. Rows that report a count of
zero or more than twelve are left untouched.

The file's totals are the sum of its categories: a row reading
`7, 0,0,0,0,0,0,2,0,0,4,0,1` is `2+4+1`. So the categories are scaled and the
total recomputed from them, which keeps the row self-consistent instead of
letting two numbers drift apart. Category bytes saturate at 255.

`sub_4645F0` is `__thiscall` with one stack argument and ends in `retn 4`, so the
hook has to re-push the line pointer and clean the caller's argument itself:

```
51                push ecx                ; save record; line is now at [esp+8]
FF 74 24 08       push dword ptr [esp+8]  ; re-push the line argument
8B 4C 24 04       mov  ecx, [esp+4]       ; record back into ecx
B8 F0 45 46 00    mov  eax, 0x004645F0
FF D0             call eax                ; callee cleans the pushed argument
59 51             pop ecx / push ecx
E8 ...            call ScalePopRow
83 C4 04          add  esp, 4
C2 04 00          ret  4                  ; clean the caller's argument
```

`sub_4645F0` is shared with the vehicle population file, but only the
pedestrian call site is hooked. The vehicle counts ship as zero in every row, so
scaling them would do nothing either way.

Note this is the one change in the mod that alters gameplay rather than
rendering: it raises populations in interiors and scripted areas too, not just
the street.

### Inline assembly and constants

MSVC inline assembly treats a `constexpr` symbol as a **memory operand**, not an
immediate. Writing

```
mov eax, kPedPopParseFunc
```

assembles to `mov eax, dword ptr [&kPedPopParseFunc]` (`A1`, not `B8`). That
happens to work, because the constant is materialised in `.data` and the load
fetches its value, but it is not what the line appears to say and it costs a
memory read on every call.

Both population hooks therefore use literals with a `static_assert` tying them to
the named constant, and the emitted bytes are checked against the built binary
rather than assumed.

### Camera far clip

`0x453046` and `0x4530BA` hold the *address* of the far-clip constant the game
loads (`0x00906530`), not the value. The mod repoints both at its own double.
`NiCamera::SetViewFrustum` is hooked separately at `0x762DDF` for the hardware
projection matrix.

Depth precision is the constraint here, not distance. The timecycle carries a
`NearFarRatio` column set to 2500, and Gamebryo tracks `m_fMaxFarNearRatio`. A
0.25 m near plane against a 2000 m far plane is 8000:1 and will Z-fight on
distant coplanar surfaces; against 600 m it is 2400:1 and within budget.

### Distance culling

`0x5111D0` and `0x51175E` are early-out jumps. Both were checked before being
NOPed, because bypassing a validity check and bypassing a distance check are
indistinguishable without reading the instructions that set the flags:

```
5111C3: faddp          ; dx*dx + dy*dy
5111C7: fmulp st(2)    ; radius*radius
5111C9: fcompp         ; squared distance vs squared radius
5111D0: jz             <- bypassed
```

Both are FPU comparisons of squared distance against a radius, so neither leaves
a null pointer to dereference. The second sits in corona code -- it writes
`byte_C660CE[esi]`, one of the relocated table fields.

### Sector traversal

Traversal is expanded to 1296 sectors (36x36) with five bounds guards on the
insertion sites (`0x4525C1`, `0x4526D2`, `0x452891`, `0x4529E2`, `0x452C22`).
The vanilla insertion is:

```
4525C1: mov ecx, dword_C11D60   ; write cursor
4525C7: mov [ecx], esi          ; store
4525C9: add dword_C11D60, 4     ; advance cursor
4525D0: add dword_C13F00, 1     ; bump count
```

Each guard checks `dword_C13F00 < 1999` and either resumes at `0x4525C9`
(letting the original advance and increment) or jumps past the store *and* both
increments. Expanding traversal without these overflows the array.

### Corona / visible light table

The table is an array of 56 structs at `0x00C660A0` with stride `0x30`, proven by
its own clearing loop:

```
510E54: lea ecx, [ebx+38h]   ; ebx is 0 here, so ecx = 56
510E57: mov eax, offset dword_C660AC
510E60: mov dword ptr [eax], 0
510E66: add eax, 30h          ; stride
510E69: sub ecx, 1
510E6C: jnz short loc_510E60
```

It is relocated to `0x020F4000` and expanded to 1024 slots by 97 patches: field
base pointers move from `0x00C660xx` to `0x020F40xx`, bounds move from `0x38`
(56) to `0x400` (1024), and a count-1 bound at `0x8DD4A3` moves from 55 to 1023.
`1024 * 0x30 = 0xC000`, which is exactly the gap to the secondary table at
`0x02100000`; `0x20000` is cleared to cover both.

`0x020F4000` is dead space at the tail of the image left by the packer. It has
no cross-references anywhere in the binary, and the region reads as zeros from
`0x020E0000` through `0x02140000`. The image spans `0x400000`-`0x2146000`, so
the whole relocation is inside mapped memory.

### MSAA and vegetation

`sub_8826E0` initialises hardware alpha-to-coverage. The check at `0x8827A3`
never activates it for foliage and wire fences under MSAA, which is what causes
the white halos. The mod replaces the check with its own.

There is no flickering-texture fix, and that is deliberate -- see the patches
section below.

### What "fog" actually is

Two separate things get called fog in this game and only one of them is fog.

The effect players describe as fog rolling in is **object alpha fade**. Object
definitions carry a `FadeDistance` property, read alongside `MaxRadius` in
`sub_666F70` at `0x6670A5`, so geometry ramps in from transparent as it streams
rather than appearing at full opacity. Pushing draw distance out reduces the
effect not by changing any fog parameter but by making the geometry exist and
stay opaque further away.

The shader fog is the separate subtle haze that gives distant mountains and
cliffs depth. That is what the uniforms below control. Vanilla sits between the
two extremes: geometry fading in *and* a haze over it.

This matters for the settings. `DisableDistanceFog` removes the haze. It does not
stop buildings materialising in front of the camera; the draw distance settings
do that. Turning both off gives distant terrain no depth cue at all.

### Fog and motion blur

Both are switched off by blanking the shader uniform *name* the engine looks the
constant up by. With the name gone the by-name lookup fails, the parameter is
never uploaded, and the shader constant keeps its default.

The strings sit back to back with unrelated uniforms, so the length must be
exact:

```
0x90064C: "cFog" + 4 pad (8) + "vFogNearFar" + NUL (12) = 20   then "BoneIdx"
0x91B940: "gMotionBlurStrength" + NUL              = 20   then "gMotionBlurTexture"
```

Twenty bytes at each. Twenty-four overruns into `BoneIdx`, the skinning bone
index, and into `gMotionBlurTexture`.

## Patches that looked right and were not

Every one of these applied cleanly and matched its expected vanilla bytes, and
every one did something other than what it was believed to do. They are recorded
because the failure mode is consistent: an address and a replacement byte string
are not enough to tell you what a patch does. Only the disassembly at that
address is, and each of these was written without checking it.

**`0x5E6837`, described as redirecting a branch.** The byte is not an opcode. It
is the displacement of `jz short loc_5E6841` at `0x5E6836`:

```
5E6834: 3B C3     cmp eax, ebx
5E6836: 74 09     jz short loc_5E6841
```

Writing `0xEB` retargets the jump from +9 to +235, landing at `0x5E6923` --
inside `sub_5E6920` and in the middle of `mov edi, ecx`, so execution resumes on
the `0xF9` byte as `stc`, runs that function without its prologue, and hits a
pop-based epilogue against the wrong stack frame. A crash or silent stack
corruption every time `eax == ebx`. No displacement from that jump can express a
correct fix; every reachable target is inside `sub_5E6510` or past its end.

**`0x40F744`, described as adding 20 m to a light's shadow radius.** Those nine
bytes are three field initialisations in a constructor, with `edx` already
zeroed:

```
40F744: 89 50 08   mov [eax+8],   edx
40F747: 89 50 0C   mov [eax+0Ch], edx
40F74A: 89 50 10   mov [eax+10h], edx
```

Replacing them with a write to `[eax+4]` leaves `+8`, `+0Ch` and `+10h` holding
whatever was in the freshly allocated struct. The cited mechanism was wrong too:
`sub_410320` is the debug string formatter, not a radius accumulator.

**The uniform-name blanking.** The mechanism was sound; only the length was
wrong, by exactly four bytes at both sites. This one is recorded separately from
the others because the first response to finding the overrun was to remove the
whole feature, on the untested assumption that blanking a uniform name could not
disable an effect at all. It can, and it did. The lesson cuts both ways: finding
one real bug is not licence to act on a second, plausible-sounding claim without
testing that one too.

**`0x7594B4` and `0x7594C3`, believed to be a shadow map pool resolution.** They
are an `NiTArray` element count -- `sub_776B50` allocates `4 * n` bytes for a
pointer array. Setting them to 4096 allocated a bigger pointer array and changed
nothing about shadows.

**Attaching scene roots to the shadow generator.** `sub_75DB00`, `sub_75E040`
and `sub_75E240` are renderer-side, dispatch through vtable slots 25, 28 and 29,
and dereference `this+0x18`. Calling them on scene graph nodes is undefined
behaviour. Characters already cast mesh shadows from the spot lights, so the
feature was not needed.

## Getting a readable binary

Retail `Bully.exe` is packed. Addresses cannot be found in the file on disk.

`UnpackHook` hooks `SystemParametersInfoA` through the import table, which the
packer calls after unpacking itself in memory. The mod checks for a known
unpacked byte pattern at `0x860C6B` first, in case it loaded late. Set
`DumpUnpackedBinary = 1` under `[General]` in the INI to write the decrypted image to
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


## The LOD mesh switch is not a draw-distance knob

`flt_C3CD00` at `0x00C3CD00` scales the distance at which an object gives up its
full-detail mesh:

```
sub_5273E0:  fld  [esp+arg_0]        ; distance to the object
             fld  dword [ecx+24h]    ; the object's own switch distance
             fmul flt_C3CD00
             fcompp
             jnz  -> return 0        ; past it: no full-detail model
             mov  eax, [ecx+20h]     ; else: the full-detail model
```

It reads like a draw-distance multiplier and it is not. It changes which *mesh*
is used, not whether the object is drawn at all -- that is the far clip.

**Raising it crashes the game.** This shipped tied to `LodMultiplier`, so the 2.0
default crashed within a minute or two of gameplay, and it was bisected by
running each thing `LodMultiplier` touches on its own with the rest at vanilla:
pools alone at 2x were fine, the switch scale alone at 2x crashed exactly as
reported.

The call site in `sub_452000` shows the shape of it:

```
452210: call sub_5273E0        ; pick the model
452217: test ebx, ebx
452219: jz   loc_45232F        ; null model is handled properly
45221f: cmp  dword [edi+18h], 0
452230: call eax               ; vtable+0x28, load the resource on demand
452232: mov  eax, [edi+18h]    ; used with no null check after that load
45223a: call sub_824EC0
```

The model pointer is checked. The resource fetched immediately afterwards is not.
Raising the switch distance pushes far more distant objects through that path and
forces on-demand streaming the game was not built to do.

It is now `LodSwitchScale`, defaulting to 1.0, documented as crash-prone above
that. Anyone revisiting it should start at the missing null check rather than at
the multiplier.

### What was ruled out on the way

- **The visible-object list.** A 2000-entry array at `0x00C11FC0` whose count sits
  immediately after it at `0x00C13F00`, which is what makes 2000 a hard ceiling.
  It looked like an excellent explanation for interiors vanishing, because a
  dropped object has neither mesh nor collision. Measured live with Frida at
  ~60 Hz it peaks at **209/2000 at 2x and 256/2000 at 3x** -- around 10%. It is
  not involved. The ASI's own 1 Hz sampler was too coarse to establish this; the
  faster sampling is what settled it.
- **The 14 LOD pool capacities.** All 14 sites are `mov dword [esi+8], imm32` on
  pool descriptors built in `sub_44D320`. That looks like constructor field
  initialisation, but `sub_44AD50` consumes the field as the element count and
  sizes both allocations from it (`288 * count` and `count`), so they are genuine
  capacities. Scaling them only allocates more slots. A tester confirmed 2x pools
  alone causes no crash.

### `0x00BD1008` is the current area id, as an int

Confirmed by reading it live while walking into the BMX Park: it goes `0` to `62`
and back to `0`. Interpreted as a float it is a denormal, so IDA's `float` typing
of that address is wrong. Area 0 is the outdoor world. This is the signal to use
for anything that needs to know whether the player is indoors.


## Raising the LOD mesh switch is not achievable, and here is the evidence

`LodSwitchScale` scales the distance at which an object keeps its full-detail
mesh. Outdoors it works and looks right. It also breaks every interior, and five
rounds of testing failed to separate the two.

### What it actually is

`flt_C3CD00` at `0x00C3CD00` has **ten readers and no writers**, and its initial
value in the image is `0.0`. So in a retail session it is zero for the whole
run, and `sub_5273E0` -- which does `switchDist * flt_C3CD00 <= distance` --
always returns null. The full-detail mesh path never executes in the shipped
game. It is dormant, not merely unscaled.

### Do not write the global

Writing `flt_C3CD00` enables all ten readers at once, and most are not about
mesh detail. `sub_4769C0` is the clearest:

```
if (dist <= flt_C3CD00 * 25.0 * (flt_C3CD00 * 25.0)) { ...all the work... }
```

At `0.0` that is `0 <= 0`, always true, so the work always happens. Give the
global a value and it becomes a live comparison that can fail, and the function
returns having done nothing.

Only two readers are the mesh switch, and both are the same encoding:

```
5273E7  d8 0d 00 cd c3 00   fmul flt_C3CD00   (sub_5273E0, selector)
527424  d8 0d 00 cd c3 00   fmul flt_C3CD00   (sub_527420, accessor)
```

Repointing those two operands at a private float scales the mesh switch and
leaves the other eight readers at vanilla. That part works, and is what the mod
does.

### It still breaks interiors

With only those two operands repointed, and `flt_C3CD00` still `0.0`, the BMX
Park (area 62) loses its geometry and the player falls through. Tested
repeatedly. Outdoor distance is visibly correct in the same session, so the
scaling itself does what it is supposed to.

### Switching it off indoors does not help, and cannot

`sub_4158A0` changes area, and it writes the area id **last**:

```
4158A0: mov eax, flt_BD1008        ; the OLD area
        if (old != *a1) sub_668150(...)  ; tear down
        sub_454240(*a1);                 ; LOADS THE NEW AREA
        sub_561040(...); sub_4F3720(...) ; camera re-init
        flt_BD1008 = *a1;                ; id updated only now
```

So anything watching `0x00BD1008` learns of the change after the interior has
been built. A 60 Hz poll is not slow, it is structurally too late.

Hooking the function's **entry** and reading the incoming area from its argument
does set the scale to vanilla before `sub_454240` runs. That was tested, the
hook applied (`0x004158A0`, five bytes, exactly a `jmp rel32`), the log confirmed
the scale flipping to 1.0 on entry -- **and the park still broke.**

That is the important result. The failure is not decided during the transition
and not by the scale in force inside the interior. Running the mesh switch above
1.0 *outdoors* damages state that survives into the area load.

### Where to pick this up

Anyone revisiting it should start from that last fact rather than from the
multiplier. The question is what outdoor state at a raised switch the interior
load later depends on -- not the timing, which is settled, and not the other
nine readers, which are ruled out.

Ruled out along the way, each by measurement rather than reasoning:

- the 2000-entry visible-object list (peaks at ~10% -- see above)
- the 14 LOD pool capacities (2x pools alone, no crash, park fine)
- the camera far clip (park fine at 600 m with the switch at 1.0)
- the missing null check at `0x452232` (guard installed at both sites, **never
  fired once**, park still broke)
- the other eight readers of `flt_C3CD00` (repoint test leaves them vanilla)
- transition timing (entry hook proves the scale is vanilla before the load)
