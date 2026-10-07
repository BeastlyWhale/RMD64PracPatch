# Room transitions, display-list headroom, and frame pacing

Rockman DASH – Hagane no Boukenshin (N64, JP, NRHJ). All instruction addresses are
KSEG0 and come from `build/boot.asm` (boot segment, ROM 0x1000–0x101000 -> RAM
0x80025C00). ROM-wide claims ("only N sites in the whole 32 MB") were verified with a
raw-ROM word/`lui`+lo-part scanner over `baserom.z64`, not just over the boot segment.

Neither `baserom.z64` nor `build/boot.asm` was modified.

---

# PART A — Room / map transition

## A.0 TL;DR

| Item | Address | Type | Meaning |
|---|---|---|---|
| **Area / map id** | **`0x801C4508`** | `s8` | 0..30, **directly indexes overlay table A** |
| **Entrance / sub-room id** | **`0x801C4509`** | `u8` | destination entrance within the area |
| (packed) | `0x801C4508` | `u32` | `area<<24 \| entrance<<16 \| …` — compared as one word |
| **Currently-loaded area** | **`0x801C4520`** | `s16` | only changes when overlay A is actually swapped |
| **Currently-loaded B-overlay (sub-room)** | **`0x801C452C`** | `s16` | only changes when overlay B is actually swapped |
| **Pending-transition request** | **`0x8020C89C`** | `s8` | non-zero while a warp is pending, cleared when the room is live |
| Requested area / entrance | `0x8020C89E` / `0x8020C89F` | `u8` | copied into `0x801C4508/09` by the mode-1 handler |
| Requested spawn X/Y/Z | `0x8020C890/892/894` | `s16` ×3 | |
| Requested spawn facing | `0x8020C89A` | `u16` | |
| **Game mode** | `0x801C44B8` | `s8` | 1 = area load, 2 = room init, 3 = gameplay |
| **Player (Rock) object** | **`0x8020CA80`** | struct, ≥ 0x448 B | static, not heap-allocated |
| Pointer to the player object | `0x80218420` | `void*` | written once at init (`0x8002A21C`), = `0x8020CA80` |
| **Universal "room load done" hook** | **`0x80073758`** | — | see A.2 |

## A.1 Proof that `0x801C4508` is the area id and `0x801C4509` the entrance

### (a) It is copied from the warp request by the mode-1 handler

Main-loop mode table `0x800AC500`, entry 1 -> `0x8003BE70`. Its `a0` is the save-block
base `0x801C44B8` (`s2`), and `s1 = 0x8020C888` is the warp-request struct:

```
8003BE90  2631c888  addiu  s1,s1,-14200   ; s1 = 0x8020C888  (warp request)
8003BE98  2610ca80  addiu  s0,s0,-13696   ; s0 = 0x8020CA80  (player object)
8003BF08  82220015  lb     v0,21(s1)      ; 0x8020C89D  "keep current position"
8003BF0C  14400030  bnez   v0,0x8003bfd0
8003BF14  92220016  lbu    v0,22(s1)      ; 0x8020C89E  requested AREA
8003BF18  a2420050  sb     v0,80(s2)      ; -> 0x801C4508   <<<< AREA ID
8003BF1C  92420053  lbu    v0,83(s2)      ; 0x801C450B
8003BF20  92230017  lbu    v1,23(s1)      ; 0x8020C89F  requested ENTRANCE
8003BF24  a2420052  sb     v0,82(s2)      ; -> 0x801C450A  (previous entrance)
8003BF28  a2430051  sb     v1,81(s2)      ; -> 0x801C4509   <<<< ENTRANCE ID
```

### (b) It is compared as a packed `(area, entrance)` pair

`0x8003BDDC`, run when `mode == 5`:

```
8003BDFC  8c634508  lw     v1,17672(v1)   ; v1 = *(u32*)0x801C4508
8003BE00  3c02ffff  lui    v0,0xffff
8003BE04  00622024  and    a0,v1,v0       ; a0 = area<<24 | entrance<<16
8003BE08  3c030302  lui    v1,0x302       ; (area 0x03, entrance 0x02)
8003BE0C  00831826  xor    v1,a0,v1
8003BE14  3c02130b  lui    v0,0x130b      ; (area 0x13, entrance 0x0B)
8003BE18  00821026  xor    v0,a0,v0
8003BE28  3c021b06  lui    v0,0x1b06      ; (area 0x1B, entrance 0x06)
8003BE2C  10820003  beq    a0,v0,0x8003be3c
```

Three hard-coded `(area, entrance)` pairs — conclusive.

### (c) It indexes the overlay-A descriptor table and three per-area tables

`0x800732E0` (the area loader):

```
800732EC  82104508  lb     s0,17672(s0)   ; s0 = area id
80073318  0c00a890  jal    0x8002a240
8007331C  94843a10  lhu    a0,14864(a0)   ; 0x800B3A10[area]  (u16 per-area, BGM)
80073324  84424520  lh     v0,17696(v0)   ; v0 = *(s16*)0x801C4520  currently loaded
80073328  1050003a  beq    v0,s0,0x80073414  ; <<<< SKIP RELOAD IF UNCHANGED
...
80073378  a4304520  sh     s0,17696(at)   ; 0x801C4520 = area   (the only writer here)
...
800733EC  0c021c90  jal    0x80087240     ; osStopThread(gfx thread 0x801C3840)
800733F4  0c01eedc  jal    0x8007bb70     ; loadOverlayA(area)   <<<< a0 = area id
800733F8  02002021  move   a0,s0
800733FC  0c021c98  jal    0x80087260     ; osStartThread(gfx thread)
...
80073420  8c223b5c  lw     v0,15196(at)   ; 0x800B3B5C[area]
80073424  0040f809  jalr   v0             ; per-area entry point
```

and `0x800735F8`:

```
800736D8  8c223bd8  lw     v0,15320(at)   ; 0x800B3BD8[area]
800736F0  0040f809  jalr   v0             ; second per-area entry point
```

Table dumps (from ROM):
* `0x800B3B5C`: **31** words, all `0x8022D2F0` (overlay-slot-1 base).
* `0x800B3BD8`: **31** words, `0x8022D354` / `0x8022D35C`.
* `0x800B3A10`: **31** halfwords.

31 entries == the 31 descriptors in overlay table A (`docs/romlayout.md` §2). So
**area id ∈ [0, 30] and is exactly the overlay-A index.** `loadOverlayA` is reached from
one site in the entire ROM (`0x800733F4`), whose `a0` is `*(s8*)0x801C4508`.

### (d) Sub-room id `0x801C452C` indexes overlay table B

```
80073540  8442452c  lh     v0,17708(v0)   ; currently loaded B id
80073544  10440003  beq    v0,a0,0x80073554   ; skip if unchanged
8007354C  0c01ce5e  jal    0x80073978          ; loadSubRoom(newId)

80073990  a430452c  sh     s0,17708(at)   ; 0x801C452C = new id
8007399C  jal 0x80225b70  (a0 = 0x800B3C5C[id])
800739BC  jal 0x80083b00  (a0 = 0x800B3C94[id])
800739D8  2e020002  sltiu  v0,s0,2
800739E4  0c01eef6  jal    0x8007bbd8     ; loadOverlayB(id), skipped for id<2
```

`0x800B3C5C` / `0x800B3C94` are 14-entry tables; table B has 14 slots with indices 0 and 1
empty (`romlayout.md` §2). Matches exactly.

## A.2 The room-load function and the once-per-load hook point

### The call graph (verified ROM-wide, counts are for the whole 32 MB)

```
jal 0x800732E0  (load area: overlay A)         3 sites: 8003BFD0, 8005594C, 8005C504
jal 0x800735F8  (enter room: init everything)  4 sites: 8003C18C, 8003C5E4, 80055954, 8005C50C
jal 0x80073978  (load sub-room: overlay B)     1 site : 8007354C   (inside 0x800734B0)
jal 0x800734B0  (sub-room check)               7 sites: 8007372C, 80073BCC + 5 in overlays
jal 0x8007BB70  loadOverlayA                   1 site : 800733F4
jal 0x8007BBD8  loadOverlayB                   1 site : 800739E4
```

The four room-load paths:

| Path | Trigger | Sequence |
|---|---|---|
| Full area change | door / warp sets `0x8020C89C` | mode 3 -> mode 1 (`0x8003BE70`: sets `0x801C4508/09`, places player, `jal 0x800732E0`, mode:=2) -> mode 2 (`0x8003C01C`: places player, `jal 0x800735F8` @ `0x8003C18C`, mode:=3) |
| In-place sub-room change | `0x8020C89C == -1` | inside mode 3: player re-placed at `0x8003C574`+, `jal 0x800735F8` @ `0x8003C5E4`, then `sb zero,20(s2)` @ `0x8003C5F0` |
| Load game from save | file menu | `0x80055790`: forces `0x801C4520 = -1`, `jal 0x800732E0` @ `0x8005594C`, `jal 0x800735F8` @ `0x80055954`, mode:=5 |
| New game / start | `0x8005C440` | `jal 0x800732E0` @ `0x8005C504`, `jal 0x800735F8` @ `0x8005C50C` |

**Every path ends in `0x800735F8`.** In every path the player object `0x8020CA80` is
written with the destination position *before* `0x800735F8` is called
(`0x8003BF60`–`0x8003BF94`, `0x8003C0B8`–`0x8003C120`, `0x8003C574`–`0x8003C5DC`).

### `0x800735F8` — "enter room" (this is the room-init function)

```
80073604  82314508  lb     s1,17672(s1)     ; s1 = area id
80073690  0c01ce81  jal    0x80073a04       ; sub-area table lookup / 0x801C4528
800736A4  0c013da7  jal    0x8004f69c       ; one nested per-frame update
800736D8  8c223bd8  lw     v0,15320(at)     ; 0x800B3BD8[area]
800736F0  0040f809  jalr   v0               ; per-area init entry point
800736F8  0c00e140  jal    0x80038500
8007372C  0c01cd2c  jal    0x800734b0       ; -> loadOverlayB if sub-room changed
80073730  24040001  li     a0,1
80073734  0c00ede9  jal    0x8003b7a4       ; actor / system init
8007373C  0c011c84  jal    0x80047210
80073744  0c0146ac  jal    0x80051ab0
8007374C  0c00dadf  jal    0x80036b7c
80073754  a260007e  sb     zero,126(s3)     ; 0x801C4536 = 0
80073758  0c0180fc  jal    0x800603f0       ; <<<< HOOK HERE
8007375C  240401ff  li     a0,511           ;      (delay slot, sets a0 for the callee)
80073760  00001021  move   v0,zero
80073764  8fbf0020  lw     ra,32(sp)
...
80073778  03e00008  jr     ra
```

There is **no early `jr ra`** anywhere in `0x800735F8` — the only exit is `0x80073778`,
so `0x80073758` is reached on every call.

### >>> RECOMMENDED HOOK: `0x80073758` <<<

* Executes **exactly once per room load**, on all four paths.
* Executes **after** the overlay-A DMA (done in mode 1 / `0x800732E0`), after the
  overlay-B DMA (`0x8007372C` -> `0x800734B0` -> `0x80073978`), after the per-area init
  entry point (`0x800736F0`), after all actor/system init
  (`0x8003B7A4`, `0x80047210`, `0x80051AB0`, `0x80036B7C`), and after the player object
  has been placed by the caller.
* It is a `jal`, so a one-instruction replacement is clean: overwrite `0x80073758` with
  `jal <yourStub>`. The delay slot `0x8007375C` (`li a0,511`) still executes *before* the
  branch target, so your stub receives `a0 = 511` already set; save `ra` and `a0`, take
  the snapshot, then `jal 0x800603F0` with `a0 = 511`, restore `ra`, `jr ra`.
* `0x80073758` is in the boot segment (`0x80025C00`–`0x800D37A0`), i.e. **resident** and
  safe to patch (see `romlayout.md` §3).

Caveat (stated as such): if the load-from-save path (`0x80055954`) is immediately
followed by a mode-1 area load, `0x800735F8` will run twice for one perceived "load".
Last-one-wins snapshotting handles this.

### Secondary hook for overlay-driven sub-room swaps

`0x800734B0` is also called from five **overlay** sites (ROM `0x212CE4`, `0x212D84`,
`0x24BBC8`, `0x2633C4`, `0x26FBBC` — inside overlays A#19, A#23, A#25, A#26), i.e. some
areas swap the B-overlay mid-area without going through `0x800735F8`. To catch those,
additionally hook **`0x800739EC`** (`jal 0x80087260`, the last call in `0x80073978`,
executed after `loadOverlayB`), which fires only when `0x801C452C` actually changed.

### Alternative one-instruction hooks (narrower)

| Address | Instruction | Covers |
|---|---|---|
| `0x8003C19C` | `sb v0,0(s0)` with `v0 = 3` — mode := 3 | full-area loads only |
| `0x8003C5F0` | `sb zero,20(s2)` — clears `0x8020C89C` | in-place sub-room only |
| `0x8003BFD0` | `jal 0x800732e0` | full-area loads, but **before** init |

## A.3 Pollable "loading in progress" flags (fallback for the flush hook)

Ranked:

1. **`*(s8*)0x8020C89C`** — the transition request.
   Non-zero from the moment a door/warp fires until the room is live; cleared to `0` at
   `0x8003C198` (mode-2 path, immediately before `mode := 3`) and at `0x8003C5F0`
   (in-place path, immediately after `0x800735F8` returns). A **non-zero -> 0 edge is the
   room-load-complete edge**. It stays `0` during normal gameplay.
   Limitation: the load-from-save and new-game paths (`0x80055790`, `0x8005C440`) do not
   set it, so those two produce no edge.

2. **`*(s16*)0x801C4520`** (loaded area) and **`*(s16*)0x801C452C`** (loaded sub-room).
   Writers, ROM-wide: `0x80073378` + `0x80055944` for `0x4520`; `0x80073990` +
   `0x80037C28` for `0x452C`. A change on either = an overlay was swapped. These are
   written *before* init finishes, so add a 1–2 frame delay, or combine with (1).

3. **`*(s8*)0x801C44B8`** (game mode): `1` = area load, `2` = room init, `3` = gameplay.
   A `{1,2} -> 3` edge is a load boundary. **Racy**: mode 1 and mode 2 each occupy a
   single main-loop iteration, and the graphics thread is `osStopThread`'d across the
   overlay DMA (`0x800733EC` / `0x800733FC`, `0x800739AC` / `0x800739EC`), so the flush
   may never observe mode 1 or 2. Do not rely on this alone.

4. Bonus: **`*(s8*)0x800D37B0`** is a global "freeze object updates" byte, tested at
   `0x80026330` right before `jalr` on each object's update function at `0x80026338`.
   Non-zero freezes all object updates (and therefore the main-loop tick, see Part C)
   while rendering continues — this is the game's structural pause.

## A.4 Player / Rock object

**`0x8020CA80`** is a static struct (not heap-allocated, no dynamic pointer to chase).

Evidence:
* It is the object whose position is overwritten with the warp destination on **every**
  room transition: `0x8003BF60`–`0x8003BF94` (mode 1) and `0x8003C574`–`0x8003C5DC`
  (mode 3 in-place). Fields: `+0x14/+0x16/+0x18` = `s16` X/Y/Z, `+0x1C/+0x20/+0x24` =
  the same as 16.16 fixed (`x << 16`), `+0x2C…+0x3C` = a duplicate set,
  `+0x56`/`+0x5E` = facing, `+0xBD` = flag byte, `+0x428/+0x438/+0x440` = saved copies.
  Struct size ≥ 0x448.
* Overlay enemy code stores `&0x8020CA80` into its own `+0x380` "target" field
  (six sites, e.g. `0x80111748`+`0x80111758` in the stale boot-time copy of an overlay).
* The camera delta is computed from `lhu v0,20(s0)` / `lhu a2,24(s0)` on it at
  `0x8003C4A4`/`0x8003C4AC`.

**`0x80218420`** holds a pointer to it, written once during init:

```
8002A1C4  2442ca80  addiu  v0,v0,-13696    ; v0 = 0x8020CA80
8002A214  3c018022  lui    at,0x8022
8002A218  0c00a9d7  jal    0x8002a75c
8002A21C  ac228420  sw     v0,-31712(at)   ; 0x80218420 = 0x8020CA80  (delay slot)
```

ROM-wide it has only 4 references (`0x8002A21C` write; `0x8002AC70`, `0x8002AE54`,
`0x8002AE70` reads), all in one small subsystem — so treat it as a sanity-check value
(`*(u32*)0x80218420 == 0x8020CA80`), not as the general accessor.

---

# PART B — Display-list headroom


## A.5 Telling a sub-room swap from a real area change

The hook at `0x80073758` is inside `0x800735F8`, which all four paths end in, so
it cannot tell you *which* path reached it. The automatic snapshot needs to know,
because an in-place sub-room swap happens mid-stride in the middle of an open
area and a 174 ms hitch there is worse than the snapshot is worth.

Two independent signals, both established above:

**The game mode at the hook.** A full area change reaches `0x800735F8` from the
mode-2 handler, which writes mode 3 only *after* the call returns:

```
8003c18c  jal   0x800735f8        <- the hook is inside this
8003c190  nop
8003c194  li    v0,3
8003c19c  sb    v0,0(s0)          ; mode := 3, after
```

An in-place sub-room swap reaches it from inside mode 3 and never touches the
mode at all:

```
8003c5e4  jal   0x800735f8
8003c5e8  nop
8003c5ec  j     0x8003c6f4
8003c5f0  sb    zero,20(s2)       ; just clears the request flag
```

So the mode is **2** for an area change and **3** for a sub-room swap — but only
*at the hook*. One frame later both read 3, so it has to be sampled in the shim
and carried forward. That is the whole reason `room_shim` writes `VAR_PENDMODE`.

**The loaded area.** `0x801C4520` has exactly three references in the 32 MB ROM:
the skip-if-unchanged test and the store inside the area loader (`0x80073324`,
`0x80073378`), and the `-1` the file-load path forces in at `0x80055944`. The
in-place sub-room path calls neither, so the value is unchanged across one.

The snapshot is skipped only when **both** say sub-room, so anything unexpected
errs towards taking one. That also gets the awkward cases right for free: a door
that loops back into its own area goes through mode 1/2 and so fails the first
test; loading a save forces `0x801C4520` to -1 and so fails the second.

Do not be tempted by `0x801C452C`, the sub-room id. It is the right *identity*
for a sub-room, but it changes on some area changes too (a new area loads a new
overlay B), so on its own it answers a different question.

## B.1 Where the DL buffers live

Allocated as compile-time constants in `0x80027550` (the graphics-subsystem init, called
once from `0x80026A14`):

```
80027554  3c050001  lui    a1,0x1
80027558  34a51000  ori    a1,a1,0x1000    ; a1 = 0x00011000   (scratch pool size)
8002755C  3c060001  lui    a2,0x1
80027560  34c6e000  ori    a2,a2,0xe000    ; a2 = 0x0001E000   <<<< DL BUFFER SIZE
80027598  3c028011  lui    v0,0x8011
8002759C  24423290  addiu  v0,v0,12944     ; v0 = 0x80113290
800275A0  3c038012  lui    v1,0x8012
800275A4  24634290  addiu  v1,v1,17040     ; v1 = 0x80124290
800275AC  ac227270  sw     v0,29296(at)    ; 0x800D7270 = 0x80113290  scratch pool [0]
800275B4  ac227278  sw     v0,29304(at)    ; 0x800D7278 = 0x80113290  scratch cursor
800275B8  00451021  addu   v0,v0,a1
800275C0  ac22727c  sw     v0,29308(at)    ; 0x800D727C = 0x80124290  scratch end
800275C8  24427290  addiu  v0,v0,29328     ; v0 = 0x800D7290
800275D0  ac237274  sw     v1,29300(at)    ; 0x800D7274 = 0x80124290  scratch pool [1]
800275D8  24635290  addiu  v1,v1,21136     ; v1 = 0x800F5290
800275E0  ac227280  sw     v0,29312(at)    ; 0x800D7280 = 0x800D7290  <<<< DL BASE [0]
800275E8  ac2211c0  sw     v0,4544(at)     ; 0x801B11C0 = 0x800D7290  cursor
800275EC  00461021  addu   v0,v0,a2        ; v0 = 0x800F5290
80027624  ac237284  sw     v1,29316(at)    ; 0x800D7284 = 0x800F5290  <<<< DL BASE [1]
8002762C  ac227288  sw     v0,29320(at)    ; 0x800D7288 = 0x800F5290  end-of-buffer
```

The size is re-derived every frame at the top of the flush function `0x800276EC`:

```
80027778  8c637280  lw     v1,29312(v1)    ; v1 = 0x800D7280[bufIdx]
8002777C  3c020001  lui    v0,0x1
80027780  3442e000  ori    v0,v0,0xe000    ; 0x1E000
8002778C  00621021  addu   v0,v1,v0
80027794  ac227288  sw     v0,29320(at)    ; 0x800D7288 = base + 0x1E000
800277A0  ac2311c0  sw     v1,4544(at)     ; cursor = base
800277AC  3c02de00  lui    v0,0xde00       ; first command: G_DL (0xDE) at base+0
800277B0  ac620000  sw     v0,0(v1)
800277BC  ac620004  sw     v0,4(v1)        ;   -> 0x8023D020
800277A8  ac2211c0  sw     v0,4544(at)     ; cursor = base + 8
```

### The graphics arena — exact, contiguous, and tight

| Range | Size | Use |
|---|---|---|
| `0x800D7290` – `0x800F5290` | `0x1E000` = 122,880 | **DL buffer 0** (`0x800D7280[0]`) |
| `0x800F5290` – `0x80113290` | `0x1E000` = 122,880 | **DL buffer 1** (`0x800D7280[1]`) |
| `0x80113290` – `0x80124290` | `0x11000` = 69,632 | per-frame scratch pool 0 (`0x800D7270[0]`) |
| `0x80124290` – `0x80135290` | `0x11000` = 69,632 | per-frame scratch pool 1 (`0x800D7270[1]`) |
| `0x80135290` | 4 | first unrelated global (the gfx callback ptr, `0x80027568`) |

Total `0x5E000` = 385,024 bytes, all inside the boot bss (`0x800D37A0`–`0x80225B70`),
zeroed by the entry stub. `tools/xref.py refcount 0x800D7000 0x80140000` confirms there
are **no** other globals anywhere inside `0x800D7290`–`0x80135290`.

The buffers abut: overflowing buffer 0 corrupts buffer 1; overflowing buffer 1 corrupts
scratch pool 0 (which holds this frame's viewports, matrices and lights — allocated by
`0x800281C8` from `0x800D7278`/`0x800D727C`).

Which buffer is used is selected by the byte at `0x8021D66C`, toggled at
`0x80028104`–`0x80028118` (`newIdx = 1 - idx`).

## B.2 Used length, and the only bounds check

**The used length is computed in two places, and nothing in the entire ROM reads it.**

Inside the per-object draw loop (once per object, *before* calling that object's draw
callback):

```
80027CE8  3c060001  lui    a2,0x1
80027CEC  34c6d800  ori    a2,a2,0xd800    ; a2 = 0x0001D800 = 120,832  <<<< SOFT CAP
...
80027E90  3c028021  lui    v0,0x8021
80027E94  9042d66c  lbu    v0,-10644(v0)   ; buffer index
80027E9C  258c7280  addiu  t4,t4,29312     ; &0x800D7280
80027EA4  8c6311c0  lw     v1,4544(v1)     ; cursor
80027EB0  8c420000  lw     v0,0(v0)        ; base
80027EB4  00621023  subu   v0,v1,v0        ; used = cursor - base
80027EBC  ac22728c  sw     v0,29324(at)    ; 0x800D728C = used
80027EC0  00c2102b  sltu   v0,a2,v0        ; (0x1D800 < used) ?
80027EC4  54400019  bnezl  v0,0x80027f2c   ; yes -> skip this object, advance the loop
...
80027F10  0040f809  jalr   v0               ; object draw callback (unbounded emission)
```

and once at the very end, after the terminator:

```
800280F0  00a32823  subu   a1,a1,v1
800280F4  3c01800d  lui    at,0x800d
800280F8  ac25728c  sw     a1,29324(at)    ; 0x800D728C = final length
```

ROM-wide scan for `0x800D728C`: **two sites, both `sw`. Zero readers.** It is a pure
statistics / debug variable — nothing consumes it, nothing asserts on it.

ROM-wide scan for `0x800D7288` (the end-of-buffer pointer): **two sites, both `sw`
(`0x8002762C`, `0x80027794`). Zero readers.** *There is no hard bounds check against the
physical end of the DL buffer anywhere in the game.* (The allocator at `0x800281C8` does
bounds-check, but against the **scratch pool** end `0x800D727C`, not the DL buffer.)

ROM-wide scan for `0x800D7280`: 4 sites, all in the boot segment
(`0x800275E0`, `0x80027778`, `0x80027E9C`, `0x800280BC`) — no overlay touches it.

**So the only guard is the soft cap `0x1D800` = 120,832 bytes at `0x80027EC0`.** When it
trips, the game silently stops submitting further objects for the rest of the frame —
objects pop out of existence, nothing crashes and nothing is reported.

**Typical used length cannot be determined statically** (it depends entirely on scene
content). What *is* static:

* physical buffer size: `0x1E000` = 122,880
* soft cap: `0x1D800` = 120,832
* **slack between the cap and the physical end: `0x800` = 2,048 bytes**
* after the object loop exits, the fixed tail emits at most **7 × 8 = 56 bytes**
  (`0x80027F58`–`0x80028070`, conditional on `*(u8*)(viewport+159) != 0`), then the
  terminator adds **16 bytes** (`0x80028088`–`0x800280DC`).

## B.3 Is appending 512 bytes safe?

**Yes, with one caveat, and you will not trip any guard — because there isn't one.**

Worst-case accounting at the point you want to insert (immediately before `0x8002809C`):

```
used at the last guard check          <= 0x1D800        (120,832)
+ last object's emission              = U               (unbounded in principle)
+ fixed tail                          <= 0x38           (56)
+ YOUR INSERTION                      = 0x200           (512)
+ G_RDPFULLSYNC + G_ENDDL             = 0x10            (16)
--------------------------------------------------------
must stay <= 0x1E000                  (122,880)
=> U <= 0x5B8 = 1,464 bytes            (was 0x7B8 = 1,976 without your 512)
```

So your 512 bytes eat 25 % of the existing safety margin. Recommendations:

1. **Preferred:** also patch the cap constant so the margin is unchanged — change
   `0x80027CEC` from `ori a2,a2,0xd800` (`0x34C6D800`) to `ori a2,a2,0xd600`
   (`0x34C6D600`), i.e. cap `0x1D600`. That reclaims exactly 512 bytes from the object
   budget and leaves the physical margin at 2,048 bytes.
2. Insert **after** the object loop and **before** `0x8002809C`, as you planned — that is
   the right place: the cursor `0x801B11C0` is the single write pointer, and
   `0x800280F0` recomputes the final length from it, so the length reported downstream
   stays correct automatically as long as you advance `0x801B11C0` by 512.
3. Do not append **after** `G_ENDDL` — the RSP stops there, and `0x800280F0`'s length
   would then cover bytes the RSP never reads (harmless but pointless), while
   `0x80028104`+ flips the buffer immediately.
4. Do not exceed ~1.5 KB of insertion without also lowering the cap.

## B.4 Is `0x80028088`–`0x800281B8` the only place a graphics task is kicked?

**Yes for everything after boot.** ROM-wide scan for the instruction word `0x0C024A4B`
(`jal 0x8009292C`) across all 33,554,432 bytes finds exactly **three** occurrences:

| ROM | RAM | Function | Classification |
|---|---|---|---|
| `0x000010F8` | `0x80025CF8` | boot entry stub (`0x80025C00`) | one-shot, builds a 24-byte DL on the stack (`0xDE000000, 0x000A6980, 0xE9000000, 0, 0xDF000000, 0`) to clear the framebuffer at reset |
| `0x000011B4` | `0x80025DB4` | `0x80025D14` | one-shot, same shape; `0x80025D14` is called exactly once, from `0x80027680` inside the graphics init `0x80027550` |
| `0x000034FC` | `0x800280FC` | `0x800276EC` | **the per-frame flush** |

No overlay and no part of segment 2 calls `0x8009292C`. `0x8009292C` itself is the
scheduler submit: it fills an `OSScTask` at `*(0x80195300)` — `+0x40` = `data_ptr` (your
DL base), `+0x44` = `data_size` (the length from `0x800280F0`), `+0x0C` = framebuffer,
`+0x14` = task flags — and bumps the in-flight-task count at `0x80207C80`
(`0x80092A74`).

**Therefore a single hook just before `0x8002809C` covers every screen** — title, file
menu, cutscenes, gameplay, results. There is exactly one flush path. (The two boot-time
kicks happen before any hook of yours would matter, and they build their DL on the stack,
not in `0x800D7290`.)

Corollary: the flush is *skipped* on some frames — see Part C.

---

# PART C — Frame pacing

## C.1 The retrace chain (all 30 Hz)

```
VI interrupt
  -> VI manager thread 0x80091E44, waits for msg 666
       0x80091EB8  0x800CF404++                 (raw retrace count)
       0x80091EBC  jal 0x800920DC               broadcast to registered clients
  -> 0x800920DC walks the client list at 0x8021D8B8 and osSendMesg()s each match
  -> scheduler thread 0x800925B8 (created in 0x80092560: osCreateThread(0x801C3840,
     entry 0x800925B8, pri 50)) receives msg, reads its type halfword:
       80092618  beq v0,1  -> 0x80092630
       80092634  lw v0, 0x800CF410             (callback ptr, = 0x80026A30)
       80092644  jalr v0  with a0 = *(u32*)0x80207C80   (in-flight gfx task count)
  -> 0x80026A30  the per-frame callback
```

The retrace client is registered by `0x80091BE0(a0, a1 = retraceCount)`, and its only
caller is `0x80089CFC`:

```
80089D38  24040002  li   a0,2
80089D3C  0c0246f8  jal  0x80091be0
80089D40  24050002  li   a1,2       ; <<<< retraceCount = 2
```

**retraceCount = 2 -> one retrace message every 2 NTSC fields = 30 Hz.** This is an
independent confirmation of the 30 Hz rate that `docs/timer.md` derived from the `/30`
magic at `0x8007667C`.

## C.2 What one callback does, in order

```
80026A30  <cb>(a0 = in-flight gfx task count)
80026A40    move s0,a0
80026A5C    0x800A6950++                          raw callback count
80026A60    bnez 0x800A6A70 -> 0x80026B8C          reentrancy guard: bail
80026A74    0x800A6A70 = 1
80026A80    0x800A6954++ ; jal 0x80028BC4          -> 0x80092E00(0x801C4450) = READ PADS
80026A90    bnez *(u8*)0x800A6958 -> 0x80026B40    "render disabled" (see below)
80026AA0    jal 0x800281BC(s0)
80026AB0    beqz *(u8*)0x800CEE10 -> 0x80026B24    0 on NTSC (set !=0 only if osTvType!=1)
80026B24    jal 0x800261C4                         >>> OBJECT-LIST UPDATE <<<
80026B38    jal 0x800276EC(a0 = s0)                >>> BUILD + FLUSH DL <<<
80026B84    0x800A6A70 = 0
```

`*(u8*)0x800A6958` is **read at `0x80026A8C` and written nowhere in the entire 32 MB ROM**
(verified by ROM-wide scan), so it is permanently 0 and that branch is dead.

`0x800261C4` walks the object list (head `0x800D37B4`, cursor `0x801C39F4`) and calls each
node's `+0x14` update function:

```
8002631C  8c430014  lw    v1,20(v0)         ; node->update
80026328  804237b0  lb    v0,14256(v0)      ; *(s8*)0x800D37B0  "freeze" flag
80026330  14400003  bnez  v0,0x80026340     ; frozen -> skip the call
80026338  0060f809  jalr  v1
```

One of those node update functions is `0x80032908` (installed via
`0x800328F0`–`0x800328F8` -> `0x80026810`, which writes `a0` into `(*0x801C39F4)->+0x14`),
and it contains:

```
800329A8  0c00a3c2  jal  0x80028f08          >>> COOPERATIVE SCHEDULER TICK <<<
```

`0x80028F08` scans the 4 task slots at `0x801C3A00` (stride 456), decrements each waiting
slot's countdown at `+6`, and when it hits 0 does `osSendMesg` on that slot's queue
(`0x801352A0 + idx*24`, `0x80028FD8`) — **which is exactly what unblocks the main loop**:

```
800290C4  <frameSync>(a0)
80029120  a2020000  sb   v0(1),0(s0)         ; slot->waiting = 1
80029128  a6020002  sh   v0(1),2(s0)         ; slot->state   = 1
8002913C  a6110006  sh   s1,6(s0)            ; slot->countdown = a0  (main loop passes 1)
8002914C  0c0268a8  jal  0x8009a2a0          ; osRecvMesg(slotQueue, NULL, BLOCK)
```

and the main loop's back edge is

```
8003E274  0c00a431  jal  0x800290c4
8003E278  24040001  li   a0,1                ; wait 1 tick
8003E27C  0800f86b  j    0x8003e1ac
```

## C.3 Answer

**Both the main-loop tick and the graphics flush hang off the same 30 Hz retrace
callback, and in the normal case they are 1:1 — but the flush can be skipped on a frame
where the loop still ticks.**

Within one callback, `0x800261C4` (which ticks the main loop) runs *before*
`0x800276EC` (which flushes). The three ways they can diverge:

| Condition | Main loop ticks? | Flush at `0x80028088` runs? |
|---|---|---|
| Normal | yes | yes |
| Callback re-entered (`0x800A6A70 != 0`, bail at `0x80026A60`) | no | no |
| `*(u8*)0x800A6958 != 0` (`0x80026A90`) | no | no — **dead code, never happens** |
| **>= 2 gfx tasks already in flight** | **yes** | **no** |
| `*(s8*)0x800D37B0 != 0` (object updates frozen, `0x80026330`) | **no** | **yes** |
| gfx thread `osStopThread`'d across an overlay DMA (`0x800733EC`/`0x800733FC`, `0x800739AC`/`0x800739EC`) | yes (main thread is the one doing the DMA) | no |

The backpressure case is this, at the top of `0x800276EC`:

```
800276EC  27bdff80  addiu  sp,sp,-128
800276F4  28840002  slti   a0,a0,2         ; a0 = in-flight gfx task count (0x80207C80)
80027730  1080027b  beqz   a0,0x80028120   ; >= 2 in flight -> skip ALL rendering,
                                           ;    jumps past 0x80028088, no task kicked
80027738  8c42424c  lw     v0,16972(v0)    ; else: 0x801C424C++  (RENDER FRAME COUNTER)
80027748  ac22424c  sw     v0,16972(at)
```

`0x80207C80` is incremented by `0x8009292C` (`0x80092A74`) and decremented by the
task-done handler `0x80092690` (`0x80092774`); `0x80092B70` is a busy-wait for it to
reach 0.

### Practical answer for your HUD hook

**Your hook at `0x80028088` fires once per *rendered* frame, not strictly once per
game-logic frame.** They coincide whenever the RSP/RDP is keeping up. If the RDP falls
behind by two frames, the game logic advances (`0x801C44C4` increments) while your HUD
code does not run. For a HUD this is exactly what you want — you draw when a frame is
actually drawn.

The game already maintains both counters for you:

| Counter | Address | Incremented at | Meaning |
|---|---|---|---|
| main-loop / play time | `0x801C44C4` | `0x8003E1D4` | one per main-loop iteration |
| **render frames** | **`0x801C424C`** | **`0x80027748`** | one per successful DL build+flush |

`0x801C424C` is read at `0x8002600C` and stamped into object fields (`+0x08`) as a
"last rendered on frame N" timestamp — confirming the semantics. ROM-wide it has exactly
3 references (`0x8002600C` read, `0x8002773C` read, `0x80027748` write).

---

# Things that could NOT be proven statically, and the experiments that settle them

### E1 — Is the flush exactly 1:1 with the main-loop tick in practice?
Static analysis proves only that both are driven by the same 30 Hz retrace callback and
that the flush is additionally gated on `0x80207C80 < 2`.

**Experiment (BizHawk N64, RDRAM domain = RAM address & 0x1FFFFFF):**
RAM Watch `0x001C44C4` (u32 BE) and `0x001C424C` (u32 BE). Note `delta = 44C4 - 424C` at
the start of gameplay. Play 5 minutes including a heavy scene (Main Gate boss, a crowded
street). If `delta` ever increases, the flush was skipped and the two are **not** 1:1.
Cross-check with an execute breakpoint on `0x80027730`, condition "branch taken".
Also watch `0x00207C80` (u32) — it should sit at 0 or 1 and only reach 2 under load.

### E2 — Typical and worst-case DL length.
**Experiment:** execute breakpoint at `0x800280F8` (the final `sw a1,0x800D728C`), log
`a1` (or just RAM-watch `0x000D728C` as u32 BE with a max-tracker). Walk every area in
the game, including the crowded city, the boss rooms and the ending. Compare the observed
max against `0x1D800` (120,832) and `0x1E000` (122,880). Also breakpoint `0x80027EC4`
(the soft-cap bail); if it ever fires you are already at the limit in stock, and adding
512 bytes will make objects disappear slightly sooner.

### E3 — Does `0x800735F8` ever run twice per perceived load?
**Experiment:** execute breakpoint at `0x80073758`. It should hit exactly once per
door/warp. Also breakpoint `0x8003C18C` and `0x8003C5E4` to see which path fired.
Verify `0x001C4508` (s8) and `0x001C4509` (u8) hold the new area/entrance at that moment,
and `0x001C4520` (s16) equals `0x001C4508`.

### E4 — Confirm `0x8020CA80` is Rock.
**Experiment:** RAM Watch `0x0020CA94` (s16, X), `0x0020CA96` (s16, Y), `0x0020CA98`
(s16, Z). Walk Rock around; all three must track him. Then write-breakpoint
`0x8020CA94`; it must fire from the player movement code every frame, and from
`0x8003BF84`/`0x8003C584` on a transition. Also verify `*(u32*)0x80218420 == 0x8020CA80`.

### E5 — Is the area id really the overlay-A index at runtime?
**Experiment:** execute breakpoint at `0x800733F4` (`jal loadOverlayA`). Log `a0` and
compare against `0x001C4508`; then check that the descriptor at
`0x800BD610 + a0*0x24` has the `romStart` listed in `romlayout.md` for that index.

### E6 — Overlay-driven sub-room swaps.
**Experiment:** execute breakpoints at ROM-loaded addresses corresponding to the five
overlay call sites of `0x800734B0` are impractical; instead breakpoint `0x800734B0`
itself and log `ra`. Any `ra` outside `0x80073730` / `0x80073BD0` is an overlay-driven
swap that your `0x80073758` hook would miss — add the `0x800739EC` hook if you see them.

### E7 — The freeze flag `0x800D37B0`.
**Experiment:** RAM Watch `0x000D37B0` (u8) while opening the in-game pause/menu. If it
goes to 1, it is the pause flag and the main-loop tick stops while rendering continues —
which also explains why `0x801C44C4` does not advance in some menus (`timer.md` §7).
