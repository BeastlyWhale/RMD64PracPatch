# Quiescence — what is outstanding, and how to test it cheaply

Goal: a predicate evaluable from the frame hook (which runs on the **main game
thread**, `0x8018AE70`, entry `0x80026930`, priority 10 — the lowest real thread in
the system) that means "no hardware operation is outstanding anywhere, and no
thread is parked waiting for an event that has already fired".

Everything below is derived from `build/boot.asm` created with --disasm flag in build.py
(the resident boot segment,`0x80025C00`–`0x80125BFC`). Lines marked **PROVED** are read directly off
disassembly; **INFERRED** is reasoning on top of that.

---

## 0. OS primitive layouts (PROVED)

### `OSMesgQueue` — from `osCreateMesgQueue` `0x8009A130`

```
8009A130  lui  v0,0x800d
8009A134  addiu v0,v0,80        ; v0 = 0x800D0050  (= __osThreadTail)
8009A138  sw   v0,0(a0)         ; +0x00 mtqueue   = &__osThreadTail
8009A13C  sw   v0,4(a0)         ; +0x04 fullqueue = &__osThreadTail
8009A140  sw   zero,8(a0)       ; +0x08 validCount = 0
8009A144  sw   zero,12(a0)      ; +0x0C first      = 0
8009A148  sw   a2,16(a0)        ; +0x10 msgCount   = a2
8009A150  sw   a1,20(a0)        ; +0x14 msg        = a1
```

| field | offset |
|---|---|
| `mtqueue` | **+0x00** |
| `fullqueue` | +0x04 |
| **`validCount`** | **+0x08** |
| `first` | +0x0C |
| `msgCount` | +0x10 |
| `msg` | +0x14 |

Cross-checked against `osRecvMesg` `0x8009A2A0` (`lw v1,8(s0)` @ `8009A2CC` is the
`validCount == 0` test; `addiu v0,v0,-1 / sw v0,8(s0)` @ `8009A370`–`8009A374`
decrements it; `lw v0,12(s0)` / `lw v1,20(s0)` @ `8009A318`–`8009A31C` index
`msg[first]`) and `osSendMesg` `0x8009A3D0` (`lw v1,8(s0)` / `lw a0,16(s0)` /
`slt` @ `8009A400`–`8009A408` is `validCount >= msgCount`).

**`__osThreadTail = 0x800D0050`** — proved twice: the immediate above, and the
image contents at `0x800D0054 = 0xFFFFFFFF` (the sentinel's `priority = -1`) with
`0x800D0058` (`__osRunQueue`) and `0x800D005C` initialised to `0x800D0050`.
`__osRunningThread` is at **`0x800D0060`** (`lw v0,96(v0)` @ `8009A2F8`).

### `OSThread` (PROVED)

`__osEnqueueThread` `0x8009FDB4` ends with

```
8009FDF4  jr   ra
8009FDF8  sw   a0,8(a1)        ; t->queue = queue   =>  queue at +0x08
```

and `osRecvMesg` sets the waiting state with `sh s3,16(v0)` where `s3 = 8`
(`li s3,8` @ `8009A2D8`), i.e. `state` is a **u16 at +0x10**, `OS_STATE_WAITING = 8`.
`osStartThread` `0x8009B6B0` reads it back (`lhu v1,16(s0)` @ `8009B6CC`) and
writes `OS_STATE_RUNNABLE = 2` (`sh v0,16(s0)` @ `8009B6E8`).

> **Important gotcha.** `__osPopThread` (`0x8009FDFC`) unlinks a thread from a
> queue by advancing the head pointer only — it does **not** clear `t->queue`.
> So `thread->queue == Q` alone does *not* mean "blocked on Q"; it can be stale.
> Either also test `thread->state == 8`, **or** test from the queue side
> (`mq->mtqueue`), which is always accurate. The queue-side test is used below.

**Generic test, 1 load + 1 compare:**

* `*(u32*)Q == 0x800D0050` → *nobody* is blocked receiving on `Q`.
* `*(u32*)Q == T` → thread `T` is parked receiving on `Q` (when `T` is the only
  receiver, which holds for every queue below).

---

## 1. The PI manager and its command queue

### `osCreatePiManager` = `0x800941C0` (PROVED)

`a0 = pri, a1 = cmdQ, a2 = cmdBuf, a3 = cmdMsgCnt`:

```
80094200  jal  0x8009a130       ; osCreateMesgQueue(a1=cmdQ, a2=cmdBuf, a3=cnt)
...
80094294  lui  a2,0x8009
80094298  addiu a2,a2,18272     ; entry = 0x80094760  (__osDevMgrMain)
800942E4  move a3,s4            ; arg   = 0x800CF4F0  (&__osPiDevMgr)
800942EC  sw   s1,-2828(at)     ; __osPiDevMgr.thread   = 0x801997A0
800942F4  sw   s5,-2824(at)     ; __osPiDevMgr.cmdQueue = a1
800942FC  sw   s3,-2820(at)     ; __osPiDevMgr.evtQueue = 0x8019A950
800942B0  sw   v1,-2816(at)     ; acsQueue = 0x801E0E10
800942C0  sw   v1,-2812(at)     ; dma      = 0x800A00C0  (osPiRawStartDma)
800942D0  sw   v1,-2808(at)     ; edma     = 0x80094350
```

`dma == 0x800A00C0` matching the already-known `osPiRawStartDma` confirms the
whole `OSDevMgr` layout at **`0x800CF4F0`**:

| offset | addr | field | value |
|---|---|---|---|
| +0x00 | `0x800CF4F0` | `active` | 1 once started |
| +0x04 | `0x800CF4F4` | `thread` | `0x801997A0` |
| +0x08 | `0x800CF4F8` | `cmdQueue` | `0x801974E0` |
| +0x0C | `0x800CF4FC` | `evtQueue` | `0x8019A950` |
| +0x10 | `0x800CF500` | `acsQueue` | `0x801E0E10` |
| +0x14 | `0x800CF504` | `dma` | `0x800A00C0` |
| +0x18 | `0x800CF508` | `edma` | `0x80094350` |

`osPiGetCmdQueue` `0x800A0190` returns `*(u32*)0x800CF4F8`; `osPiStartDma`
`0x80094C10` posts every request there (`jal 0x8009A3D0` @ `80094CA0`, or
`osJamMesg 0x8009A160` @ `80094C80` for `OS_MESG_PRI_HIGH`).

### The one call site (PROVED)

`0x80092D80` is the only caller of `osCreatePiManager`:

```
80092D84  li    a0,150                     ; pri      = 150
80092D88  lui   a1,0x8019
80092D8C  addiu a1,a1,29920                ; cmdQ     = 0x801974E0
80092D90  lui   a2,0x8019
80092D94  addiu a2,a2,29944                ; cmdBuf   = 0x801974F8
80092D9C  jal   0x800941c0
80092DA0  li    a3,50                      ; msgCount = 50
```

`0x801974E0 + 0x18 = 0x801974F8` — the buffer starts exactly past the 24-byte
queue struct, as expected.

> ### ANSWERS TO QUESTION 1
> * **PI command queue `OSMesgQueue` = `0x801974E0`** (50 messages, buffer
>   `0x801974F8`–`0x801975C0`).
> * **`validCount` offset = `+0x08`**, i.e. the u32 to test is **`0x801974E8`**.
> * PI manager thread `OSThread` = **`0x801997A0`**, priority 150, entry
>   `0x80094760`, arg `0x800CF4F0`.
> * PI event (interrupt) queue = **`0x8019A950`** (1 message, buffer `0x8019A968`);
>   registered with `osSetEventMesg(OS_EVENT_PI=8, 0x8019A950, 0x22222222)` at
>   `0x80094248`.

**`validCount == 0` is not sufficient on its own.** The devmgr can have dequeued a
request and be sitting in `osRecvMesg(evtQueue)` at `0x8009482C`/`0x80094868`
waiting for the PI interrupt, with `validCount` already back to 0. The strictly
stronger, equally cheap test is:

```c
*(u32*)0x801974E0 == 0x801997A0   /* cmdQueue.mtqueue == &piManagerThread */
```

i.e. the PI manager is parked on its command queue. Since `osPiStartDma` sends
`OS_MESG_NOBLOCK`, a send immediately pops the devmgr off `mtqueue` and starts it,
so this is true **iff** nothing is queued *and* nothing is in flight. (One load,
one compare. Keep `0x801974E8 == 0` as a cheap belt-and-braces second term.)

---

## 2. Other PI / DMA-in-flight indicators

### Every PI DMA site in the resident image (PROVED)

`ana.py callers` on the three entry points:

| function | callers |
|---|---|
| `osPiStartDma 0x80094C10` | `0x80026D1C`, `0x80026E00`, `0x80026EAC`, `0x80026F7C`, `0x80027018` (all in `0x80026C80`), `0x800271D4` (in `0x800270F8`), `0x800272E8` (in `0x8002724C`), `0x80027440` (in `loadSegment 0x80027388`), `0x80027500` (in `0x80027490`) |
| `osPiRawStartDma 0x800A00C0` | **none** (only reached via `__osPiDevMgr.dma`) |
| `osEPiRawStartDma 0x80094350` | **none** |
| `0x8009453C` (osEPiStartDma) | **none** |

So **all PI traffic funnels through five game functions in `0x80026C80`–`0x80027500`**,
and all of them use `osPiStartDma` → the PI command queue. This is what makes the
`0x801974E0` test authoritative.

### The synchronous loaders (PROVED)

`loadSegment 0x80027388` is synchronous *on the calling thread*:

```
80027440  jal 0x80094c10        ; osPiStartDma(..., retQ = 0x800D3CE8)
80027450  jal 0x8009a2a0        ; osRecvMesg(0x800D3CE8, &msg, OS_MESG_BLOCK)
```

Same shape in `0x8002724C` (`0x800272E8`/retQ `0x800D3CE8`), `0x80027334`
(wrapper over `0x80026C80`, retQ `0x800D3CE8`) and `0x80027490` (retQ
`0x800D3CE8`). The overlay loaders `loadOverlayA 0x8007BB70` / `loadOverlayB
0x8007BBD8` call `loadSegment`, so **an overlay swap parks the calling thread in
`osRecvMesg(0x800D3CE8)`**.

Because the frame hook *is* the main game thread, the main thread can never be
inside one of these at hook time — but a second thread could be, so test the
queue anyway.

### The asynchronous loader thread (PROVED)

`0x80026BD0` is the loader subsystem init (called from `0x80026954`, in `main`):

| object | address | note |
|---|---|---|
| retQueue A | `0x800D3CC8` | 1 msg, buffer `0x800D3CE0` |
| retQueue B | `0x800D3CE8` | 1 msg, buffer `0x800D3D00` — **the shared "DMA done" queue** used by all four synchronous loaders |
| request queue | `0x800D3BB0` | **64 msgs**, buffer `0x800D3BC8`–`0x800D3CC8` |
| loader thread | `0x800D3880` | id 7, priority 100, entry `0x800270F8` |

Loop body of `0x800270F8`:

```
80027120  a0 = 0x800D3BB0
80027128  jal 0x8009a2a0        ; osRecvMesg(requestQ, &req, BLOCK)   <-- idle here
...
800271D4  jal 0x80094c10        ; osPiStartDma(..., retQ = 0x800D3CC8)
800271E4  jal 0x8009a2a0        ; osRecvMesg(0x800D3CC8, BLOCK)
...
80027214  lw  v0,27264(v0)      ; v0 = *(u32*)0x800A6A80
80027218  addiu v0,v0,-1
80027224  sw  v0,27264(at)      ; *(u32*)0x800A6A80 = v0 - 1
```

> ### ANSWERS TO QUESTION 2
> * **Game-level "DMA in flight" counter: `0x800A6A80` (u32).** It is *decremented*
>   by the loader thread when a request finishes (`0x80027214`–`0x80027224`), and
>   there is a public getter **`0x8002753C`** (`lui v0,0x800a / lw v0,27264(v0) / jr ra`)
>   which the game's own task scheduler polls at `0x80026384`
>   (`jal 0x8002753C; bnez v0, skip`). Semantics: **0 = no queued/in-flight async
>   loads.**
> * **Caveat (PROVED negative):** no instruction in the resident image *increments*
>   `0x800A6A80`, and nothing in the resident image *sends* to the request queue
>   `0x800D3BB0` (only `0x80026C20`/`0x80026C50` init and `0x80027120` recv).
>   `grep` for every encoding of `0x800A6A80` finds exactly 5 references
>   (`0x80027214`, `0x80027224`, `0x80027544`, `0x800DED48`, `0x8010734C` — all
>   reads except the one decrement). **INFERRED:** the enqueue side lives in
>   overlay code (`0x8022D2F0`+), which is outside `boot.asm`. So treat
>   `0x800A6A80 == 0` as corroborating evidence, not as the primary test — the
>   primary test is the loader thread's own park state.
> * **Loader thread idle test:** `*(u32*)0x800D3BB0 == 0x800D3880`.
> * **Nobody blocked on a loader retQueue:** `*(u32*)0x800D3CC8 == 0x800D0050`
>   **and** `*(u32*)0x800D3CE8 == 0x800D0050`.
> * There is **no libultra "pi manager busy" flag** in this build beyond
>   `__osPiDevMgr.active` (`0x800CF4F0`), which is a one-shot "initialised" flag,
>   not a busy flag (`osPiStartDma` `0x80094C24` only tests it for != 0).
> * Hardware backstop: `PI_STATUS 0xA4600010 & 3` (the same spin `osPiRawStartDma`
>   uses at `0x800A00D4`, and `osCartRomInit` at `0x80094640`).
> * `acsQueue 0x801E0E10` is the PI access mutex — irrelevant here, nothing in the
>   game takes it.
> * **FlashRAM save I/O is a separate PI-ish blocker**: `0x800872B8` /
>   `0x800874E4` / `0x80087678` / `0x800877B8` call `0x80095350`
>   (`osFlashReadArray`-shaped, 128-byte pages) with `OSIoMesg 0x801856E0` and then
>   `osRecvMesg(0x801856F8, BLOCK)` at `0x80087390`. Only live during save/load, but
>   `*(u32*)0x801856F8 == 0x800D0050` is one more cheap term if you want it.

---

## 3. Audio

### The audio thread (PROVED)

Created at `0x8008E880` inside `0x8008E76C`:

```
8008E850  lui   s0,0x8019
8008E854  addiu s0,s0,-12032       ; OSThread = 0x8018D100
8008E86C  lui   a2,0x8009
8008E870  addiu a2,a2,-5976        ; entry    = 0x8008E8A8
8008E868  lw    v1,12(s2)          ; priority from the runtime config struct
```

Main loop `0x8008E8A8`:

```
8008E8FC  v0 = *0x800CF11C ; v0 = *(v0+4) ; jalr v0     ; per-poll callback
8008E910  jal 0x80095510                               ; read AI_STATUS  (0xA450000C)
8008E918  jal 0x80095500                               ; read AI_LEN     (0xA4500004)
8008E920  and s0,s0,0x80000000
8008E924  bnez s0,0x8008e8fc                           ; <-- BUSY-WAIT, not a recv
...
8008E95C  jal 0x80095640                               ; osAiSetNextBuffer
8008E9CC  v0 = *(*0x800CF11C + 8) ; jalr v0            ; submit the audio RSP task
8008EA00  j   0x8008e8fc                               ; loop forever
```

`*(u32*)0x800CF11C` points at the driver vtable **`0x800CEE28`** (image contents:
`0x800CEE28 = 0x80089E94`, `+4 = 0x80089EF8`, `+8 = 0x80089F88`; the byte at
`0x800CEE20` is 1, which is exactly the `lbu v1,-4576(v1); li v0,1; bne` guard at
the top of `0x80089F88`). Set up by `0x8008AFDC` (called from `0x80089E74`).

### `0x80089F88` — submit audio RSP task and wait (PROVED)

```
80089F88  ...
80089FC8  a0 = 0x8021D254                ; scheduler HIGH-PRIORITY task queue
80089FE4  jal 0x8009a3d0                 ; osSendMesg(0x8021D254, task 0x801B5DB0, BLOCK)
80089FF0  a0 = 0x801AD4E8
80089FF8  jal 0x8009a2a0                 ; osRecvMesg(0x801AD4E8, NULL, BLOCK)   <-- BLOCKS
```

`0x801AD4E8` is created in `0x80089E94` with **1 message**, buffer `0x8018D030`
(`0x80089ECC`). It is the audio-task-done queue.

> ### ANSWERS TO QUESTION 3
> * **Audio thread `OSThread` = `0x8018D100`, entry `0x8008E8A8`.**
> * It **never blocks on a message queue in its outer loop** — it busy-polls
>   `AI_STATUS` bit 31. The *only* place it blocks is
>   **`osRecvMesg(0x801AD4E8)` inside `0x80089F88`**, once per audio frame, for the
>   duration of the audio RSP task.
> * **The audio system does NOT stream from ROM.** This is a proved negative: the
>   complete list of `osPiStartDma` callers (§2) contains no audio code. The only
>   ROM read the audio subsystem ever does is a one-shot bank/table load at
>   `0x80088F80` (ROM `0x718DF0`–`0x718F10` → `0x80188B30`, `0x80188C90`), reached
>   once from `0x80036A10`. Samples are played out of RAM-resident banks.
>   **There is therefore no "music playing ⇒ never quiescent" problem.**
> * **Simple global for "audio RSP task outstanding":** yes — `0x8021D8C0` (§4).
> * **Cheap test that the audio thread is not parked:**
>   `*(u32*)0x801AD4E8 == 0x800D0050`.

---

## 4. The RSP/RDP task scheduler — and the most likely cause of your hang

`0x80091BE0` builds a small scheduler around base `SC = 0x8021D2C4`, with three
threads (all `osCreateThread` at `0x80091D8C`, `0x80091DC4`, `0x80091DFC`):

| thread | `OSThread` | id | pri | entry | blocks on |
|---|---|---|---|---|---|
| sched | `0x8021D3A8` | 19 | 120 | `0x80091E44` | `0x8021D2C4` (VI retrace 666 / PRENMI 669) |
| **hi-pri task runner** | `0x8021D558` | 18 | 110 | `0x8009213C` | **`0x8021D254`** |
| **task runner** | `0x8021D708` | 17 | 100 | `0x80092298` | **`0x8021D28C`** |

Queues (all created in `0x80091BE0`, 8 messages each):

| address | role | evidence |
|---|---|---|
| `0x8021D254` | hi-priority (audio) task request | recv `0x80092170`; send `0x80089FE4` |
| `0x8021D28C` | normal (graphics) task request | recv `0x800922BC`; send **`0x80092A9C` — inside the known graphics submit `0x8009292C`** |
| `0x8021D2C4` | scheduler events | `osViSetEvent(…,666)` `0x80091D28`; `osSetEventMesg(OS_EVENT_PRENMI=14,…,669)` `0x80091D58` |
| `0x8021D2FC` | **SP done** | `osSetEventMesg(OS_EVENT_SP=4, 0x8021D2FC, 667)` @ `0x80091D38` |
| `0x8021D334` | **DP done** | `osSetEventMesg(OS_EVENT_DP=9, 0x8021D334, 668)` @ `0x80091D48` |
| `0x8021D36C` | "hi-pri task finished" notification | send `0x80092214`; recv `0x80092310` |

The event numbers 4 / 8 / 9 / 14 all match stock libultra `OS_EVENT_SP` /
`_PI` / `_DP` / `_PRENMI` — strong confirmation the model is right.

Two globals record what the RSP is doing (PROVED):

| address | meaning | written |
|---|---|---|
| **`0x8021D8BC`** | normal (graphics) `OSTask*` currently loaded on the RSP | set `0x80092344`, cleared `0x80092384` (thread 17); read `0x800921A0` (thread 18, to decide whether to `osSpTaskYield`) |
| **`0x8021D8C0`** | hi-priority (audio) `OSTask*` currently loaded on the RSP | set `0x800921E0`, cleared `0x80092208` (thread 18); read `0x800922EC` (thread 17, to decide whether to wait on `0x8021D36C`) |

Both are zeroed at init (`0x80091C18`, `0x80091C20`).

The libultra RSP calls used are `0x8009A5B0` (`osSpTaskLoad`), `0x8009A7BC`
(`osSpTaskStartGo`), `0x8009A7F0` (`osSpTaskYield`), `0x8009A810`
(`osSpTaskYielded`) — identified from their use order in `0x8009213C` and
`0x80092298`.

### Why the console dies completely (INFERRED, but tightly)

Your current gate `*(u32*)0x80207C80 == 0` only covers **graphics** tasks
(`0x80092A6C` increments, `0x8009276C` decrements, `0x80092B70` spins). It says
nothing about the **audio** RSP task, which is submitted through an entirely
separate path (`0x80089F88` → `0x8021D254` → thread 18).

If the snapshot is taken while an audio task is on the RSP:

1. thread 18 (`0x8021D558`) is parked in `osRecvMesg(0x8021D2FC)` at `0x800921F8`
   waiting for the SP interrupt, with `0x8021D8C0 != 0`;
2. the audio thread (`0x8018D100`) is parked in `osRecvMesg(0x801AD4E8)` at
   `0x80089FF8`.

On restore, that SP interrupt fired long ago. So:

* thread 18 never wakes → `0x8021D8C0` stays non-zero **forever**;
* thread 17 (`0x80092298`), on the next graphics task, reads `0x8021D8C0` at
  `0x800922EC`, finds it non-zero and blocks on `0x8021D36C` at `0x80092310`
  waiting for a "hi-pri done" message that will never come;
* therefore `0x80207C80` never decrements, and the main thread spins forever in
  `0x80092B70` → **video frozen, your HUD clock frozen**;
* and the audio thread never wakes → **audio silent at the same instant**.

That is exactly the symptom you describe, and it is reachable *without* any PI
DMA being involved at all. The same argument applies with thread 17 parked on
`0x8021D2FC`/`0x8021D334` if an RDP task is outstanding.

---

## 5. What makes the intro special

**Proved:**

* The main loop is `0x8003E168`, dispatching a mode table at `0x800AC500` indexed
  by `*(s8*)0x801C44B8`:
  `0:0x8003E2A0  1:0x8003BE70  2:0x8003C01C  3:0x8003C1B8  4:0x8003CF4C
   5:0x8003D0D8  6:0x8003E394  7:0x8003E4F0  8:0x8003D5BC  9,10:0x8003D85C`.
  The "alternative frame loops" from `docs/timer.md` are these mode handlers and
  the fade helper `0x8003DFEC` (a 41-frame countdown, `li s0,41` @ `0x8003DFF4`).
* All of them yield through `0x800290C4`, which is a **coroutine yield on the
  same main thread** (`osGetThreadPri 0x8009B5C0`, then a state write and
  `osSendMesg` to `0x801352A0`) — **not** a separate thread. So the intro does not
  add threads and does not move the frame hook off the main thread.
* **The intro cannot be streaming audio from ROM**, because no audio code calls
  `osPiStartDma` at all (§2, §3).
* Overlay loads (`0x8007BB70` / `0x8007BBD8` → `loadSegment`) are synchronous on
  the calling thread; while one is running the main thread is inside
  `osRecvMesg(0x800D3CE8)` and therefore cannot be at your hook.

**Inferred / not proved:** I could not find a per-frame ROM streamer for the
intro anywhere in the resident image. The async loader path (request queue
`0x800D3BB0`, counter `0x800A6A80`) has **no producer in the resident image**, so
if the intro does bulk-load asynchronously it does so from overlay code I cannot
see. Two consequences:

1. I cannot prove the intro is free of in-flight PI DMA — hence keep the PI terms
   in the predicate.
2. I *can* say the audio-task race of §4 is present in the intro and in normal
   rooms alike, and it is the only mechanism I found that produces a **total**
   freeze (video + your own hook + audio) rather than a partial one.

**Answer to "can wait-for-quiescence ever succeed during the intro?" — yes.**
Nothing in this game holds a thread blocked on hardware indefinitely: audio tasks
are short and per-frame, loads are bounded, and the audio thread spends most of
its time *spinning* (not blocked). A retry-next-frame loop will succeed quickly.
The reason your current gate fails is not that quiescence is unreachable, it is
that `0x80207C80` measures only one of three independent task paths.

---

## 6. Ranked recommendation

### 6.1 Make the test atomic first

The frame hook runs on the **lowest-priority** thread (`0x8018AE70`, pri 10).
Every thread above it can preempt you between the test and the copy. So:

```c
u32 old = osSetIntMask(OS_IM_NONE);      /* 0x80094120 */
if (quiescent()) { /* snapshot or restore here */ }
osSetIntMask(old);                        /* if not, just retry next frame */
```

With interrupts masked, libultra cannot reschedule (it only switches on an
interrupt or on a blocking call), so the predicate cannot go stale under you.
Keep your existing "wait for `0x80207C80 == 0` with interrupts **enabled**" as the
first stage, then mask and re-test everything.

### 6.2 The predicate — in priority order of what actually bites you

```c
#define TAIL 0x800D0050u          /* &__osThreadTail */

static int quiescent(void)
{
    /* --- tier 1: RSP/RDP task state.  Fixes the freeze you are seeing. --- */
    if (*(volatile u32*)0x80207C80 != 0)        return 0; /* gfx tasks outstanding */
    if (*(volatile u32*)0x8021D8BC != 0)        return 0; /* normal RSP task loaded   */
    if (*(volatile u32*)0x8021D8C0 != 0)        return 0; /* hi-pri (audio) RSP task  */
    if (*(volatile u32*)0x8021D2FC != TAIL)     return 0; /* someone parked on SP-done*/
    if (*(volatile u32*)0x8021D334 != TAIL)     return 0; /* someone parked on DP-done*/
    if (*(volatile u32*)0x801AD4E8 != TAIL)     return 0; /* audio thread parked      */
    if (*(volatile u32*)0x8021D36C != TAIL)     return 0; /* thread 17 waiting on 18  */

    /* --- tier 2: PI.  One load proves the whole PI subsystem is idle. --- */
    if (*(volatile u32*)0x801974E0 != 0x801997A0) return 0; /* PI mgr parked on cmdQ */
    if (*(volatile u32*)0x801974E8 != 0)          return 0; /* cmdQ.validCount == 0  */
    if (*(volatile u32*)0x800D3BB0 != 0x800D3880) return 0; /* loader thread parked  */
    if (*(volatile u32*)0x800D3CC8 != TAIL)       return 0; /* loader retQ A idle    */
    if (*(volatile u32*)0x800D3CE8 != TAIL)       return 0; /* loader retQ B idle    */
    if (*(volatile u32*)0xA4600010 & 3)           return 0; /* PI_STATUS DMA/IO busy */

    /* --- tier 3: cheap extras --- */
    if (*(volatile u32*)0x800A6A80 != 0)          return 0; /* pending async loads   */
    if (*(volatile u32*)0x801856F8 != TAIL)       return 0; /* FlashRAM save I/O     */
    return 1;
}
```

That is ~16 loads and compares. If you want the minimum that fixes the reported
hang, **tier 1 alone** (7 terms, and really just `0x8021D8BC`, `0x8021D8C0` and
`0x801AD4E8` on top of what you have) is the high-value part.

Do **not** test the scheduler thread `0x8021D3A8` / queue `0x8021D2C4`: it is
permanently parked waiting for VI retrace, which keeps arriving after a restore,
so it is always safe.

### 6.3 If you ever need to restore without quiescence

Excluding the OS state from the snapshot is impractical — the thread structs and
queues are scattered (`0x800D3880`, `0x800D3BB0`–`0x800D3D04`, `0x800CF4F0`,
`0x8018ACC0`, `0x8018AE70`, `0x8018D100`, `0x801997A0`, `0x801974E0`,
`0x8019A950`, `0x801AD4E8`, `0x80195308`, `0x801C3840`, `0x8021D254`–`0x8021D8C4`,
plus ten thread stacks). The tractable alternative is a **post-restore fixup**:
after the RAM copy, before resuming, walk the six queues below and, for each one
whose `mtqueue` (+0x00) is not `0x800D0050`, `osSendMesg(q, dummy, OS_MESG_NOBLOCK)`
once per parked thread to release it:

`0x8021D2FC` (SP), `0x8021D334` (DP), `0x801AD4E8` (audio done), `0x8021D36C`
(hi-pri done), `0x800D3CC8` and `0x800D3CE8` (loader done).

Also force `*(u32*)0x8021D8BC = 0`, `*(u32*)0x8021D8C0 = 0` and
`*(u32*)0x80207C80 = 0`. That resynthesises exactly the events that were consumed
between snapshot and restore. It is racy by nature — prefer 6.2.
