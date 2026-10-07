#!/usr/bin/env python3
"""Build the RMD64 speedrun practice ROM.

  baserom.z64  ->  build/rmd64_practice.z64  +  build/rmd64_practice.bps

Two passes: the payload is compiled and linked first so its length is known,
then the boot stub is assembled with that length baked in, then both blobs and
the entry-stub patch are written into a copy of the base ROM and the CIC-6102
checksum is recomputed.
"""
import os, struct, subprocess, sys, hashlib, argparse

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC, BUILD = os.path.join(ROOT, 'src'), os.path.join(ROOT, 'build')
CROSS = 'mips-linux-gnu-'

BASE_MD5 = '0c74b44680276ffe808cfa6045329819'

PAYLOAD_RAM = 0x807C0000
PAYLOAD_ROM = 0x01FE0800          # start of the 0xFF-filled tail of the ROM
BOOT_RAM    = 0x800A62C0
BOOT_ROM    = 0x000816C0          # 16-byte-aligned start of the zero hole
BOOT_MAX    = 0x42C

# The segment IPL3 loads, for --disasm. Not to be confused with BOOT_ROM/BOOT_RAM
# above, which point at the zero hole *inside* it where the boot stub goes.
SEG_ROM = 0x001000                # first byte IPL3 copies
SEG_END = 0x101000                # one past the last (1 MB)
SEG_RAM = 0x80025C00              # where it lands in KSEG0

# EverDrive-64 X-series developer override
MAGIC_VALUE  = 0x524D4450      # 'RMDP', must match src/rmd64.h

ED64_CART_ID = b'ED'
ED64_CONFIG  = 0x52          # FlashRAM + region free

CFLAGS = ('-Wa,--fatal-warnings -march=vr4300 -mtune=vr4300 -mabi=32 -mno-abicalls -fno-pic -G0 '
          '-mno-gpopt -fno-builtin -ffreestanding -nostdlib -fno-common '
          '-fomit-frame-pointer -Os -Wall -Wextra -Wno-unused-parameter '
          '-mno-check-zero-division').split()
# No floating point is used in C we generate, the shim still needs the FP
# opcodes available, so hard-float stays enabled for the assembler.


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(' '.join(cmd) + '\n' + r.stdout + r.stderr)
        sys.exit(1)
    return r.stdout


def check_sym(elf, name, want):
    for line in run([CROSS + 'nm', elf]).splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == name:
            got = int(parts[0], 16)
            if want is not None and got != want:
                sys.exit('%s is at 0x%08X, expected 0x%08X '
                         '(section alignment moved it)' % (name, got, want))
            return got
    sys.exit('symbol %s not found in %s' % (name, elf))


def ram_to_rom(ram):
    """Boot-segment address -> ROM offset (IPL3 maps 0x1000 -> 0x80025C00)."""
    return ram - 0x80025C00 + 0x1000


def jal(target):
    assert (target >> 28) == 0x8
    return 0x0C000000 | ((target >> 2) & 0x03FFFFFF)


# checksum
def cic6102_crc(rom):
    """N64 bootcode checksum for CIC-6102/7101 over ROM 0x1000..0x101000."""
    M = 0xFFFFFFFF
    seed = 0xF8CA4DDC
    t1 = t2 = t3 = t4 = t5 = t6 = seed

    def rol(v, n):
        n &= 31
        return ((v << n) | (v >> (32 - n))) & M

    for i in range(0x1000, 0x101000, 4):
        d = struct.unpack('>I', rom[i:i + 4])[0]
        r = rol(d, d & 0x1F)
        if ((t6 + d) & M) < t6:
            t4 = (t4 + 1) & M
        t6 = (t6 + d) & M
        t3 ^= d
        t5 = (t5 + r) & M
        if t2 > d:
            t2 ^= r
        else:
            t2 ^= t6 ^ d
        t1 = (t1 + (t5 ^ d)) & M          # the 0x710 table term is CIC-6105 only
    return (t6 ^ t4 ^ t3) & M, (t5 ^ t2 ^ t1) & M


# build
STAGES = {
    0: 'header/CRC change only, no injected code at all',
    1: 'entry stub + bootstrap + payload DMA, no hooks installed',
    2: 'frame hook installed: HUD, timer and difficulty unlock',
    3: '(identical to stage 2 - STAGE is only tested >=2 and >=4)',
    4: 'full: room hook and savestates',
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--stage', type=int, default=4, choices=sorted(STAGES))
    ap.add_argument('--ed64', type=int, default=1, choices=(0, 1),
                    help='write the EverDrive-64 save-type override header')
    ap.add_argument('--hud', type=int, default=6)
    ap.add_argument('--cap', type=int, default=0,
                    help='lower the display-list object watermark')
    ap.add_argument('--minimal', type=int, default=1, choices=(0, 1),
                    help='clock only (default). --minimal 0 --cap 1 adds the '
                         'position readout and input display')
    ap.add_argument('--out', default=None)
    ap.add_argument('--disasm', action='store_true',
                    help='disassemble the boot segment to build/boot.asm for '
                         'the analysis tools, then exit without building')
    args = ap.parse_args()
    stage, ed64 = args.stage, args.ed64

    os.makedirs(BUILD, exist_ok=True)
    base = open(os.path.join(ROOT, 'baserom.z64'), 'rb').read()
    md5 = hashlib.md5(base).hexdigest()
    if md5 != BASE_MD5:
        sys.exit('baserom.z64 md5 is %s, expected %s' % (md5, BASE_MD5))

    if args.disasm:
        seg = base[SEG_ROM:SEG_END]
        binp = os.path.join(BUILD, 'bootseg.bin')
        asmp = os.path.join(BUILD, 'boot.asm')
        open(binp, 'wb').write(seg)
        cmd = [CROSS + 'objdump', '-D', '-b', 'binary', '-m', 'mips:4300',
               '-EB', '--adjust-vma=0x%08X' % SEG_RAM, binp]
        # not run(): objdump emits 9 MB here, so it goes straight to the file
        with open(asmp, 'w') as f:
            r = subprocess.run(cmd, stdout=f, stderr=subprocess.PIPE, text=True)
        if r.returncode:
            sys.exit(' '.join(cmd) + '\n' + r.stderr)
        print('segment     ROM 0x%06X-0x%06X -> RAM 0x%08X, %d insns'
              % (SEG_ROM, SEG_END, SEG_RAM, len(seg) // 4))
        print('disassembly %s (%.1f MB)'
              % (asmp, os.path.getsize(asmp) / 1048576.0))
        return

    # font tables are generated, not hand-maintained
    run([sys.executable, os.path.join(ROOT, 'tools', 'genfont.py')])

    # pass 1: payload 
    stagedef = ['-DSTAGE=%d' % stage, '-DHUD_MODE=%d' % args.hud,
                '-DHUD_MINIMAL=%d' % args.minimal,
                '-DPOKE_CAP=%d' % args.cap]
    objs = []
    # main.c is the per-frame hot path: -O2 over -Os, so the display-list
    # emitters inline and the cursor stays in a register.
    for f, extra in (('shim.S', ['-x', 'assembler-with-cpp']), ('main.c', ['-O2'])):
        o = os.path.join(BUILD, f + '.o')
        run([CROSS + 'gcc'] + CFLAGS + extra + stagedef + ['-I', SRC, '-c',
             os.path.join(SRC, f), '-o', o])
        objs.append(o)

    # The frame hook only saves the floating-point register file on frames where a snapshot may be taken, so the C side must never touch one
    fp_ops = ('lwc1', 'swc1', 'ldc1', 'sdc1', 'mtc1', 'mfc1', 'ctc1', 'cfc1',
              'dmtc1', 'dmfc1', 'add.s', 'sub.s', 'mul.s', 'div.s', 'cvt.',
              'trunc.', 'mov.s', 'neg.s', 'abs.s', 'c.')
    bad = []
    for line in run([CROSS + 'objdump', '-d', objs[1]]).splitlines():
        parts = line.split('\t')
        if len(parts) >= 3:
            mn = parts[2].split()[0]
            if any(mn.startswith(o) for o in fp_ops):
                bad.append(line.strip())
    if bad:
        sys.exit('main.c uses floating point, which the frame hook does not '
                 'preserve:\n' + '\n'.join(bad[:10]))
    elf = os.path.join(BUILD, 'payload.elf')
    run([CROSS + 'ld', '-T', os.path.join(SRC, 'payload.ld'), '-o', elf,
         '--no-warn-rwx-segments'] + objs)
    undef = [l for l in run([CROSS + 'nm', '-u', elf]).splitlines() if l.strip()]
    if undef:
        sys.exit('payload has undefined symbols:\n' + '\n'.join(undef))
    check_sym(elf, 'payload_entry', PAYLOAD_RAM)

    # `.set gp=64` for 64 bit copy
    dis = run([CROSS + 'objdump', '-d', '--disassemble=blockcopy', elf])
    n_ld = sum(1 for l in dis.splitlines() if '\tld\t' in l)
    n_sd = sum(1 for l in dis.splitlines() if '\tsd\t' in l)
    if n_ld < 4 or n_sd < 4:
        sys.exit('blockcopy assembled with %d ld / %d sd - the 64-bit copy was '
                 'expanded into 32-bit pairs' % (n_ld, n_sd))
    check_sym(elf, 'hud_shim', None)
    pbin = os.path.join(BUILD, 'payload.bin')
    run([CROSS + 'objcopy', '-O', 'binary', elf, pbin])
    payload = bytearray(open(pbin, 'rb').read())
    payload += b'\0' * ((-len(payload)) % 4)
    magic_off = len(payload)
    payload += struct.pack('>II', MAGIC_VALUE, 0xA5A5A5A5)
    payload += b'\0' * ((-len(payload)) % 32)
    payload = bytes(payload)
    plen = len(payload)

    # pass 2: boot stub 
    defs = stagedef + ['-DPAYLOAD_ROM=0x%X' % PAYLOAD_ROM,
            '-DPAYLOAD_LEN=0x%X' % plen,
            '-DPAYLOAD_MAGIC_OFF=0x%X' % magic_off,
            '-DJAL_HUD=0x%08X' % jal(PAYLOAD_RAM),
            '-DJAL_ROOM=0x%08X' % jal(PAYLOAD_RAM + 8)]
    bo = os.path.join(BUILD, 'boot.o')
    run([CROSS + 'gcc'] + CFLAGS + ['-x', 'assembler-with-cpp'] + defs +
        ['-I', SRC, '-c', os.path.join(SRC, 'boot.S'), '-o', bo])
    belf = os.path.join(BUILD, 'boot.elf')
    run([CROSS + 'ld', '-T', os.path.join(SRC, 'boot.ld'), '-o', belf,
         '--no-warn-rwx-segments', bo])
    check_sym(belf, 'rmd_boot', BOOT_RAM)
    bbin = os.path.join(BUILD, 'boot.bin')
    run([CROSS + 'objcopy', '-O', 'binary', belf, bbin])
    boot = open(bbin, 'rb').read()
    if len(boot) > BOOT_MAX:
        sys.exit('boot stub is %d bytes, hole is %d' % (len(boot), BOOT_MAX))

    # assemble the ROM 
    rom = bytearray(base)

    def poke(off, word, expect=None):
        cur = struct.unpack('>I', rom[off:off + 4])[0]
        if expect is not None and cur != expect:
            sys.exit('ROM 0x%06X is %08X, expected %08X' % (off, cur, expect))
        rom[off:off + 4] = struct.pack('>I', word)

    def blank(off, length, fill):
        if any(b != fill for b in rom[off:off + length]):
            sys.exit('ROM 0x%06X..0x%06X is not free' % (off, off + length))

    if stage == 0:
        poke(ram_to_rom(0x80025C3C), 0x3400FFFF, 0x00000000)
    else:
        # entry stub: divert to the bootstrap
        poke(ram_to_rom(0x80025C28), jal(BOOT_RAM), 0x3C0A8009)  # lui t2,0x8009
        poke(ram_to_rom(0x80025C2C), 0x00000000, 0x254A9CA0)     # addiu t2,...
        poke(ram_to_rom(0x80025C34),
             0x08000000 | ((0x80089CA0 >> 2) & 0x03FFFFFF), 0x01400008)  # jr t2

        blank(BOOT_ROM, BOOT_MAX, 0x00)
        rom[BOOT_ROM:BOOT_ROM + len(boot)] = boot

        blank(PAYLOAD_ROM, plen, 0xFF)
        rom[PAYLOAD_ROM:PAYLOAD_ROM + plen] = payload

    # The internal ROM name left for detection
    if ed64:
        rom[0x3C:0x3E] = ED64_CART_ID
        rom[0x3F] = ED64_CONFIG

    crc1, crc2 = cic6102_crc(bytes(rom))
    rom[0x10:0x18] = struct.pack('>II', crc1, crc2)

    out = args.out or os.path.join(
        BUILD, 'rmd64_practice.z64' if (stage == 4 and ed64)
        else 'stage%d%s.z64' % (stage, '_ed' if ed64 else ''))
    open(out, 'wb').write(bytes(rom))

    # report 
    print('stage %d     %s' % (stage, STAGES[stage]))
    print('ed64 hdr    %s' % ('cart id "ED", byte 0x3F = 0x%02X' % ED64_CONFIG
                              if ed64 else 'not written'))
    if stage:
        print('boot stub   %4d bytes at ROM 0x%06X (RAM 0x%08X), %d free'
              % (len(boot), BOOT_ROM, BOOT_RAM, BOOT_MAX - len(boot)))
        print('payload     %4d bytes at ROM 0x%06X (RAM 0x%08X), magic +0x%X'
              % (plen, PAYLOAD_ROM, PAYLOAD_RAM, magic_off))
    print('new CRC     %08X %08X   (save_db line: 0x%08X=%02X)'
          % (crc1, crc2, crc1, ED64_CONFIG))
    print('output      %s' % out)
    print('md5         %s' % hashlib.md5(bytes(rom)).hexdigest())

    # sanity: the original checksum algorithm must reproduce the stock header
    o1, o2 = cic6102_crc(base)
    got = struct.unpack('>II', base[0x10:0x18])
    print('crc selftest %s (%08X %08X vs header %08X %08X)'
          % ('PASS' if (o1, o2) == got else 'FAIL', o1, o2, *got))
    if (o1, o2) != got:
        sys.exit('checksum routine disagrees with the stock ROM header')


if __name__ == '__main__':
    main()
