# Detecting the Title screen and the End/Results screen (RAM-only tests)

All addresses are KSEG0 RAM. "boot" addresses are in `build/boot.asm`; overlay code is
given as RAM (overlay slot 1, base `0x8022D2F0`) plus its raw ROM offset. Every variable
used below is in boot **bss** (`0x800D37A0-0x80225B70`): it exists from reset, is never
inside an overlay, and is inside `0x80000400-0x803B5000`.

---

## 0. TL;DR

```c
#define GAME_LOOP_ENTRY  (*(volatile u32*)0x801C3BC0) /* task slot 0 entry fn */
#define TITLE_STATE      (*(volatile s8 *)0x801AD518) /* title-loop state     */
#define GAME_MODE        (*(volatile s8 *)0x801C44B8) /* main-loop mode       */
#define EV_ACTIVE        (*(volatile u8 *)0x801C4680) /* event running        */
#define EV_ID            (*(volatile u8 *)0x801C4681) /* event id             */
#define EV_SUB           (*(volatile u8 *)0x801C4685) /* event director sub   */
#define EV_SHOT          (*(volatile u8 *)0x801C4688) /* current script shot  */

/* (A) "PRESS START" title screen only */
int on_title   = GAME_LOOP_ENTRY == 0x8005D200 && TITLE_STATE == 2;
/* (A') title + main menu + file (load) screen + menu message screen */
int on_menus   = GAME_LOOP_ENTRY == 0x8005D200 &&
                 (TITLE_STATE == 2 || TITLE_STATE == 4 ||
                  TITLE_STATE == 5 || TITLE_STATE == 6);
/* (B) final results screen that shows the clear time */
int on_results = EV_ACTIVE != 0 && EV_ID == 74 && EV_SHOT == 11 && EV_SUB >= 3
                 && GAME_MODE == 3;                  /* last term: optional guard */
```

`TITLE_STATE == 2` alone is already exact (see 1.4); the loop-entry term is a cheap guard.
For (B), `EV_SUB >= 3` starts on the exact frame the clear time is formatted and its
window opened; use `EV_SUB == 3 || EV_SUB == 4` to exclude the final fade-out.

---

## 1. (A) Title screen

### 1.1 The two top-level loops and which one is running: `*(u32*)0x801C3BC0`

The game runs its top-level loop as a restartable task in slot 0 of the task table at
`0x801C3A00` (slots are 0x1C8 bytes; slot k runs at OS priority 54-k). `+0x1C0` holds
the slot's entry function:

* `0x800290C0  sw a1,15296(at)`: set the entry (`0x80029078(slot,fn)`). Called once for
  slot 0 at `0x800328E8` with `fn = 0x80037B20` (the boot sequencer).
* `0x80029320  sw s1,448(s0)`: `0x80029298(fn)` = "replace the current task's entry
  with fn and restart". The slot is chosen from the caller's priority. All callers run in
  slot 0.
* The task manager reads it at `0x80029010` (`lw a2,448(s1)`), then calls `osCreateThread`
  and `osStartThread`.
* ROM-wide scan: no other code, overlays included, writes `0x801C3BC0`.

Values: `0x80037B20` boot, **`0x8005D200` title/menu loop**, **`0x8003E168` main game
loop** (gameplay, cutscenes, attract demo, ending). Switches:

| Site | New entry | Meaning |
|---|---|---|
| `0x80037CFC` (in `0x80037C50`) | `0x8005D200` | boot -> title (after `bzero(0x801AD518,16)` @ `0x80037C80`) |
| `0x8003DFD0` (in `0x8003DE6C`) | `0x8005D200` | soft reset / game over / quit -> title |
| `0x8005D4C8` (title state 0) | `0x8003E168` | start the attract demo or intro |
| `0x8005DB8C` (title state 4, sub 5) | `0x8003E168` | New Game |
| `0x8005DDF8` (title state 5) | `0x8003E168` | Continue from the file screen |
| A#1 `0x8022D780` (ROM `0x147150`), A#7 `0x8022D9EC` (ROM `0x18222C`) | `0x8005D200` | demo/intro ends -> title |
| A#21 `0x8022DA0C` (ROM `0x22DCBC`) | `0x8005D200` | after the results screen -> title |

### 1.2 Title-loop state: `*(s8*)0x801AD518` (with sub-state at `0x801AD519`)

Title loop `0x8005D200` sets `s0 = 0x801AD518` (`0x8005D25C/60`). Each frame it runs
`lb v0,0(s0)` @ `0x8005D3D8` and dispatches through the table at `0x800AE7EC`:

| state | handler | screen |
|---|---|---|
| 0 | `0x8005D484` | 1 frame: picks area 1 or 7, sets demo flag `0x801C453A=0x80`, starts the main loop (attract demo/intro) |
| 1 | `0x8005D4DC` | load title assets (a couple of frames), then state := 2 |
| **2** | `0x8005D5A0` | **"PRESS START" title with logo** (subs 1..4: fade-in, 300-frame idle, fade-out) |
| 3 | `0x8005D764` | 1 frame: state := 0 (idle timeout -> demo) |
| 4 | `0x8005D770` | main menu drawn over the logo (cursor `0x801AD524`: 0 New Game, 1 Continue, 2 -> state 6, 3 difficulty `0x801C4513`, shown only if `0x801C44D0 != 0`) |
| 5 | `0x8005DBA8` | file (save-slot) load screen, `0x8005F12C` + FlashRAM |
| 6 | `0x8005DE64` | message window from the menu (text `*(0x800B2904)`), exits on button to state 4 |

**All writers of `0x801AD518`** (boot: a `lui`/lo scan that handles jal delay slots,
plus every pointer use; overlays: a raw ROM scan of all 43 overlays):

| Address | Value | Context |
|---|---|---|
| `0x80037C80` | 0 (bzero) | boot, before the title loop starts |
| `0x8003DF10` | `sh 0x0100` -> state 1, sub 0 | soft reset / game over (`0x8003DE6C`) |
| `0x8005D428` | **4** | state 2 + (`*(u16*)0x801E0896 & 0x6008`) = Start pressed -> **leave title** |
| `0x8005D584` | **2** (sub := 1 @ `0x8005D58C`) | state 1 done -> **enter title** |
| `0x8005D764` | 0 | state 3 |
| `0x8005DA9C` / `0x8005DABC` | 5 / 6 | menu -> file screen / message |
| `0x8005DB48` | **2** (sub := 1 @ `0x8005DB54`) | menu "back" (`& 0x8000`) -> **enter title** |
| `0x8005DE44`, `0x8005DF78` | 4 | file screen / message -> menu |
| `0x8005DDFC` | 0 | file screen -> starts the main loop |
| `0x8005E00C` | state+1 | only reached from state 2 sub 4 (`0x8005D74C`): 2 -> 3, **leave title** (idle timeout) |
| A#1 `0x8022D784` (ROM `0x147154`), A#7 `0x8022D9F0` (ROM `0x182230`) | state+1, sub 0 | demo/intro ended, switching to the title loop |
| A#21 `0x8022DA04` (ROM `0x22DCB4`) | `sw zero` (state 0) | results ended, switching to the title loop |

The other references only read or pass `&T`: `0x8003719C`, `0x800625D0`, `0x8005F12C`
(which touches `+6/+8/+10` only), and `0x8005E018`.

### 1.3 Why `TITLE_STATE == 2` cannot be true outside the title screen

* The value 2 is written only at `0x8005D584` and `0x8005DB48`, and both run inside the
  title loop.
* State 2 is left only via `0x8005D428` (-> 4) or `0x8005E00C` (-> 3). Neither starts the
  main loop.
* The main loop is only ever started with T.state = 0 (`0x8005D4C8`, `0x8005DDFC`) or 4
  (New Game, `0x8005DB8C`: T is left at 4). During gameplay, cutscenes, the ending, pause
  menus and demos, T.state is therefore 0 or 4, or 1 or 5 for the single frame after the
  A#1/A#7 increment, when the loop has already been switched.
* No main-loop code writes 2. The overlay writers only write "+1" from 0 or 4, or 0.

So on its own, **`*(s8*)0x801AD518 == 2` is true exactly on the press-start title**,
including its fade-in, its 10 s idle and its idle fade-out. It is false on the menu (4),
the file screen (5) and the message screen (6). Adding `GAME_LOOP_ENTRY == 0x8005D200`
costs one load and protects against a future overlay write.

**Stale value caveat:** after the title is left through New Game, T.state stays 4 during
gameplay. That is why the menu-inclusive variant (A') **must** include the loop-entry
term: `T.state == 4` alone would also be true in-game.

---

## 2. (B) End / Results screen

### 2.1 How the ending is built

Cutscenes are "events", driven by a director struct **E = `0x801C4680`**:

| Field | Addr | Meaning |
|---|---|---|
| +0 | `0x801C4680` u8 | event active |
| +1 | `0x801C4681` u8 | event id (255 = none) |
| +4 | `0x801C4684` u8 | director state |
| +5 | `0x801C4685` u8 | director sub-state (used by the results shot) |
| +8 | `0x801C4688` u8 | current script shot id (entry[0]) |
| +0xC | `0x801C468C` u32 | director frame counter |
| +0x10 | `0x801C4690` u32 | frames into the current shot |
| +0xA4 | `0x801C4724` u32 | pointer to the current script entry `{u8 id,u8,u16 dur; fn}` |

* Dispatcher `0x800600F4`, called only from the mode-3 handler at `0x8003C1CC`: if
  `E.active`, it calls `table_0x800B004C[E.id](E)` at `0x80060168`.
* Start event: `0x800604F0(id)` sets active=1 (`0x80060510`), id (`0x80060528`), clears
  +4..+7 (`0x80060558`, so **sub := 0**), clears +8 (`0x80060520`) and the pointer at
  +0xA4 (`0x80060570`).
* Script start: `0x8005FFFC` (writes +0xA4 and +8 at `0x80060008`/`0x80060014`).
* Script advance: `0x8005FE78` moves to the next entry when `frames == dur`. An entry
  with `dur == 0xFFFF` never advances.

Ending chain (warp scan of every write to the warp-request area byte `0x8020C89E`):

1. **Area 27** (overlay A#27), event 73, director `0x8022E978`. At `0x8022EA40` it calls
   `0x8022E7D0`, which copies the play clock into the clear time (`0x8022E85C sw
   v1,0x44cc(at)`, ROM `0x29D81C`) and sets Hard (`0x8022E880`). It then warps to area 30
   (`0x8022EAF4`, ROM `0x29DAB4`).
2. **Area 30** (A#30), event 76, started at ROM `0x2C10C4`, director `0x8022D9DC`. At the
   end it warps to area 21 (ROM `0x2C1738`). A#30 is the only writer of "area 21" outside
   A#21 itself.
3. **Area 21** (A#21, ROM `0x22D5A0-0x230400`), **event 74**:
   * The room-0 tick `0x8022D518` starts it once: `jal 0x800604F0 ; li a0,74` at
     `0x8022D534` (ROM `0x22D7E4`). This is the only `li a0,74` + `jal 0x800604F0` in the
     ROM.
   * `table_0x800B004C[74]` = word at `0x800B0174` = `0x8022D850` (the A#21 director).
   * The director starts script `0x8022F9B8` at `0x8022D930`. The script has shots 1..10
     (fns `0x8022DA2C..0x8022DDA8`, 3027 frames in total), then **shot 11 = `0x8022D6B0`
     with dur `0xFFFF`** (entry at `0x8022FA08` = `0B00FFFF 8022D6B0`).

### 2.2 Shot 11 = the results screen (`0x8022D6B0`, sub-state = `E+5`)

| sub | code | what happens |
|---|---|---|
| 0 | `0x8022D6E4` | wait for the fade, switch scene objects, music 86, fade in, counter = 0 |
| 1 | `0x8022D738` | 300 frames of the preceding picture, then fade out (`0x80036df8(10)`) |
| 2 -> **3** | `0x8022D750` | swap to the results picture, fade in, **`jal 0x80076854` @ `0x8022D79C`** (formats **`0x801C44CC`, the clear time**, into glyphs at `0x800B3128+`), **sub := 3 @ `0x8022D7A0`** (ROM `0x22DA50`), opens message window 0 (`0x80061370`) @ `0x8022D7B0` |
| **3** | `0x8022D7C0` | time shown, 300 frames, then sub := 4 (`0x8022D810`) |
| **4** | `0x8022D7D8` | time shown until 1500 frames or a button (`0x801E0896 & 8`); fade out (`0x80036df8(11)`), sub := 5 |
| **5** | `0x8022D814` | fade-out; when done, `E+4++` (4 -> 5) |

When the director sees state 5 on that same frame, it runs `E.active := 0` at `0x8022D9C8`
(ROM `0x22DC78`), sets T.state := 0 at `0x8022DA04`, and calls `0x80029298(0x8005D200)`.
This returns to the title loop, which starts the intro/attract demo (state 0).

`0x80076854` (the clear-time formatter) has exactly **one caller in the whole ROM**: ROM
`0x22DA4C`, the instruction above. That identifies this sub-state range as the screen that
shows the final time.

### 2.3 Why the test cannot be true elsewhere

* `EV_SUB` is written only by `0x80060558` (0 at every event start) and by `0x8022D7A0` and
  `0x8022D810` in shot 11 of event 74. In A#21, no other shot or director writes `+5`: its
  directors use `+4`, and the other shots write actor structs.
* `EV_SHOT == 11` together with `EV_ID == 74` identifies the last entry of the A#21 script.
  An event id selects its handler from a global table, so event 74 can only run while A#21
  is loaded. It is only ever started by A#21's room-0 tick.
* `EV_ACTIVE` is cleared by the results exit (`0x8022D9C8`) and, unconditionally, by the
  title-loop init `0x80048BC0` (`0x80048C08 sb zero,0x4680`, called at `0x8005D24C`). It is
  also cleared on every area load (`0x800600BC`, via `0x80060094` at `0x80073434`).
  **Staleness is therefore impossible**: returning to the title by any path (results end,
  soft-reset combo, game over) clears it before the title is drawn.
* Gameplay, other cutscenes, the file menu and the title all have either `EV_ACTIVE == 0` or
  a different `EV_ID`. The event save/restore helpers (`0x80034F7C`, `0x80034DB4`,
  `0x80034DEC`) set id = 255, or restore a saved id, and only run when no event is active
  (`0x800631C4` check).
* `GAME_MODE == 3` is implied, because the dispatcher only runs from the mode-3 handler. It
  is an optional guard, in case some other mode ever froze the event and put up its own
  screen.

**What the test covers:** it is true from the frame the clear-time window opens until the
frame the game returns to the title, about 10 s minimum and up to 60 s or until a button is
pressed, plus the fade-out. It excludes:
* the credits and epilogue shots 1-10 of event 74;
* event 76 (area 30);
* event 73 (area 27, where the time is recorded);
* the ~10 s picture in shot-11 subs 0-1 that precedes the results picture.

For "the whole post-credits state" instead, use `EV_ACTIVE && EV_ID == 74 && EV_SHOT == 11`.

---

## 3. Caveats

* All values are live from reset. None of them depends on an overlay being loaded, but the
  (B) values can only become true while A#21 is loaded.
* `0x801C44B8` (game mode) is **stale** while the title loop runs, so do not use it to detect
  the title. `0x801AD518` is stale (0 or 4) while the main loop runs, which is why (A')
  needs the `0x801C3BC0` term.
* `0x801C3BC0` changes at the moment `0x80029298` is called. The actual thread restart
  follows before the next frame of the new loop, so the one-frame boundary is harmless.
* The (B) numbers (event 74, shot 11, sub >= 3) come from static analysis. Visual
  confirmation on an emulator is recommended:
  * put a write breakpoint on `0x801C4685` and check that the time appears when it becomes 3;
  * put an execute breakpoint on `0x80076854`;
  * for (A), put a write breakpoint on `0x801AD518`.
