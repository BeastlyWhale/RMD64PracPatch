# Rockman DASH / Mega Man 64 (JP, NRHJ) — ROM layout

ROM: `./baserom.z64`, 33,554,432 bytes (0x2000000), big-endian z64,
CIC-6102, header entry `0x80025C00` (word at ROM 0x08), name "RockMan Dash", cart id
`NRHJ` (ROM 0x3B).

All ROM offsets are absolute file offsets. All RAM addresses are KSEG0 virtual.

---

## 1. Master ROM map

| ROM range | Size | Contents |
|---|---|---|
| `0x0000000 - 0x0001000` | 0x1000 | Header + IPL3 boot code |
| `0x0001000 - 0x00AEBA0` | 0xADBA0 | **Boot segment** code+data -> RAM `0x80025C00-0x800D37A0` |
| `0x00AEBA0 - 0x00B6310` | 0x7770 | **Segment 2** (resident code+data) -> RAM `0x80225B70-0x8022D2F0` |
| `0x00B6310 - 0x02CC6D0` | 0x2163C0 | **43 code overlays** (2 tables, 45 slots, 2 empty) |
| `0x02CC6D0 - 0x1FE0780` | 0x1D140B0 | **Asset data** (~30.4 MB, custom-compressed, high entropy) |
| `0x1FE0780 - 0x2000000` | 0x1F880 | **0xFF padding (free)** |

Real data = 33,425,792 bytes (0x1FE0780, 31.88 MB). The ROM *is* padded out to 32 MB,
but only with 126.1 KB of filler.

### 1a. Free space (contiguous single-byte runs)

Exactly **one** run >= 4 KB exists in the entire ROM. There are no repeating-pattern
filler regions (checked periods 2/4/8/16, minimum 4096 bytes — none found).

| Rank | Start | Length | Fill | Where |
|---|---|---|---|---|
| 1 | `0x01FE0780` | `0x1F880` (129,152) | `0xFF` | **tail pad** |
| 2 | `0x000816B8` | `0x434` (1,076) | `0x00` | boot seg (RAM 0x800A62B8) |
| 3 | `0x0008043C` | `0x320` (800) | `0x00` | boot seg (RAM 0x800A503C) |
| 4 | `0x0022F6C4` | `0x150` (336) | `0x00` | inside an overlay |
| 5 | `0x001476C8` | `0x124` (292) | `0x00` | inside an overlay |
| 6 | `0x0085265C` | `0x10D` (269) | `0x00` | asset data |
| 7 | `0x00266A8C` | `0x108` (264) | `0x00` | inside an overlay |
| 8 | `0x000ACD0E` | `0xF4` (244) | `0x00` | boot seg (RAM 0x800D190E) |
| 9 | `0x000AD12E` | `0xF4` (244) | `0x00` | boot seg (RAM 0x800D1D2E) |
| 10 | `0x002AC1FC` | `0xF0` (240) | `0x00` | inside an overlay |

The last non-`0x00`/non-`0xFF` byte in the ROM is at **`0x01FE077F`** (value `0x22`).

**Injection target: `0x01FE0780 .. 0x02000000`, 129,152 bytes of `0xFF`.**
Use `0x01FE0800` for a 16-byte-aligned base -> 0x1F800 = 129,024 bytes usable,
comfortably above the 64 KB requirement. This is safe for the CIC-6102 checksum,
which only covers ROM `0x1000-0x101000`.

---

## 2. Overlay / segment tables

There is no single flat "file table" of ROM offsets for the whole ROM. Code loading is
driven by two **9-word (36-byte) segment descriptor tables** in boot data.

### Descriptor format (9 x u32 = 0x24 bytes)

| Word | Offset | Meaning |
|---|---|---|
| 0 | +0x00 | `romStart` (absolute ROM offset) |
| 1 | +0x04 | `romEnd` |
| 2 | +0x08 | `dmaDest` (RAM address the DMA writes to) |
| 3 | +0x0C | `textStart` (icache invalidate start) |
| 4 | +0x10 | `textEnd` (icache invalidate end) |
| 5 | +0x14 | `dataStart` (dcache invalidate start) |
| 6 | +0x18 | `dataEnd` (dcache invalidate end) |
| 7 | +0x1C | `bssStart` |
| 8 | +0x20 | `bssEnd` |

Invariant verified on all entries: `dataEnd - dmaDest == romEnd - romStart`.

### Table A — "big" overlay slot
* RAM `0x800BD610`, ROM `0x00098A10`, **31 entries**, 0x45C bytes.
* Referenced at `0x8007BB84` (`lui v1,0x800c; addiu v1,v1,-10736`).
* Loader: `loadOverlayA(int index)` at **`0x8007BB70`**.
* All entries load at `dmaDest = 0x8022D2F0`. Max bss end across the table = `0x80260660`.

### Table B — "small" overlay slot
* RAM `0x800BDA6C`, ROM `0x00098E6C`, **14 entries** (indices 0 and 1 are all-zero), 0x1F8 bytes.
* Referenced at `0x8007BBEC` (`lui v1,0x800c; addiu v1,v1,-9620`).
* Loader: `loadOverlayB(int index)` at **`0x8007BBD8`**.
* All entries load at `dmaDest = 0x80260660`. Max bss end = `0x80261480` (= heap start).

Combined, the 43 non-empty descriptors cover ROM `0x000B6310 - 0x002CC6D0`
**contiguously with no gaps** (verified). Largest overlay: table A index 26,
ROM `0x0026B070-0x0029C2B0`, 0x31240 bytes.

### Compression
Overlay payloads are **raw, uncompressed** MIPS code+data — they are DMA'd straight to
RAM and jumped into. Confirmed by disassembling ROM `0xAEBA0+` and matching internal
`j` targets (see section 3).

### Asset region (`0x02CC6D0 - 0x1FE0780`)
* No `Yay0` / `Yaz0` / `MIO0` magic anywhere in the ROM (0 occurrences each).
* No zlib/gzip container use (`78 9C` x86, `78 DA` x70, `1F 8B` x367 — all at chance
  rate for 32 MB of random-looking data, not at block starts).
* Shannon entropy of 64 KB samples: `0x2CC6D0` 7.72, `0x500000` 7.30, `0x1000000` 7.30,
  `0x1F00000` 7.73 bits/byte (vs. 5.74 for boot code). => **custom compression / packed data.**
* Identified asset table (audio): **ROM `0x00082BE4` / RAM `0x800A77E4`**, 85 x u32
  absolute ROM offsets, values `0x00AA1E20 .. 0x0194DEA0`, `0x007195D0` used as a
  null/default sentinel. Every non-sentinel entry points at a blob starting with the
  ASCII magic `"N64 WaveTables "` (69 such banks exist in the ROM).
* Many *relative* archive directories exist in boot data, format `{u32 size|0x80000000,
  u32 endOffsetOfFile}` with `end[i] - end[i-1] == align4(size[i])`. 49 such tables
  found; the largest is **ROM `0x00099644` / RAM `0x800BE244`, 444 entries,
  total 0x1D8B60**. Others sit at ROM `0x00082E1C`, `0x0009A51C`, `0x0009A83C`,
  `0x000A1944` ... `0x000A491C`. Bit 31 is set on every size word (likely a
  "compressed" flag; it is never clear in this ROM).

### Full overlay descriptor dump

| # | tbl | romStart | romEnd | size | text | data | bss end |
|---|-----|----------|--------|------|------|------|---------|
| 0 | A | 000B6310 | 000D1220 | 1AF10 | 8022D2F0-802446D0 | 802446D0-80248200 | 8024B780 |
| 1 | A | 00146CC0 | 00147B00 | E40 | 8022D2F0-8022DD00 | 8022DD00-8022E130 | 8022E130 |
| 2 | A | 0012A340 | 00131B20 | 77E0 | 8022D2F0-80233300 | 80233300-80234AD0 | 80236B80 |
| 3 | A | 00131B20 | 00146CC0 | 151A0 | 8022D2F0-8023E900 | 8023E900-80242490 | 802445B0 |
| 4 | A | 00147B00 | 001535D0 | BAD0 | 8022D2F0-80236480 | 80236480-80238DC0 | 80238E60 |
| 5 | A | 001535D0 | 00181B30 | 2E560 | 8022D2F0-80255CE0 | 80255CE0-8025B850 | 8025DBB0 |
| 6 | A | 00107E70 | 0012A340 | 224D0 | 8022D2F0-8024A7D0 | 8024A7D0-8024F7C0 | 8024FD80 |
| 7 | A | 00181B30 | 001883D0 | 68A0 | 8022D2F0-80231F00 | 80231F00-80233B90 | 80235BE0 |
| 8 | A | 001883D0 | 0019AB20 | 12750 | 8022D2F0-8023CF90 | 8023CF90-8023FA40 | 80241AF0 |
| 9 | A | 000EB800 | 00107E70 | 1C670 | 8022D2F0-80245070 | 80245070-80249960 | 80249B00 |
| 10 | A | 0019AB20 | 001AF9B0 | 14E90 | 8022D2F0-8023FE50 | 8023FE50-80242180 | 802421B0 |
| 11 | A | 000D1220 | 000EB800 | 1A5E0 | 8022D2F0-80243EC0 | 80243EC0-802478D0 | 802499A0 |
| 12 | A | 001AF9B0 | 001BC780 | CDD0 | 8022D2F0-802384E0 | 802384E0-8023A0C0 | 8023A0F0 |
| 13 | A | 001BC780 | 001CA260 | DAE0 | 8022D2F0-80238070 | 80238070-8023ADD0 | 8023CDD0 |
| 14 | A | 001CA260 | 001D9D60 | FB00 | 8022D2F0-8023AC40 | 8023AC40-8023CDF0 | 8023D140 |
| 15 | A | 001D9D60 | 001DF8C0 | 5B60 | 8022D2F0-802314B0 | 802314B0-80232E50 | 80232E70 |
| 16 | A | 001DF8C0 | 001EF260 | F9A0 | 8022D2F0-8023ABB0 | 8023ABB0-8023CC90 | 8023CCC0 |
| 17 | A | 001EF260 | 001F8C60 | 9A00 | 8022D2F0-80235A20 | 80235A20-80236CF0 | 80236D00 |
| 18 | A | 001F8C60 | 00200690 | 7A30 | 8022D2F0-802335C0 | 802335C0-80234D20 | 80234D40 |
| 19 | A | 00200690 | 002181A0 | 17B10 | 8022D2F0-80241150 | 80241150-80244E00 | 80246EF0 |
| 20 | A | 002181A0 | 0022D5A0 | 15400 | 8022D2F0-8023F4A0 | 8023F4A0-802426F0 | 802447B0 |
| 21 | A | 0022D5A0 | 00230400 | 2E60 | 8022D2F0-8022F3D0 | 8022F3D0-80230150 | 802321A0 |
| 22 | A | 00230400 | 00234150 | 3D50 | 8022D2F0-80230190 | 80230190-80231040 | 80233060 |
| 23 | A | 00234150 | 00250150 | 1C000 | 8022D2F0-80245560 | 80245560-802492F0 | 8024B420 |
| 24 | A | 00250150 | 00253A60 | 3910 | 8022D2F0-8022FD00 | 8022FD00-80230C00 | 80230C40 |
| 25 | A | 00253A60 | 0026B070 | 17610 | 8022D2F0-802401F0 | 802401F0-80244900 | 80244960 |
| 26 | A | 0026B070 | 0029C2B0 | 31240 | 8022D2F0-80256050 | 80256050-8025E530 | 80260660 |
| 27 | A | 0029C2B0 | 002A8EA0 | CBF0 | 8022D2F0-80237570 | 80237570-80239EE0 | 80239F50 |
| 28 | A | 002A8EA0 | 002ACF70 | 40D0 | 8022D2F0-80230510 | 80230510-802313C0 | 802313C0 |
| 29 | A | 002ACF70 | 002C0E70 | 13F00 | 8022D2F0-8023F670 | 8023F670-802411F0 | 802432D0 |
| 30 | A | 002C0E70 | 002C6730 | 58C0 | 8022D2F0-80230C20 | 80230C20-80232BB0 | 80234BF0 |
| 0 | B | (empty) | | | | | |
| 1 | B | (empty) | | | | | |
| 2 | B | 002C6730 | 002C6EB0 | 780 | 80260660-80260DC0 | 80260DC0-80260DE0 | 80260DE0 |
| 3 | B | 002C6EB0 | 002C7450 | 5A0 | 80260660-80260BD0 | 80260BD0-80260C00 | 80260C00 |
| 4 | B | 002C7450 | 002C7800 | 3B0 | 80260660-80260A00 | 80260A00-80260A10 | 80260A10 |
| 5 | B | 002C7800 | 002C8460 | C60 | 80260660-80261270 | 80261270-802612C0 | 802612D0 |
| 6 | B | 002C8460 | 002C8C80 | 820 | 80260660-80260E30 | 80260E30-80260E80 | 80260E80 |
| 7 | B | 002C8C80 | 002C9060 | 3E0 | 80260660-80260A20 | 80260A20-80260A40 | 80260A40 |
| 8 | B | 002C9060 | 002C98B0 | 850 | 80260660-80260E40 | 80260E40-80260EB0 | 80260EB0 |
| 9 | B | 002C98B0 | 002CA6D0 | E20 | 80260660-80261380 | 80261380-80261480 | 80261480 |
| 10 | B | 002CA6D0 | 002CAD90 | 6C0 | 80260660-80260CC0 | 80260CC0-80260D20 | 80260D20 |
| 11 | B | 002CAD90 | 002CB4C0 | 730 | 80260660-80260D60 | 80260D60-80260D90 | 80260D90 |
| 12 | B | 002CB4C0 | 002CBCE0 | 820 | 80260660-80260E00 | 80260E00-80260E80 | 80260E80 |
| 13 | B | 002CBCE0 | 002CC6D0 | 9F0 | 80260660-80260FE0 | 80260FE0-80261050 | 80261050 |

---

## 3. Code outside the boot segment — YES, two kinds

### 3a. Segment 2 — always resident (safe for hooks)
Loaded exactly once during early init at **`0x8002695C`** with hardcoded arguments:

```
8002695C  lui   a0,0xb      ; a0 = 0x000AEBA0   romStart
80026960  addiu a0,a0,-5216
80026964  lui   a1,0xb      ; a1 = 0x000B6310   romEnd
80026968  addiu a1,a1,25360
8002696C  lui   a2,0x8022   ; a2 = 0x80225B70   dmaDest
80026974  lui   a3,0x8022   ; a3 = 0x80225B70   textStart
80026984  sw    v0,16(sp)   ;      0x80226010   textEnd
80026990  sw    v0,20(sp)   ;      0x80226010   dataStart
8002699C  sw    v0,24(sp)   ;      0x8022D2E0   dataEnd
800269A8  sw    v0,28(sp)   ;      0x8022D2E0   bssStart
800269B4  jal   0x80027388  ;      0x8022D2F0   bssEnd
```

=> **RAM `0x80225B70 - 0x8022D2F0`**: text `0x80225B70-0x80226010` (0x4A0),
data `0x80226010-0x8022D2E0` (0x72D0), bss `0x8022D2E0-0x8022D2F0` (0x10).
It is the *only* caller of the loader with these arguments and no overlay descriptor
ever targets this range (min overlay `dmaDest` = `0x8022D2F0`), so it is **permanently
resident**.

Boot code calls into it with static `jal`s — evidence it is linked as a fixed segment:
`0x800358A0 jal 0x80225F04`, `0x80035988 jal 0x80225D30`,
`0x80073964 / 0x8007399C / 0x80073C30 / 0x80073C38 / 0x80073C40 / 0x80073C48 jal 0x80225B70`.

Note: IPL3 loads a full 1 MB (ROM `0x1000-0x101000` -> RAM `0x80025C00-0x80125C00`),
so ROM `0xAEBA0+` is *also* splattered into `0x800D37A0+` at reset — but the entry stub
at `0x80025C00` immediately bzeros `0x800D37A0 .. 0x80225B70` (t1 = 0x1523D0), destroying
it. That is why segment 2 must be re-DMA'd. (`build/boot.asm` lines at
`0x800D39B8 j 0x80225ED4` / `0x800D3B64 j 0x80225FEC` are this stale copy, not live code.)

### 3b. Swappable overlays (NOT safe for hooks)
* Slot 1: **`0x8022D2F0 - 0x80260660`** (table A, 31 descriptors)
* Slot 2: **`0x80260660 - 0x80261480`** (table B, 12 live descriptors)

Boot code also makes static calls into slot 1 (`0x80080814 jal 0x8023A98C`,
`0x8003918C jal 0x802446A0`, `0x800A8308 / 0x800B3930 / 0x800B393C j 0x8024282C`) —
those addresses all fall inside overlay A#0's text (`0x8022D2F0-0x802446D0`), i.e. A#0
acts as a common overlay. Code at those addresses changes whenever another overlay is
swapped in, so nothing there may be hooked.

### 3c. Full RAM map

| RAM range | Size | Use |
|---|---|---|
| `0x80025C00 - 0x800D37A0` | 0xADBA0 | boot code+data (resident, **hook here**) |
| `0x800D37A0 - 0x80225B70` | 0x1523D0 | bss, bzero'd by the entry stub at `0x80025C00` |
| `0x80225B70 - 0x8022D2F0` | 0x7780 | segment 2 (resident, **hook here**) |
| `0x8022D2F0 - 0x80260660` | 0x33370 | overlay slot 1 (volatile) |
| `0x80260660 - 0x80261480` | 0xE20 | overlay slot 2 (volatile) |
| `0x80261480 - 0x803B5000` | 0x153B80 | heap; zeroed byte-by-byte at `0x80025DD0` |
| `0x803B5000 - 0x80400000` | 0x4B000 | in use — `bzero(0x803B5000, 0x4B000)` at `0x8002766C` and `0x80028B30` (framebuffers/Z) |

Nothing above `0x80400000` is touched: the game fits in 4 MB base RDRAM.
There is **no unused resident RAM hole**; injected code must either take over dead
space inside the boot segment / segment 2, or shrink the heap by patching the
`0x803B5000` constant at `0x80025DE0`.

---

## 4. DMA routines

### `loadSegment` — the game's ROM->RAM segment loader
**`0x80027388`**, 9 arguments (a0-a3 + 5 stack words at caller `sp+0x10 .. sp+0x20`):

```c
void loadSegment(u32 romStart, u32 romEnd, void *dmaDest,
                 void *textStart, void *textEnd,
                 void *dataStart, void *dataEnd,
                 void *bssStart,  void *bssEnd);
```

Body (`0x80027388-0x8002748C`):
1. `if (bssEnd != bssStart) bzero(bssStart, bssEnd - bssStart)` — `0x800273D4 jal 0x80099f70`
2. `osInvalICache(textStart, textEnd - textStart)` — `0x800273E0 jal 0x800991B0`
3. `osInvalDCache(dataStart, dataEnd - dataStart)` — `0x800273EC jal 0x80099100`
4. `len = (romEnd - romStart + 1) & ~1`
5. loop, max 0x1000 bytes per iteration:
   `osPiStartDma(&ioMesg, 0 /*pri*/, 0 /*OS_READ*/, romAddr, dest, chunk, &mq@0x800D3CE8)`
   at `0x80027440`, then `osRecvMesg(&mq, &msg, OS_MESG_BLOCK)` at `0x80027450`.

This is the easiest hook point for loading injected code: it is called from exactly three
sites (`0x800269B4`, `0x8007BBC4`, `0x8007BC2C`) and takes plain ROM offsets.

### libultra primitives
| Address | Function | Signature |
|---|---|---|
| **`0x80094C10`** | `osPiStartDma` | `(OSIoMesg *mb, s32 pri, s32 dir, u32 devAddr, void *dramAddr, u32 size, OSMesgQueue *mq)` |
| **`0x800A00C0`** | `osPiRawStartDma` | `(s32 dir, u32 devAddr, void *dramAddr, u32 size)`; `dir==0` = ROM->RAM |
| `0x80099870` | `osVirtualToPhysical` | `(void *)` |
| `0x800991B0` | `osInvalICache` | `(void *, s32)` |
| `0x80099100` | `osInvalDCache` | `(void *, s32)` |
| `0x80099F70` | `bzero` | `(void *, s32)` |
| `0x80099CA0` | `bcopy`-style byte copy | `(src, dst, len)` |
| `0x8009A2A0` | `osRecvMesg` | |
| `0x8009A3D0` / `0x8009A160` | `osSendMesg` / `osJamMesg` | |

`osPiRawStartDma` internals, verified at `0x800A00C0`:
```
800A00D4  lui v0,0xa460 ; ori v0,0x10   -> poll PI_STATUS (0xA4600010) & 3
800A0110  jal 0x80099870                -> osVirtualToPhysical(dramAddr)
800A0134  sw  v0,0(a0)  a0=0xA4600000   -> PI_DRAM_ADDR
800A0140  sw  v1,0(a2)  a2=0xA4600004   -> PI_CART_ADDR = (*(u32*)0x80000308 | devAddr) & 0x1FFFFFFF
800A015C  v1 = 0xA460000C (PI_RD_LEN)   when dir == 0  (cart -> RAM)
800A0168  v1 = 0xA4600008 (PI_WR_LEN)   when dir == 1
800A0170  sw  (size-1),0(v1)
```
Note `osPiRawStartDma` is never reached by a direct `jal` — it is driven by the PI
manager thread. For injected boot-time copying, call **`osPiRawStartDma` (0x800A00C0)**
directly (no OS services needed) or reuse **`loadSegment` (0x80027388)**.

---

## 5. Practical injection recipe

1. Write payload to ROM `0x01FE0800` (up to 0x1F800 bytes). Checksum-safe:
   CIC-6102 only sums ROM `0x1000-0x101000`.
2. Pick a RAM landing zone. There is no free resident hole, so either
   (a) patch `0x80025DE0` (`ori v1,v1,0x5000` on `lui v1,0x803b`) to lower the heap top
   and carve RAM out of `0x803B5000-`, or
   (b) reuse dead zero space at RAM `0x800A62B8` (0x434 bytes) / `0x800A503C` (0x320
   bytes) for a small trampoline only.
3. Hook early: replace the `jal 0x80027388` at `0x800269B4` (segment-2 load) or one of
   the `jal`s right after it (`0x800269D4`, `0x800269DC`, `0x800269E4`) with a call to
   your loader stub, which does
   `osPiRawStartDma(0, 0x01FE0800, dest, len)` then polls PI_STATUS, then
   `osInvalICache(dest, len)` / `osInvalDCache(dest, len)`.
4. Anything you hook must live in `0x80025C00-0x800D37A0` or `0x80225B70-0x8022D2F0`.
   Never hook `0x8022D2F0-0x80261480` — those bytes are overwritten on every overlay swap.
