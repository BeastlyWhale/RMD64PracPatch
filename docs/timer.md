# Rockman DASH – Hagane no Boukenshin (N64, JP): play-time clock

**Status: identified with high confidence.** All claims below are backed by exact
instruction addresses in `build/boot.asm` (boot segment, RAM 0x80025C00–0x80125C00,
ROM 0x1000–0x101000) or by raw-ROM offsets for overlay code.

---

## 1. Summary

| Item | Value |
| --- | --- |
| **Play-time counter** | `0x801C44C4`, **u32**, single monotonic tick count |
| **Tick rate** | **30 Hz** (30 ticks = 1 second; the game runs at 30 fps) |
| **Units** | 1 tick = 1 main-loop iteration = 1 game frame = 1/30 s |
| **Max value (clamp)** | `0x00A4CB7F` = 10,799,999 = **99:59** displayed |
| **Save-block base** | `0x801C44B8` |
| **Offset in save block** | **+0x0C** |
| **Save-file image** | `0x8019BD40`, 6144 (0x1800) bytes; play time at image **+0x80** |
| **Storage medium** | **FlashRAM** (not SRAM, not Controller Pak) |
| **Incremented at** | `0x8003E1D4` (inside main loop `0x8003E168`) |
| **Formatted at** | `0x8005695C` (HH:MM), `0x8007667C` (HH:MM:SS) |

There are **no** separate hour/minute/second fields. It is a single 32-bit frame
counter; hours and minutes are derived at display time by magic-number division.

---

## 2. How it was found

The compiler used for this game emits **magic-number reciprocal multiplies** for
constant division (proved by the `/10` sequence at `0x80073F64`), not `div`
instructions — a ROM-wide scan found only 31 constant-divisor `div`/`divu` sites and
none by 60.

Scanning all 32 MB for `lui rX,hi ; ori rX,rX,lo` magic constants found **exactly one**
occurrence of `0x91A2B3C5` (the unsigned magic for `/3600`) in the entire ROM, at
`0x80056C4C` — four instructions after a load of `0x801C44C4`, with the `/60`
(`0x88888889`) and `/10` (`0x66666667`) magics immediately adjacent. That is the
save-screen time formatter.

---

## 3. The counter: `0x801C44C4` (u32)

### 3.1 The increment — main loop `0x8003E168`

`0x8003E168` is the game's main loop. It is the **only** referencer of the mode
dispatch table at `0x800AC500` (`addiu s3,s3,-15104` at `0x8003E174`), it dispatches on
the mode byte at `0x801C44B8`, and it never returns — `j 0x8003e1ac` at `0x8003E27C`
closes the loop (the `jr ra` epilogue at `0x8003E284` is dead code). It is started via
`0x80029298` with `a0 = 0x8003E168` at `0x8005DDEC`–`0x8005DDF0`.

```
; --- loop prologue (once) ---
8003E194  3c11801c  lui      s1,0x801c
8003E198  263144b8  addiu    s1,s1,17592     ; s1 = 0x801C44B8  (save-block base / mode byte)
8003E17C  3c1000a4  lui      s0,0xa4
8003E180  3610cb7f  ori      s0,s0,0xcb7f    ; s0 = 0x00A4CB7F = 10,799,999  (clamp)
8003E170  3c13800b  lui      s3,0x800b
8003E174  2673c500  addiu    s3,s3,-15104    ; s3 = 0x800AC500  (mode jump table)

8003E1AC  <loop>:                            ; <-- j target from 0x8003E27C
8003E1AC  82220000  lb       v0,0(s1)        ; game mode
8003E1B0  00021080  sll      v0,v0,0x2
8003E1B4  00531021  addu     v0,v0,s3
8003E1B8  8c420000  lw       v0,0(v0)
8003E1BC  0040f809  jalr     v0               ; run mode handler
8003E1C0  02202021  move     a0,s1
8003E1C4  0c013da7  jal      0x8004f69c       ; per-frame update

; ---------------- PLAY TIME ++ ----------------
8003E1CC  3c02801c  lui      v0,0x801c
8003E1D0  8c4244c4  lw       v0,17604(v0)    ; v0 = *(u32*)0x801C44C4
8003E1D4  24420001  addiu    v0,v0,1         ; ++
8003E1D8  3c01801c  lui      at,0x801c
8003E1DC  ac2244c4  sw       v0,17604(at)    ; store back
8003E1E0  0202102b  sltu     v0,s0,v0        ; (10799999 < new) ?
8003E1E4  10400003  beqz     v0,0x8003e1f4
8003E1E8  00000000  nop
8003E1EC  3c01801c  lui      at,0x801c
8003E1F0  ac3044c4  sw       s0,17604(at)    ; clamp to 10,799,999
8003E1F4  <continue>
...
8003E274  0c00a431  jal      0x800290c4       ; frame sync / yield
8003E278  24040001  li       a0,1
8003E27C  0800f86b  j        0x8003e1ac       ; loop
```

The clamp value `0x00A4CB7F` = 10,799,999 = `100 * 108000 - 1`, i.e. exactly one tick
short of 100 hours at 30 Hz. This is independent confirmation of both the tick rate and
the HH:MM display format (max 99:59).

### 3.2 Every writer of `0x801C44C4` in the whole ROM

A ROM-wide scan (`lui rX,0x801c` followed by a memory op with offset `0x44C4`) finds
exactly 10 accesses; only four are writes:

| Address | Op | Role |
| --- | --- | --- |
| `0x8003E1DC` | `sw v0` | **the per-frame increment** |
| `0x8003E1F0` | `sw s0` | clamp to 10,799,999 |
| `0x80074A0C` | `sw a2` | **load** from save image (`0x8019BDC0` = image+0x80) |
| `0x8007536C` | `sw zero` | **new game / clear** |
| `0x8003E1D0` | `lw v0` | read for increment |
| `0x80056B78` | `lw v0` | formatter — hours |
| `0x80056C48` | `lw v1` | formatter — minutes |
| `0x80076680` | `lw v0` | formatter #2 — HH:MM:SS |
| `0x8004B7F0` | `addiu t4` | half-second blink (see §5) |
| ROM `0x0029D808` | `lw v1` | overlay: game-clear time check (see §6) |

No other code anywhere in the 32 MB image touches it.

---

## 4. The display formatter — `0x8005695C` (save/load file screen)

This is the function that produces the on-screen **HH:MM**. It emits two 2-digit fields
through identical least-significant-digit-first loops that write glyph indices
(`digit*8 + 96`) into 20-byte display records.

### 4.1 Hours

```
80056B58  3c03026d  lui      v1,0x26d
80056B5C  346360dd  ori      v1,v1,0x60dd    ; v1 = 0x026D60DD = 40,722,653
80056B74  3c02801c  lui      v0,0x801c
80056B78  8c4244c4  lw       v0,17604(v0)    ; v0 = ticks
80056B84  00021142  srl      v0,v0,0x5       ; pre-shift: ticks >> 5
80056B88  00430019  multu    v0,v1
80056BB0  0000c010  mfhi     t8
80056BB4  00182942  srl      a1,t8,0x5       ; a1 = HOURS
```

`ceil(2^42 / 108000) = 40,722,653 = 0x026D60DD`, so
`a1 = ticks / 108000` — and **108000 = 30 * 3600**, i.e. ticks per hour at 30 Hz.

### 4.2 Minutes

```
80056C44  3c03801c  lui      v1,0x801c
80056C48  8c6344c4  lw       v1,17604(v1)    ; v1 = ticks
80056C4C  3c0291a2  lui      v0,0x91a2
80056C50  3442b3c5  ori      v0,v0,0xb3c5    ; v0 = 0x91A2B3C5
80056C54  00620019  multu    v1,v0
80056C70  0000c010  mfhi     t8
80056C7C  00182282  srl      a0,t8,0xa       ; a0 = ticks / 1800 = TOTAL MINUTES
80056C74  3c028888  lui      v0,0x8888
80056C78  34428889  ori      v0,v0,0x8889    ; v0 = 0x88888889
80056C80  00820019  multu    a0,v0
80056CA8  0000c010  mfhi     t8
80056CAC  00181942  srl      v1,t8,0x5       ; v1 = a0 / 60 = hours
80056CB0  00031100  sll      v0,v1,0x4       ;   v1*16
80056CB4  00431023  subu     v0,v0,v1        ;   v1*15
80056CB8  00021080  sll      v0,v0,0x2       ;   v1*60
80056CBC  00822823  subu     a1,a0,v0        ; a1 = total_min % 60 = MINUTES
```

`ceil(2^42 / 1800) = 2,443,359,173 = 0x91A2B3C5`, so `a0 = ticks / 1800`, and
**1800 = 30 * 60** = ticks per minute at 30 Hz.

### 4.3 Digit emission (same shape for both fields)

```
80056CE8  00ad0018  mult     a1,t5            ; t5 = 0x66666667
80056D14  0000c010  mfhi     t8
80056D18  00181883  sra      v1,t8,0x2        ; v1 = a1 / 10
80056D1C  00621023  subu     v0,v1,v0
80056D24  00031080  sll      v0,v1,0x2
80056D28  00431021  addu     v0,v0,v1
80056D2C  00021040  sll      v0,v0,0x1        ; (a1/10)*10
80056D30  00a21023  subu     v0,a1,v0         ; v0 = a1 % 10  -> digit
80056D34  000210c0  sll      v0,v0,0x3
80056D38  24420060  addiu    v0,v0,96         ; glyph index
80056D3C  a0c2000c  sb       v0,12(a2)
80056D44  0521ffde  bgez     t1,0x80056cc0    ; t1 started at 1 -> 2 digits
80056D48  00602821  move     a1,v1
```

### 4.4 Verification

Reproducing the exact magic arithmetic in Python:

| ticks | hours | minutes | display |
| --- | --- | --- | --- |
| 0 | 0 | 0 | 00:00 |
| 1,799 | 0 | 0 | 00:00 |
| 1,800 | 0 | 1 | 00:01 |
| 107,999 | 0 | 59 | 00:59 |
| 108,000 | 1 | 0 | 01:00 |
| 10,799,999 (clamp) | 99 | 59 | **99:59** |

The clamp lands exactly on 99:59, which is conclusive.

### 4.5 Second formatter — `0x8007667C` (HH:MM:SS)

```
8007667C  3c02801c  lui      v0,0x801c
80076680  8c4244c4  lw       v0,17604(v0)    ; ticks
80076684  3c038888  lui      v1,0x8888
80076688  34638889  ori      v1,v1,0x8889
8007668C  00430019  multu    v0,v1
80076690  00001010  mfhi     v0
80076694  00022102  srl      a0,v0,0x4       ; a0 = ticks / 30 = TOTAL SECONDS
8007669C  00830019  multu    a0,v1
800766A0  00001010  mfhi     v0
800766B0  00022942  srl      a1,v0,0x5       ; a1 = seconds / 60 = TOTAL MINUTES
800766B4  00051100  sll      v0,a1,0x4
800766B8  00451023  subu     v0,v0,a1
800766C0  00021080  sll      v0,v0,0x2
800766C4  00821023  subu     v0,a0,v0        ; v0 = seconds % 60 = SECONDS
```

`2^36 / 0x88888889 = 30` exactly. **This is the single clearest proof that the counter
ticks at 30 Hz**: the game itself divides it by 30 to obtain seconds. Digits are written
as bytes to a buffer around `0x80218C38` using a glyph table at `0x800B301C`.

---

## 5. Half-second blink derived from the same counter — `0x8004B778`

```
8004B7EC  3c0c801c  lui      t4,0x801c
8004B7F0  258c44c4  addiu    t4,t4,17604     ; t4 = &playtime
8004B7F4  3c098888  lui      t1,0x8888
8004B7F8  35298889  ori      t1,t1,0x8889
...
8004B8C0  8d820000  lw       v0,0(t4)
8004B8C4  00490019  multu    v0,t1
8004B8C8  0000c810  mfhi     t9
8004B8CC  001920c2  srl      a0,t9,0x3       ; a0 = ticks / 15   (2^35 / M = 15)
8004B8D4  00890019  multu    a0,t1
8004B8DC  00191942  srl      v1,t9,0x5       ; /60
8004B8EC  00822023  subu     a0,a0,v0        ; a0 = (ticks/15) % 60
8004B8F0  30840001  andi     a0,a0,0x1       ; parity -> blink
```

15 ticks = half a second at 30 Hz — a menu blink driven off the play clock. Another
independent confirmation of the rate.

---

## 6. Overlay use: game-clear time (ROM `0x0029D7E0`+)

The only overlay reference to the counter, in the ending/results code:

```
ROM 0029D804  3c03801c  lui  v1,0x801c
ROM 0029D808  8c6344c4  lw   v1,0x44c4(v1)   ; ticks
ROM 0029D80C  3c020004  lui  v0,0x0004
ROM 0029D810  3442f19f  ori  v0,v0,0xf19f    ; 0x0004F19F = 323,999
ROM 0029D814  0043102b  sltu v0,v0,v1        ; (323999 < ticks)
ROM 0029D818  3c01801c  lui  at,0x801c
ROM 0029D81C  ac2344cc  sw   v1,0x44cc(at)   ; 0x801C44CC = clear-time copy
ROM 0029D820  10400005  beqz v0,+5
...
ROM 0029D83C  3c01801c  lui  at,0x801c
ROM 0029D840  a42244d2  sh   v0,0x44d2(at)   ; 0x801C44D2 = 1  (Hard mode flag)
```

**323,999 + 1 = 324,000 = exactly 3 hours at 30 Hz.** The clear time is stashed at
`0x801C44CC` (save-block +0x14), and the Hard-mode flag `0x801C44D2` — one of the two
known GameShark addresses — is set here. This ties the whole structure together.

---

## 7. Is it paused?

**The increment itself is unconditional** — it sits in the straight-line body of the
main loop at `0x8003E1CC`–`0x8003E1DC` with no guard. However, the clock only advances
while control is *in that loop*. Several routines run their **own** frame loops
(calling the same per-frame update `0x8004F69C` and frame-sync `0x800290C4`) without
touching the counter, so time is frozen while they are active:

| Sub-loop | Evidence |
| --- | --- |
| `0x8005D200` | own jump-table dispatch, `jal 0x8004f69c` @ `0x8005D444`, `jal 0x800290c4` @ `0x8005D44C`, back-edge `j 0x8005d3d8` @ `0x8005D454`. Its handler `0x8005D770` reads the difficulty flags `0x801C44D0/D2` → title / difficulty-select / file menu. |
| `0x8003CF4C` (mode 4) | `jal 0x800290c4` @ `0x8003D0AC` |
| `0x8003D0D8` (mode 5) | `jal 0x800290c4` @ `0x8003D360` |
| `0x8003D5BC` (mode 8) | `jal 0x800290c4` @ `0x8003D6F8` |
| `0x8003DE6C`, `0x8003DFEC` | `jal 0x800290c4` @ `0x8003DE94`, `0x8003E104` |

So: **no explicit pause flag exists**; pausing is structural. Loading stalls also
freeze it naturally because the loop blocks. Note that the clock *does* keep running
during anything that is dispatched as a mode of the main loop (i.e. normal in-game
pause menus reached via the mode table still tick).

Separately, `0x8003E1F4`–`0x8003E270` gates a *different* counter, `0x801C44C2`
(block +0x0A), which only advances when `mode >= 3 && mode != 6` and a pad mask
`(*(u16*)0x801E0892 & 9) == 9` holds; at 61 it calls `0x8003DE6C`, which writes
`0x801C44D0/D2`. That is the button-hold **difficulty-unlock cheat**, not a clock.

---

## 8. Save-data block layout

### 8.1 Live block: base `0x801C44B8`

Established from the serializer `0x80073E80`, which sets
`s5 = 0x801C44B8` (`lui s5,0x801c ; addiu s5,s5,17592` @ `0x80073E94`–`0x80073E98`)
and `s4 = 0x8019BD40` (`lui s4,0x801a ; addiu s4,s4,-17088` @ `0x80073EA0`–`0x80073EA4`).
`lw a2,12(s5)` @ `0x80073FFC` → `sw a2,128(s4)` @ `0x80074040` pins the base exactly.

| Live addr | Blk off | Image off | Width | Meaning |
| --- | --- | --- | --- | --- |
| `0x801C44B8` | +0x00 | (not saved) | s8 | **game mode** (index into `0x800AC500`) |
| `0x801C44C2` | +0x0A | – | u16 | cheat button-hold counter |
| **`0x801C44C4`** | **+0x0C** | **+0x80** | **u32** | **PLAY TIME (ticks @ 30 Hz)** |
| `0x801C44C8` | +0x10 | +0x2C | u32 | – |
| `0x801C44CC` | +0x14 | +0x30 | u32 | game-clear time copy (§6) |
| `0x801C44D0` | +0x18 | +0x34 | u16 | **Easy unlocked** (GameShark `801C44D0 0001`) |
| `0x801C44D2` | +0x1A | +0x36 | u16 | **Hard unlocked** (GameShark `801C44D2 0001`) |
| `0x801C44D4` | +0x1C | +0x84 | u32 | – |
| `0x801C44D8` | +0x20 | +0x88 | u32 | – |
| `0x801C44DC` | +0x24 | +0x8C | u32 | – |
| `0x801C44E0`–`0x801C44F4` | +0x28..+0x3C | +0x1600..+0x1614 | u32 ×6 | – |
| `0x801C44F8`–`0x801C4502` | +0x40..+0x4A | +0x1618..+0x1622 | u16 ×6 | – |
| `0x801C4504` | +0x4C | +0x90 | u32 | – |
| `0x801C4508`–`0x801C450B` | +0x50..+0x53 | +0x94..+0x97 | u8 ×4 | +0x50 also indexes a table @ `0x80073ECC` |
| `0x801C450C`–`0x801C4513` | +0x54..+0x5B | +0x98..+0x9F | u8 ×7 | +0x5B (`0x801C4513`) read by the ending code |
| `0x801C4518` | +0x60 | +0xD0 | u16 | init 128 by clear (`0x80075344`) |
| `0x801C451A` | +0x62 | +0xD2 | u16 | – |
| `0x801C451C` | +0x64 | +0xE0 | u32 | – |
| `0x801C4530`–`0x801C4535` | +0x78..+0x7D | +0x100..+0x107 | u8 ×6 | +0x79/+0x7A init to −1 by clear |
| `0x801C453C`+ | +0x84 | +0x1640 | 15 B | block copy @ `0x80074228` |

The clear routine `0x80075334` touches up to `0x801C453A`, so the live block spans
roughly `0x801C44B8` … `0x801C4540` (~0x90 bytes).

### 8.2 The three accessors

| Function | Role | Called from |
| --- | --- | --- |
| `0x80073E80` | serialize live block → file image `0x8019BD40` | `0x8007623C` (in `0x80075E40`) |
| `0x8007494C` | deserialize file image → live block | `0x8003E308` (in mode-0 handler `0x8003E2A0`) |
| `0x80075334` | clear for New Game (`sw zero,17604(at)` @ `0x8007536C` zeroes the clock) | `0x8003E2F8` (same handler) |

### 8.3 Storage medium: **FlashRAM**

```
8007623C  0c01cfa0  jal      0x80073e80      ; build image
80076244  0c01d096  jal      0x80074258      ; fill scattered salt words (RNG 0x800A3110)
80076254  3c11801a  lui      s1,0x801a
80076258  2631bd40  addiu    s1,s1,-17088    ; s1 = 0x8019BD40 (image)
80076260  9084c76e  lbu      a0,-14482(a0)   ; device / slot index
80076270  8ca548b0  lw       a1,18608(a1)    ; name ptr from table 0x800B48B0
80076274  02203021  move     a2,s1
80076278  0c01cf58  jal      0x80073d60      ; write
8007627C  24071800  li       a3,6144         ; 0x1800 bytes
```

`0x80073D60` → `0x800872B8` → the PI save driver at `0x80094CC0`–`0x800953xx`, which
programs a PI handle at `0x80206CCC` with domain `0xA8000000` (`lui v1,0xa800` @
`0x80094CF0`) and issues the complete **FlashRAM** command set:

| Address | Constant | FlashRAM command |
| --- | --- | --- |
| `0x80094E14` | `0xE1000000` | status |
| `0x80094F2C`, `0x80095070` | `0x78000000` | erase |
| `0x8009501C` | `0x4B000000` | set erase page |
| `0x8009519C`, `0x80095238` | `0xB4000000` | write |
| `0x80095250` | `0xA5000000` | set write offset |
| `0x8009539C` | `0xF0000000` | read mode |

So the save block is **not** copied to SRAM or the Controller Pak — it goes to
**FlashRAM**, 6144 bytes per slot. The ASCII marker `"  RockManDashN64 SaveSlot Exist"`
at `0x800CECD0` is the slot-presence magic. (`0x800872B8` parses two ASCII digits out
of the name at `name[4]`/`name[5]` — `lbu` @ `0x8008732C`/`0x80087330`, then `-48` — to
select the slot, and allocates a 16 KB staging buffer @ `0x800872F0`.)

---

## 9. Confirming on hardware/emulator

### Test A — the counter itself (BizHawk or Project64)
1. Open a RAM Watch on **`0x801C44C4`** as **u32 big-endian**
   (BizHawk N64 RDRAM domain address **`0x001C44C4`**; Project64 will accept
   `801C44C4`).
2. Start a new game. The value must reset to **0** at that moment
   (breakpoint on write at `0x8007536C` to see it).
3. Let it run and confirm it increases by **exactly 1800 per wall-clock minute**
   (30/second). If your emulator reports 60 increments/second instead, the loop is
   running double-speed and the display will run 2× fast — worth noting.
4. Open the save/load file screen and read the displayed HH:MM. It must equal
   `ticks/108000` : `(ticks/1800)%60`. E.g. poke **`108000` (0x0001A5E0)** → the screen
   must read **01:00**; poke **`10799999` (0x00A4CB7F)** → **99:59**.
5. Poke **`10800000`** and step: the clamp at `0x8003E1F0` must pull it back to
   `0x00A4CB7F` within one frame.

### Test B — the increment site
Set an execute breakpoint at **`0x8003E1D4`**. It should hit exactly once per rendered
frame during normal gameplay, and **stop hitting** while you are in the title screen /
file menu (sub-loop `0x8005D200`) and during loads. That confirms §7.

### Test C — the formatter
Set an execute breakpoint at **`0x80056C48`** (or `0x80056B78`). It should fire only
when the save/load file screen is drawn. Breakpoint **`0x80076680`** should fire for the
HH:MM:SS variant — noting which screen triggers it identifies that UI.

### Test D — persistence
Save, then compare RDRAM `0x8019BDC0` (image+0x80) against `0x801C44C4` — they must be
identical u32s. Then dump the emulator's `.fla` save file; the play time should appear
at offset **0x80** within the 6144-byte slot record.

---

## 10. Confidence and residual uncertainty

**Very high** for: the counter address, its width and units, the increment site, the
clamp, both formatters, the save-block base and the +0x0C / image +0x80 offsets, and
FlashRAM as the medium. Four mutually independent constants (`/30`, `/1800`, `/108000`,
clamp `10799999`, plus the 3-hour threshold `324000` in the ending overlay) all agree.

**Lower confidence** on:
- The exact identity of sub-loop `0x8005D200` (inferred as title/file menu from its
  handler `0x8005D770` reading the difficulty flags). Test B settles it.
- Whether the *actual* main-loop rate is 30 Hz on hardware. The display code
  unambiguously assumes 30 ticks/second; Test A step 3 confirms the real rate.
- `0x80074258` is described as "salt/padding" — it writes RNG (`0x800A3110`) words into
  scattered image slots (`0x8019BDB0`, `+0xB0`, `+0xF0`, `+0x270`, …). It is not a
  checksum over the play-time field, but it was not fully reversed.

## 11. Tooling added

- `tools/romscan.py` — raw-ROM MIPS scanner for constant `div`/`divu` across all
  overlays (no load address needed).
- `tools/gx.py` — copy of the `lui`/lo-part pairing xref tool
  (`ref <addr> <len>`, `dis`, `fs`, `refcount`), kept separate so the shared
  `tools/xref.py` is not clobbered.

Neither `baserom.z64` nor `build/boot.asm` was modified.
