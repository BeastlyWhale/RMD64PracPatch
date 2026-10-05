/* shim.S - payload entry points, register-file capture, and the savestate copy engine. Linked at PAYLOAD_BASE; the first two slots are a fixed jump table so boot.S can hardcode the JAL targets. */
#include "rmd64.h"

    .set noreorder
    .set noat

    .section .entry, "ax"
    .globl payload_entry
payload_entry:
    j       hud_shim                    /* PAYLOAD_BASE + 0x00 */
     nop
    j       room_shim                   /* PAYLOAD_BASE + 0x08 */
     nop

    .text

/* Save/restore the FP register file relative to base register \b. The N64 runs with Status.FR = 0, so the 32 FGRs are addressed as 16 double registers; sdc1/ldc1 on the even ones covers all 128 bytes. */
    .macro FSAVE b, o
    sdc1 $f0,(\o+  0)(\b); sdc1 $f2,(\o+  8)(\b)
    sdc1 $f4,(\o+ 16)(\b); sdc1 $f6,(\o+ 24)(\b)
    sdc1 $f8,(\o+ 32)(\b); sdc1 $f10,(\o+40)(\b)
    sdc1 $f12,(\o+48)(\b); sdc1 $f14,(\o+56)(\b)
    sdc1 $f16,(\o+64)(\b); sdc1 $f18,(\o+72)(\b)
    sdc1 $f20,(\o+80)(\b); sdc1 $f22,(\o+88)(\b)
    sdc1 $f24,(\o+96)(\b); sdc1 $f26,(\o+104)(\b)
    sdc1 $f28,(\o+112)(\b); sdc1 $f30,(\o+120)(\b)
    .endm

    .macro FLOAD b, o
    ldc1 $f0,(\o+  0)(\b); ldc1 $f2,(\o+  8)(\b)
    ldc1 $f4,(\o+ 16)(\b); ldc1 $f6,(\o+ 24)(\b)
    ldc1 $f8,(\o+ 32)(\b); ldc1 $f10,(\o+40)(\b)
    ldc1 $f12,(\o+48)(\b); ldc1 $f14,(\o+56)(\b)
    ldc1 $f16,(\o+64)(\b); ldc1 $f18,(\o+72)(\b)
    ldc1 $f20,(\o+80)(\b); ldc1 $f22,(\o+88)(\b)
    ldc1 $f24,(\o+96)(\b); ldc1 $f26,(\o+104)(\b)
    ldc1 $f28,(\o+112)(\b); ldc1 $f30,(\o+120)(\b)
    .endm

/* hud_shim - called from 0x80028088, returns to 0x80028090. The caller reloads everything it needs after the call site except $a1, which must come back holding the display-list cursor. */
    .globl hud_shim
hud_shim:
    li      $at, CTX_ENTRY
    sw      $ra, 31*4($at)
    sw      $sp, 29*4($at)
    sw      $t0,  8*4($at)
    sw      $t1,  9*4($at)
    mfc0    $t0, $12
    sw      $t0, CTX_SR*4($at)

    /* Remember if this frame was armed for a savestate */
    li      $t0, (VAR_BASE + VAR_ARMED*4)
    lw      $t0, 0($t0)
    li      $t1, (VAR_BASE + VAR_CTXOK*4)
    sw      $t0, 0($t1)
    beqz    $t0, hs_run
     nop

    /* Armed: the rest of the register file */
    sw      $v0,  2*4($at)
    sw      $v1,  3*4($at)
    sw      $a0,  4*4($at)
    sw      $a1,  5*4($at)
    sw      $a2,  6*4($at)
    sw      $a3,  7*4($at)
    sw      $t2, 10*4($at)
    sw      $t3, 11*4($at)
    sw      $t4, 12*4($at)
    sw      $t5, 13*4($at)
    sw      $t6, 14*4($at)
    sw      $t7, 15*4($at)
    sw      $s0, 16*4($at)
    sw      $s1, 17*4($at)
    sw      $s2, 18*4($at)
    sw      $s3, 19*4($at)
    sw      $s4, 20*4($at)
    sw      $s5, 21*4($at)
    sw      $s6, 22*4($at)
    sw      $s7, 23*4($at)
    sw      $t8, 24*4($at)
    sw      $t9, 25*4($at)
    sw      $gp, 28*4($at)
    sw      $fp, 30*4($at)
    mfhi    $t0
    mflo    $t1
    sw      $t0, CTX_HI*4($at)
    sw      $t1, CTX_LO*4($at)
    cfc1    $t0, $31
    sw      $t0, CTX_FCR31*4($at)
    FSAVE   $at, CTX_FGR*4

hs_run:
    /* run the C side on our own scratch stack */
    li      $sp, SCRATCH_SP
    addiu   $sp, $sp, -64
    jal     hud_frame
     nop

    /* restore only needs saved context, save needs CTX_ENTRY so queue it, now checks for a sate in slot before loading */
    li      $t0, ACT_RESTORE
    bne     $v0, $t0, hs_capture
     nop
    li      $at, (VAR_BASE + VAR_VALID*4)
    lw      $at, 0($at)
    bnez    $at, do_restore
     nop
    b       hud_return
     nop

hs_capture:
    li      $t0, ACT_CAPTURE
    bne     $v0, $t0, hud_return
     nop
    li      $t0, (VAR_BASE + VAR_CTXOK*4)
    lw      $t0, 0($t0)
    bnez    $t0, do_capture
     nop

hud_return:
    li      $at, CTX_ENTRY
    lw      $ra, 31*4($at)
    lw      $sp, 29*4($at)
    li      $a1, G_DL_CURSOR
    lw      $a1, 0($a1)
    jr      $ra
     nop

/* do_capture - CTX_ENTRY becomes the saved context, then RDRAM is mirrored into the Expansion Pack. Interrupts stay on*/
do_capture:
    jal     quiet_wait                  /* interrupts on */
     nop
    li      $at, (VAR_BASE + VAR_QFAIL*4)
    sw      $v0, 0($at)
    bnez    $v0, 9f
     nop

    mfc0    $t0, $12
    li      $t1, -2                     /* clear interrupts at Status.IE */
    and     $t0, $t0, $t1
    mtc0    $t0, $12
    nop
    nop
    nop

    jal     quiet_check                 /* re-check atomically */
     nop
    li      $at, (VAR_BASE + VAR_QFAIL*4)
    sw      $v0, 0($at)
    beqz    $v0, 8f
     nop
    /* Lost the race between the wait and the mask. Put interrupts back. */
    li      $at, CTX_ENTRY
    lw      $t0, CTX_SR*4($at)
    mtc0    $t0, $12
    nop
    nop
9:  /* Not quiet. The action stays pending and the C side retries next frame. */
    b       hud_return
     nop
8:  /* committed from here on */
    li      $at, (VAR_BASE + VAR_PENDACT*4)
    sw      $zero, 0($at)

    li      $t8, CTX_ENTRY
    li      $t9, CTX_SAVED
    li      $t7, CTX_WORDS
1:  lw      $t0, 0($t8)
    addiu   $t7, $t7, -1
    sw      $t0, 0($t9)
    addiu   $t8, $t8, 4
    bnez    $t7, 1b
     addiu  $t9, $t9, 4

    jal     dma_wait
     nop
    li      $a0, SAVE_SRC
    li      $a1, SAVE_BUF
    li      $a2, SAVE_LEN
    jal     blockcopy
     nop

    li      $at, (SAVE_BUF + (0x800A6964 - SAVE_SRC))
    li      $t0, -1
    sw      $t0, 0($at)

    li      $at, (VAR_BASE + VAR_VALID*4)
    li      $t0, 1
    sw      $t0, 0($at)
    li      $at, (VAR_BASE + VAR_FLASH*4)
    li      $t0, 20
    sw      $t0, 0($at)
    li      $at, CTX_ENTRY
    lw      $t0, CTX_SR*4($at)
    mtc0    $t0, $12                    /* interrupts back on */
    nop
    nop
    b       hud_return
     nop

/* do_restore - interrupts off, RDRAM copied back, caches flushed, register file reloaded, return into the middle of the frame-flush function in savestate. */
do_restore:
    jal     quiet_wait                  /* interrupts on - bounded */
     nop
    li      $at, (VAR_BASE + VAR_QFAIL*4)
    sw      $v0, 0($at)
    bnez    $v0, 9f
     nop

    mfc0    $t0, $12
    li      $t1, -2                     /* clear interrupts at Status.IE */
    and     $t0, $t0, $t1
    mtc0    $t0, $12
    nop
    nop
    nop

    jal     quiet_check                 /* re-check atomically */
     nop
    li      $at, (VAR_BASE + VAR_QFAIL*4)
    sw      $v0, 0($at)
    beqz    $v0, 8f
     nop
    /* Lost the race between the wait and the mask. Put interrupts back. */
    li      $at, CTX_ENTRY
    lw      $t0, CTX_SR*4($at)
    mtc0    $t0, $12
    nop
    nop
9:  /* Not quiet. The action stays pending and the C side retries next frame. */
    b       hud_return
     nop
8:  /* committed from here on */
    li      $at, (VAR_BASE + VAR_PENDACT*4)
    sw      $zero, 0($at)

    jal     dma_wait
     nop
    li      $a0, SAVE_BUF
    li      $a1, SAVE_SRC
    li      $a2, SAVE_LEN
    jal     blockcopy
     nop

    /* write back every dirty data line, then drop the whole I-cache */
    li      $t0, 0x80000000
    li      $t1, 512
2:  cache   0x01, 0($t0)                /* Index_WriteBack_Invalidate_D */
    addiu   $t1, $t1, -1
    bnez    $t1, 2b
     addiu  $t0, $t0, 16
    li      $t0, 0x80000000
    li      $t1, 512
3:  cache   0x00, 0($t0)                /* Index_Invalidate_I */
    addiu   $t1, $t1, -1
    bnez    $t1, 3b
     addiu  $t0, $t0, 32

    li      $at, CTX_SAVED
    FLOAD   $at, CTX_FGR*4
    lw      $t0, CTX_FCR31*4($at)
    ctc1    $t0, $31
    lw      $t0, CTX_HI*4($at)
    mthi    $t0
    lw      $t0, CTX_LO*4($at)
    mtlo    $t0
    lw      $v0,  2*4($at)
    lw      $v1,  3*4($at)
    lw      $a0,  4*4($at)
    lw      $a1,  5*4($at)
    lw      $a2,  6*4($at)
    lw      $a3,  7*4($at)
    lw      $t0,  8*4($at)
    lw      $t1,  9*4($at)
    lw      $t2, 10*4($at)
    lw      $t3, 11*4($at)
    lw      $t4, 12*4($at)
    lw      $t5, 13*4($at)
    lw      $t6, 14*4($at)
    lw      $t7, 15*4($at)
    lw      $s0, 16*4($at)
    lw      $s1, 17*4($at)
    lw      $s2, 18*4($at)
    lw      $s3, 19*4($at)
    lw      $s4, 20*4($at)
    lw      $s5, 21*4($at)
    lw      $s6, 22*4($at)
    lw      $s7, 23*4($at)
    lw      $t8, 24*4($at)
    lw      $t9, 25*4($at)
    lw      $gp, 28*4($at)
    lw      $fp, 30*4($at)
    lw      $ra, 31*4($at)
    lw      $sp, 29*4($at)
    lw      $at, CTX_SR*4($at)
    mtc0    $at, $12                    /* restores Status, re-enables IE */
    nop
    nop
    /* 0x80028088/8C were the producer of $a1, so it is dead-in at 0x80028090, rebuilding here */
    li      $a1, G_DL_CURSOR
    lw      $a1, 0($a1)
    jr      $ra
     nop

/* room_shim - replaces `jal 0x800603F0` at 0x80073758 */
    .globl room_shim
room_shim:
    addiu   $sp, $sp, -16
    sw      $ra, 12($sp)
    li      $at, (VAR_BASE + VAR_PENDING*4)
    li      $t0, 1
    sw      $t0, 0($at)
    
    /* Sample the game mode here because it does not survive to the frame hook*/
    li      $at, G_GAME_MODE
    lbu     $t1, 0($at)
    li      $at, (VAR_BASE + VAR_PENDMODE*4)
    sw      $t1, 0($at)

    .word   0x0C0180FC                  /* jal 0x800603F0 - the displaced call */
    nop
    lw      $ra, 12($sp)
    jr      $ra
     addiu  $sp, $sp, 16
     

/* Helpers */
/* dma_wait: bounded wait until nothing but the CPU can reach RDRAM. Clobbers $t0-$t2. */
    .globl dma_wait
dma_wait:
    /* PI: no cartridge DMA in flight */
    lui     $t0, 0xA460
    li      $t2, 0x200000
1:  lw      $t1, 0x10($t0)
    andi    $t1, $t1, 3
    beqz    $t1, 2f
     addiu  $t2, $t2, -1
    bnez    $t2, 1b
     nop

    /* RSP: halted, and no SP DMA in flight */
2:  lui     $t0, 0xA404
    li      $t2, 0x200000
3:  lw      $t1, 0x10($t0)
    andi    $t3, $t1, 3                 /* HALT | BROKE */
    beqz    $t3, 4f
     andi   $t3, $t1, 4                 /* DMA_BUSY */
    beqz    $t3, 5f
     nop
4:  addiu   $t2, $t2, -1
    bnez    $t2, 3b
     nop

    /* RDP: queue drained */
5:  lui     $t0, 0xA410
    li      $t2, 0x200000
6:  lw      $t1, 0xC($t0)
    andi    $t1, $t1, 0x140             /* CMD_BUSY | DMA_BUSY */
    beqz    $t1, 7f
     addiu  $t2, $t2, -1
    bnez    $t2, 6b
     nop
7:  jr      $ra
     nop

/* quiet_check: v0 = 1 when no thread is parked on an event that has already happened, so RDRAM can be snapshotted or put back consistently. Run with interrupts masked, Clobbers $t0-$t2. */
    .globl quiet_check
quiet_check:
    li      $t0, Q_GFX_TASKS
    lw      $t1, 0($t0)
    bnez    $t1, 1f
     li     $v0, 1
    li      $t0, Q_AUD_PENDING
    lw      $t1, 0($t0)
    bnez    $t1, 1f
     li     $v0, 2
    li      $t0, Q_AUD_RUNNING
    lw      $t1, 0($t0)
    bnez    $t1, 1f
     li     $v0, 3
    li      $t0, Q_AUD_QUEUE
    lw      $t1, 0($t0)
    li      $t2, Q_THREAD_TAIL
    bne     $t1, $t2, 1f
     li     $v0, 4
    li      $t0, Q_PI_CMDQ
    lw      $t1, 0($t0)
    li      $t2, Q_PI_MANAGER
    bne     $t1, $t2, 1f
     li     $v0, 5
    li      $t0, PI_STATUS_REG
    lw      $t1, 0($t0)
    andi    $t1, $t1, 3
    bnez    $t1, 1f
     li     $v0, 6
    move    $v0, $zero
1:  jr      $ra
     nop

/* quiet_wait: wait until quiet_check passes, or until CP0 Count has advanced. Clobbers $t0-$t5, $ra. */
    .globl quiet_wait
quiet_wait:
    move    $t5, $ra
    mfc0    $t3, $9                     /* CP0 Count */
2:  jal     quiet_check
     nop
    beqz    $v0, 3f
     nop
    mfc0    $t4, $9
    subu    $t4, $t4, $t3
    li      $t2, QUIET_WAIT_COUNT
    sltu    $t4, $t4, $t2
    bnez    $t4, 2b
     nop
3:  jr      $t5
     nop

/* blockcopy(a0=src, a1=dst, a2=len), len a multiple of 32, all 8-byte aligned. */
    .set push
    .set mips3
    .set gp=64
    .globl blockcopy
blockcopy:
1:  ld      $t0,  0($a0)
    ld      $t1,  8($a0)
    ld      $t2, 16($a0)
    ld      $t3, 24($a0)
    sd      $t0,  0($a1)
    sd      $t1,  8($a1)
    sd      $t2, 16($a1)
    sd      $t3, 24($a1)
    addiu   $a0, $a0, 32
    addiu   $a2, $a2, -32
    bnez    $a2, 1b
     addiu  $a1, $a1, 32
    jr      $ra
     nop
    .set pop
