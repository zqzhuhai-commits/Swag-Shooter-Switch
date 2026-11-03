/* Preserve native movement state, vectors and flags across final speed selection. */
.text
.align 2
.global local_movement_bridge
.type local_movement_bridge, %function
local_movement_bridge:
sub sp, sp, #704
stp x0, x1, [sp, #0]
stp x2, x3, [sp, #16]
stp x4, x5, [sp, #32]
stp x6, x7, [sp, #48]
stp x8, x9, [sp, #64]
stp x10, x11, [sp, #80]
stp x12, x13, [sp, #96]
stp x14, x15, [sp, #112]
stp x16, x17, [sp, #128]
str x18, [sp, #144]
str x30, [sp, #152]
stp q0, q1, [sp, #176]
stp q2, q3, [sp, #208]
stp q4, q5, [sp, #240]
stp q6, q7, [sp, #272]
stp q8, q9, [sp, #304]
stp q10, q11, [sp, #336]
stp q12, q13, [sp, #368]
stp q14, q15, [sp, #400]
stp q16, q17, [sp, #432]
stp q18, q19, [sp, #464]
stp q20, q21, [sp, #496]
stp q22, q23, [sp, #528]
stp q24, q25, [sp, #560]
stp q26, q27, [sp, #592]
stp q28, q29, [sp, #624]
stp q30, q31, [sp, #656]
mrs x16, nzcv
str x16, [sp, #160]
mrs x16, fpcr
str x16, [sp, #168]
mrs x16, fpsr
str x16, [sp, #688]
mov x0, x19
bl local_movement_prepare
ldr x16, [sp, #168]
msr fpcr, x16
ldr x16, [sp, #688]
msr fpsr, x16
ldr x16, [sp, #160]
msr nzcv, x16
ldp q0, q1, [sp, #176]
ldp q2, q3, [sp, #208]
ldp q4, q5, [sp, #240]
ldp q6, q7, [sp, #272]
ldp q8, q9, [sp, #304]
ldp q10, q11, [sp, #336]
ldp q12, q13, [sp, #368]
ldp q14, q15, [sp, #400]
ldp q16, q17, [sp, #432]
ldp q18, q19, [sp, #464]
ldp q20, q21, [sp, #496]
ldp q22, q23, [sp, #528]
ldp q24, q25, [sp, #560]
ldp q26, q27, [sp, #592]
ldp q28, q29, [sp, #624]
ldp q30, q31, [sp, #656]
ldp x0, x1, [sp, #0]
ldp x2, x3, [sp, #16]
ldp x4, x5, [sp, #32]
ldp x6, x7, [sp, #48]
ldp x8, x9, [sp, #64]
ldp x10, x11, [sp, #80]
ldp x12, x13, [sp, #96]
ldp x14, x15, [sp, #112]
ldp x16, x17, [sp, #128]
ldr x18, [sp, #144]
ldr x30, [sp, #152]
add sp, sp, #704
/* Replay the displaced loads before the native speed read/physics move. */
ldr x0, [x22]
ldr s11, [x19, #500]
ldr s10, [x19, #504]
ldr s8, [x19, #508]
adrp x16, local_movement_resume
ldr x16, [x16, :lo12:local_movement_resume]
br x16
.size local_movement_bridge, .-local_movement_bridge
