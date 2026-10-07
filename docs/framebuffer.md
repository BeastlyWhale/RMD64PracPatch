# Which colour framebuffer is "this frame"? — evidence and the answer

Scope: what a hook installed at **0x80028088** (tail of the per-frame graphics flush
`0x800276EC`) can read to learn the colour framebuffer the task it is about to submit
will render into, so it can emit its own `G_SETCIMG`.

---

## 0. TL;DR

**Read `*(u32*)0x80218B20`.** It is a KSEG0 pointer (`0x803B5000` or `0x803DA800`).
Mask it yourself: `phys = virt & 0x1FFFFFFF`.

At the hook point it is *already* the value that will be copied into this frame's
`OSScTask.framebuffer` and later handed to `osViSwapBuffer`. It is advanced to the
*next* buffer inside `0x8009292C` — i.e. **after** the hook runs. There is no lag.

> ### The address you tried was off by 0x10000
> `0x80228B20` is **not** referenced by a single instruction in the resident segment
> (verified by an exhaustive `lui 0x8023` + `0x8b20`/`-29920` pair scan of `boot.asm`
> — zero hits). It is untouched BSS, which is exactly why it read as unreliable.
>
> The real symbol is **`0x80218B20`**. The disassembly prints
> `lui a0,0x8022` / `lw a0,-29920(a0)`; the offset is *negative*
> (`0x8b20` sign-extends to `-0x74E0`), so the effective address is
> `0x80220000 - 0x74E0 = 0x80218B20`, not `0x80228B20`.
> (Cross-check: the in-flight count you already identified, `0x80207C80`, comes from
> `lui v1,0x8020` / `lw v1,31872(v1)` — a *positive* offset, which is why that one
> came out right.)

---

## 1. The objects

| Address | Size | Meaning | Set by |
|---|---|---|---|
| `0x800A6978` | `u32[2]` | the framebuffer table: `{0x803B5000, 0x803DA800}` | ROM data (verified, see §1.1) |
| `0x80218CC0` | `u32*` | pointer to that table | `0x80092C28` |
| `0x800CF420` | `u32` | number of buffers = **2** | `0x80092C30` |
| `0x801B7544` | `u32` | index of the buffer currently in `0x80218B20` | `0x80092C38`, `0x80092A34`, `0x80092CB8` |
| **`0x80218B20`** | **`u32`** | **KSEG0 pointer to the buffer the *next submitted* gfx task renders into** | `0x80092C44`, `0x80092CC4` (init), **`0x80092A40`** (per frame) |
| `0x801B11C0` | `Gfx*` | display-list write cursor | many, incl. `0x800277A0` |
| `0x800D7280` | `u32[2]` | the two **display-list** buffers (not framebuffers) | `0x800275E0`, `0x80027624` |
| `0x8020D66C` | `u8` | **display-list** buffer index (not a framebuffer index) | `0x8002760C`, `0x80028118` |
| `0x800A695A` / `0x800A695C` | `u16` | screen width / height = `320` / `240` | ROM data |

### 1.1 Proof of the table contents

ROM data at VA `0x800A6970` (ROM `0x81D70`):

```
800A6970: 800A4430 800D1720 803B5000 803DA800
```

so `0x800A6978 = {0x803B5000, 0x803DA800}`. Consistent with everything else:

* `0x800A695A = 0x0140` (320), `0x800A695C = 0x00F0` (240) → RGBA16 buffer is
  `320*240*2 = 0x25800` bytes.
* `0x803DA800 - 0x803B5000 = 0x25800`. ✅
* The clear at `0x8002766C`–`0x8002767C` is
  `bzero(0x803B5000, 0x4B000)` and `0x4B000 = 2 * 0x25800`. ✅ (same clear repeated at
  `0x80028B30`–`0x80028B3C`.)

### 1.2 Registration

```
80025D30  addiu a0,a0,27000      ; a0 = 0x800A6978  (framebuffer table)
80025D34  jal   0x80092C10
80025D38  li    a1,2             ; a1 = 2 buffers
```
inside `0x80092C10`:
```
80092C28  sw a0,-29504(at)   ; 0x80218CC0 = 0x800A6978
80092C30  sw s1,-3040(at)    ; 0x800CF420 = 2
80092C38  sw zero,30020(at)  ; 0x801B7544 = 0
80092C3C  lw v0,0(a0)        ; v0 = table[0] = 0x803B5000
80092C44  sw v0,-29920(at)   ; 0x80218B20 = 0x803B5000
```
(The identical pair `0x80025C74` → `0x80092C10` in `0x80025C60` is the re-init path;
see §5.)

---

## 2. When `0x80218B20` is written, relative to the hook

### 2.1 Per-frame call chain

```
0x80026A30              per-frame function
  0x80026A90  bnez v0 -> 0x80026B40          ; if *(u8*)0x800A6958 != 0, frame is
                                             ;   skipped entirely (flush not called)
  ...
  0x80026B38  jal 0x800276EC                 ; THE FLUSH
      0x80027730  beqz a0 -> 0x80028120      ; early-out if arg >= 2: builds nothing,
                                             ;   submits nothing
      0x80027758  lbu 0x8020D66C             ; DL buffer index for this frame
      0x80027778  lw  0x800D7280[idx]        ; DL base -> 0x801B11C0 cursor
      ... per-view display-list construction ...
      0x80027AA0  lw a0,0x80218B20  <<<<<<   ; the game's own conditional G_SETCIMG
      0x80027AC0  jal 0x80099870             ;   osVirtualToPhysical(that)
      0x80027AC4  sw v0,0(s0)                ;   w0 = 0xFF100000 | (width-1)
      0x80027ADC  sw v0,4(s0)                ;   w1 = physical address
      ...
==>   0x80028088  YOUR HOOK                  ; 0x80218B20 still holds THIS frame's fb
      0x8002808C  lw a1,0x801B11C0           ; cursor reloaded here (see §6)
      0x800280AC  sw 0xE9000000 -> G_RDPFULLSYNC
      0x800280B4  sw 0xDF000000 -> G_ENDDL
      0x800280F0  subu a1,a1,v1              ; data_size = cursor - dl_base
      0x800280F8  sw a1,0x800D728C
      0x800280FC  jal 0x8009292C             ; SUBMIT (a3 = 0x40001)
      0x80028100  ori a3,a3,0x1              ;   delay slot -> bit0 set
      0x80028118  sb v0,0x8020D66C           ; toggle DL buffer index (NOT the fb)
```

Between `0x80028088` and `0x800280FC` there is **no branch and no call** — the submit
is unconditional once the hook runs.

### 2.2 Inside the submit, `0x8009292C`

```
80092934  lw  t0,0x80195300          ; t0 = the OSScTask being filled
80092950  sw  a0,64(t0)              ; +0x40 list.t.data_ptr  = DL base
80092954  sw  a1,68(t0)              ; +0x44 list.t.data_size
80092958  sw  v0,20(t0)              ; +0x14 list.t.flags = a3>>16 = 4
8009296C  lw  a0,-29920(a0)          ; a0 = *(u32*)0x80218B20      <<< READ
8009298C  sw  a0,12(t0)              ; +0x0C OSScTask.framebuffer  <<< COMMIT
800929A0  sw  v0,8(t0)               ; +0x08 OSScTask.flags = a3 & 0xFFFF = 1
800929D4  andi v0,a3,0x1
800929D8  beqz v0,0x80092A44          ; gfx tasks (bit0 = 1) fall through
800929E4  lw  v0,0x801B7544          ; idx
800929EC  lw  v1,0x800CF420          ; count = 2
800929F0  addiu v0,v0,1
800929F4  divu ...                   ; a1 = (idx+1) % 2
80092A0C  lw  a0,0x80218CC0          ; table
80092A2C  lw  v0,0(v0)               ; table[(idx+1)%2]
80092A34  sw  a1,0x801B7544          ; idx = new
80092A40  sw  v0,-29920(at)          ; 0x80218B20 = NEXT buffer   <<< ADVANCE
80092A6C  addiu v1,v1,1
80092A74  sw  v1,0x80207C80          ; in-flight gfx task count ++
80092A9C  jal 0x8009A3D0             ; osSendMesg(schedQ 0x8021D28C, task, BLOCK)
```

**The read at `0x8009296C` happens before the advance at `0x80092A40`.** So the value
visible at your hook (`0x80028088`) is *exactly* the value stored into
`OSScTask.framebuffer`. Not a frame behind, not a frame ahead.

The game itself relies on this: the conditional `G_SETCIMG` it builds at
`0x80027A8C`–`0x80027ADC` reads the *same* global, earlier in the same flush. Your
hook's `G_SETCIMG` will therefore always agree with the game's, when the game emits one.

### 2.3 That framebuffer is what actually gets displayed

Full chain, all in the resident segment:

```
80092298  scheduler thread: osRecvMesg(0x8021D28C) -> task
800922D8    jal 0x800923DC        ; wait until task->framebuffer (+0x0C, read at
80092404    lw s2,12(a0)          ;   0x80092404) is neither osViGetCurrentFramebuffer()
8009241C    jal 0x8009BF10        ;   (0x8009BF10) nor osViGetNextFramebuffer()
8009242C    jal 0x8009BF50        ;   (0x8009BF50)
80092354    jal 0x8009A5B0        ; osSpTaskLoad(&task->list)
80092360    jal 0x8009A7BC        ; osSpTaskStartGo
800923B4    lw  a0,80(a1)         ; task->msgQ (+0x50) = 0x80225AD0
800923B8    jal 0x8009A3D0        ; post finished task there

80092690  "task done" thread on 0x80225AD0
800926E0    lw v0,84(a0)          ; task->msg (+0x54)
800926E4    lh v0,0(v0)           ; 4 for gfx tasks (see below)
800926E8    beq v0,4 -> 0x80092700
80092704    lw v0,0x800CF42C      ; callback
80092710    jalr v0               ; a0 is still the task pointer (unclobbered)
80092774    sw v1,0x80207C80      ; in-flight count --

80092B50  the registered callback
80092B58    jal 0x8009C540        ; osViSwapBuffer(
80092B5C    lw  a0,12(a0)         ;   task->framebuffer )     <<<<<<
8009C540  osViSwapBuffer: __osViNext->framep = a0; state |= 0x10
```

Callback registration:
```
80025D44  lui   a0,0x8009
80025D48  addiu a0,a0,11088    ; a0 = 0x80092B50
80025D50  jal   0x80092B10
80092B2C  sw    s0,-3028(at)   ; 0x800CF42C = 0x80092B50
```

Message discrimination (`0x800927A4`):
```
800927B0  sh 8,0x80195304      ; non-gfx tag
800927C4  sh 4,0x80195306      ; gfx tag
80092A1C/80092A20  task->msg = 0x80195306   (a3 & 1 == 1, i.e. gfx)
80092A50/80092A54  task->msg = 0x80195304   (a3 & 1 == 0)
```
`0x800CF430` (the handler for tag 8) is never written anywhere in the resident segment,
so non-gfx tasks never swap. Only gfx tasks swap, and they swap to `+0x0C`.

**Conclusion: `OSScTask+0x0C` ← `*(u32*)0x80218B20` (at your hook) → `osViSwapBuffer`.
The value you read is the buffer that is rendered into *and* the buffer that is shown.**

---

## 3. Does it alternate every frame, or can it repeat?

It **advances once per submitted graphics task**, not once per frame:

* `0x80092A40` is the only per-frame writer, and it is gated on `a3 & 1` at
  `0x800929D4`. The flush passes `a3 = 0x40001` (`0x80028100`), so bit 0 is set and it
  always advances.
* If the frame is skipped, the advance is skipped too, because both live inside the
  same straight-line region:
  * `0x80026A90 bnez v0,0x80026B40` — `*(u8*)0x800A6958 != 0` skips the `jal 0x800276EC`
    entirely;
  * `0x80027730 beqz a0,0x80028120` — flush called with arg >= 2 returns without
    building or submitting.
  In both cases your hook does not run either, so you can never observe a stale value.
* The scheduler does not drop gfx tasks. `0x80092298` unconditionally loads and starts
  every task it receives; the only conditional is the *wait* in `0x800923DC`, which
  blocks (it does not discard).

Two callers submit with `a3 = 0x40000` (bit 0 clear): `0x80025CF8` (delay slot
`0x80025CFC sw zero,36(sp)`) and `0x80025DB4` (delay slot `0x80025DB8 sw zero,36(sp)`).
Those are the one-shot init display lists. They take the `0x80092A44` path, so they
**do not** advance `0x80218B20` and **do not** swap — but they do still copy
`0x80218B20` into their own task's `+0x0C`. Harmless, and they never coincide with your
hook.

**Practical answer:** from your hook's point of view the value strictly alternates
`0x803B5000` → `0x803DA800` → `0x803B5000` → … on every invocation of the hook, and the
only things that can break that alternation (`0x80092C10` re-init, §5) also reset it to a
known state. Reading the global means you never have to care.

---

## 4. Do **not** use `0x8020D66C`

`0x8020D66C` is the **display-list** double-buffer index for the table at `0x800D7280`
(`0x80027758`, `0x80027778`, `0x80028094`, `0x800280C0`, toggled at `0x80028118`). It is
*not* a framebuffer index, and it can fall out of phase with `0x801B7544`:

* `0x8020D66C` is zeroed only at `0x8002760C` (inside gfx init `0x80027550`).
* `0x801B7544` / `0x80218B20` are reset at `0x80092C38` / `0x80092C44`, reached from
  **`0x80028AFC` → `0x80028B44` → `0x80025C60` → `0x80025C78` → `0x80092C10`**, and
  `0x80028AFC` is called from the *main loop* at `0x80026B60` (video-mode change path,
  selected by `*(u32*)0x800A6964`). That path resets the framebuffer index without
  touching `0x8020D66C`.

So after any mode change the two indices desync. `0x80218B20` remains correct;
`0x8020D66C` does not track the framebuffer.

Similarly, `osViGetNextFramebuffer()` (`0x8009BF50`) is **not** usable: it reflects the
last *completed* task's swap, i.e. it lags by a frame (and by more when the RSP/RDP is
behind).

---

## 5. Ranked candidates

| # | Expression | Verdict |
|---|---|---|
| **1** | **`*(u32*)0x80218B20`** | **Use this.** Single load, no indirection, provably the value committed to `OSScTask+0x0C` at `0x8009298C`, advanced only afterwards at `0x80092A40`. |
| 2 | `((u32*)*(u32*)0x80218CC0)[*(u32*)0x801B7544]` | Identical value by construction (`0x80092A2C`/`0x80092A34`/`0x80092A40` write them together). Two extra loads, survives nothing that #1 doesn't. Useful only as a cross-check. |
| 3 | `((u32*)0x800A6978)[*(u32*)0x801B7544]` | Same as #2 with the table hard-coded. Breaks if `0x80092C10` is ever called with a different table; it never is in the resident segment. |
| 4 | `*(u32*)0x8020D66C` used as a framebuffer index | **Wrong.** See §4. |
| 5 | `*(u32*)0x80228B20` | **Does not exist.** Zero references in the resident segment. Off-by-0x10000 transcription of #1. |

**The single test to use:** read `*(u32*)0x80218B20` at `0x80028088`, mask with
`0x1FFFFFFF`.

---

## 6. Practical notes for the hook

### Virtual vs physical
`0x80218B20` holds a **KSEG0 virtual** pointer. `osVirtualToPhysical` (`0x80099870`) for
a KSEG0 argument is literally `return vaddr & 0x1FFFFFFF` (`0x800998B4`
`and v0,a0,v1` with `v1 = 0x1FFFFFFF`, reached because the
`vaddr - 0x80000000 <= 0x1FFFFFFF` test at `0x80099880`–`0x80099888` passes). So masking
in your hook is exactly equivalent — no need to call in.

### The G_SETCIMG the game emits (copy this encoding)
From `0x80027A8C`–`0x80027ADC`:
```
w0 = 0xFF100000 | ((*(u16*)0x800A695A - 1) & 0xFFF)   ; G_SETCIMG, fmt RGBA, siz 16b
w1 = osVirtualToPhysical(*(u32*)0x80218B20)
```
With width = 320 that is `w0 = 0xFF10013F`, `w1 = 0x003B5000` or `0x003DA800`.
Note this command is **conditional** in the game — gated on `lb v0,168(s1)` at
`0x80027A80` (a per-view flag at `+0xA8`). That is why you must emit your own: on views
where the flag is clear the RDP's colour image is whatever the previous frame's list
left it at.

### Appending
`0x801B11C0` is the `Gfx*` write cursor. Your hook at `0x80028088` runs *before*
`0x8002808C` reloads it, so: read `0x801B11C0`, write your commands, store the advanced
cursor back to `0x801B11C0`, return. The original code reloads it and continues.
The task's `data_size` is computed from the final cursor at `0x800280F0`
(`subu a1,a1,v1`, `v1 = 0x800D7280[idx]`), so your commands are included automatically.

Headroom: the DL buffer limit is `0x800D7288 = 0x800D7280[idx] + 0x1E000`
(`0x8002777C`–`0x80027794`); the flush's own overflow bookkeeping is at
`0x80027E90`–`0x80027EC4`.

### Cheap sanity guard
```c
u32 fb = *(volatile u32 *)0x80218B20;
if (fb != 0x803B5000 && fb != 0x803DA800)
    goto skip_hud;                 /* one dropped HUD frame, never a wrong write */
u32 phys = fb & 0x1FFFFFFF;
```
Skipping is preferable to guessing: a wrong `G_SETCIMG` writes over the buffer that is
currently *on screen*, which is far more visible than a missing HUD frame.

During bring-up, the "draw into both" fallback is also safe here (the two buffers are
`0x25800` apart and both are cleared together by `bzero(0x803B5000, 0x4B000)` at
`0x8002766C`), but it doubles the fill cost and needs a final `G_SETCIMG` back to the
real buffer plus a `G_RDPPIPESYNC` between the two passes — not worth it once the guard
above is in place.
