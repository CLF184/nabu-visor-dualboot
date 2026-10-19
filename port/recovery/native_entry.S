/* SPDX-License-Identifier: MIT
 * Preserve nabu's Recovery and off-mode charging handoff before the FD
 * overwrites x0/x1.
 * Entered only after the unchanged Renegade marker scan found no marker.
 * Linux arm64 entry: x0 = bootloader FDT, x1..x3 = 0. No stack is used;
 * x0..x3 remain intact. Invalid/unrecognised data continues to the menu.
 * On this nabu boot protocol normal Android has force_normal_boot=1;
 * the original bootloader omits it for Recovery (including the key path).
 * androidboot.mode=charger must also continue the original kernel directly,
 * retaining the bootloader charging environment without a menu/reset cycle.
 */
.Lnative_recovery:
    /* Check the original FDT pointer before dereferencing it. */
    mov x4, #0x80000000
    cmp x0, x4
    b.lo .Lnative_continue
    mov x4, #0x200000000
    cmp x0, x4
    b.hs .Lnative_continue
    tst x0, #7
    b.ne .Lnative_continue
    ldr w4, [x0]
    rev w4, w4
    mov w5, #0xfeed
    movk w5, #0xd00d, lsl #16
    cmp w4, w5
    b.ne .Lnative_continue
    ldr w4, [x0, #4]
    rev w4, w4
    cmp w4, #40
    b.lo .Lnative_continue
    mov w5, #0x200000
    cmp w4, w5
    b.hi .Lnative_continue
    add x5, x0, x4
    mov x6, #0x200000000
    cmp x5, x6
    b.hi .Lnative_continue
    ldr w5, [x0, #20]
    rev w5, w5
    cmp w5, #17
    b.ne .Lnative_continue
    ldr w5, [x0, #24]
    rev w5, w5
    cmp w5, #17
    b.hi .Lnative_continue
    /* Bound both structure and string blocks within totalsize. */
    ldr w5, [x0, #8]
    rev w5, w5
    cmp w5, #40
    b.lo .Lnative_continue
    tst w5, #3
    b.ne .Lnative_continue
    ldr w6, [x0, #36]
    rev w6, w6
    cmp w6, #16
    b.lo .Lnative_continue
    add x7, x5, x6
    cmp x7, x4
    b.hi .Lnative_continue
    add x8, x0, x5
    add x9, x0, x7
    ldr w5, [x0, #12]
    rev w5, w5
    cmp w5, #40
    b.lo .Lnative_continue
    ldr w6, [x0, #32]
    rev w6, w6
    add x7, x5, x6
    cmp x7, x4
    b.hi .Lnative_continue
    add x10, x0, x5
    add x11, x0, x7
    mov w12, #0                 /* Node depth. */
    mov w13, #0                 /* In the top-level chosen node. */
    mov w14, #0                 /* model=1, bootargs=2, normal=4, qcom=8, charger=16. */
.Lnative_token:
    add x4, x8, #4
    cmp x4, x9
    b.hi .Lnative_continue
    ldr w4, [x8], #4
    rev w4, w4
    cmp w4, #1
    b.eq .Lnative_begin
    cmp w4, #2
    b.eq .Lnative_end_node
    cmp w4, #3
    b.eq .Lnative_property
    cmp w4, #4
    b.eq .Lnative_token
    cmp w4, #9
    b.ne .Lnative_continue
    cbnz w12, .Lnative_continue
    /* Charging takes precedence over force_normal_boot, as in Android init. */
    orr w4, w14, #4
    cmp w4, #31
    b.eq _ClearFlags
    cmp w14, #11
    b.ne .Lnative_continue
    /* Preserve the incoming kernel arguments and use the upstream clear
     * loop/Android continuation, with no BCB write or additional reboot. */
    b _ClearFlags
.Lnative_begin:
    mov x6, x8
.Lnative_node_name:
    cmp x8, x9
    b.hs .Lnative_continue
    ldrb w4, [x8], #1
    cbnz w4, .Lnative_node_name
    add x8, x8, #3
    and x8, x8, #-4
    cmp x8, x9
    b.hi .Lnative_continue
    cbnz w12, .Lnative_child
    ldrb w4, [x6]
    cbnz w4, .Lnative_continue
.Lnative_child:
    cmp w12, #1
    b.ne .Lnative_depth
    mov x15, x6
    adr x16, .Lnative_chosen
    mov x17, x9
    bl .Lnative_equal
    mov w13, w18
.Lnative_depth:
    add w12, w12, #1
    cmp w12, #64
    b.hi .Lnative_continue
    b .Lnative_token
.Lnative_end_node:
    cbz w12, .Lnative_continue
    cmp w12, #2
    b.ne .Lnative_pop
    mov w13, #0
.Lnative_pop:
    sub w12, w12, #1
    b .Lnative_token
.Lnative_property:
    cbz w12, .Lnative_continue
    add x4, x8, #8
    cmp x4, x9
    b.hi .Lnative_continue
    ldr w5, [x8], #4
    rev w5, w5
    ldr w6, [x8], #4
    rev w6, w6
    sub x4, x11, x10
    cmp x6, x4
    b.hs .Lnative_continue
    add x6, x10, x6             /* Property name. */
    mov x7, x8                 /* Property value. */
    add x4, x8, x5
    cmp x4, x9
    b.hi .Lnative_continue
    add x8, x4, #3
    and x8, x8, #-4
    cmp x8, x9
    b.hi .Lnative_continue
    cmp w12, #1
    b.ne .Lnative_chosen_property
    mov x15, x6
    adr x16, .Lnative_model
    mov x17, x11
    bl .Lnative_equal
    cbz w18, .Lnative_token
    cmp w5, #5
    b.ne .Lnative_continue
    mov x15, x7
    adr x16, .Lnative_nabu
    add x17, x7, x5
    bl .Lnative_equal
    cbz w18, .Lnative_continue
    orr w14, w14, #1
    b .Lnative_token
.Lnative_chosen_property:
    cmp w12, #2
    b.ne .Lnative_token
    cbz w13, .Lnative_token
    mov x15, x6
    adr x16, .Lnative_bootargs
    mov x17, x11
    bl .Lnative_equal
    cbz w18, .Lnative_token
    tbnz w14, #1, .Lnative_continue /* Reject duplicate bootargs. */
    cbz w5, .Lnative_continue
    cmp w5, #0x4000
    b.hi .Lnative_continue
    mov x21, x7
    add x22, x7, x5
    ldrb w4, [x22, #-1]
    cbnz w4, .Lnative_continue
    orr w14, w14, #2
.Lnative_cmdline:
    cmp x21, x22
    b.hs .Lnative_continue
    ldrb w4, [x21]
    cbz w4, .Lnative_token
    mov x15, x21
    adr x16, .Lnative_normal
    mov x17, x22
    bl .Lnative_prefix
    cbz w18, .Lnative_check_charger
    orr w14, w14, #4
.Lnative_check_charger:
    /* Match the whole charger token, not another argument's value/prefix. */
    cmp x21, x7
    b.eq .Lnative_charger_value
    ldrb w4, [x21, #-1]
    cmp w4, #32
    b.ne .Lnative_hardware
.Lnative_charger_value:
    mov x15, x21
    adr x16, .Lnative_charger
    mov x17, x22
    bl .Lnative_prefix
    cbz w18, .Lnative_hardware
    cmp x15, x22
    b.hs .Lnative_continue
    ldrb w4, [x15]
    cbz w4, .Lnative_is_charger
    cmp w4, #32
    b.ne .Lnative_hardware
.Lnative_is_charger:
    orr w14, w14, #16
.Lnative_hardware:
    mov x15, x21
    adr x16, .Lnative_qcom
    mov x17, x22
    bl .Lnative_prefix
    cbz w18, .Lnative_next_char
    /* Hardware must be the full qcom value, not qcom-something. */
    cmp x15, x22
    b.hs .Lnative_continue
    ldrb w4, [x15]
    cbz w4, .Lnative_is_qcom
    cmp w4, #32
    b.ne .Lnative_next_char
.Lnative_is_qcom:
    orr w14, w14, #8
.Lnative_next_char:
    add x21, x21, #1
    b .Lnative_cmdline
/* Compare NUL-terminated names within their FDT block. */
.Lnative_equal:
    cmp x15, x17
    b.hs .Lnative_no_match
    ldrb w18, [x15], #1
    ldrb w19, [x16], #1
    cmp w18, w19
    b.ne .Lnative_no_match
    cbnz w18, .Lnative_equal
    mov w18, #1
    ret
/* AOSP ForceNormalBoot uses a substring search in the command line. */
.Lnative_prefix:
    ldrb w19, [x16], #1
    cbz w19, .Lnative_match
    cmp x15, x17
    b.hs .Lnative_no_match
    ldrb w18, [x15], #1
    cmp w18, w19
    b.eq .Lnative_prefix
.Lnative_no_match:
    mov w18, #0
    ret
.Lnative_match:
    mov w18, #1
    ret
.Lnative_continue:
    b _CopyUEFI
.Lnative_model: .asciz "model"
.Lnative_nabu: .asciz "NABU"
.Lnative_chosen: .asciz "chosen"
.Lnative_bootargs: .asciz "bootargs"
.Lnative_normal: .asciz "androidboot.force_normal_boot=1"
.Lnative_charger: .asciz "androidboot.mode=charger"
.Lnative_qcom: .asciz "androidboot.hardware=qcom"
.balign 4
