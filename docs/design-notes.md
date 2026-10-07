# Design notes: why the practice patch is built the way it is

## 1. The constraints that decided almost everything

Four requirements, stated up front, did more design work than any amount of
cleverness afterwards:

1. **It has to run on an EverDrive X7 on real hardware.** Not "it works in
   BizHawk". Hardware punishes cache mistakes, undefined RDP state and save-chip
   assumptions that emulators forgive.
2. **It is a practice tool for speedrunning.** Timing fidelity matters more
   than features. Anything that changes how much work the game does per frame is
   suspect.
3. **A savestate must restore the room *as it was entered*,** including
   inventory. That rules out anything scripted and demands a real memory
   snapshot.

Requirement 2 is the main focus. It is why the build
drops the position readout, why the display-list budget is measured rather than
estimated, and why the game's own object watermark is left untouched.

---

## 2. The Expansion Pak 

** Require an Expansion Pak and live above `0x80400000`.** The game's heap
tops out at a hard-coded `0x803B5000` and it never reads `osMemSize` — verified,
not assumed — so with 8 MB fitted the top 4 MB is memory the game cannot
address.

What the expansion pack brings, beyond the obvious 4 MB:

* **The snapshot is trivially non-overlapping.** Source and destination can't
  collide, so the copy is a straight block move with no ordering concerns.
* **Patch variables survive a restore for free.** They live at `0x807D0000`,
  outside the snapshot region, so "room snapshots off" stays off across a
  reload — no special-casing, just address arithmetic.
* **A machine without the Pak is byte-identical to stock.** The bootstrap
  checks `osMemSize` and returns before installing anything. The only changes
  left in the ROM image are the entry stub, two dormant code blobs and the
  header — and the entry stub's first instruction is the call that bails out.

A goal was to have the game play as vanilla without an expansion pack

---

## 3. Static patch vs runtime poke

Four instructions in the game are changed. Only three of them live in the ROM
image; the rest are written into RAM by the bootstrap:

| where | why |
|---|---|
| entry stub (`0x80025C28`, `2C`, `34`) — in the ROM | it has to be in the ROM, it's the thing that starts us |
| frame hook (`0x80028088`, `8C`) — poked at runtime | must not exist without the Expansion Pak |
| room hook (`0x80073758`) — poked at runtime | same |

This split is deliberate. Anything poked at runtime is conditional on the
environment check having passed, which means the "no Pak" path can't be broken
by a hook pointing at code that was never loaded.

The difficulty unlock is the same idea taken further. The Easy/Hard flags could
have been poked once at boot. Instead they are written **every frame**:

```c
if (MMIO16(G_DIFF_EASY) != 1) MMIO16(G_DIFF_EASY) = 1;
if (MMIO16(G_DIFF_HARD) != 1) MMIO16(G_DIFF_HARD) = 1;
```

because the game clears that block on new game, overwrites it from FlashRAM on
load, and our own savestate restores overwrite it too. A once-at-boot poke
would have worked in every casual test and failed the first time someone loaded
a save. The value is `1` specifically because that's what the game's own ending
code writes there — copying the game's convention rather than inventing one
avoids a whole class of "works but weird" bugs.

The comparison before the write is not micro-optimisation for its own sake: it
keeps the store off the bus on the ~99.99% of frames where nothing changed.

---

## 4. The frame hook: why *that* instruction

Requirements for the hook site: runs once per frame, on every screen, late
enough that drawing lands on top, and on a thread where the CPU state is sane.

The tail of the frame flush (`0x800276EC`, hooked at `0x80028088`) satisfies all
four, and the evidence for "every screen" is structural: there is exactly one
path that submits graphics to the scheduler. Title, file menu, cutscenes,
gameplay and results all go through it.

The two displaced instructions were chosen because their entire effect is one
value in one register:

```
lui a1,0x801b ; lw a1,0x11C0(a1)      ->     a1 = *(0x801B11C0)
```

The shim rebuilds `a1` from the same address on the way out. **Prefer hook
sites whose displaced instructions have a small, checkable contract.** A site
whose instructions had side effects on memory, or set up several registers,
would have been a worse choice even if it ran at a more convenient time.

One subtlety that caused a real bug: after a *restore*, `a1` must be rebuilt
from the live cursor, not taken from the restored register file. The snapshot's
`a1` was correct for the frame it was captured in; the display list the game is
building now is a different one. Restoring the stale value corrupted the list.
The general form of this: **a savestate restores the game's state, but your hook
still has to hand back values that are correct for the *current* moment.**

---

## 5. Drawing: fill rectangles, and the preamble

**Why not a textured font?** Because it would have to load TMEM, set the colour
combiner, and put back exactly what the game had. Every one of those is state
the game owns at an arbitrary point in its frame. Fill rectangles need a colour
and coordinates and nothing else.

**Why generate the glyph rectangles at build time?** The naive way to draw a
3×5 bitmap font with rectangles is one rectangle per horizontal run: `0` costs
8. Allowing vertical rectangles too, the minimum cover for `0` is 4. Across the
font that's 34% fewer rectangles, which is 34% fewer display-list commands on
every frame forever. Computing the optimum is an exhaustive set cover over at
most 90 candidate rectangles — instant at build time, impossible to justify at
runtime. `tools/genfont.py` writes `src/font_rects.h`; the payload just walks a
table.

**The preamble is not boilerplate.** The game guarantees only three commands at
this point in the frame: a branch to its own header list, `G_RDPFULLSYNC` and
`G_ENDDL`. It does *not* guarantee the RDP's colour image is set, and
`othermode_L` may still have Z compare enabled from the last object drawn.
Entering fill mode with Z compare on hangs a real RDP — the console froze with a
black screen while emulators rendered the HUD happily. So the patch emits an
explicit render mode (copied verbatim from the game's own header list, so it is
a state the game is known to be happy with), an explicit `G_SETCIMG`, fill
cycle and scissor, and puts the pipeline back to 1-cycle afterwards.

### Both framebuffers, on purpose

Three attempts:

1. **The game's own framebuffer pointer (`0x80228B20`).** Written only on a
   conditional `G_SETCIMG` path. It pinned the HUD to one buffer while the game
   alternated: on hardware, a HUD flickering at half the frame rate.
2. **`VI_ORIGIN`, draw into "the other" buffer.** Correct reasoning, wrong in
   two ways. libultra points `VI_ORIGIN` one scanline into the buffer
   (base + 640), so the exact comparison never matched — and the fallback (draw
   into both) is what silently ran for weeks, including through all the console
   testing. Even with the comparison fixed, the video swap happens when the
   *previous* frame finishes, which isn't in step with this hook, so "the other
   buffer" is a guess that can be wrong on exactly the frames where it matters.
3. **Draw into both, always.** Costs a second copy of a small list; cannot
   flicker; cannot tear. This is what actually ran for weeks, because both
   clever paths failed their comparisons and fell through to it.

4. **The real pointer, `0x80218B20`.** The game reads it for its own
   `G_SETCIMG` at `0x80027AA0`; the scheduler submit reads it at `0x8009296C`
   and commits it to `OSScTask+0x0C`; only then, at `0x80092A40`, does it
   advance to the other buffer. The hook runs before all of that, so the value
   names this frame's target. Crucially it advances **per submitted task**, not
   per frame, so a skipped frame skips the hook and the advance together and a
   stale value cannot be observed. (3) stays as the fallback if the pointer is
   neither known buffer. I'm staying with this for V1, but I might try and figure
   out a way to have the clock update on lag frames.

The bug hid for so long because the fallback was *correct*, just twice as
expensive — and it only became visible when a feature (the mark) was
deliberately disabled on that path. **Dead paths that silently do the right
thing are the hardest kind to notice.** Two lessons: sign-extend offsets when
reading a disassembly, and prefer the variable the game itself uses for a
decision over one derived from hardware state.

### Budget, measured

The game leaves ~2 KB of slack past its object watermark. The worst-case
display list — every digit at its most expensive glyph, every indicator lit,
both buffers — is computed from the generated font table:

| build | worst case | headroom |
|---|---|---|
| main (clock only) | 720 bytes (1392 on the fallback path) | 2048 |
| full (+ position, + input display) | 1336 bytes (2624 on the fallback) | 3072 after lowering the watermark |

Worst case, not typical, because the failure mode of overrunning is memory
corruption, not a dropped glyph. This is also what decided that the full build
hides the position readout on the two mark screens: it was the cheapest way to
stay inside its budget without lowering the watermark further.

---

## 6. Combos: asymmetric timing beats a modifier key

The reload combo is a strict subset of the set-point combo, and later of the
toggle combo. Ways to resolve that:

**(a) Make them disjoint** (e.g. reload on Z+R+A, set on Z+D-up). Safe, but the
pairing stops being memorable, and the user asked for these specific combos.

**(b) Fire on release rather than press.** Removes the ambiguity, but adds
latency to the action that matters most and feels wrong for a practice tool.

**(c) Asymmetric settle.** The superset acts immediately; the subset must hold
steady for 4 frames (~130 ms) first; a latch then blocks everything until full
release. Chosen.

(c) works because the failure it prevents is *directional*. A human rolling onto
Z+R+A+C-down passes through Z+R+A for one or two frames, never for four.
Meanwhile the superset can act instantly, because no further button press can
turn it into something else. The latch closes the remaining hole — releasing
C-down while still holding the other three would otherwise look exactly like a
fresh reload.

The cost is 130 ms of latency on the reload. That is worth spending here, and
the blue indicator exists to spend it visibly.

---

## 7. The status light as a debugging instrument

The square left of the clock is not decoration. Its states map to states of the
machinery:

| colour | meaning | what it tells you |
|---|---|---|
| green | a snapshot exists | the reload will do something |
| blue | combo recognised, action not yet performed | your press registered |
| amber | snapshot just taken | the auto-capture fired on room entry |
| red | gave up after two seconds | plus tick marks naming which of the six quiescence terms was blocking |
| pink outline | room snapshots disabled | the toggle is off |

Blue was added for those moments where you don't know if it took the load combo.
It deliberately lights the moment the *pattern is recognised*, not when the
action is queued, because that gap includes the 4-frame settle as well as the
wait for a quiet frame.

The red-plus-ticks state is the one to copy into other projects. When an action
is dropped, the indicator doesn't just say "failed", it says *which term of the
predicate failed* — a 6-rectangle diagnostic that turns "it didn't work" into a
specific, actionable fact, at zero cost on frames where nothing fails. 

---

## 8. Quiescence: the hardest correctness problem here

Swapping 3.8 MB of RAM under a running console is not a memory operation, it is
a *synchronisation* operation. The memory image encodes which threads are
waiting for which events; the hardware encodes which events have already
happened. Restore one without the other and a thread waits for something that
already occurred.

This was learned the expensive way. The first version waited on the graphics
task counter only. It worked everywhere except the intro — where a restore
froze video and audio at the same instant, which is the signature of a thread
parked forever rather than a crash. The audio system submits its own RSP tasks
through a path that never touches the graphics counter.

The predicate grew to six terms covering graphics, audio (three separate ways),
the PI manager thread and the PI hardware itself. They are read with interrupts
masked so the six loads are one atomic observation of the system, not six
observations of six different moments.

Then a second, subtler error: the six terms were *sampled* once per frame. At
the tail of a frame the previous graphics task is normally still in flight, so
the sample essentially never passed and the combos silently stopped working
entirely. The correction:

* **wait**, with interrupts *enabled*, so the threads that would clear the
  condition can actually run — a busy-wait with interrupts off is a deadlock
  with extra steps;
* **bound the wait** by the CP0 cycle counter (~20 ms), because an unbounded
  wait in a hook is an unbounded hang;
* **re-check atomically** with interrupts masked before committing, because the
  wait ended in a world where anything could have changed;
* **retry on later frames** for up to two seconds, then drop the action and
  show red.

Three principles fall out of this:

1. **A test is not a wait, and a wait is not a test.** Decide which one the
   situation needs; here it needed both, in that order.
2. **Every wait gets a bound, every bound gets a visible outcome.** A dropped
   reset costs a practice attempt. A hang costs the session and the user's
   confidence in the tool.
3. **The commit point must be unambiguous.** Once the patch decides to copy, the
   action is cleared from the queue and cannot be retried, so a slow copy can
   never be started twice.

---

## 9. Room detection: record intent, act somewhere safe

The room-entry snapshot could have been driven by watching the area/room ID
change from the frame hook. That is polling, and it is racy: the mode byte
passes through its intermediate values within a single frame, so a per-frame
observer can miss a transition entirely.

Instead the patch hooks the last call of the room-init routine, which runs
exactly once per load on all four paths into a room. But the hook does not take
the snapshot. It sets a flag, and the next frame hook takes it.

Why: the room-init hook runs deep inside the game's call stack, on the game's
stack, with the system in an arbitrary state. The frame hook already has a saved
register file, a private stack, and the quiescence machinery. **Detect where
detection is reliable; act where acting is safe.** The flag is the seam between
the two, and it costs three instructions.

The later "toggle room snapshots" feature slots into that seam for free: when
the toggle is off, the flag is still set and simply discarded. No change to the
hook, no second code path.

---

## 10. The watermark

The idea is to put something on screen on the title and the end screen so that
people know at a glance that something was performed on the practice patch
and not the vanilla game

The detection had to be *exact*, which is a different standard from "works when
I tried it". The evidence: enumerate every instruction in the entire 32 MB — the
resident segment and all 43 overlays — that writes the state variables, and show
that the combination cannot occur elsewhere. For the title that meant pairing
the title-loop entry pointer with the loop's own state byte, because that byte
is left stale at 4 during a whole game session. For the results screen it meant
event id + shot + sub-state, each of which is reset at event start.


## 11. What "performance fidelity" actually cost

Running list of decisions made for timing fidelity rather than features:

* clock-only default build, so the display list fits the existing slack and the
  object watermark instruction is untouched;
* the position readout and input display compiled *out* of the default, not
  merely hidden, so they cost nothing;
* difficulty flags written only when they differ;
* screen detection is byte loads and comparisons, no function calls into the
  game;
* the per-frame C code has no loops over game data, no division except the
  clock's, and no floating point at all;
* the snapshot copy happens only when asked, and only on a quiet frame, so it
  never lands in the middle of a busy scene's frame budget.

What is *not* free, and is worth saying plainly: the frame hook itself saves and
restores 32 GPRs and 32 FP registers every frame, and the HUD appends ~130
display-list commands. On a 30 Hz game with a 33 ms frame that is far below the
noise floor, but it is not zero. The honest claim is "no measurable difference
in practice", not "identical".

---

## 12. Build-system decisions

The build script is not incidental to the hack; several bugs were *prevented*
by things it does:

* **Asserts the base ROM's MD5.** Every downstream address is meaningless
  against a different dump.
* **Asserts the original word at every patch site.** If an address is wrong, the
  build fails instead of producing a ROM that corrupts something subtle.
* **Asserts free space is actually free** (zeros in the boot hole, `0xFF` in the
  ROM tail) before writing into it.
* **Self-tests the checksum routine** against the stock header on every run. A
  checksum function that is wrong in the same way twice would otherwise be
  invisible.
* **`-Wa,--fatal-warnings`.** The assembler warns when a pseudo-instruction
  expands inside a delay slot. That bug bit twice and is invisible in testing —
  it makes the *second* half of an address silently vanish. Turning the warning
  into an error is the single highest-value line in `CFLAGS`.
* **Refuses undefined symbols in the payload.** A link error is better than a
  jump to zero.
* **Stage flags (`--stage 0..4`).** Five ROMs that add one layer each, so a
  console failure is localised in one test session instead of a week of
  guessing. Stage 0 — checksum change only — is the control that proved the
  EverDrive problem was the save database and not the injected code.

The ED64 header override belongs in the same family: the flashcart's behaviour
is part of the system under test, so the build takes responsibility for it
rather than leaving it to a README instruction.

---

## 13. The optimization passes

Later work, after the feature set was settled, all of it aimed at "runs like the
original ROM".

### Pass one

**Draw into one framebuffer.** Covered in §5: finding `0x80218B20` halved the
display list, the RSP's per-command work and the RDP's fill work in one change.

**Lazy register-file capture.** The shim used to save all 32 GPRs, `HI`/`LO`,
Status, `FCR31` and all 32 FP registers on every frame, because a snapshot
*might* be taken. It almost never is. Now the shim saves four words on an
ordinary frame and the full file only when the C side armed it — because an
action is queued, a room load asked for one, or the reload combo's base pattern
is being held.

That rests on three facts about the call site, each checked rather than assumed:

* `$at`, `$v0`-`$v1`, `$a0`-`$a3` and `$t0`-`$t9` are dead at `0x80028090`;
* `$s0`-`$s7`, `$gp`, `$fp` are preserved by the C compiler itself;
* `HI`/`LO` and the FP registers are not live across the hook — there is no
  `mult`, `div` or coprocessor-1 instruction between the hook and the next call
  at `0x800280FC`, which is a compiler-clobber point anyway.

The FP fact is the fragile one: a future change to the C could make the compiler
emit a floating-point instruction and silently corrupt the game's FP state. So
the build disassembles `main.c.o` and fails if it contains *any* coprocessor-1
opcode. **When correctness depends on the compiler not doing something, have the
build check that it didn't.**

A capture requested on an unarmed frame stays queued and commits on the next
one — at most a frame, and in practice nothing, because the wait for a quiet
moment is usually longer.

**Smaller things in the same pass:** the display-list cursor is read from
`0x801B11C0` once and written back once instead of being re-read and re-written
through a volatile pointer for each of ~100 commands; the emitters are `inline`
and `main.c` is built `-O2` instead of `-Os`; a write-only variable was deleted.

**Measured**, by instrumenting the shim with the CP0 cycle counter and running
the same scene in the same emulator with both builds:

| | CP0 Count ticks per frame, hook entry to exit |
|---|---|
| before | 10,102 |
| after | 4,662 |

A 54% cut in CPU time inside the hook — roughly 215 µs to 99 µs of a 33 ms
frame on the emulator's timing model. The RSP and RDP savings from halving the
display list are on top of that and were not measured.

Two things worth keeping from how that was done: instrument the *shipped*
structure rather than a microbenchmark (the counter went in the shim, around the
real hook), and measure both builds in the same scene on the same emulator
rather than trusting absolute numbers from either.

### Pass two

**Padding.** The black backing box shrank from 4/3 pixels of padding to 1 —
about 30% fewer pixels filled, and nothing lost, because the box only exists to
keep the digits legible over a bright scene.

**Box-and-cut-out glyphs.** Described in §5. About a quarter off the clock's
rectangle count, at the price of drawing more pixels, which is the cheaper side
of that trade here.

**64-bit `ld`/`sd` in the snapshot copy.** Half the memory instructions per 32
bytes. The interesting part was the failure mode: under `-mabi=32` the assembler
expands `ld` into two `lw`s into *consecutive registers*, so the copy silently
kept working at the old speed while clobbering the second register of each pair.
`.set gp=64` produces the real opcodes, and the build now disassembles
`blockcopy` and fails unless at least four `ld` and four `sd` came out.
**When an assembler is allowed to rewrite your instruction, assert on the
bytes it actually produced.**
