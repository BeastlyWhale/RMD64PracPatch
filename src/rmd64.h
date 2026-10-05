#ifndef RMD64_H
#define RMD64_H

/* globals */
#define G_DL_CURSOR     0x801B11C0  /* display-list write pointer */
#define G_PAD_HELD      0x801C4450  /* port-1 held buttons, raw layout */
#define G_PAD_PRESSED   0x801C4456  /* port-1 check every frame */
#define G_PLAYTIME      0x801C44C4  /* play clock, IGT */
#define G_AREA_LOADED   0x801C4520  /* area whose overlay A is loaded */
#define G_AREA_ID       0x801C4508  /* current area ID */
#define G_ENTRANCE_ID   0x801C4509  /* entrance to room that was used */
#define G_SUBROOM       0x801C452C  /* loading zones witout area transition have subroom ID */
#define G_RENDERFRAMES  0x801C424C  /* counts total rendered frames*/
#define G_DIFF_EASY     0x801C44D0  /* Easy mode flag */
#define G_DIFF_HARD     0x801C44D2  /* Hard mode flag */
#define G_FB_PTR        0x80218B20  /* colour framebuffer of this frame */
#define G_FB_WIDTH      0x800A695A  /* framebuffer width 320 */
#define G_OS_VIRT_TO_PHYS 0x80099870 /* libultra function vert to phys RDRAM */
#define G_OS_MEM_SIZE   0x80000318  /* check PI register for 0x400000 or 0x800000 RDRAM*/
#define G_GAME_MODE     0x801C44B8  /* 1 area load, 2 room entry, 3 in gameplay*/

/* Screen detection for the watermark */
#define G_TOPLOOP       0x801C3BC0  /*entry of the running top loop */
#define TOPLOOP_TITLE   0x8005D200  /* title / menu loop */
#define G_TITLE_STATE   0x801AD518  /* title-loop handler index */
#define TITLE_PRESS     2           /* "press start" title */
#define TITLE_MENU      4           /* main menu over the logo */
#define G_EVT_RUN       0x801C4680  /* cutscene event running */
#define G_EVT_ID        0x801C4681  /* event id */
#define G_EVT_SUB       0x801C4685  /* shot sub-state */
#define G_EVT_SHOT      0x801C4688  /* current shot */
#define EVT_ENDING      74          /* the final ending event */
#define SHOT_RESULTS    11          /* clear-time results shot */
#define SUB_TIME_SHOWN  3           /* clear-time window open */

/* Player position */
#define G_POS_X         0x8020CA94
#define G_POS_Y         0x8020CA96
#define G_POS_Z         0x8020CA98

/* queue check before loading state */
#define Q_GFX_TASKS     0x80207C80  /* outstanding graphics tasks */
#define Q_AUD_PENDING   0x8021D8BC  /* audio task queued */
#define Q_AUD_RUNNING   0x8021D8C0  /* audio task in flight */
#define Q_AUD_QUEUE     0x801AD4E8  /* OSMesgQueue.mtqueue the audio thread to see if a thread is waiting, restored waiting can stay waiting which is bad */
#define Q_THREAD_TAIL   0x800D0050  /* __osThreadTail: tells you if wait list is empty */
#define Q_PI_CMDQ       0x801974E0  /* PI manager command queue, .mtqueue */
#define Q_PI_MANAGER    0x801997A0  /* PI manager thread, parked means idle */
#define PI_STATUS_REG   0xA4600010

/*address of the framebuffer*/
#define VI_ORIGIN_REG   0xA4400004

/* ---- our memory, all in Expansion Pak territory ---- */
#define PAYLOAD_BASE    0x807C0000  /* injected code, DMA'd from ROM */
#define PAYLOAD_MAX     (CTX_BASE - PAYLOAD_BASE) /* max size, can't go into CTX_BASE */

#define CTX_BASE        0x807D0000 /* scratch page, has context, variables, and restore stack. should prob rename*/
#define CTX_ENTRY       (CTX_BASE + 0x0000)  /* live register file each frame, CPU context */
#define CTX_SAVED       (CTX_BASE + 0x0200)  /* register file of the savestate, restores CPU context */
#define VAR_BASE        (CTX_BASE + 0x0400)	 /* outside snapshot, shared asm and C, used for variables that are 0 at boot */
#define SCRATCH_SP      (CTX_BASE + 0x4000)  /* stack used during restore */

/* register-file layout, in words */
#define CTX_GPR         0    /* 32 words, [0], general purpose registers */
#define CTX_HI          32	 /* mult/divide remainder result register */
#define CTX_LO          33	 /* mult/divide quotient result register */
#define CTX_SR          34   /* CP0 Status interrupt mask? */
#define CTX_FCR31       35	 /* Floating point status register */
#define CTX_FGR         36   /* 32 words of floating point registers to keep in context */
#define CTX_WORDS       68	 /* total amount of context words, looped in do_capture */

/* variables (word indices from VAR_BASE) */
#define VAR_MAGIC       0
#define VAR_VALID       1    /* a snapshot is in the active slot */
#define VAR_PENDING     2    /* room hook wants to savestate*/
#define VAR_PREVCOMBO   3    /* previous controller combo held*/
#define VAR_FLASH       4    /* HUD feedback countdown, want to use for status indicator, amber/orange? can be pre-empted by redflash and greenvalid */
#define VAR_RETIRED5    5    /* retired, do not reuse */
#define VAR_HOLD        6    /* frames a button combo has been held */
#define VAR_FIRED       7    /* detect if a command has already been run with the combo */
#define VAR_FBCACHE     8    /* last good looking framebuffer address*/
#define VAR_PENDACT     9    /* action waiting for a quiet frame to run */
#define VAR_AGE        10    /* how many frames the action has been waiting to run*/
#define VAR_REDFLASH   11    /* action gave up, show red? */
#define VAR_QFAIL      12    /* which quiet item failed, needs to pass to savestate*/
#define VAR_BLUE       13    /* combo seen, room hook savestate being made, or savestate action waiting on quiet */
#define VAR_AUTOOFF    14    /* room-entry savestates 1=off */
#define VAR_PENDMODE   17    /* game mode when the room hook fired */
#define VAR_LASTAREA   18  /* G_AREA_LOADED + 1 at the last room load */

/* Register file is needed only if a savestate is captured. 
VAR_ARMED is set when an action is queued, the room hook wants a savestate on transition, or the combo L/X+R+A is detected. 
Shim copies what it saw on entry into VAR_CTXOK since VAR_ARMED could have changed by dispatch. A capture on a frame that was not armed queues one frame. */
#define VAR_ARMED      15    /* save the full register file this frame */
#define VAR_CTXOK      16    /* CTX_ENTRY holds a complete register file */

#define MAGIC_VALUE     0x524D4450  /* signature and sanity/delivery check 'RMDP' */

/* ---- snapshot region ----
 * 0x80000400 -> 0x803B5000 is everything the game uses except framebuffer and exceptions.
 */
#define SAVE_SRC        0x80000400
#define SAVE_END        0x803B5000
#define SAVE_LEN        (SAVE_END - SAVE_SRC)   /* 0x3B4C00 = 3,886,080 size */
#define SAVE_BUF        0x80400000             /* .. 0x807B4C00 */

/* The physical framebuffers */
#define FB_PHYS_LO      0x003B5000
#define FB_STRIDE       0x00025800          /* 320 * 240 * 2 */
#define FB_PHYS_HI      0x00400000

/* buttons */
#define BTN_CR  0x0001
#define BTN_CL  0x0002
#define BTN_CD  0x0004
#define BTN_CU  0x0008
#define BTN_R   0x0010
#define BTN_L   0x0020
#define BTN_A   0x8000
#define BTN_B   0x4000
#define BTN_Z   0x2000
#define BTN_ST  0x1000
#define BTN_DU  0x0800
#define BTN_DD  0x0400
#define BTN_DL  0x0200
#define BTN_DR  0x0100
#define BTN_ALLC (BTN_CU|BTN_CD|BTN_CL|BTN_CR)

/* button combos, Z is also L */
#define COMBO_RESTORE   (BTN_Z | BTN_R | BTN_A)
#define COMBO_CAPTURE   (BTN_Z | BTN_R | BTN_CD | BTN_A)
#define COMBO_TOGGLE    (BTN_Z | BTN_R | BTN_A | BTN_B)
#define COMBO_MASK      (COMBO_CAPTURE | BTN_B)
#define COMBO_SETTLE    4

/* Frames before request timeout */
#define PEND_TIMEOUT    60

/* CP0 ticks waiting for no graphic tasks */
#define QUIET_WAIT_COUNT 0xE0000

/* HUD_MINIMAL left over from attempt to show position on screen */
#ifndef HUD_MINIMAL
#define HUD_MINIMAL 1
#endif

/* HUD mode options left over from attempt to show position on screen */
#ifndef HUD_MODE
#define HUD_MODE 6
#endif

/* HUD geometry */
#define HUD_SCALE   2
#define HUD_RIGHT   312
#define HUD_TOP     8

/* actions returned by hud_frame() to the assembly shim, link between C and asm */
#define ACT_NONE     0
#define ACT_CAPTURE  1
#define ACT_RESTORE  2

/* dual-audience guard */
#ifndef __ASSEMBLER__
typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef signed char    s8;
typedef signed short   s16;
typedef signed int     s32;

#define MMIO32(a) (*(volatile u32 *)(a))
#define MMIO16(a) (*(volatile u16 *)(a))
#define MMIO8(a)  (*(volatile u8  *)(a))
#define VAR(i)    MMIO32(VAR_BASE + ((i) * 4))
#endif

#endif /* RMD64_H */
