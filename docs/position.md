# Rock's world position, facing and state

Rockman DASH – Hagane no Boukenshin (N64, JP, NRHJ). All instruction addresses are
KSEG0 and come from `build/boot.asm`. Neither `baserom.z64` nor `build/boot.asm` was
modified. Builds on `docs/roomhook.md` §A.4 (player object = static struct at
`0x8020CA80`).

---

## 0. TL;DR — what to watch

| Quantity | Address | Type | Notes |
|---|---|---|---|
| **X (live, fine)** | **`0x8020CA9C`** | `s32` **Q16.16** | 1 world unit = `0x10000` |
| **Y (live, fine)** | **`0x8020CAA0`** | `s32` Q16.16 | **vertical, DOWN-positive** |
| **Z (live, fine)** | **`0x8020CAA4`** | `s32` Q16.16 | |
| **X (live, integer)** | **`0x8020CA94`** | `s16` | = high half of `0x8020CA9C` |
| **Y (live, integer)** | **`0x8020CA96`** | `s16` | = high half of `0x8020CAA0` |
| **Z (live, integer)** | **`0x8020CA98`** | `s16` | = high half of `0x8020CAA4` |
| Facing (yaw) | `0x8020CAD6` | `u16`, mask `0xFFF` | **4096 = 360°** |
| Yaw bias (subtract for world yaw) | `0x8020CB96` | `u16` | see §6 |
| Velocity X / Y / Z | `0x8020CAC4/C6/C8` | `s16` ×3 | units of **1/16 unit per frame** |
| Grounded / floor id | `0x8020CAF4` | `s8` | `0` = airborne |
| Action state / sub-state | `0x8020CA89` / `0x8020CA8A` | `u8` | jump-table indices |

The struct is **static** (boot bss, absolute `lui`/`addiu` at ~80 sites). **Hardcode the
absolute addresses.** Do *not* dereference `0x80218420` — it is written once at
`0x8002A21C` and is only useful as a sanity check (`*(u32*)0x80218420 == 0x8020CA80`).

Human-readable value: `x = *(s32*)0x8020CA9C / 65536.0`. Or just read the `s16` mirror
at `0x8020CA94` if whole units are enough — the game itself uses that mirror for the
camera and for collision.

---

## 1. The player struct's position block

From the warp placement code (`roomhook.md` §A.2 already noted these offsets; here is the
full field map, derived below):

```
+0x14  s16  X          \
+0x16  s16  Y           |  live position, integer units
+0x18  s16  Z          /
+0x1A  s16  (unused/pad)
+0x1C  s32  X          \
+0x20  s32  Y           |  live position, Q16.16  (this is the authoritative copy)
+0x24  s32  Z          /
+0x28  s32  (4th slot, copied along with the vec)
+0x2C  s16  prevX \
+0x2E  s16  prevY  |  position at the start of this frame (collision restore target)
+0x30  s16  prevZ /
+0x34  s32  prevX \
+0x38  s32  prevY  |  same, Q16.16
+0x3C  s32  prevZ /
+0x44  s16  velX   \
+0x46  s16  velY    |  velocity, 1/16 unit per frame
+0x48  s16  velZ   /
+0x4C  s16  accX   \
+0x4E  s16  accY    |  acceleration (added to velocity); confirmed for the generic
+0x50  s16  accZ   /   updater 0x8005EE7C, plausible-but-unproven for Rock
+0x56  u16  yaw        (rotation about Y)
+0x5E  u16  yaw (second copy, written at warps)
+0x74  s8   floor / grounded byte
+0x78  ptr  floor-geometry pointer
+0x116 u16  yaw bias
+0x428 (16 B) saved copy of +0x1C block (transition backup)
+0x438 (8 B)  saved copy of +0x14 block (transition backup)
+0x440 (8 B)  saved copy of +0x54 block (yaw backup)
```

---

## 2. Proof that `+0x1C` is Q16.16 and `+0x14` is its integer part

### 2a. The velocity integrator — `0x8005EF38`–`0x8005EF8C` (irrefutable)

```
8005EF38  86020044  lh   v0,68(s0)    ; velX      (+0x44)
8005EF3C  8e03001c  lw   v1,28(s0)    ; posX32    (+0x1C)
8005EF40  86040048  lh   a0,72(s0)    ; velZ      (+0x48)
8005EF44  8e050024  lw   a1,36(s0)    ; posZ32    (+0x24)
8005EF48  00021300  sll  v0,v0,0xc    ; velX << 12
8005EF4C  00621821  addu v1,v1,v0
8005EF50  86020046  lh   v0,70(s0)    ; velY      (+0x46)
8005EF54  ae03001c  sw   v1,28(s0)    ; posX32 += velX<<12
8005EF58  8e030020  lw   v1,32(s0)    ; posY32    (+0x20)
8005EF5C  00042300  sll  a0,a0,0xc
8005EF60  00a42821  addu a1,a1,a0
8005EF64  ae050024  sw   a1,36(s0)    ; posZ32 += velZ<<12
8005EF68  86040024  lh   a0,36(s0)    ; <<< lh at +0x24  = HIGH half of posZ32
8005EF6C  00021300  sll  v0,v0,0xc
8005EF70  00621821  addu v1,v1,v0
8005EF74  8602001c  lh   v0,28(s0)    ; <<< lh at +0x1C  = HIGH half of posX32
8005EF78  ae030020  sw   v1,32(s0)    ; posY32 += velY<<12
8005EF7C  86030020  lh   v1,32(s0)    ; <<< lh at +0x20  = HIGH half of posY32
8005EF80  a6040018  sh   a0,24(s0)    ; +0x18 = intZ
8005EF84  a6020014  sh   v0,20(s0)    ; +0x14 = intX
8005EF88  08017be9  j    0x8005efa4
8005EF8C  a6030016  sh   v1,22(s0)    ; +0x16 = intY
```

MIPS is big-endian, so `lh rt,0x1C(base)` reads bits 31..16 of the word at `+0x1C`. The
code writes the word, then re-reads its top half and stores it into the `s16` mirror.
**Therefore `+0x14` ≡ `+0x1C >> 16` and the binary point of `+0x1C` sits at bit 16:
signed Q15.16 ("Q16.16"), 1 world unit = 65536.**

### 2b. The rounding variant — `0x8004DCC4`

Called as `f(&obj+0x1C, &obj+0x14)` from `0x8004DAC0`:

```
8004DCC4  8c820000  lw   v0,0(a0)      ; pos32
8004DCC8  00021c03  sra  v1,v0,0x10    ; pos32 >> 16
8004DCCC  000213c3  sra  v0,v0,0xf
8004DCD0  30420001  andi v0,v0,0x1     ; bit 15 (the 0.5 bit)
8004DCD4  00621821  addu v1,v1,v0      ; round half up
8004DCD8  a4a30000  sh   v1,0(a1)      ; -> +0x14
```
…repeated at `+4/+8 -> +2/+4`. Same 16-bit binary point, this time with rounding, so the
`s16` mirror can be 1 higher than the truncated high half. **The Q16.16 word is the
authoritative value.**

### 2c. Independent confirmation — the `+8 units` nudge at `0x80046848`

```
80046848  96420014  lhu   v0,20(s2)    ; intX
8004684C  8e43001c  lw    v1,28(s2)    ; posX32
80046850  24420008  addiu v0,v0,8      ; intX += 8
80046854  a6420014  sh    v0,20(s2)
80046858  3c020008  lui   v0,0x8       ; 0x00080000 = 8 << 16
8004685C  00621821  addu  v1,v1,v0
80046864  ae43001c  sw    v1,28(s2)
```

### 2d. And the warp placement (`0x8003BF38`–`0x8003BF90`, `0x8003C554`–`0x8003C5A4`)

Spawn coords are `s16` in the warp request `0x8020C890/892/894`; they are loaded with
`lh` (**signed**) and stored as `x<<16` into `+0x1C/+0x20/+0x24` and as-is into
`+0x14/+0x16/+0x18` (and duplicated into the `+0x2C/+0x34` prev-position block so the
first frame's collision sweep has zero length).

### Velocity scale (bonus)

`vel << 12` added to a Q16.16 position ⇒ **velocity LSB = 2^12/2^16 = 1/16 world unit per
frame**. The same `sll 12` appears in Rock's own mover at `0x8004DC64`
(`sll v0,s2,0x10; sra v0,v0,0x4`). A different object class (`0x8007B9A8`) uses
`sll 8` = 1/256 unit/frame — do not assume the 1/16 scale for non-player objects.

---

## 3. Which triple is live position — ranked

| Rank | Addresses | What it is | Why |
|---|---|---|---|
| **1** | `0x8020CA9C/AA0/AA4` (s32) and `0x8020CA94/96/98` (s16) | **LIVE position** | Written every frame by the integrator (§2a); read by the camera (§4a); restored *from* the prev block on collision (§3a); overwritten with the warp destination on every room change |
| 2 | `0x8020CAB4/AB8/ABC` (s32), `0x8020CAAC/AAE/AB0` (s16) | **previous-frame position** | It is the *source* of the collision restore and the *destination* of the end-of-move copy (§3a) |
| 3 | `0x8020CAC4/C6/C8` | **velocity**, not position | `sll 12` then added to the position words (§2a); zeroed on collision |
| 4 | `0x8020CEA8` (16 B) / `0x8020CEB8` (8 B) | **transition snapshots** | `memcpy(obj+0x428, obj+0x1C, 16)` at `0x8003BFB8` / `0x8003C5CC`, only on warps |
| 5 | `0x8020C890/892/894` | **pending warp spawn point**, `s16` | Only meaningful while `*(s8*)0x8020C89C != 0` |

### 3a. `0x80040EE0` — the move/collide routine, which settles ranks 1 vs 2

`a0 = object`. `s4 = obj+0x14` (new), `s3 = obj+0x2C` (old):

```
80040F3C  addiu s4,s0,20        ; &newPos  (+0x14)
80040F44  addiu s3,s0,44        ; &oldPos  (+0x2C)
80040F5C  jal   0x8004118c      ; swept collision(newPos, oldPos, …)
...
; --- on a blocked move: restore X and Z from the PREV block ---
80041014  lhu v0,44(s0)  ; prevX16     80041030  sh v0,20(s0)
80041018  lhu v1,48(s0)  ; prevZ16     80041034  sh v1,24(s0)
8004101C  lw  a0,52(s0)  ; prevX32     80041038  sw a0,28(s0)
80041020  lw  a1,60(s0)  ; prevZ32     80041040  sw a1,36(s0)
; --- Y handled separately (ceiling/floor) ---
80041044  lhu v0,46(s0)  ; prevY16     80041054  sh v0,22(s0)
80041048  lw  v1,56(s0)  ; prevY32     8004105C  sw v1,32(s0)
80041050  sh  zero,70(s0)             ; velY = 0
8004104C  sb  zero,116(s0)            ; +0x74 = 0  (lost the floor)
; --- the other blocked branch (s1 == 0): full restore, current := prev ---
80041060  jal 0x8009a074  (a0=obj+0x14, a1=obj+0x2C, n=8)    ; memcpy(dst,src,n)
80041074  jal 0x8009a074  (a0=obj+0x1C, a1=obj+0x34, n=16)
```

Note the direction: `0x8009A074` is `memcpy(dst, src, n)` (cf. `0x8003BFA0`, which saves
`memcpy(obj+0x438, obj+0x14, 8)`). Both branches copy **`+0x2C` → `+0x14`**, i.e. they
*undo* the move. Nothing here ever writes `+0x2C` from `+0x14`.

The matching **`prev := current`** copy lives at the end of each object's own update, e.g.
`0x8005EE98` (generic updater, before the move), `0x8006EB20`, and — in the Rock-module
mover `0x8006F8C0` — immediately after the `jal 0x80040EE0`:

```
8006F900  0c0103b8  jal   0x80040ee0
8006F908  2604002c  addiu a0,s0,44      ; dst = obj+0x2C
8006F90C  26050014  addiu a1,s0,20      ; src = obj+0x14
              jal 0x8009a074, a2=8
              then a0=obj+0x34, a1=obj+0x1C, a2=16
```

A further sanity check on the pairing: `0x8004D480` ("shift this object by a world
delta", used for moving platforms) adds the same X/Z delta to **both** blocks —
`+0x14/+0x18` and `+0x1C/+0x24`, then `+0x2C/+0x30` and `+0x34/+0x3C` — which only makes
sense if they are two snapshots of one quantity.

**Rock really uses this routine.** `0x80069B14` is the player update:

```
80069B40  3c108021  lui   s0,0x8021
80069B44  2610ca80  addiu s0,s0,-13696   ; s0 = 0x8020CA80  (Rock)
80069B6C  0c01a60d  jal   0x80069834     ; a0 = s0
```
and `0x80069834` ends with `jal 0x80040EE0` (`0x80069A08`, `a0 = s0`). `0x80069B14` is
called from 14 sites, all inside the mode-3 gameplay handler `0x8003C1B8` and friends.

---

## 4. Axis convention: **Y is vertical and increases DOWNWARD**

Four independent proofs.

### 4a. The camera pan zeroes the middle component — `0x8003C4A0`

`s2 = 0x8020C888` (warp request), `s0 = 0x8020CA80` (Rock):

```
8003C4A0  lhu v1,8(s2)    ; destX
8003C4A4  lhu v0,20(s0)   ; Rock X   (+0x14)
8003C4A8  lhu a1,12(s2)   ; destZ
8003C4AC  lhu a2,24(s0)   ; Rock Z   (+0x18)
8003C4B8  a7a00012  sh zero,18(sp)   ; <<<< vec.y := 0
8003C4BC  subu v1,v1,v0                ; vec.x = dX
8003C4C0  subu a1,a1,a2                ; vec.z = dZ
8003C4CC  sh v1,16(sp)  /  8003C4D0  sh a1,20(sp)
8003C50C  jal 0x80034d20  (a0 = sp+16)
```
The vec is `{dx, 0, dz}` — a horizontal-only camera shift. X and Z are the ground plane.

### 4b. Object transforms only rotate about Y — `0x80050580` (generic object draw)

```
80050564  lh   v0,20(s2)        ; X
80050578  sllv v0,v0,v1         ; << global scale shift (0x80218D01, normally 0)
8005057C  sw   v0,0(s0)         ; 0x80218B44 = X
80050580  lh   v0,22(s2)  ...   ; 0x80218B48 = Y
80050590  lh   v0,24(s2)  ...   ; 0x80218B4C = Z
80050598  sh   zero,16(sp)      ; rot.x = 0
800505A8  lhu  v0,86(s2)        ; yaw
800505AC  lhu  v1,278(s2)       ; yaw bias
800505BC  subu v0,v0,v1
800505C4  sh   v0,18(sp)        ; rot.y = yaw - bias
800505B8  sh   zero,20(sp)      ; rot.z = 0
800505C0  jal  0x800313d8       ; build rotation matrix
```
Characters rotate about **Y** only ⇒ Y is the up/down axis. (The scale shift at
`0x80218D01` is zeroed by room init at `0x800736C4`, so world units map 1:1 into model
space in normal play.)

### 4c. Gravity is **+** on Y-velocity, jumps are **−** — `0x8006AB54` (a Rock state handler,
listed in the table at `0x800B3290`)

```
8006AC7C  lhu   v0,70(s0)      ; velY
8006AC80  addiu v0,v0,48       ; velY += 48   <<<< GRAVITY
8006AC84  sh    v0,70(s0)
...
8006AC18  li    v0,-800  -> sh v0,70(s0)      <<<< jump / launch
8006ADCC  li    v0,-448  -> sh v0,70(s0)
8006B7B4  li    v0,-352  -> sh v0,70(s0)
8006B1E4  li    v0,-128  -> sh v0,70(s0)
```
Jump = negative velY, gravity = positive velY ⇒ **decreasing Y = going up**. (Gravity
= 48/16 = 3 units/frame²; a −352 jump = 22 units/frame ≈ 80 units of peak height.)

### 4d. Ground snap / step-down — `0x80069940` (Rock's floor handler)

```
80069938  jal 0x8003e8e4       ; floor query -> v0
80069940  lw  v1,120(s0)       ; +0x78 floor record
80069944  lh  v1,10(v1)
80069948  lh  a0,22(s0)        ; Rock Y
8006994C  subu v1,v0,v1        ; v1 = floor Y
80069950  addiu v0,v1,-32
80069954  slt  v0,a0,v0        ; RockY < floorY - 32 ?
80069958  beqz v0,0x800699f8   ;   no -> just snap
   ...   (yes -> enter the falling / land state, 0x800699C8 sb v0,9(s0) with v0=7)
800699F8  beqz v0,0x80069a08   ; v0 = (RockY < floorY)
800699FC  sll  v0,v1,0x10
80069A00  sh   v1,22(s0)       ; RockY := floorY
80069A04  sw   v0,32(s0)       ;  (and the Q16.16 copy)
```
With Y-down this reads naturally: within 32 units *above* the floor, snap down onto it
(step-down); more than 32 units above it, start falling. **32 units is the step-down /
ground-stick tolerance.**

The floor query `0x8003E8E4` also confirms the field roles:
`lh v0,22(a0)` (curY) − `lh a2,46(a0)` (prevY) = the vertical sweep delta.

### Naming

Nothing in the boot segment gives a symbol. `X / Y / Z` in struct order is the game's own
order (the warp request, the collision scratch `0x801E0F78/F7A/F7C` and the rotation vec
are all laid out X,Y,Z). Y is height (down-positive), X/Z are the horizontal plane. This
is the PlayStation convention the PS1 original used, kept in the N64 port.

---

## 5. Typical magnitudes and bounds

Hard bound: the `s16` mirror makes the usable range **−32768 … +32767 world units** on
every axis. Nothing wider can survive a frame, because the mirror is rebuilt from the
Q16.16 word every frame.

Real hard-coded warp destinations found in `boot.asm` (each `li` immediately before a
store into the warp request `0x8020C890/892/894/89A`):

| site | X | Y | Z | yaw |
|---|---:|---:|---:|---:|
| `0x800DB6B0` | 2822 | −1 | −4833 | — |
| `0x800F6A98` | 2560 | −610 | −8192 | 1536 |
| `0x800F6F5C` | −512 | −257 | −768 | 2048 |
| `0x800F7D64` | 1800 | −129 | −40 | — |
| `0x8010C60C` | 256 | −1 | −29440 | — |
| `0x8010C658` | −3179 | −385 | 2816 | 1024 |

So:
* **X, Z: signed, routinely in the thousands, occasionally near ±30000.** Budget 6
  characters plus a sign.
* **Y: signed, small, almost always ≤ 0** (ground datum ≈ 0, negative = above it). The
  observed spawn values are −1, −129, −257, −385, −610 — i.e. −(128·n + 1), suggesting
  floor decks at 128-unit spacing. 4 characters plus a sign is plenty; jumps add a few
  hundred.
* Scale anchor: the camera look-at target is `RockY − 118` (`0x80059B4C`,
  `0x80059F60`: `lh v0,22(v1); addiu v0,v0,-118`) and the lock-on point is `RockY − 100`
  (`0x80054C58`). With Y-down and the origin at Rock's feet, **Rock is ≈ 118 units
  tall**, i.e. **1 unit ≈ 1.2 cm**. Divide by ~78 to get metres if you want SI.

Bounds checks / clamps actually present:
* `0x80069950` — the ±32-unit ground-stick window quoted in §4d.
* `0x8007B80C` — a range test on `targetX − RockX`: `addiu v1,v1,26750; sltu v1,0xd0fc,v1`
  i.e. “|Δ| ≤ 26750 units”, the game's idea of a whole-map span.
* There is **no** absolute world-extent clamp on the player's position anywhere in the
  boot segment. Out-of-bounds is prevented by the collision mesh, not by a min/max.

---

## 6. Facing and state (the bonus)

**Angle unit: 4096 = 360°.** Both trig helpers start by masking:

```
80032480  sin:  andi a0,a0,0xfff   ; sin(0x400)=  4096  (Q12)
800324C0  cos:  andi a0,a0,0xfff   ; cos(0x400)=  0
```
so `degrees = raw * 360.0 / 4096.0` (= `raw * 0.087890625`). Observed spawn yaws 1024 /
1536 / 2048 = 90° / 135° / 180°.

* **`0x8020CAD6`** (`+0x56`), `u16` — Rock's yaw. Incremented by the per-frame angular
  velocity (`0x8005EFA4`), written at every warp from `0x8020C89A` (`0x8003BF94`).
* **`0x8020CB96`** (`+0x116`), `u16` — a yaw *bias*. Both the renderer (`0x800505A8`) and
  the mover (`0x8006FA34`: `subu v0,v0,v1` before moving, `addu` after) use
  **`yaw − bias`** as the true world heading. In normal standing/walking the bias is 0
  (`0x8006AB9C  sh zero,278(s0)`), so `0x8020CAD6` alone is right most of the time; use
  `(0x8020CAD6 − 0x8020CB96) & 0xFFF` if you want it exact during turns/strafes.
* `0x8020CADE` (`+0x5E`) — a second yaw copy written only at warps. Not live.

**Grounded: `0x8020CAF4` (`+0x74`), `s8`.** `0` = no floor / airborne. Non-zero = the id
of the floor surface Rock is standing on, copied from the collision result byte
`0x8022842C` at `0x80040FFC`–`0x80041008`, and cleared at `0x8004104C` whenever the
vertical move is rejected. Rock's own handler gates the ground query on it:
`0x80069924  lw v0,116(s0); and 0xFF00FF00; bnez -> skip`.

**Grounded *last* frame: `0x8020CAF5` (`+0x75`).** `0x8006F8FC  sb a1,117(s0)` copies
`+0x74` into `+0x75` (and `+0x76` into `+0x77`) immediately before the move each frame.
`+0x74 != 0 && +0x75 == 0` is a clean **landing edge**; `+0x74 == 0 && +0x75 != 0` is a
**leaving-the-ground edge**.

Other bytes a speedrunner may want (identified but not fully mapped):
* `0x8020CA88` (`+0x08`) — control mode; `{1,2}` = normal player control
  (`0x80069910`), `3` = a special mode tested at warp time (`0x8003BF48`).
* `0x8020CA89` (`+0x09`) — major action state, index into the jump table at `0x800D2C08`
  (range 0..10). `7` is the fall/land state set at `0x800699C8`.
* `0x8020CA8A` (`+0x0A`) — sub-state / step counter, index into `0x800D2AF8` (range 0..6).
* `0x8020CAC4/C6/C8` — velocity, `s16`, 1/16 unit per frame (see §2). `velY` is the
  cleanest "am I in the air and which way" readout: `0` on the ground, negative rising,
  positive falling.

---

## 7. Stability, and reading from the graphics-thread hook at `0x80028088`

**The address never moves.** `0x8020CA80` is a compile-time bss address; every one of the
~80 references in `boot.asm` is an absolute `lui 0x8021 / addiu -13696` pair (see
`tools/xref.py ref 0x8020CA80 0x448`). It is *below* the resident-segment-2 boundary
`0x80225B70` and far below the volatile overlay slots `0x8022D2F0`, so no overlay load can
relocate or reuse it (`docs/romlayout.md` §3). It is the same in every room, area and
overlay, and across every one of the four room-load paths.

Consequences for the hook:

1. **The read can never fault.** No pointer chase, nothing to NULL-check. Do not use
   `0x80218420`; it is written once and read by only three instructions in one subsystem.
2. **Use the KSEG0 address** (`0x8020CA80`), not KSEG1 `0xA020CA80` — the game thread's
   writes sit in the data cache and an uncached read would show stale values.
3. **Non-atomic triple.** The game thread stores X, Y and Z as three separate `sw`
   (`0x8005EF54` / `0x8005EF64` / `0x8005EF78`). A graphics-thread read can land between
   them and mix one frame's X with the previous frame's Y. Cosmetic only for a HUD; if it
   matters, read the `s16` mirror block `0x8020CA94..0x8020CA99` as a single `ld`/two
   `lw` — those three `sh` are also separate, but they are adjacent and written within
   three instructions of each other.
4. **During overlay DMA the hook simply does not run.** The graphics thread is
   `osStopThread`'d across both overlay loads (`0x800733EC`/`0x800733FC` for overlay A,
   `0x800739AC`/`0x800739EC` for overlay B). So you cannot observe the room-swap window
   at all; you will see the *old* room's coordinates on the last frame before the stop and
   the *new* room's coordinates on the first frame after.
5. **The values are meaningless before the first room load** (boot logo, title, file
   select) and are stale leftovers, not zero. Gate the display on
   `*(s8*)0x801C44B8 == 3` (gameplay mode) and optionally `*(s8*)0x8020C89C == 0` (no warp
   pending). During a pending warp the coordinates still belong to the *departure* room
   until `0x8003BF60` / `0x8003C574` runs.
6. **During cutscenes/demos the fields are still valid position** — they are just driven
   by a script rather than by the pad (e.g. `0x800598E0` writes literal `X=−48, Y=−72,
   Z=34` straight into `+0x14/+0x16/+0x18`). Nothing to guard against; just don't be
   surprised by teleports.
7. **Menus / pause:** `*(s8*)0x800D37B0` freezes all object updates while rendering
   continues (`roomhook.md` §A.3). The position simply stops changing; the read stays
   valid.

---

## 8. Emulator verification plan (BizHawk)

RAM Watch domain **`RDRAM`**, offset = KSEG0 address − `0x80000000`, **Big Endian
checked**. (If the core exposes RDRAM word-swapped, single-*byte* watches need the offset
XOR'd with 3; the 16- and 32-bit watches below are unaffected in the usual configuration.)

| Watch | Offset | Size | Signed | Expected behaviour |
|---|---|---|---|---|
| **X fine** | `0x20CA9C` | 4 bytes | signed | changes by `velX*4096` every frame while walking; low 16 bits churn constantly |
| **Y fine** | `0x20CAA0` | 4 bytes | signed | **frozen** while walking on flat ground; **decreases** the instant you jump, then increases while falling; snaps to an exact multiple-ish value on landing |
| **Z fine** | `0x20CAA4` | 4 bytes | signed | changes while walking; **zero change while jumping straight up** |
| X int | `0x20CA94` | 2 bytes | signed | always ≈ `X fine / 65536`, ±1 |
| Y int | `0x20CA96` | 2 bytes | signed | same relation |
| Z int | `0x20CA98` | 2 bytes | signed | same relation |
| yaw | `0x20CAD6` | 2 bytes | unsigned, hex | sweeps 0x000–0xFFF as you turn; wraps, never exceeds 0xFFF |
| velY | `0x20CAC6` | 2 bytes | signed | `0` standing; jumps to −128/−352/−448 on a jump; then **+48 per frame** exactly |
| grounded | `0x20CAF4` | 1 byte | signed | non-zero standing, **`0` the whole time you are airborne** |

**The discriminating checks:**

* *Walk in one straight line.* Exactly **one** of `0x20CA9C` / `0x20CAA4` should change
  monotonically; the other should change only when you turn. If both change together and
  `0x20CAA0` also drifts, you are looking at a velocity triple, not a position triple.
* *Jump straight up without moving.* **Only `0x20CAA0` may change**, and it must go
  **more negative** on the way up. If it goes more positive on the way up, my Y-down
  reading is wrong; if `0x20CA9C`/`0x20CAA4` move too, you have the wrong struct.
* *Stand perfectly still for 60 frames.* All three words must be bit-identical every
  frame. If they oscillate, you are watching the `+0x2C/+0x34` previous-position block
  (`0x20CAAC` / `0x20CAB4`), which lags by one frame — that is the single most likely
  mis-identification.
* *Walk into a wall.* `0x8020CA9C` and `0x8020CAA4` freeze while `velX/velZ` stay non-zero
  — that is the `0x80041014` restore path firing, and it only ever writes the live block.
* *Sanity:* `*(u32*)0x80218420` must read `0x8020CA80`. If it does not, the struct base
  assumption from `roomhook.md` §A.4 has broken and nothing here applies.
* *Cross-room:* walk through a door and re-check. The watch addresses must not need
  changing, and the new values must equal the destination the warp request held
  (`0x20C890/892/894`, `s16` ×3) shifted left 16.

---

## 9. What is NOT the position (so you don't chase it)

* `0x8020CAAC / 0x8020CAB4` — previous frame. Lags by one frame and is *equal* to the
  live block most of the time; the easiest false positive.
* `0x8020CAC4` — velocity.
* `0x8020CEA8 / 0x8020CEB8` — transition snapshots; only updated on warps.
* `0x8020C890/892/894` — the requested spawn point, `s16`. Stale between warps.
* `0x80218B44 / 0x80218B48 / 0x80218B4C` — the *current object being drawn*'s scaled
  position (`0x8005057C`+). It holds Rock's position only during Rock's own draw call;
  from the flush hook at `0x80028088` it holds whatever object was drawn last.
* `0x801E0F78 / 0x8021C8AC / 0x801B74F0` — collision scratch (desired position, previous
  position, delta) for whichever object is currently being swept.
