#include "rmd64.h"

    .set noreorder
    .set noat
    .section .boot, "ax"
    .globl rmd_boot
rmd_boot:
    /* check for expansion */
    lui     $t0, 0x8000
    lw      $t1, 0x318($t0)             /* osMemSize */
    lui     $t2, 0x0080                 /* 0x00800000 */
    sltu    $t3, $t1, $t2
    bnez    $t3, rb_done
     nop

    /* raw PI DMA: ROM PAYLOAD_ROM -> RDRAM PAYLOAD_BASE */
    lui     $t0, 0xA460
1:  lw      $t1, 0x10($t0)              /* PI_STATUS */
    andi    $t1, $t1, 3                 /* DMA_BUSY | IO_BUSY */
    bnez    $t1, 1b
     nop

    li      $t1, (PAYLOAD_BASE & 0x1FFFFFFF)
    sw      $t1, 0x00($t0)              /* PI_DRAM_ADDR */
    li      $t1, (0x10000000 + PAYLOAD_ROM)
    sw      $t1, 0x04($t0)              /* PI_CART_ADDR */
    li      $t1, (PAYLOAD_LEN - 1)
    sw      $t1, 0x0C($t0)              /* PI_WR_LEN, starts the transfer */
2:  lw      $t1, 0x10($t0)
    andi    $t1, $t1, 3
    bnez    $t1, 2b
     nop
    li      $t1, 2
    sw      $t1, 0x10($t0)              /* clear the PI interrupt we raised */

    /* Payload arrived by DMA, drop any cache lines covering it. Do before the zeroing. Alias so dirty lines land on fresh */
    li      $t0, PAYLOAD_BASE
    li      $t1, PAYLOAD_LEN
4:  cache   0x11, 0($t0)                /* Hit_Invalidate_D, 16-byte lines */
    cache   0x11, 16($t0)
    cache   0x10, 0($t0)                /* Hit_Invalidate_I, 32-byte lines */
    addiu   $t1, $t1, -32
    bgtz    $t1, 4b
     addiu  $t0, $t0, 32

    /* zero variable page */
    li      $t0, CTX_BASE
    li      $t1, 0x1100
3:  sw      $zero, 0($t0)
    addiu   $t1, $t1, -4
    bnez    $t1, 3b
     addiu  $t0, $t0, 4

    /* verify payload */
    li      $t0, (PAYLOAD_BASE + PAYLOAD_MAGIC_OFF)
    lw      $t1, 0($t0)
    li      $t2, MAGIC_VALUE
    bne     $t1, $t2, rb_done
     nop
    lw      $t1, 4($t0)
    li      $t2, 0xA5A5A5A5
    bne     $t1, $t2, rb_done
     nop

    li      $t0, (VAR_BASE + VAR_MAGIC * 4)
    li      $t1, MAGIC_VALUE
    sw      $t1, 0($t0)

    /* hook here so if there is no expansion it never runs */
    .macro POKE addr, word
    li      $t0, \addr
    li      $t1, \word
    sw      $t1, 0($t0)
    cache   0x19, 0($t0) /* Hit_WriteBack_D */
    cache   0x10, 0($t0) /* Hit_Invalidate_I */
    .endm

#if STAGE >= 2
    POKE    0x80028088, JAL_HUD /* end-of-frame flush -> hud shim */
    POKE    0x8002808C, 0x00000000 /* its old delay-slot load */
#endif
#if POKE_CAP
    POKE    0x80027CEC, 0x34C6D400 /* DL soft cap 0x1D800 -> 0x1D400 */
#endif
#if STAGE >= 4
    POKE    0x80073758, JAL_ROOM /* room entry complete -> room shim */
#endif

rb_done:
    jr      $ra
     nop
