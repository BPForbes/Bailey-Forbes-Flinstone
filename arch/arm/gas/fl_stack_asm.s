/* fl_stack_t and elevation slot counting (AArch64 GAS). */
.section .note.GNU-stack,"",@progbits
.text
.globl asm_fl_stack_push
.globl asm_fl_stack_pop
.globl asm_fl_stack_count
.globl asm_fl_elev_count_active

.equ FL_STACK_OFF_DATA, 0
.equ FL_STACK_OFF_CAP,  8
.equ FL_STACK_OFF_TOP,  16
.equ FL_ELEV_STRIDE,    56
.equ FL_ELEV_OFF_EXPIRES, 40
.equ FL_ELEV_OFF_USED,  48

/* int asm_fl_stack_push(void *stack, uintptr_t v) */
asm_fl_stack_push:
    cbz     x0, .L_push_fail
    ldr     x3, [x0, #FL_STACK_OFF_TOP]
    ldr     x4, [x0, #FL_STACK_OFF_CAP]
    cmp     x3, x4
    b.hs    .L_push_fail
    ldr     x5, [x0, #FL_STACK_OFF_DATA]
    str     x1, [x5, x3, lsl #3]
    add     x3, x3, #1
    str     x3, [x0, #FL_STACK_OFF_TOP]
    mov     w0, #0
    ret
.L_push_fail:
    mov     w0, #-1
    ret

/* int asm_fl_stack_pop(void *stack, uintptr_t *out) */
asm_fl_stack_pop:
    cbz     x0, .L_pop_fail
    ldr     x3, [x0, #FL_STACK_OFF_TOP]
    cbz     x3, .L_pop_fail
    sub     x3, x3, #1
    ldr     x4, [x0, #FL_STACK_OFF_DATA]
    cbz     x1, .L_pop_skip_load
    ldr     x5, [x4, x3, lsl #3]
    str     x5, [x1]
.L_pop_skip_load:
    str     x3, [x0, #FL_STACK_OFF_TOP]
    mov     w0, #0
    ret
.L_pop_fail:
    mov     w0, #-1
    ret

/* size_t asm_fl_stack_count(const void *stack) */
asm_fl_stack_count:
    cbz     x0, .L_count_zero
    ldr     x0, [x0, #FL_STACK_OFF_TOP]
    ret
.L_count_zero:
    mov     x0, #0
    ret

/* size_t asm_fl_elev_count_active(void *slots, size_t max_slots, int64_t now_ns) */
asm_fl_elev_count_active:
    mov     x3, #0
    cbz     x0, .L_elev_done
    mov     x4, #0
.L_elev_loop:
    cmp     x4, x1
    b.hs    .L_elev_done
    mov     x5, x4
    mov     x6, #FL_ELEV_STRIDE
    mul     x5, x5, x6
    add     x5, x0, x5
    ldr     w6, [x5, #FL_ELEV_OFF_USED]
    cbz     w6, .L_elev_next
    ldr     x6, [x5, #FL_ELEV_OFF_EXPIRES]
    cmp     x6, x2
    b.le    .L_elev_expire
    add     x3, x3, #1
    b       .L_elev_next
.L_elev_expire:
    mov     x6, #0
.L_elev_zero_q:
    str     xzr, [x5, x6]
    add     x6, x6, #8
    cmp     x6, #FL_ELEV_STRIDE
    b.lo    .L_elev_zero_q
.L_elev_next:
    add     x4, x4, #1
    b       .L_elev_loop
.L_elev_done:
    mov     x0, x3
    ret
