# Controller input — Rockman DASH (Mega Man 64 JP)

All addresses are KSEG0 virtual addresses in the 1 MB boot segment
(ROM 0x1000..0x101000 -> RAM 0x80025C00). Disassembly quoted from
`build/boot.asm`. Helper: `tools/xref.py` (global `lui`+lo-part pairing;
`ref <addr> [len]`, `refcount <lo> <hi>`, `dis`, `fs`).

---

## TL;DR — answers to the four questions

| # | Item | Address | Type |
|---|------|---------|------|
| 1 | Controller 1 **held** buttons, standard N64 layout | **0x801C4450** | `u16` |
| 2 | Controller 1 **newly pressed** (raw layout, `new & ~old`) | **0x801C4456** | `u16` |
| 2b| Controller 1 newly pressed, **game-logical layout** (what gameplay code actually tests) | **0x801E0896** | `u16` |
| 3 | `OSContPad pads[4]` DMA/parse target | **0x801C4808**, 24 bytes (4 x 6) | refreshed by **0x80093E8C** |
| 4 | Storage width / stride | `u16`, never widened. Game array stride **8**; `OSContPad` stride **6** | |

Controller 1 = index 0 (PIF channel 0). Ports 2..4 are at +8, +16, +24
(game array) and +6, +12, +18 (`OSContPad` array).

---

## 1. libultra SI/controller layer (identified)

| Symbol | Address | Proof |
|---|---|---|
| `__osSiRawStartDma` | 0x8009B270 | 0x8009B274 `lui v0,0xa480` + `ori v0,0x18` -> reads SI_STATUS; 0x8009B2C4 `sw v0,0(v1)` with v1=0xA4800000 (SI_DRAM_ADDR); 0x8009B2E4 writes 0x1FC007C0 to 0xA4800004 (RD64B) or 0xA4800010 (WR64B) |
| `osCreateMesgQueue` / `osRecvMesg` / `osSendMesg` | 0x8009A130 / 0x8009A2A0 / 0x8009A3D0 | call shapes at 0x8009B34C / 0x8009B3CC / 0x8009B35C |
| `__osSiCreateAccessQueue` | 0x8009B320 | creates MQ 0x80225B28 with buf 0x8019A9C0, count 1; sets flag 0x800D0040=1 |
| `__osSiGetAccess` | 0x8009B374 | 0x8009B378 `lw v0,0x40(0x800d....)`, then `osRecvMesg(0x80225B28, &dummy, BLOCK)` |
| `__osSiRelAccess` | 0x8009B3E0 | |
| `osContInit` | 0x8009955C | 0x80099644-0x8009964C `li v0,4` / `sb v0,0x8B9(0x801e0000)` -> `__osMaxControllers = 4` |
| `osContStartQuery` | 0x800992E0 | `__osSiGetAccess`; if `__osContLastCmd != 0` -> `__osPackRequestData(0)`, WR DMA, recv; RD DMA; `__osContLastCmd = 0` (0x80099340) |
| `osContGetQuery` | 0x80099360 | thin wrapper -> `__osContGetInitData(&sp[16], a0)` |
| `osContStartReadData` | 0x80099380 | 0x80099398 `lbu v1,__osContLastCmd`; `beq v1,1` skip; else 0x800993A8 `jal __osPackReadData`; 0x800993B8 `__osSiRawStartDma(OS_WRITE=1, 0x801B4AD0)`; 0x800993D8 `__osSiRawStartDma(OS_READ=0, 0x801B4AD0)`; 0x800993E8 `sb v1(=1), __osContLastCmd` |
| `osContGetReadData` | 0x80099408 | see below |
| `__osPackReadData` | 0x800994A4 | |
| `__osContGetInitData` | 0x800996FC | writes `OSContStatus` (stride 4: 0x80099794 `addiu a2,a2,4`, 0x800997A0 `addiu t1,t1,4`) |
| `__osPackRequestData` | 0x800997B0 | |
| `bcopy(src,dst,len)` | 0x80099C50 | overlap-aware byte copy |

Globals:

* `__osContPifRam` = **0x801B4AD0** (64 B) — 0x800993B4 `addiu a1,a1,19152` (0x4AD0) with `lui a1,0x801b`.
* `__osContLastCmd` = **0x8020C0C4** (u8) — 0x80099398 `lbu v1,-16188(0x80210000)`.
* `__osMaxControllers` = **0x801E08B9** (u8, = 4) — set at 0x8009964C.
* `__osSiAccessQueue` = **0x80225B28**, msg buffer 0x8019A9C0.

### `osContGetReadData` @ 0x80099408 — proves the `OSContPad` layout

```
80099408  addiu   sp,sp,-16
8009940C  move    t1,a0              ; t1 = OSContPad *pad
80099414  lbu     v0,0x8B9(0x801e)   ; __osMaxControllers
8009941C  addiu   t0,t0,19152        ; t0 = __osContPifRam = 0x801B4AD0
80099428  addiu   a2,a0,3            ; a2 = &pad->stick_y
8009942C  lwl/lwr v0,0(t0) ... 7(t0) ; copy 8-byte PIF channel block to sp[0..7]
8009944C  lbu     v0,2(sp)
80099450  andi    v0,v0,0xc0
80099454  srl     v0,v0,0x4
80099458  bnez    v0,0x80099478
8009945C  sb      v0,1(a2)           ; pad->errno  (offset +4)
80099460  lhu     v0,4(sp)
80099464  sh      v0,0(t1)           ; pad->button (offset +0)  <-- u16
80099468  lbu     v0,6(sp)
8009946C  sb      v0,-1(a2)          ; pad->stick_x (offset +2)
80099470  lbu     v0,7(sp)
80099474  sb      v0,0(a2)           ; pad->stick_y (offset +3)
8009947C  addiu   t0,t0,8            ; next PIF channel block
80099488  addiu   a2,a2,6
80099494  addiu   t1,t1,6            ; <-- OSContPad stride = 6 (packed)
```

`OSContPad { u16 button; s8 stick_x; s8 stick_y; u8 errno; /*1 pad byte*/ }`,
`sizeof == 6` in this build (no 8-byte padding).

---

## 2. The `OSContPad` array: 0x801C4808

Dispatch table of SI jobs at **0x800CF4C4** (ROM 0x9C8C4):

```
800CF4C4: 80093E8C   ; [0] read controllers
800CF4C8: 80093F64   ; [1] pak read
800CF4CC: 80093FDC   ; [2] pak write
800CF4D0: 80093E3C   ; [3] query / detect
```

`0x80093E8C` is the refresh function:

```
80093EC0  jal     0x80099380         ; osContStartReadData(mq = 0x8021DA98)
80093EC4  addiu   a0,a0,-9576
80093ED8  jal     0x8009a2a0         ; osRecvMesg(mq, NULL, OS_MESG_BLOCK)
80093EFC  jal     0x8009a3d0         ; osSendMesg(0x80225B48, 0, BLOCK)  -- lock
80093F04  lui     a0,0x801c
80093F08  jal     0x80099408         ; osContGetReadData(0x801C4808)
80093F0C  addiu   a0,a0,18440        ; 0x4808
80093F1C  jal     0x8009a2a0         ; osRecvMesg(0x80225B48, ...)       -- unlock
```

Extent proof: `tools/xref.py ref 0x801C4800 0x28` shows the next unrelated
global at **0x801C4820**, so the array occupies 0x801C4808..0x801C481F =
24 bytes = 4 x 6, matching `__osMaxControllers == 4`.

The subsystem is started by 0x80092DC0 (enqueue at 0x80092EB0 ->
0x80093BE0(0x800CF470)), itself called from 0x80028BA0.

---

## 3. Game-side pad state: 0x801C4450, stride 8, 4 ports

`0x80092E00` (function body begins after the `nop` at 0x80092DFC):

```
80092E1C  jal     0x80093dec         ; osSendMesg(0x80225B48,...)  lock pads
80092E24  move    s4,zero            ; i = 0
80092E28  lui     s3,0x801c
80092E2C  addiu   s3,s3,18440        ; s3 = &pads[0]      = 0x801C4808
80092E30  move    s2,zero            ; byte index into pads[]
80092E34  move    s1,s0              ; s1 = &dst[0]       (a0)
; loop:
80092E38  lhu     s0,0(s1)           ; OLD held buttons  (dst[i].held)
80092E3C  move    a0,s3
80092E40  move    a1,s1
80092E44  jal     0x80099c50         ; bcopy(&pads[i], &dst[i], 6)
80092E48  li      a2,6
80092E4C  addiu   s3,s3,6            ; pads stride 6
80092E58  lhu     v0,18440(v0)       ; v0 = pads[i].button  (NEW held)
80092E5C  addiu   s2,s2,6
80092E60  addiu   s4,s4,1
80092E64  nor     s0,zero,s0         ; ~old
80092E68  and     v0,v0,s0           ; new & ~old
80092E6C  sh      v0,6(s1)           ; dst[i].pressed  (offset +6)  <-- u16
80092E70  sltiu   v0,s4,4            ; 4 ports
80092E74  bnez    v0,0x80092e38
80092E78  addiu   s1,s1,8            ; dst stride = 8
80092E7C  jal     0x80093e14         ; osRecvMesg(0x80225B48,...)  unlock
```

Caller `0x80028BC4` supplies the array base and applies an analog deadzone:

```
80028BCC  lui     s0,0x801c
80028BD0  addiu   s0,s0,17488        ; s0 = 0x801C4450
80028BD8  jal     0x80092e00
80028BDC  move    a0,s0
80028BE4  addiu   v1,s0,3            ; &stick_y
80028BE8  addiu   s0,s0,2            ; &stick_x
; per port:
80028BEC  lbu     v0,0(s0)
80028BF0  addiu   v0,v0,4
80028BF4  andi    v0,v0,0xff
80028BF8  sltiu   v0,v0,9            ; |x| <= 4 ?
80028BFC  bnezl   v0,...
80028C00  sb      zero,0(s0)         ; deadzone x
80028C04..80028C18                   ; same for y
80028C1C  addiu   v1,v1,8            ; stride 8
80028C2C  addiu   s0,s0,8
```

`0x80028BC4` is called once per frame from the main loop `0x80026A30`
(0x80026A80 `jal 0x80028bc4`), right after the frame counter at 0x800A6950
is bumped and before the render calls 0x8002609C / 0x800281BC.

### Layout — `gPadState[4] @ 0x801C4450`, `sizeof == 8`

| Offset | Type | Meaning | Port 1 address |
|---|---|---|---|
| +0x0 | `u16` | **held** buttons, raw N64 layout | **0x801C4450** |
| +0x2 | `s8`  | stick X (deadzoned +-4) | 0x801C4452 |
| +0x3 | `s8`  | stick Y (deadzoned +-4) | 0x801C4453 |
| +0x4 | `u8`  | `errno` from `OSContPad` | 0x801C4454 |
| +0x5 | `u8`  | padding (untouched) | 0x801C4455 |
| +0x6 | `u16` | **pressed** = `new & ~old` (raw layout) | **0x801C4456** |

Ports 2/3/4: 0x801C4458, 0x801C4460, 0x801C4468.

The button field is **`u16` everywhere** — every access is `lhu`/`sh`
(0x80099464, 0x80092E38, 0x80092E58, 0x80092E6C, 0x80032D54). It is never
sign/zero-extended into a 32-bit global.

---

## 4. Remap layer -> the globals gameplay actually uses

`0x80032D20` (called only from `0x80032E38` with `a0 = 0x801E0890`, itself
called from the per-frame input entry `0x80032908`) converts the *raw* port-1
held mask into a *game-logical* mask via a 16-entry `u16` table:

```
80032D34  lhu     v0,2(s0)           ; save previous logical held
80032D3C  addiu   a2,a2,17488        ; a2 = 0x801C4450  (port 1 ONLY)
80032D44  addiu   a1,a1,-30552       ; a1 = 0x800A88A8  (remap table)
80032D48  sb      zero,0(s0)
80032D4C  sh      zero,2(s0)
80032D50  sh      v0,4(s0)           ; prev = old held
; for (bit = 0; bit < 16; bit++)
80032D54  lhu     v0,0(a2)           ; raw held
80032D58  srav    v0,v0,a0
80032D5C  andi    v0,v0,0x1
80032D60  beqz    v0,skip
80032D6C  lhu     v1,0(a1)           ; table[bit]
80032D70  or      v0,v0,v1
80032D74  sh      v0,2(s0)           ; held |= table[bit]
80032D80  addiu   a1,a1,2
80032D8C  jal     0x80032c08         ; analog stick -> extra direction bits
                                     ;   (reads lb 2(0x801C4450), lb 3(...))
; diagonal fixup: if ((held & 0xC00) == 0xC00) held = (held & ~0xC00) | 0x200
80032DB8  lhu     v0,4(s0)           ; prev
80032DBC  lhu     v1,2(s0)           ; new
80032DC8  nor     v0,zero,v0
80032DCC  and     v1,v1,v0           ; new & ~prev
80032DD0  nor     a1,zero,a1
80032DD4  and     a2,a2,a1           ; prev & ~new
80032DD8  sh      v1,6(s0)           ; PRESSED
80032DE0  sh      a2,8(s0)           ; RELEASED
```

### Logical input struct @ 0x801E0890

| Offset | Address | Type | Meaning |
|---|---|---|---|
| +0x00 | 0x801E0890 | `u8` | cleared each frame |
| +0x01 | 0x801E0891 | `u8` | bit0/bit1 state machine (0x80032E14) |
| +0x02 | **0x801E0892** | `u16` | **held** (logical layout) |
| +0x04 | 0x801E0894 | `u16` | previous held |
| +0x06 | **0x801E0896** | `u16` | **pressed** (`new & ~old`) |
| +0x08 | 0x801E0898 | `u16` | released (`old & ~new`) |
| +0x0A | 0x801E089A | `u16` | auto-repeat latch mask (0x80032ED4/0x80032E58, for 0x10 and 0x40) |
| +0x12 | 0x801E08A2 | `u8` | |

Reference counts (`tools/xref.py refcount`): 0x801E0896 = 51 direct
references across the ROM, 0x801E0892 = 9 — this is the pair the gameplay
and menu code reads. Observed `andi` masks against 0x801E0896:
0x1, 0x8, 0x10, 0x20, 0x40, 0x80, 0x200, 0x400, 0xC00, 0x8000, 0x6000,
0x8008, 0x8009, 0x6008, 0x4080, 0x4828.

### Remap table @ 0x800A88A8 (16 x `u16`, ROM offset 0x83CA8)

```
raw 0x0001 CR    -> 0x0100        raw 0x0100 DR    -> 0x0020
raw 0x0002 CL    -> 0x1000        raw 0x0200 DL    -> 0x0080
raw 0x0004 CD    -> 0x2000        raw 0x0400 DD    -> 0x0040
raw 0x0008 CU    -> 0x0001        raw 0x0800 DU    -> 0x0010
raw 0x0010 R     -> 0x0800        raw 0x1000 START -> 0x0008
raw 0x0020 L     -> 0x0400        raw 0x2000 Z     -> 0x0400
raw 0x0040 -     -> 0x0000        raw 0x4000 B     -> 0x8000
raw 0x0080 -     -> 0x0000        raw 0x8000 A     -> 0x4000
```

So the **game-logical** bit layout is:

| bit | meaning |
|---|---|
| 0x0001 | C-Up |
| 0x0008 | START |
| 0x0010 | Up (D-pad U or stick up) |
| 0x0020 | Right |
| 0x0040 | Down |
| 0x0080 | Left |
| 0x0100 | C-Right |
| 0x0200 | L+R combo (synthesised, see 0x80032D9C-0x80032DB0) |
| 0x0400 | L **or** Z |
| 0x0800 | R |
| 0x1000 | C-Left |
| 0x2000 | C-Down |
| 0x4000 | A |
| 0x8000 | B |

`0x801E0892/0x801E0896` are cleared by 0x8005DBA8 (0x8005DC40/0x8005DC48).

---

## 5. Confidence

* **Proven** (unambiguous instruction chain): `osContGetReadData` @ 0x80099408
  and the 6-byte `OSContPad` layout; the pad array at 0x801C4808 and its
  refresher 0x80093E8C; the game array at 0x801C4450 with stride 8 and
  `pressed = new & ~old` at +6; the per-frame call site 0x80026A80.
* **Proven**: the remap table at 0x800A88A8 and the logical struct at
  0x801E0890 (single call site 0x80032E44 supplies the base).
* **Caveat on Q2**: the *raw*-layout pressed word 0x801C4456 is computed every
  frame but has **no** direct readers in the boot segment
  (`refcount 0x801C4440 0x801C4490` lists 0x801C4450/52/53 only). Gameplay
  edge-detects on the logical word **0x801E0896** instead. If you want the
  word a trainer/cheat should watch, use 0x801E0896; if you want the literal
  standard-layout `new & ~old`, it is 0x801C4456.
* **Not determined**: which thread/entry invokes 0x80032908 (no `jal`
  callers; reached through an indirect state-machine dispatch), and the exact
  scheduler that pumps the SI job table at 0x800CF4C4 (functions 0x80093A30 /
  0x80093BE0 / 0x80093C80 operating on the list head 0x800CF4B0). Neither
  affects the addresses above.
