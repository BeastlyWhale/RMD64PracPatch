# RMD64 Practice Patch

RockMan DASH: Hagane no Boukenshin N64 patch designed for speedrun practice on N64 console via flashcart

Requires N64 Expansion Pak.

Tested on Everdrive X7, ED64, and SummerCart 64

## Table of Contents

- [Ai Disclosure](#ai-disclosure)
- [Features](#features)
  - [Difficulties Unlocked](#difficulties-unlocked)
  - [IGT Display](#igt-display)
  - [Savestates](#savestates)
  - [Status Box](#status-box)
- [Applying the Patch](#applying-the-patch)
- [Building](#building)
- [Projects Referenced](#projects-referenced)

## AI DISCLOSURE

Qwen3.8-27B-Q4_K_M model was ran locally on a 3090 for planning, reverse engineering, and coding

## Features

### Difficulties Unlocked

Easy and hard mode selectable by default without needing the usual requirements

### IGT Display

In-game time display visible during gameplay. Resets back to 0 when a new game is started

### Savestates

- Load Savestate: L/Z + R + A
- Save Savestate: L/Z + R + A + C-Down
- Disable/Enable auto-savestate on room entry (on by default): L/Z + R + A + B

### Status Box

- Black/Blank: No savestate has been saved to the expansion pak
- Green: A savestate is in the expansion pak and ready to be loaded
- Blue: Savestate combo recognized, hold for activation
- Orange: a savestate has just been taken
- Red: Savestate attempt took longer than 2 seconds for a quiet moment to execute and stopped, try again
- Pink outline around the box: automatic room-entry savestates are switched off

## Applying the Patch

Dump a z64 big endian RMD64 rom that matches the following

- MD5: `0c74b44680276ffe808cfa6045329819`
- SHA1: `e807ed78db0b3440f76b445bf989a943bc05e0ad`
- Size: 33,554,432 bytes

Apply the BPS file from the releases section via your preferred BPS patcher

Here is a link to an online patcher that you can access via a web browser;

<https://www.marcrobledo.com/RomPatcher.js/>

You can also use the `tools/bps.py` script in the respository and place it in a folder with your vanilla rom as well as the bps and run the following command;

```
python3 bps.py apply "Rockman Dash - Hagane no Boukenshin (Japan).z64" rmd64_practice.bps rmd64_practice.z64
```

You may need to change the rom and bps name in the command to get them to match what is in the folder with bps.py

## Building

I used the following in WSL

- gcc-mips-linux-gnu
- binutils-mips-linux-gnu
- Python 3.7+

Dump a z64 big endian RMD64 rom that matches the following

- MD5: `0c74b44680276ffe808cfa6045329819`
- SHA1: `e807ed78db0b3440f76b445bf989a943bc05e0ad`
- Size: 33,554,432 bytes

- name it baserom.z64 and put it in the project root folder
- use build.py to build rmd64_practice.z64
- use BPS creator of choice to make the patch, such as Flips

tools/bps.py can also be used to create the bps patch with the following command from the project root

```
python3 tools/bps.py create baserom.z64 build/rmd64_practice.z64 build/rmd64_practice.bps
```

`--disasm` flag for build.py — disassembles the megabyte IPL3 loads at boot
(ROM `0x1000`–`0x101000` → RAM `0x80025C00`) to `build/boot.asm`, which the tools in `tools/` read.

## Projects Referenced

This patch's hooks, addresses and structure were derived from the game's own disassembly — see `docs/` for the working.
But, I also did reference other projects during the process to see how others solved similar problems:

**Work on this game specifically**

* **[Megaman64-JP-Mod](https://github.com/Rainchus/Megaman64-JP-Mod)** by
  Rainchus — the reference for injecting custom code into this game's Japanese
  ROM, and where a lot of the early thinking on the approach came from.
* **[RockmanDash-Decomp](https://github.com/Rainchus/RockmanDash-Decomp)** by
  Rainchus — the decompilation effort on this exact ROM. Both this and
  Megaman64-JP-Mod were referenced to help understand the game's structure.

**Practice ROMs/Tools that shaped what this one does**

* **[sc64-savestates](https://github.com/john-steinmeyer/sc64-savestates)** by
  john-steinmeyer — savestates on real hardware from the flashcart side. This
  patch takes the opposite approach (resident snapshot in RDRAM, Expansion Pak
  required), but seeing it done convincingly on console is why I believed this
  was worth attempting.
* **[Usamune ROM](https://sites.google.com/view/supermario64/usamunepj/usamunerom)**
  by Usamune Projects — the reference for what belongs on a practice HUD. The
  on-screen timer, the input display, the savestate slots and the convention
  that a practice ROM marks itself visibly so you can tell at a glance it isn't
  the vanilla rom are from it's influence.
* **[gz](https://github.com/glankk/gz)** by glank — the N64 practice ROM that
  established what one of these should do. The combo-triggered savestate, and
  the idea that a practice patch belongs inside the game rather than beside it, etc
