.text
.align 2

/* Test each possible GPR destination, including callee-save registers and LR.
 * Preserve the host ABI around each deliberate MRS destination clobber. */
.macro read_tls number
.global tls_read_\number
.type tls_read_\number, %function
tls_read_\number:
    str x18, [sp, #-16]!
    sub sp, sp, #96
    stp x19, x20, [sp, #0]
    stp x21, x22, [sp, #16]
    stp x23, x24, [sp, #32]
    stp x25, x26, [sp, #48]
    stp x27, x28, [sp, #64]
    stp x29, x30, [sp, #80]
    mrs x\number, tpidr_el0
    mov x0, x\number
    ldp x19, x20, [sp, #0]
    ldp x21, x22, [sp, #16]
    ldp x23, x24, [sp, #32]
    ldp x25, x26, [sp, #48]
    ldp x27, x28, [sp, #64]
    ldp x29, x30, [sp, #80]
    add sp, sp, #96
    ldr x18, [sp], #16
    ret
.size tls_read_\number, .-tls_read_\number
.endm
.irp number,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
    read_tls \number
.endr

.global tls_read_discard
.type tls_read_discard, %function
tls_read_discard:
    mrs xzr, tpidr_el0
    mov x0, xzr
    ret
.size tls_read_discard, .-tls_read_discard

/* Capture all GPRs, NZCV and all SIMD bytes immediately after an MRS x9.
 * The patched trampoline must change x9 only. Save the caller's callee-save
 * registers separately from the 768-byte result snapshot. */
.global tls_capture
.type tls_capture, %function
tls_capture:
    sub sp, sp, #1008
    add x17, sp, #768
    stp x19, x20, [x17, #0]
    stp x21, x22, [x17, #16]
    stp x23, x24, [x17, #32]
    stp x25, x26, [x17, #48]
    stp x27, x28, [x17, #64]
    stp x29, x30, [x17, #80]
    stp q8, q9, [x17, #96]
    stp q10, q11, [x17, #128]
    stp q12, q13, [x17, #160]
    stp q14, q15, [x17, #192]
    str x0, [sp, #992]
    str x18, [sp, #1000]
.irp number,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
    mov x\number, #(0x100 + \number)
.endr
.irp number,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    movi v\number\().16b, #(1 + \number)
.endr
    cmp x0, x0
    mrs x9, tpidr_el0
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    stp x8, x9, [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    str x30, [sp, #240]
    mrs x0, nzcv
    str x0, [sp, #248]
    stp q0, q1, [sp, #256]
    stp q2, q3, [sp, #288]
    stp q4, q5, [sp, #320]
    stp q6, q7, [sp, #352]
    stp q8, q9, [sp, #384]
    stp q10, q11, [sp, #416]
    stp q12, q13, [sp, #448]
    stp q14, q15, [sp, #480]
    stp q16, q17, [sp, #512]
    stp q18, q19, [sp, #544]
    stp q20, q21, [sp, #576]
    stp q22, q23, [sp, #608]
    stp q24, q25, [sp, #640]
    stp q26, q27, [sp, #672]
    stp q28, q29, [sp, #704]
    stp q30, q31, [sp, #736]
    ldr x16, [sp, #992]
    mov x17, sp
    mov x18, #48
1:
    ldp x0, x1, [x17], #16
    stp x0, x1, [x16], #16
    subs x18, x18, #1
    b.ne 1b
    add x17, sp, #768
    ldp q8, q9, [x17, #96]
    ldp q10, q11, [x17, #128]
    ldp q12, q13, [x17, #160]
    ldp q14, q15, [x17, #192]
    ldp x19, x20, [x17, #0]
    ldp x21, x22, [x17, #16]
    ldp x23, x24, [x17, #32]
    ldp x25, x26, [x17, #48]
    ldp x27, x28, [x17, #64]
    ldp x29, x30, [x17, #80]
    ldr x18, [sp, #1000]
    add sp, sp, #1008
    ret
.size tls_capture, .-tls_capture
.section .note.GNU-stack,"",%progbits
